// SPDX-License-Identifier: MPL-2.0
// A mod's room paths, the pure part (no game memory): the manifest's "paths" section, the numbers the
// port gives them, the record the game reads in place of a room path, and the game's own area test.
//
// The game keeps a room's paths in its room data (dStage_roomDt's path table: a count and 12-byte
// entries); actors name one by number in their parameters and look it up with dPath_GetRoomPath(number,
// room). A mod path is never added to that table: the port answers the lookup with its own record (the
// same layout), only for a number the room's table does not have and only for the mod's own actors
// (story_room_paths.cpp). Numbers come from kFirstNumber..kLastNumber: the largest room table of the
// game has 56 paths, so these never meet a game path, and the game's lookup runs first anyway.
//
// One record per path, in guest memory that full save states capture. Layout (big endian):
//   0x000 the game's path header (12 bytes): point count u16, next path u16 (none), argument u8 (0xFF),
//         flags u8 (bit 0: closed), switch u8 (none), padding, points pointer u32 (record + kPoints)
//   0x00C magic 'MRP1'; 0x010 number u8, room u8; 0x014 path hash u32; 0x018 stage name (8 bytes)
//   0x020 kMaxPoints points of 16 bytes: 4 argument bytes (0xFF), x, y, z
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace story_room_path {

struct Point { float x = 0, y = 0, z = 0; bool operator==(const Point&) const = default; };
// A manifest path: "paths": [{"id", "stage", "room", "points": [{x, y, z}, ...], "closed": bool}]
struct Path {
    std::string id, stage;
    unsigned room = 0;
    std::vector<Point> points;
    bool closed = false;  // an area (the last point joins the first) rather than a line
    std::vector<bool> release;  // per point (empty: none): a Kargaroc lets its rider go there (point argument 3 = 5)
    std::string next;           // the id of the path that follows it (the game's "next path": a Wizzrobe summoner's summon points), "" none
    bool operator==(const Path&) const = default;
    bool releases(size_t i) const { return i < release.size() && release[i]; }
};
inline constexpr uint8_t kReleaseRider = 5;  // d_a_bb: a point whose argument 3 is 5 releases the rider (6 would delete it: never written)

inline constexpr unsigned kMinPoints = 2, kMinAreaPoints = 3, kMaxPoints = 32;
inline constexpr unsigned kMaxPerMod = 16;    // paths one mod declares
inline constexpr unsigned kMaxPerRoom = 24;   // paths of all enabled mods in one room
inline constexpr unsigned kSlots = 64;        // records (paths of all enabled mods)
inline constexpr uint8_t kFirstNumber = 0xC0, kLastNumber = 0xFD;  // 0xFE: a placed actor's own route, 0xFF: none
inline constexpr uint32_t kMagic = 0x4D525031;  // 'MRP1'
inline constexpr float kMinSpacing = 1.f;       // points are more than this apart (across)
inline constexpr uint32_t kSlotBytes = 0x020 + kMaxPoints * 16, kMemoryBytes = kSlotBytes * kSlots;
enum : uint32_t { kHeader = 0x000, kMagicAt = 0x00C, kNumber = 0x010, kRoom = 0x011, kHash = 0x014, kStage = 0x018, kPoints = 0x020 };

inline bool is_number(uint32_t n) { return n >= kFirstNumber && n <= kLastNumber; }

inline void put16(uint8_t* b, uint32_t at, uint16_t v) { b[at] = uint8_t(v >> 8); b[at + 1] = uint8_t(v); }
inline void put32(uint8_t* b, uint32_t at, uint32_t v) { for (int i = 0; i < 4; i++) b[at + i] = uint8_t(v >> (24 - 8 * i)); }
inline void putf(uint8_t* b, uint32_t at, float v) { uint32_t u; std::memcpy(&u, &v, 4); put32(b, at, u); }
inline uint32_t get32(const uint8_t* b, uint32_t at) { return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3]; }

