// SPDX-License-Identifier: MPL-2.0
// Fuzz target for everything a client can send the server: handshake packets, data packets and decrypted messages,
// plus the client's own decoders for what a server sends. libFuzzer (-DWWHD_ONLINE_FUZZ=ON with a clang that ships
// it), or the built-in driver: `wwhd_online_fuzz <iterations> [seed]` runs random and mutated inputs.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include "proto/channel.h"
#include "proto/client.h"
#include "proto/handshake.h"
#include "proto/messages.h"
#include "server.h"

using namespace online;
using namespace online::proto;

static server::Server* server_once() {
    static std::unique_ptr<server::Server> s;
    if (!s) {
        server::Config c;
        c.port = 0;
        c.key_file = "fuzz-server.key";
        c.ban_file = "fuzz-bans.txt";
        c.report_file = "";
        c.team_state_file = "";
        c.admin_socket = "";
        c.pow_bits = 0;
        c.loopback_exempt = true;
        s = std::make_unique<server::Server>(c);
        s->log = [](const std::string&) {};
        std::string why;
        if (!s->start(why)) {
            std::fprintf(stderr, "fuzz: %s\n", why.c_str());
            std::abort();
        }
    }
    return s.get();
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n) {
    if (n < 1) return 0;
    server::Server* s = server_once();
    Address from;
    std::string why;
    resolve("127.0.0.1", 40000, from, why);
    uint8_t sel = d[0] % 4;
    const uint8_t* p = d + 1;
    size_t pn = n - 1;
    switch (sel) {
        case 0: s->fuzz_datagram(p, pn, from); break;
        case 1:
            if (pn >= 2) s->fuzz_message(p[0], p[1] & 1, p + 2, pn - 2);
            break;
        case 2: {
            // a sealed data packet with the fuzz body: reaches the frame parser
            uint8_t k[32] = {};
            Channel c;
            c.setup(5, k, k, false, 1);
            Bytes dg;
            PacketHeader h;
            h.kind = kPktData;
            h.conn_id = 5;
            h.pkt = 1 + (pn ? p[0] : 0);
            write_header(dg, h);
            Bytes body(p, p + std::min(pn, kMaxDatagram - kHeaderBytes - kTagBytes));
            uint8_t tag[16];
            crypto::seal(k, uint64_t(h.pkt) | (1ull << 63), dg.data(), kHeaderBytes, body.data(), body.size(), tag);
            dg.insert(dg.end(), body.begin(), body.end());
            dg.insert(dg.end(), tag, tag + 16);
            std::vector<Delivered> got;
            c.receive(dg.data(), dg.size(), 2, got);
            break;
        }
        case 3: {
            Challenge ch;
            decode_challenge(p, pn, ch);
            Welcome w;
            decode_welcome_outer(p, pn, w);
            Refuse f;
            decode_refuse(p, pn, f);
            std::vector<PoseEntry> e;
            get_pose_batch(p, pn, e);
            std::vector<Marker> m;
            get_markers(p, pn, m);
            PlayerIn pi;
            decode_player_in(p, pn, pi);
            PartyState ps;
            decode_party_state(p, pn, ps);
            std::vector<RunnerEntry> re;
            decode_runners(p, pn, re);
            Reader r(p, pn);
            std::vector<TeamField> tf;
            get_team_fields(r, tf);
            break;
        }
    }
    static int calls = 0;
    if (++calls % 64 == 0) s->fuzz_tick();
    return 0;
}

#ifndef WWHD_LIBFUZZER
int main(int argc, char** argv) {
    long iters = argc > 1 ? std::atol(argv[1]) : 200000;
    std::mt19937 rng(argc > 2 ? unsigned(std::atol(argv[2])) : 1u);
    // seeds: real packets, then random mutations of them
    std::vector<Bytes> seeds;
    auto id = crypto::Identity::generate();
    Hello h;
    h.name = "seed";
    Bytes hb = encode_hello(h, id);
    hb.insert(hb.begin(), 0);
    seeds.push_back(hb);
    Connect c;
    Bytes cb = encode_connect(c);
    cb.insert(cb.begin(), 0);
    seeds.push_back(cb);
    for (uint8_t t = 0; t < 20; ++t) seeds.push_back({1, t, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    Bytes pose = {1, kMsgPose, 0};
    {
        Writer w(pose);
        PoseHeader ph;
        ph.where = Where::of("sea", 1);
        put_pose_header(w, ph);
    }
    seeds.push_back(pose);
    for (long i = 0; i < iters; ++i) {
        Bytes in;
        if (rng() % 4 == 0) {
            in.resize(rng() % 1300);
            for (auto& x : in) x = uint8_t(rng());
        } else {
            in = seeds[rng() % seeds.size()];
            int muts = 1 + int(rng() % 8);
            for (int k = 0; k < muts && !in.empty(); ++k) {
                switch (rng() % 4) {
                    case 0: in[rng() % in.size()] = uint8_t(rng()); break;
                    case 1: in.resize(rng() % (in.size() + 1)); break;
                    case 2: in.insert(in.begin() + long(rng() % in.size()), uint8_t(rng())); break;
                    case 3: in[rng() % in.size()] ^= uint8_t(1u << (rng() % 8)); break;
                }
            }
        }
        LLVMFuzzerTestOneInput(in.data(), in.size());
    }
    std::printf("fuzz: %ld inputs, no crash\n", iters);
    return 0;
}
#endif
