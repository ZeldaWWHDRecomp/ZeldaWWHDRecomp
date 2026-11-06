// SPDX-License-Identifier: MPL-2.0
// Unit tests of the online protocol library: hashes, friend codes, proof-of-work, codecs, and the reliable layer
// under loss, duplication and reordering.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "proto/channel.h"
#include "proto/handshake.h"
#include "proto/messages.h"
#include "proto/mods.h"
#include "proto/protocol.h"
#include "proto/sha256.h"
#ifdef WWHD_ONLINE_MONOCYPHER
#include "proto/third_party/monocypher/monocypher.h"
#endif

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

static std::vector<uint8_t> unhex(const char* h) {
    std::vector<uint8_t> v;
    for (size_t i = 0; h[i] && h[i + 1]; i += 2) {
        unsigned x = 0;
        std::sscanf(h + i, "%2x", &x);
        v.push_back(uint8_t(x));
    }
    return v;
}

static void test_crypto() {
    std::printf("crypto backend: %s\n", crypto::backend_name());
    // signatures: valid, then any change to the message, the signature or the key fails
    auto id = crypto::Identity::generate(), other = crypto::Identity::generate();
    const char* msg = "hello from the hero";
    uint8_t sig[64];
    crypto::sign(id, msg, std::strlen(msg), sig);
    CHECK(crypto::verify(id.pub, msg, std::strlen(msg), sig));
    CHECK(!crypto::verify(other.pub, msg, std::strlen(msg), sig));
    std::string m2 = msg;
    m2[0] ^= 1;
    CHECK(!crypto::verify(id.pub, m2.data(), m2.size(), sig));
    sig[10] ^= 1;
    CHECK(!crypto::verify(id.pub, msg, std::strlen(msg), sig));
    // the same seed gives the same identity (identity files keep only the seed)
    auto again = crypto::Identity::from_secret(id.secret);
    CHECK(std::memcmp(again.pub, id.pub, 32) == 0);
    // packets: sealed bytes differ from the plain text (secure backend), open restores them, tampering fails
    uint8_t key[32];
    crypto::random_bytes(key, 32);
    uint8_t ad[17] = {1, 2, 3};
    Bytes plain(300);
    for (size_t i = 0; i < plain.size(); ++i) plain[i] = uint8_t(i);
    Bytes box = plain;
    uint8_t tag[16];
    crypto::seal(key, 42, ad, sizeof ad, box.data(), box.size(), tag);
    if (crypto::backend_secure()) CHECK(box != plain);
    Bytes t = box;
    CHECK(crypto::open(key, 42, ad, sizeof ad, t.data(), t.size(), tag) && t == plain);
    t = box;
    CHECK(!crypto::open(key, 43, ad, sizeof ad, t.data(), t.size(), tag));  // another nonce
    t = box;
    t[5] ^= 1;
    CHECK(!crypto::open(key, 42, ad, sizeof ad, t.data(), t.size(), tag));
    t = box;
    uint8_t ad2[17] = {1, 2, 4};
    CHECK(!crypto::open(key, 42, ad2, sizeof ad2, t.data(), t.size(), tag));
#ifdef WWHD_ONLINE_MONOCYPHER
    // X25519, RFC 7748 section 6.1
    crypto::Ephemeral alice, bob;
    auto as = unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    auto bs = unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    std::memcpy(alice.secret, as.data(), 32);
    std::memcpy(bob.secret, bs.data(), 32);
    auto ap = unhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    auto bp = unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    std::memcpy(alice.pub, ap.data(), 32);
    std::memcpy(bob.pub, bp.data(), 32);
    uint8_t atx[32], arx[32], btx[32], brx[32];
    CHECK(crypto::session_keys(alice, bob.pub, true, atx, arx));
    CHECK(crypto::session_keys(bob, alice.pub, false, btx, brx));
    CHECK(std::memcmp(atx, brx, 32) == 0 && std::memcmp(arx, btx, 32) == 0 && std::memcmp(atx, arx, 32) != 0);
    // the RFC's public keys and shared secret from the vendored library itself
    uint8_t pk[32], shared[32];
    crypto_x25519_public_key(pk, as.data());
    CHECK(std::memcmp(pk, ap.data(), 32) == 0);
    crypto_x25519_public_key(pk, bs.data());
    CHECK(std::memcmp(pk, bp.data(), 32) == 0);
    crypto_x25519(shared, as.data(), bp.data());
    CHECK(hex(shared, 32) == "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    auto e = crypto::Ephemeral::generate();
    crypto_x25519_public_key(pk, e.secret);
    CHECK(std::memcmp(pk, e.pub, 32) == 0);
    // a low-order point (all zero) is refused
    uint8_t zero[32] = {};
    CHECK(!crypto::session_keys(alice, zero, true, atx, arx));
#endif
}

// a burst far over one packet's ack range (a mass handover on a link): everything arrives, in order, and is acked
static void test_channel_burst() {
    uint8_t k1[32] = {1}, k2[32] = {2};
    Channel a, b;
    a.setup(7, k1, k2, true, 1);
    b.setup(7, k2, k1, false, 1);
    a.set_queue_cap(64u << 20, 1u << 20);
    b.set_queue_cap(64u << 20, 1u << 20);
    const int kN = 3000;
    for (int i = 0; i < kN; ++i) {
        Bytes m(250, uint8_t(i));
        Writer(m).u32(uint32_t(i));
        a.send_reliable(20, m);
    }
    int got = 0;
    bool order = true;
    uint32_t now = 2;
    for (; now < 60000 && (got < kN || a.reliable_queued()); now += 33) {
        std::vector<Bytes> out;
        a.flush(now, out, 8192, 250);
        for (auto& d : out) {
            std::vector<Delivered> g;
            b.receive(d.data(), d.size(), now + 1, g);
            for (auto& m : g) {
                Reader r(m.body.data() + 250, 4);
                (void)r;
                order = order && m.body.size() == 254 && m.body[0] == uint8_t(got);
                ++got;
            }
        }
        out.clear();
        b.flush(now + 1, out, 8192, 250);
        for (auto& d : out) {
            std::vector<Delivered> g;
            a.receive(d.data(), d.size(), now + 2, g);
        }
    }
    std::printf("channel burst: %d/%d delivered, in order %d, %zu still queued after %u ms, resends %llu\n", got, kN, order,
                a.reliable_queued(), now, (unsigned long long)a.stats().resends);
    CHECK(got == kN && order && a.reliable_queued() == 0);
}

static ModRef mref(const char* id, const char* ver, uint8_t h) {
    ModRef m;
    m.id = id;
    m.version = ver;
    m.hash.fill(h);
    return m;
}

static void test_mod_rules() {
    using K = ModProblemKind;
    auto has = [](const std::vector<ModProblem>& v, K k, const char* id) {
        return std::any_of(v.begin(), v.end(), [&](const ModProblem& p) { return p.problem == k && p.id == id; });
    };
    std::vector<ModRef> mine = {mref("randomizer", "", 7), mref("qol", "1.2", 1)};
    ModRules none;
    CHECK(check_mods(none, mine).empty());  // no rules: anything goes
    ModRules van;
    van.vanilla_only = true;
    auto p = check_mods(van, mine);
    CHECK(p.size() == 2 && has(p, K::kVanillaOnly, "qol"));
    CHECK(check_mods(van, {}).empty());
    // required: missing, wrong version, wrong hash
    ModRule r;
    ModRules req;
    CHECK(parse_mod_rule("notebook@0.6.1", ModRuleKind::kRequired, r));
    req.rules.push_back(r);
    CHECK(has(check_mods(req, mine), K::kMissing, "notebook"));
    std::vector<ModRef> nb = {mref("notebook", "0.6.0", 2)};
    CHECK(has(check_mods(req, nb), K::kWrongVersion, "notebook"));
    nb[0].version = "0.6.1";
    CHECK(check_mods(req, nb).empty());
    std::string hex64(64, 'a');
    CHECK(parse_mod_rule("randomizer@*#" + hex64, ModRuleKind::kRequired, r) && r.has_hash && r.version.empty());
    ModRules rh;
    rh.rules.push_back(r);
    CHECK(has(check_mods(rh, mine), K::kWrongVersion, "randomizer"));  // the seed (hash) differs
    std::vector<ModRef> right = {mref("randomizer", "", 0xaa)};
    CHECK(check_mods(rh, right).empty());
    // allowed (a whitelist): others are not allowed, a pinned version is checked
    ModRules al;
    CHECK(parse_mod_rule("qol@1.3", ModRuleKind::kAllowed, r));
    al.rules.push_back(r);
    p = check_mods(al, mine);
    CHECK(has(p, K::kNotAllowed, "randomizer") && has(p, K::kWrongVersion, "qol"));
    CHECK(al.has_whitelist() && (al.flags() & 2));
    // banned: by id, or only one version
    ModRules ban;
    CHECK(parse_mod_rule("qol", ModRuleKind::kBanned, r));
    ban.rules.push_back(r);
    CHECK(has(check_mods(ban, mine), K::kBanned, "qol"));
    ModRules ban2;
    CHECK(parse_mod_rule("qol@1.1", ModRuleKind::kBanned, r));
    ban2.rules.push_back(r);
    CHECK(check_mods(ban2, mine).empty());
    // bad entries
    CHECK(!parse_mod_rule("Bad Id", ModRuleKind::kAllowed, r));
    CHECK(!parse_mod_rule("x@1#abc", ModRuleKind::kAllowed, r));
    CHECK(mod_rule_text(r).size() > 0);
    // party matching: 6 the party has it, 7 you have it, 8 another version
    std::vector<ModRef> party = {mref("randomizer", "", 7), mref("notebook", "0.6.1", 3)};
    std::vector<ModRef> me = {mref("randomizer", "", 8), mref("qol", "1.2", 1)};
    p = party_mod_problems(party, me);
    CHECK(has(p, K::kPartyOther, "randomizer") && has(p, K::kPartyHas, "notebook") && has(p, K::kYouHave, "qol"));
    CHECK(party_mod_problems(party, {party[1], party[0]}).empty());  // the same set in another order
    // codecs
    Bytes b;
    Writer w(b);
    put_mod_refs(w, me);
    put_mod_problems(w, p);
    Reader rd(b);
    std::vector<ModRef> me2;
    std::vector<ModProblem> p2;
    CHECK(get_mod_refs(rd, me2) && get_mod_problems(rd, p2) && rd.done() && me2 == me && p2.size() == p.size());
}

int main() {
    test_sha256();
    test_crypto();
    test_mod_rules();
    test_codes();
    test_pow();
    test_handshake();
    test_messages();
    test_channel_burst();
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