// twice the signed area across (x, z): the sign is the winding
inline double winding(const std::vector<Point>& p) {
    double s = 0;
    for (size_t i = 0; i < p.size(); i++) {
        const auto& a = p[i];
        const auto& b = p[(i + 1) % p.size()];
        s += double(a.x) * b.z - double(b.x) * a.z;
    }
    return s;
}
// The points as the record holds them. The game's area test (inside() below) takes an area's inside
// to lie on one side of its points' order; an area is written in that order whichever way it was drawn.
inline std::vector<size_t> game_indices(const Path& p) {
    std::vector<size_t> out(p.points.size());
    for (size_t i = 0; i < out.size(); i++) out[i] = i;
    if (p.closed && out.size() >= 3 && winding(p.points) > 0) std::reverse(out.begin() + 1, out.end());
    return out;
}
inline std::vector<Point> game_order(const Path& p) {
    std::vector<Point> out;
    for (size_t i : game_indices(p)) out.push_back(p.points[i]);
    return out;
}

inline uint32_t hash(const Path& p, uint8_t number) {
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { for (int i = 0; i < 4; i++) { h ^= (v >> (8 * i)) & 0xFF; h *= 16777619u; } };
    auto f = [&](float v) { uint32_t u; std::memcpy(&u, &v, 4); mix(u); };
    for (size_t i = 0; i < p.points.size(); i++) { const auto& q = p.points[i]; f(q.x); f(q.y); f(q.z); mix(p.releases(i)); }
    mix(uint32_t(p.points.size())); mix(p.closed); mix(number); mix(p.room);
    for (char c : p.next) mix(uint8_t(c));
    for (char c : p.stage) mix(uint8_t(c));
    return h;
}

// The record of `p` with `number`, at guest address `slot`.
inline std::array<uint8_t, kSlotBytes> encode(const Path& p, uint8_t number, uint32_t slot, uint16_t next = 0xFFFF) {
    std::array<uint8_t, kSlotBytes> b{};
    const auto order = game_indices(p);
    const unsigned count = unsigned(std::min<size_t>(order.size(), kMaxPoints));
    put16(b.data(), kHeader + 0, uint16_t(count));
    put16(b.data(), kHeader + 2, next);  // the next path's number (a mod path of the same room), 0xFFFF none
    b[kHeader + 4] = 0xFF;
    b[kHeader + 5] = p.closed ? 1 : 0;
    b[kHeader + 6] = 0xFF;
    put32(b.data(), kHeader + 8, slot + kPoints);
    put32(b.data(), kMagicAt, kMagic);
    b[kNumber] = number;
    b[kRoom] = uint8_t(p.room);
    put32(b.data(), kHash, hash(p, number));
    std::memcpy(b.data() + kStage, p.stage.c_str(), std::min<size_t>(p.stage.size(), 8));
    for (unsigned i = 0; i < count; i++) {
        const uint32_t at = kPoints + i * 16;
        const auto& q = p.points[order[i]];
        put32(b.data(), at, p.releases(order[i]) ? 0xFFFFFF00u | kReleaseRider : 0xFFFFFFFFu);
        putf(b.data(), at + 4, q.x); putf(b.data(), at + 8, q.y); putf(b.data(), at + 12, q.z);
    }
    return b;
}

// cM_atan2s(dx, dz): the game's angle (0 along +z, 0x4000 along +x)
inline int16_t angle(double dx, double dz) { return int16_t(int32_t(std::lround(std::atan2(dx, dz) * 32768.0 / 3.14159265358979323846))); }
// The game's area test (dNpc_PathRun_c::chkInside after setNearPathIndx, the wall cannon's): the
// point nearest (across) to (x, z), and whether (x, z) lies between its two neighbours' directions,
// turning from the next one towards the previous one (an area in game order; meant for convex areas).
inline bool inside(const std::vector<Point>& pts, float x, float z) {
    if (pts.size() < 3) return false;
    size_t nearest = 0;
    double best = 1e30;
    for (size_t i = 0; i < pts.size(); i++) {
        const double d = std::hypot(double(x) - pts[i].x, double(z) - pts[i].z);
        if (d < best) best = d, nearest = i;
    }
    const auto& c = pts[nearest];
    const auto& prev = pts[(nearest + pts.size() - 1) % pts.size()];
    const auto& next = pts[(nearest + 1) % pts.size()];
    const int16_t first = angle(double(prev.x) - c.x, double(prev.z) - c.z), direction = angle(double(x) - c.x, double(z) - c.z),
                  last = angle(double(next.x) - c.x, double(next.z) - c.z);
    const uint16_t a = uint16_t(direction - last), b = uint16_t(first - last);
    return a > 0 && a < b;
}
inline bool inside(const Path& p, float x, float z) { return p.closed && inside(game_order(p), x, z); }

