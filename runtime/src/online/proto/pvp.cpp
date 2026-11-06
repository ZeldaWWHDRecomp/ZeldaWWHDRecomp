// SPDX-License-Identifier: MPL-2.0
#include "pvp.h"

#include <algorithm>
#include <cmath>

namespace online::proto {

// Measured positions (facts, no game data): each island's centre is the middle of its land in the sea room's
// collision (the points above the water, the main landmass), as an offset from the square's centre; the radius
// reaches its farthest land point plus a ring of 5,000 of water, rounded up. One square has no island collision
// (a structure that rises from the sea): its centre and radius come from the room's object positions. The list
// with each island's name is in server/server.example.toml; operators can change it ([pvp] zones).
std::vector<PvpZone> default_pvp_zones() {
    return {
        {10, -300, -100, 12000},
        {43, -200, 19200, 15500},
        {12, 3500, -5100, 16000},
        {40, 15400, -700, 10000},
        {16, 100, 20100, 10000},   // (the ring reaches the boat's landing point, 9,300 out)
        {0, 1800, -800, 15500},
        {25, 0, 0, 18000},         // (from object positions: the boat's arrival point is 14,000 out)
        {44, 20000, 19400, 11000},
        {3, 19800, -19600, 12000},
    };
}

void put_s24(Writer& w, float f) {
    int32_t v = int32_t(std::lround(f));
    v = std::clamp(v, -0x800000, 0x7FFFFF);
    uint32_t u = uint32_t(v) & 0xFFFFFF;
    w.u8(uint8_t(u));
    w.u8(uint8_t(u >> 8));
    w.u8(uint8_t(u >> 16));
}

float get_s24(Reader& r) {
    uint32_t u = r.u8();
    u |= uint32_t(r.u8()) << 8;
    u |= uint32_t(r.u8()) << 16;
    if (u & 0x800000) u |= 0xFF000000u;
    return float(int32_t(u));
}

static uint16_t u16_of(float units_16) { return uint16_t(std::clamp(units_16 / 16.0f, 0.0f, 65535.0f)); }
static int16_t s16_of(float units_16) { return int16_t(std::clamp(units_16 / 16.0f, -32768.0f, 32767.0f)); }

void put_pvp_rules(Writer& w, const PvpRules& r) {
    w.u8(uint8_t(r.mode));
    w.u16(uint16_t(std::min<uint32_t>(r.protect_ms, 65535)));
    w.u16(uint16_t(std::min<uint32_t>(r.enter_delay_ms, 65535)));
    w.u16(u16_of(r.hide_band));
    w.u8(r.ko_rupee_percent);
    w.u16(r.ko_rupee_max);
    w.u16(u16_of(r.max_hit_dist));
    w.u8(r.cooldown_kos);
    w.u16(r.cooldown_window_s);
    w.u16(r.cooldown_s);
    size_t n = std::min<size_t>(r.zones.size(), 255);
    w.u8(uint8_t(n));
    for (size_t i = 0; i < n; ++i) {
        w.u8(r.zones[i].square);
        w.s16(s16_of(r.zones[i].cx));
        w.s16(s16_of(r.zones[i].cz));
        w.u16(u16_of(r.zones[i].radius));
    }
}

bool get_pvp_rules(Reader& r, PvpRules& o) {
    o = PvpRules();
    uint8_t mode = r.u8();
    if (mode > 2) return false;
    o.mode = PvpMode(mode);
    o.protect_ms = r.u16();
    o.enter_delay_ms = r.u16();
    o.hide_band = float(r.u16()) * 16;
    o.ko_rupee_percent = r.u8();
    o.ko_rupee_max = r.u16();
    o.max_hit_dist = float(r.u16()) * 16;
    o.cooldown_kos = r.u8();
    o.cooldown_window_s = r.u16();
    o.cooldown_s = r.u16();
    size_t n = r.u8();
    for (size_t i = 0; i < n && r.ok(); ++i) {
        PvpZone z;
        z.square = r.u8();
        z.cx = float(r.s16()) * 16;
        z.cz = float(r.s16()) * 16;
        z.radius = float(r.u16()) * 16;
        if (z.square >= 49) return false;
        o.zones.push_back(z);
    }
    return r.ok() && o.ko_rupee_percent <= 100;
}

void put_pvp_hit(Writer& w, const PvpHit& h) {
    w.u16(h.other);
    w.u8(h.kind);
    w.u8(h.damage);
    put_s24(w, h.x);
    put_s24(w, h.y);
    put_s24(w, h.z);
}

bool get_pvp_hit(Reader& r, PvpHit& h) {
    h.other = r.u16();
    h.kind = r.u8();
    h.damage = r.u8();
    h.x = get_s24(r);
    h.y = get_s24(r);
    h.z = get_s24(r);
    return r.ok() && h.kind >= kPvpCannon && h.kind <= kPvpSword;
}

void put_pvp_shot(Writer& w, const PvpShot& s, bool with_shooter) {
    if (with_shooter) w.u16(s.shooter);
    w.u8(s.kind);
    put_s24(w, s.x);
    put_s24(w, s.y);
    put_s24(w, s.z);
    w.s16(int16_t(std::clamp(s.vx / 4.0f, -32768.0f, 32767.0f)));
    w.s16(int16_t(std::clamp(s.vy / 4.0f, -32768.0f, 32767.0f)));
    w.s16(int16_t(std::clamp(s.vz / 4.0f, -32768.0f, 32767.0f)));
}

bool get_pvp_shot(Reader& r, PvpShot& s, bool with_shooter) {
    if (with_shooter) s.shooter = r.u16();
    s.kind = r.u8();
    s.x = get_s24(r);
    s.y = get_s24(r);
    s.z = get_s24(r);
    s.vx = float(r.s16()) * 4;
    s.vy = float(r.s16()) * 4;
    s.vz = float(r.s16()) * 4;
    return r.ok() && s.kind >= kPvpCannon && s.kind <= kPvpSword;
}

}  // namespace online::proto
