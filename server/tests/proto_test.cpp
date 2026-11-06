// SPDX-License-Identifier: MPL-2.0
// Unit tests of the online protocol library: hashes, friend codes, proof-of-work, codecs, and the reliable layer
// under loss, duplication and reordering.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

#include "proto/channel.h"
#include "proto/handshake.h"
#include "proto/messages.h"
#include "proto/protocol.h"
#include "proto/sha256.h"

using namespace online::proto;

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
            ++g_fail;                                                    \
        }                                                                \
    } while (0)

static std::string hex(const uint8_t* d, size_t n) {
    std::string s;
    char b[3];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, 3, "%02x", d[i]);
        s += b;
    }
    return s;
}

static void test_sha256() {
    uint8_t h[32];
    sha256("abc", 3, h);
    CHECK(hex(h, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    sha256("", 0, h);
    CHECK(hex(h, 32) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const char* m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256(m, std::strlen(m), h);
    CHECK(hex(h, 32) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // a million 'a' fed in odd pieces
    Sha256 s;
    std::string chunk(997, 'a');
    size_t left = 1000000;
    while (left) {
        size_t n = left < chunk.size() ? left : chunk.size();
        s.update(chunk.data(), n);
        left -= n;
    }
    s.final(h);
    CHECK(hex(h, 32) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // RFC 4231 test case 2
    hmac_sha256(reinterpret_cast<const uint8_t*>("Jefe"), 4, "what do ya want for nothing?", 28, h);
    CHECK(hex(h, 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

static void test_codes() {
    for (int i = 0; i < 2000; ++i) {
        auto id = crypto::Identity::generate();
        uint64_t kh = key_hash(id.pub);
        std::string fc = friend_code(kh);
        CHECK(fc.size() == 12 && fc[4] == '-' && fc[9] == '-');
        uint64_t c = 0;
        CHECK(parse_friend_code(fc, c) && c == code40_of(kh));
        // lower case and without dashes
        std::string lower;
        for (char ch : fc)
            if (ch != '-') lower += char(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
        CHECK(parse_friend_code(lower, c) && c == code40_of(kh));
        // one wrong digit is caught (10-bit check: ~1/1024 slip through; allow that)
        std::string bad = fc;
        bad[0] = bad[0] == '7' ? '8' : '7';
        uint64_t c2;
        if (parse_friend_code(bad, c2)) CHECK(c2 != c);
    }
    uint64_t dummy = 0;
    CHECK(!parse_friend_code("KQ7F-9MZ2", dummy));
    CHECK(stranger_name(0x123456789ABCDEF0ull).rfind("Sailor ", 0) == 0);
}

static void test_pow() {
    uint8_t salt[16] = {1, 2, 3};
    auto id = crypto::Identity::generate();
    uint64_t n = pow_solve(salt, id.pub, 12);
    CHECK(pow_check(salt, id.pub, n, 12));
    CHECK(pow_check(salt, id.pub, 12345, 0));
}

static void test_handshake() {
    auto server = crypto::Identity::generate();
    auto client = crypto::Identity::generate();
    Challenge c;
    c.nonce = 77;
    c.difficulty = 3;
    c.info.name = "test";
    c.info.rules = std::string(600, 'r');
    Bytes b = encode_challenge(c, server);
    CHECK(b.size() <= kMaxDatagram);
    Challenge c2;
    CHECK(decode_challenge(b.data(), b.size(), c2) && c2.nonce == 77 && c2.info.name == "test");
    b[10] ^= 1;
    CHECK(!decode_challenge(b.data(), b.size(), c2));

    Hello h;
    h.nonce = 5;
    h.game_data = 0x1122334455667788ull;
    h.name = "hero";
    h.resume = true;
    h.token[0] = 9;
    Bytes hb = encode_hello(h, client);
    Hello h2;
    size_t sl = 0;
    CHECK(decode_hello(hb.data(), hb.size(), h2, sl) && hello_signature_ok(hb.data(), sl, h2));
    CHECK(h2.game_data == h.game_data && h2.name == "hero" && h2.resume && h2.token[0] == 9);
    CHECK(std::memcmp(h2.pub, client.pub, 32) == 0);
    hb[7] ^= 1;
    CHECK(!decode_hello(hb.data(), hb.size(), h2, sl) || !hello_signature_ok(hb.data(), sl, h2));
    // truncations never crash or pass
    for (size_t n = 0; n < hb.size(); ++n) CHECK(!decode_hello(hb.data(), n, h2, sl));

    auto ce = crypto::Ephemeral::generate();
    auto se = crypto::Ephemeral::generate();
    uint8_t ctx[32], crx[32], stx[32], srx[32];
    crypto::session_keys(ce, se.pub, true, ctx, crx);
    crypto::session_keys(se, ce.pub, false, stx, srx);
    CHECK(std::memcmp(ctx, srx, 32) == 0 && std::memcmp(crx, stx, 32) == 0);
    Welcome w;
    w.nonce = 5;
    w.conn_id = 0xABCD;
    std::memcpy(w.eph, se.pub, 32);
    w.id = 42;
    w.token[3] = 7;
    Bytes wb = encode_welcome(w, server, ce.pub, stx);
    Welcome w2;
    CHECK(decode_welcome_inner(wb.data(), wb.size(), server.pub, ce.pub, crx, w2));
    CHECK(w2.id == 42 && w2.token[3] == 7 && w2.conn_id == 0xABCD);

    Refuse f;
    f.nonce = 9;
    f.reason = Refusal::kGameData;
    f.text = "nope";
    Bytes fb = encode_refuse(f);
    Refuse f2;
    CHECK(decode_refuse(fb.data(), fb.size(), f2) && f2.reason == Refusal::kGameData && f2.text == "nope");
}

static void test_messages() {
    Bytes b;
    Writer w(b);
    PoseHeader h;
    h.where = Where::of("sea", 3);
    h.x = -123456.4f;
    h.y = 250.0f;
    h.z = 98765.6f;
    h.yaw = 0x8123;
    h.anim = 77;
    h.frame = 12;
    h.flags = 3;
    uint8_t blob[700];
    for (int i = 0; i < 700; ++i) blob[i] = uint8_t(i);
    put_near(w, 7, 1234, h, blob, sizeof blob);
    put_mid(w, 8, 0x12345, h);
    put_far(w, 9, h);
    std::vector<PoseEntry> e;
    CHECK(get_pose_batch(b.data(), b.size(), e) && e.size() == 3);
    if (e.size() == 3) {
        CHECK(e[0].id == 7 && e[0].blob.size() == 700 && e[0].head.where.stage_name() == "sea" && e[0].ms == 1234);
        CHECK(e[1].tier == kTierMid && std::abs(e[1].head.x - h.x) <= 1 && std::abs(e[1].head.y - h.y) <= 2 &&
              e[1].head.anim == 77 && e[1].ms == 0x2345);
        CHECK(e[2].tier == kTierFar && std::abs(e[2].head.z - h.z) <= 1);
    }
    CHECK(b.size() == (2 + 1 + 4 + PoseHeader::kBytes + 2 + 700) + kMidEntryBytes + kFarEntryBytes);
    // fuzz-ish: random bytes never crash the decoders
    std::mt19937 rng(1);
    for (int i = 0; i < 20000; ++i) {
        Bytes r(rng() % 300);
        for (auto& x : r) x = uint8_t(rng());
        std::vector<PoseEntry> pe;
        get_pose_batch(r.data(), r.size(), pe);
        PlayerIn pi;
        decode_player_in(r.data(), r.size(), pi);
        PartyState ps;
        decode_party_state(r.data(), r.size(), ps);
        Reader rr(r);
        std::vector<TeamField> tf;
        get_team_fields(rr, tf);
        Hello hh;
        size_t sl;
        decode_hello(r.data(), r.size(), hh, sl);
    }
}

// two channels joined by a lossy, duplicating, reordering link
static void test_channel(double loss, double dup, double reorder, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u(0, 1);
    uint8_t k1[32], k2[32];
    for (int i = 0; i < 32; ++i) {
        k1[i] = uint8_t(i);
        k2[i] = uint8_t(100 + i);
    }
    Channel a, b;
    a.setup(99, k1, k2, true, 1);
    b.setup(99, k2, k1, false, 1);
    struct InFlight {
        uint32_t at;
        Bytes d;
        bool to_b;
    };
    std::vector<InFlight> wire;
    const int kMsgs = 3000;
    int sent = 0;
    std::vector<int> got_b;
    bool stream_ok = false, stream_sent = false;
    Bytes big(40000);
    for (size_t i = 0; i < big.size(); ++i) big[i] = uint8_t(i * 7);
    for (uint32_t now = 2; now < 200000; now += 10) {
        for (int i = 0; i < 3 && sent < kMsgs; ++i, ++sent) {
            Bytes m;
            Writer(m).u32(uint32_t(sent));
            m.resize(4 + (sent % 50) * 20);
            a.send_reliable(20, m);
        }
        if (sent > 1000 && !stream_sent) {
            a.send_reliable(21, big);
            stream_sent = true;
        }
        a.send_unreliable(30, Bytes(100, 1));
        std::vector<Bytes> out;
        a.flush(now, out);
        for (auto& d : out) wire.push_back({now + 20 + uint32_t(u(rng) < reorder ? rng() % 80 : 0), d, true});
        out.clear();
        b.flush(now, out);
        for (auto& d : out) wire.push_back({now + 20 + uint32_t(u(rng) < reorder ? rng() % 80 : 0), d, false});
        std::vector<InFlight> keep;
        for (auto& f : wire) {
            if (f.at > now) {
                keep.push_back(std::move(f));
                continue;
            }
            int copies = u(rng) < loss ? 0 : (u(rng) < dup ? 2 : 1);
            for (int c = 0; c < copies; ++c) {
                std::vector<Delivered> got;
                (f.to_b ? b : a).receive(f.d.data(), f.d.size(), now, got);
                for (auto& m : got) {
                    if (!f.to_b || !m.reliable) continue;
                    if (m.type == 20) {
                        Reader r(m.body);
                        got_b.push_back(int(r.u32()));
                    } else if (m.type == 21) {
                        stream_ok = m.body == big;
                    }
                }
            }
        }
        wire.swap(keep);
        if (int(got_b.size()) == kMsgs && stream_ok && a.reliable_queued() == 0) break;
    }
    bool order = int(got_b.size()) == kMsgs;
    for (int i = 0; order && i < kMsgs; ++i) order = got_b[i] == i;
    std::printf("channel loss=%.2f dup=%.2f reorder=%.2f: %zu/%d in order=%d stream=%d resends=%llu rtt=%d\n", loss,
                dup, reorder, got_b.size(), kMsgs, order, stream_ok, (unsigned long long)a.stats().resends,
                a.rtt_ms());
    CHECK(order);
    CHECK(stream_ok);
    CHECK(!a.broken() && !b.broken());
    // a tampered datagram is refused; a replayed one too
    std::vector<Bytes> out;
    a.send_reliable(20, Bytes(4, 0));
    a.flush(300000, out);
    std::vector<Delivered> got;
    Bytes t = out[0];
    t[t.size() - 1] ^= 1;
    CHECK(!b.receive(t.data(), t.size(), 300000, got));
    CHECK(b.receive(out[0].data(), out[0].size(), 300000, got));
    CHECK(!b.receive(out[0].data(), out[0].size(), 300000, got));
}

int main() {
    test_sha256();
    test_codes();
    test_pow();
    test_handshake();
    test_messages();
    test_channel(0, 0, 0, 1);
    test_channel(0.2, 0.1, 0.3, 2);
    test_channel(0.4, 0.2, 0.5, 3);
    if (g_fail) {
        std::printf("proto_test: %d FAILED\n", g_fail);
        return 1;
    }
    std::printf("proto_test: all passed\n");
    return 0;
}