// Whether segments ab and cd cross (across; touching ends of neighbours aside)
inline bool crosses(const Point& a, const Point& b, const Point& c, const Point& d) {
    auto side = [](const Point& p, const Point& q, const Point& r) {
        const double v = (double(q.x) - p.x) * (double(r.z) - p.z) - (double(q.z) - p.z) * (double(r.x) - p.x);
        return v > 1e-9 ? 1 : v < -1e-9 ? -1 : 0;
    };
    const int s1 = side(a, b, c), s2 = side(a, b, d), s3 = side(c, d, a), s4 = side(c, d, b);
    return s1 * s2 < 0 && s3 * s4 < 0;
}
// An area whose edges cross itself (the game's test then gives no sensible inside)
inline bool self_crossing(const std::vector<Point>& p) {
    const size_t n = p.size();
    if (n < 4) return false;
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++) {
            if (j == i + 1 || (i == 0 && j == n - 1)) continue;  // neighbours share a point
            if (crosses(p[i], p[(i + 1) % n], p[j], p[(j + 1) % n])) return true;
        }
    return false;
}
// An area whose corners all turn the same way (the areas the game's test is meant for)
inline bool convex(const std::vector<Point>& p) {
    const size_t n = p.size();
    if (n < 3) return false;
    int sign = 0;
    for (size_t i = 0; i < n; i++) {
        const auto &a = p[i], &b = p[(i + 1) % n], &c = p[(i + 2) % n];
        const double v = (double(b.x) - a.x) * (double(c.z) - b.z) - (double(b.z) - a.z) * (double(c.x) - b.x);
        const int s = v > 1e-9 ? 1 : v < -1e-9 ? -1 : 0;
        if (!s) continue;
        if (sign && s != sign) return false;
        sign = s;
    }
    return sign != 0;
}

// Why a path's shape is refused ("" when it is fine)
inline std::string shape_problem(const Path& p) {
    if (p.points.size() < kMinPoints || p.points.size() > kMaxPoints) return "a path has 2 to 32 points";
    for (size_t n = 1; n < p.points.size(); n++)
        if (std::hypot(double(p.points[n].x) - p.points[n - 1].x, double(p.points[n].z) - p.points[n - 1].z) <= kMinSpacing)
            return "path points must be more than one unit apart (across) from the one before";
    if (p.closed) {
        if (p.points.size() < kMinAreaPoints) return "a closed path (an area) has at least 3 points";
        if (std::hypot(double(p.points.back().x) - p.points.front().x, double(p.points.back().z) - p.points.front().z) <= kMinSpacing)
            return "a closed path's last point must be more than one unit from its first (it joins it on its own)";
        if (self_crossing(p.points)) return "a closed path's edges must not cross each other";
        if (std::fabs(winding(p.points)) < 1.0) return "a closed path must enclose an area";
    }
    return {};
}

// The numbers of a room's paths: `taken` are the numbers already used in that room; the lowest free one
// (0: none left)
inline uint8_t free_number(const std::vector<uint8_t>& taken) {
    for (unsigned n = kFirstNumber; n <= kLastNumber; n++)
        if (std::find(taken.begin(), taken.end(), uint8_t(n)) == taken.end()) return uint8_t(n);
    return 0;
}

}  // namespace story_room_path
