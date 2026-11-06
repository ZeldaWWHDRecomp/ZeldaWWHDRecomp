// SPDX-License-Identifier: MPL-2.0
#include "handshake.h"

#include <cstdio>
#include <cstring>

namespace online::proto {

namespace {
constexpr size_t kMaxName = 24, kMaxTag = 16, kMaxServerName = 48, kMaxContact = 96, kMaxRules = 600;

void put_text16(Writer& w, const std::string& s, size_t cap) {
    size_t n = s.size() > cap ? cap : s.size();
    w.u16(uint16_t(n));
    w.bytes(s.data(), n);
}
std::string get_text16(Reader& r, size_t cap) {
    size_t n = r.u16();
    if (n > cap) {
        r.fail();
        return {};
    }
    return std::string(r.view(n));
}
// display text: printable, no control characters
bool clean_text(const std::string& s) {
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7F) return false;
    return true;
}
}  // namespace

Bytes encode_connect(const Connect& c) {
    Bytes b;
    Writer w(b);
    w.u8(kPktConnect);
    w.u16(c.proto);
    w.u32(c.nonce);
    b.resize(kMaxDatagram, 0);  // padding: the challenge is never larger than this
    return b;
}

bool decode_connect(const uint8_t* d, size_t n, Connect& c) {
    if (n < kMaxDatagram) return false;  // unpadded: no answer (anti-amplification)
    Reader r(d, n);
    if (r.u8() != kPktConnect) return false;
    c.proto = r.u16();
    c.nonce = r.u32();
    return r.ok();
}

static void put_info(Writer& w, const ServerInfo& i) {
    w.str8(i.name.substr(0, kMaxServerName));
    w.str8(i.contact.substr(0, kMaxContact));
    put_text16(w, i.rules, kMaxRules);
    w.u16(i.players);
    w.u16(i.capacity);
    w.u8(i.flags);
    w.bytes(i.pub, 32);
    w.u8(i.mod_flags);
    w.u8(i.rule_pages);
    w.u32(i.rules_digest);
    w.u8(i.party_max);
}
static ServerInfo get_info(Reader& r) {
    ServerInfo i;
    i.name = r.str8(kMaxServerName);
    i.contact = r.str8(kMaxContact);
    i.rules = get_text16(r, kMaxRules);
    i.players = r.u16();
    i.capacity = r.u16();
    i.flags = r.u8();
    r.bytes(i.pub, 32);
    i.mod_flags = r.u8();
    i.rule_pages = r.u8();
    i.rules_digest = r.u32();
    i.party_max = r.u8();
    i.mods.vanilla_only = (i.mod_flags & 1) != 0;
    return i;
}

Bytes encode_challenge(const Challenge& c, const crypto::Identity& server) {
    Bytes b;
    Writer w(b);
    w.u8(kPktChallenge);
    w.u16(c.proto);
    w.u32(c.nonce);
    w.bytes(c.salt, 16);
    w.u32(c.bucket);
    w.u8(c.difficulty);
    ServerInfo i = c.info;
    std::memcpy(i.pub, server.pub, 32);
    put_info(w, i);
    uint8_t sig[64];
    crypto::sign(server, b.data(), b.size(), sig);
    w.bytes(sig, 64);
    return b;
}

bool decode_challenge(const uint8_t* d, size_t n, Challenge& c) {
    Reader r(d, n);
    if (r.u8() != kPktChallenge) return false;
    c.proto = r.u16();
    c.nonce = r.u32();
    r.bytes(c.salt, 16);
    c.bucket = r.u32();
    c.difficulty = r.u8();
    c.info = get_info(r);
    size_t signed_len = r.pos();
    r.bytes(c.sig, 64);
    if (!r.done()) return false;
    return crypto::verify(c.info.pub, d, signed_len, c.sig);
}

