// SPDX-License-Identifier: MPL-2.0
#include "protocol.h"

#include <cstring>

#include "crypto.h"
#include "sha256.h"

namespace online::proto {

const char* refusal_text(Refusal r) {
    switch (r) {
        case Refusal::kNone: return "ok";
        case Refusal::kProtocol: return "this server speaks another online protocol version";
        case Refusal::kGameData: return "this server plays another game version, region or seed";
        case Refusal::kFull: return "the server is full";
        case Refusal::kBanned: return "you are banned from this server";
        case Refusal::kBadProof: return "the join proof was not valid";
        case Refusal::kRateLimited: return "too many connections; try again later";
        case Refusal::kInviteOnly: return "this server is invite-only";
        case Refusal::kMalformed: return "malformed request";
        case Refusal::kShuttingDown: return "the server is shutting down";
        case Refusal::kKicked: return "you were removed from the server";
    }
    return "refused";
}

uint32_t pow_zero_bits(const uint8_t h[32]) {
    uint32_t bits = 0;
    for (int i = 0; i < 32; ++i) {
        if (h[i] == 0) {
            bits += 8;
            continue;
        }
        uint8_t b = h[i];
        while (!(b & 0x80)) {
            ++bits;
            b <<= 1;
        }
        break;
    }
    return bits;
}

static void pow_hash(const uint8_t salt[16], const uint8_t pub[32], uint64_t nonce, uint8_t out[32]) {
    uint8_t buf[56];
    std::memcpy(buf, salt, 16);
    std::memcpy(buf + 16, pub, 32);
    for (int i = 0; i < 8; ++i) buf[48 + i] = uint8_t(nonce >> (8 * i));
    sha256(buf, sizeof buf, out);
}

bool pow_check(const uint8_t salt[16], const uint8_t pub[32], uint64_t nonce, uint8_t difficulty) {
    if (difficulty == 0) return true;
    uint8_t h[32];
    pow_hash(salt, pub, nonce, h);
    return pow_zero_bits(h) >= difficulty;
}

uint64_t pow_solve(const uint8_t salt[16], const uint8_t pub[32], uint8_t difficulty) {
    if (difficulty == 0) return 0;
    for (uint64_t n = 0;; ++n)
        if (pow_check(salt, pub, n, difficulty)) return n;
}

uint64_t key_hash(const uint8_t pub[32]) {
    uint8_t h[32];
    crypto::hash(pub, 32, h);
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= uint64_t(h[i]) << (8 * i);
    return v;
}

namespace {
constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";  // Crockford base32

uint32_t check10(uint64_t code40) {
    uint8_t b[5];
    for (int i = 0; i < 5; ++i) b[i] = uint8_t(code40 >> (8 * i));
    uint8_t h[32];
    sha256(b, 5, h);
    return (uint32_t(h[0]) | (uint32_t(h[1]) << 8)) & 0x3FF;
}

int digit(char c) {
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (c == 'I' || c == 'L') c = '1';
    if (c == 'O') c = '0';
    for (int i = 0; i < 32; ++i)
        if (kAlphabet[i] == c) return i;
    return -1;
}
}  // namespace

std::string friend_code(uint64_t kh) {
    uint64_t c = code40_of(kh);
    uint64_t v = (c << 10) | check10(c);  // 50 bits, 10 digits
    char d[10];
    for (int i = 9; i >= 0; --i) {
        d[i] = kAlphabet[v & 31];
        v >>= 5;
    }
    std::string s;
    for (int i = 0; i < 10; ++i) {
        if (i == 4 || i == 8) s += '-';
        s += d[i];
    }
    return s;
}

bool parse_friend_code(std::string_view text, uint64_t& code40) {
    uint64_t v = 0;
    int n = 0;
    for (char ch : text) {
        if (ch == '-' || ch == ' ') continue;
        int d = digit(ch);
        if (d < 0 || ++n > 10) return false;
        v = (v << 5) | uint64_t(d);
    }
    if (n != 10) return false;
    uint64_t c = v >> 10;
    if ((v & 0x3FF) != check10(c)) return false;
    code40 = c;
    return true;
}

std::string Where::stage_name() const {
    size_t n = 0;
    while (n < stage.size() && stage[n]) ++n;
    return std::string(stage.data(), n);
}

Where Where::of(std::string_view s, int room, int layer) {
    Where w;
    for (size_t i = 0; i < s.size() && i < w.stage.size(); ++i) w.stage[i] = s[i];
    w.room = int8_t(room);
    w.layer = uint8_t(layer);
    return w;
}

void put_where(Writer& w, const Where& p) {
    w.bytes(p.stage.data(), p.stage.size());
    w.s8(p.room);
    w.u8(p.layer);
}

Where get_where(Reader& r) {
    Where p;
    r.bytes(reinterpret_cast<uint8_t*>(p.stage.data()), p.stage.size());
    p.room = r.s8();
    p.layer = r.u8();
    // stage codes are printable ASCII, zero padded; anything else is malformed
    bool end = false;
    for (char c : p.stage) {
        if (c == 0) {
            end = true;
            continue;
        }
        if (end || c < 0x21 || c > 0x7E) r.fail();
    }
    return p;
}

void put_pose_header(Writer& w, const PoseHeader& h) {
    put_where(w, h.where);
    w.f32(h.x);
    w.f32(h.y);
    w.f32(h.z);
    w.u16(h.yaw);
    w.u16(h.anim);
    w.u8(h.frame);
    w.u8(h.flags);
    w.u8(h.boat);
}

PoseHeader get_pose_header(Reader& r) {
    PoseHeader h;
    h.where = get_where(r);
    h.x = r.f32();
    h.y = r.f32();
    h.z = r.f32();
    h.yaw = r.u16();
    h.anim = r.u16();
    h.frame = r.u8();
    h.flags = r.u8();
    h.boat = r.u8();
    // no NaN or absurd coordinates
    auto sane = [](float f) { return f == f && f > -1.0e7f && f < 1.0e7f; };
    if (!sane(h.x) || !sane(h.y) || !sane(h.z)) r.fail();
    return h;
}

}  // namespace online::proto