Bytes encode_hello(const Hello& h, const crypto::Identity& me) {
    Bytes b;
    Writer w(b);
    w.u8(kPktHello);
    w.u16(h.proto);
    w.u32(h.nonce);
    w.u64(h.game_data);
    w.bytes(h.salt, 16);
    w.u32(h.bucket);
    w.bytes(me.pub, 32);
    w.bytes(h.eph, 32);
    w.u64(h.pow);
    w.str8(h.name.substr(0, kMaxName));
    w.u32(h.look);
    w.u8(h.device);
    w.u8(h.near_cap);
    w.u8(h.mid_cap);
    w.u8(h.resume ? 1 : 0);
    if (h.resume) w.bytes(h.token, 32);
    w.str8(h.tag.substr(0, kMaxTag));
    put_mod_refs(w, h.mods);
    uint8_t sig[64];
    crypto::sign(me, b.data(), b.size(), sig);
    w.bytes(sig, 64);
    return b;
}

bool decode_hello(const uint8_t* d, size_t n, Hello& h, size_t& signed_len) {
    Reader r(d, n);
    if (r.u8() != kPktHello) return false;
    h.proto = r.u16();
    h.nonce = r.u32();
    h.game_data = r.u64();
    r.bytes(h.salt, 16);
    h.bucket = r.u32();
    r.bytes(h.pub, 32);
    r.bytes(h.eph, 32);
    h.pow = r.u64();
    h.name = r.str8(kMaxName);
    h.look = r.u32();
    h.device = r.u8();
    h.near_cap = r.u8();
    h.mid_cap = r.u8();
    h.resume = r.u8() != 0;
    if (h.resume) r.bytes(h.token, 32);
    h.tag = r.str8(kMaxTag);
    if (!get_mod_refs(r, h.mods)) return false;
    signed_len = r.pos();
    r.bytes(h.sig, 64);
    if (!r.done() || h.device > kBot || !clean_text(h.name) || !clean_text(h.tag)) return false;
    return true;
}

bool hello_signature_ok(const uint8_t* d, size_t signed_len, const Hello& h) {
    return crypto::verify(h.pub, d, signed_len, h.sig);
}

static void welcome_signed_part(Bytes& b, const Welcome& w, const uint8_t client_eph[32]) {
    Writer x(b);
    x.bytes("wwhd-online welcome", 19);
    x.u32(w.nonce);
    x.u32(w.conn_id);
    x.bytes(client_eph, 32);
    x.bytes(w.eph, 32);
}

Bytes encode_welcome(const Welcome& w, const crypto::Identity& server, const uint8_t client_eph[32],
                     const uint8_t s2c_key[32]) {
    Bytes b;
    Writer x(b);
    x.u8(kPktWelcome);
    x.u32(w.nonce);
    x.u32(w.conn_id);
    x.bytes(w.eph, 32);
    Bytes sp;
    welcome_signed_part(sp, w, client_eph);
    uint8_t sig[64];
    crypto::sign(server, sp.data(), sp.size(), sig);
    x.bytes(sig, 64);
    size_t ad_n = b.size();
    Bytes inner;
    Writer y(inner);
    y.u16(w.id);
    y.bytes(w.token, 32);
    y.u8(w.tick_hz);
    y.u8(w.resumed ? 1 : 0);
    uint8_t tag[16];
    crypto::seal(s2c_key, 0, b.data(), ad_n, inner.data(), inner.size(), tag);
    x.bytes(inner.data(), inner.size());
    x.bytes(tag, 16);
    return b;
}

bool decode_welcome_outer(const uint8_t* d, size_t n, Welcome& w) {
    Reader r(d, n);
    if (r.u8() != kPktWelcome) return false;
    w.nonce = r.u32();
    w.conn_id = r.u32();
    r.bytes(w.eph, 32);
    r.bytes(w.sig, 64);
    return r.ok() && r.left() == 36 + 16;
}

bool decode_welcome_inner(const uint8_t* d, size_t n, const uint8_t server_pub[32], const uint8_t client_eph[32],
                          const uint8_t s2c_key[32], Welcome& w) {
    if (!decode_welcome_outer(d, n, w)) return false;
    Bytes sp;
    welcome_signed_part(sp, w, client_eph);
    if (!crypto::verify(server_pub, sp.data(), sp.size(), w.sig)) return false;
    size_t ad_n = 1 + 4 + 4 + 32 + 64;
    Bytes inner(d + ad_n, d + n - 16);
    if (!crypto::open(s2c_key, 0, d, ad_n, inner.data(), inner.size(), d + n - 16)) return false;
    Reader r(inner);
    w.id = r.u16();
    r.bytes(w.token, 32);
    w.tick_hz = r.u8();
    w.resumed = r.u8() != 0;
    return r.done();
}

Bytes encode_refuse(const Refuse& f) {
    Bytes b;
    Writer w(b);
    w.u8(kPktRefuse);
    w.u32(f.nonce);
    w.u8(uint8_t(f.reason));
    w.u16(f.retry_s);
    w.str8(f.text.substr(0, 200));
    if (f.reason == Refusal::kMods) put_mod_problems(w, f.mod_problems);
    return b;
}

bool decode_refuse(const uint8_t* d, size_t n, Refuse& f) {
    Reader r(d, n);
    if (r.u8() != kPktRefuse) return false;
    f.nonce = r.u32();
    f.reason = Refusal(r.u8());
    f.retry_s = r.u16();
    f.text = r.str8(200);
    if (f.reason == Refusal::kMods && !get_mod_problems(r, f.mod_problems)) return false;
    return r.done() && clean_text(f.text);
}

Bytes encode_rules_req(const RulesReq& q) {
    Bytes b;
    Writer w(b);
    w.u8(kPktRulesReq);
    w.u16(kProtoVersion);
    w.u32(q.nonce);
    w.u8(q.page);
    b.resize(kMaxDatagram, 0);  // padding: a page is never larger than this
    return b;
}

bool decode_rules_req(const uint8_t* d, size_t n, RulesReq& q) {
    if (n < kMaxDatagram) return false;
    Reader r(d, n);
    if (r.u8() != kPktRulesReq) return false;
    r.u16();
    q.nonce = r.u32();
    q.page = r.u8();
    return r.ok();
}

std::vector<std::vector<ModRule>> rule_pages(const ModRules& rules) {
    std::vector<std::vector<ModRule>> pages(1);
    size_t size = 0;
    for (auto& r : rules.rules) {
        Bytes one;
        Writer w(one);
        put_mod_rule(w, r);
        if (size + one.size() > 1000 && !pages.back().empty()) {
            pages.emplace_back();
            size = 0;
        }
        pages.back().push_back(r);
        size += one.size();
    }
    return pages;
}

Bytes encode_rules_page(const RulesPage& p, const crypto::Identity& server) {
    Bytes b;
    Writer w(b);
    w.u8(kPktRules);
    w.u32(p.nonce);
    w.u8(p.page);
    w.u8(p.pages);
    w.u8(p.vanilla_only ? 1 : 0);
    w.u8(uint8_t(p.rules.size()));
    for (auto& r : p.rules) put_mod_rule(w, r);
    uint8_t sig[64];
    crypto::sign(server, b.data(), b.size(), sig);
    w.bytes(sig, 64);
    return b;
}

bool decode_rules_page(const uint8_t* d, size_t n, const uint8_t server_pub[32], RulesPage& p) {
    Reader r(d, n);
    if (r.u8() != kPktRules) return false;
    p.nonce = r.u32();
    p.page = r.u8();
    p.pages = r.u8();
    p.vanilla_only = r.u8() != 0;
    size_t c = r.u8();
    for (size_t i = 0; i < c && r.ok(); ++i) {
        ModRule m;
        if (!get_mod_rule(r, m)) return false;
        p.rules.push_back(std::move(m));
    }
    size_t signed_len = r.pos();
    uint8_t sig[64];
    r.bytes(sig, 64);
    if (!r.done()) return false;
    return crypto::verify(server_pub, d, signed_len, sig);
}

bool handshake_nonce(const uint8_t* d, size_t n, uint32_t& nonce) {
    if (n < 7) return false;
    Reader r(d, n);
    uint8_t k = r.u8();
    if (k == kPktChallenge) r.u16();
    else if (k != kPktWelcome && k != kPktRefuse && k != kPktRules) return false;
    nonce = r.u32();
    return r.ok();
}

std::string fingerprint(const uint8_t pub[32]) {
    uint8_t h[32];
    crypto::hash(pub, 32, h);
    char s[40];
    std::snprintf(s, sizeof s, "%02X%02X %02X%02X %02X%02X %02X%02X", h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
    return s;
}

}  // namespace online::proto
