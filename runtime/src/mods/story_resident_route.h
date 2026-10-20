// SPDX-License-Identifier: MPL-2.0
// A placed resident's mod route, the pure part (no game memory): the route record a walking resident
// follows with the game's own route walking, and the port's guards around it.
//
// A placed resident with a "path" (story_world_manifest.h ManifestPath) is created with the route
// marker as its route number. Its creation then asks for that route and gets this record instead of
// a room route (story_resident_routes.cpp); from then on the character's own Wait / Turn / Walk code
// walks it: its walk animation, turning in place at corners, ground following, stopping for Link and
// talking. The port adds the authored speed, the modes the game has no flag for (once; pauses off)
// and two guards the game lacks: a walker that makes no progress (a wall) skips to its next point,
// and after three skips in a row stops; one that drops far below its route (an edge) is put back
// where it last stood on the route and stops.
//
// One record per walking resident, in guest memory that full save states capture. Layout (big endian):
//   0x000 the game's route header (12 bytes): point count u16, next route u16 (none), argument u8,
//         flags u8 (bit 0: loop), switch u8 (none), padding, points pointer u32 (record + 0x10)
//   0x00C magic 'RRT1'
//   0x010 16 points of 16 bytes: 4 argument bytes (all 0xFF: no stop to look), x, y, z
//   0x110 owner process id; 0x114 last good position (x, y, z); 0x120 closest distance to its point since progress, unused
//   0x128 steps without progress u16; 0x12A skips in a row u8; 0x12B status u8 (Status)
//   0x12C pause random state u32; 0x130 unused u8; 0x131 configuration u8 (Config bits);
//   0x132 the point skipped last u8 (0xFF none); 0x133 skips since the last progress u8
//   0x134 speed per logic step f32 (0: the character's own); 0x138 route hash u32; 0x13C lowest point y f32
//   0x140 strolling: walk steps left before its next pause u16
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include "story_world_manifest.h"

namespace story_resident_route {

inline constexpr uint32_t kSlotBytes = 0x150, kSlots = 32, kMemoryBytes = kSlotBytes * kSlots;
inline constexpr uint32_t kMagic = 0x52525431;  // 'RRT1'
inline constexpr uint8_t kRouteMarker = 0xFE;   // a placed resident's route number: "the port's route"
inline constexpr uint8_t kNoPoint = 0xFF;       // no point (the guards' blocked point)
inline constexpr float kLogicRate = 30.f;        // logic steps per second (speeds are authored per second)
inline constexpr int16_t kTurnFallback = 1500;   // turning speed for a character whose own is 0
inline constexpr int16_t kBorrowedSteer = 1200;  // steering step of a walker with a borrowed walk (the game's walkers: 400)
inline constexpr unsigned kMaxPoints = 16;       // the position and up to 15 points

enum : uint32_t {
    kHeader = 0x000, kMagicAt = 0x00C, kPoints = 0x010, kOwner = 0x110, kGood = 0x114, kAnchor = 0x120,
    kStuck = 0x128, kSkips = 0x12A, kStatus = 0x12B, kRandom = 0x12C, kArmed = 0x130, kConfig = 0x131, kBlocked = 0x132, kInRow = 0x133,
    kSpeed = 0x134, kHash = 0x138, kLowest = 0x13C, kWalkLeft = 0x140,
};
enum Config : uint8_t { kLoop = 1, kBackAndForth = 2, kPauses = 4 };
enum class Status : uint8_t { Walking = 0, Skipped = 1, Stuck = 2, Fell = 3, Finished = 4 };
inline const char* status_name(Status s) {
    switch (s) {
    case Status::Walking: return "walking";
    case Status::Skipped: return "skipped a blocked point";
    case Status::Stuck: return "stopped: blocked";
    case Status::Fell: return "stopped: left the ground of its route";
    case Status::Finished: return "finished";
    }
    return "?";
}

// the guards' limits
inline constexpr float kProgress = 8.f;          // units of horizontal progress that count as moving
inline constexpr uint16_t kStuckSteps = 45;      // logic steps without progress before a skip
inline constexpr uint8_t kMaxSkips = 3;          // skips of the same point before it stops
inline constexpr float kFallBelow = 100.f;       // this far below the route's lowest point: fallen
inline constexpr float kGoodBelow = 50.f;        // a place counts as good down to this far below it
inline constexpr uint16_t kPauseWalkMin = 200, kPauseWalkMax = 300;  // the game's strolling rhythm

inline void put16(uint8_t* b, uint32_t at, uint16_t v) { b[at] = uint8_t(v >> 8); b[at + 1] = uint8_t(v); }
inline void put32(uint8_t* b, uint32_t at, uint32_t v) { for (int i = 0; i < 4; i++) b[at + i] = uint8_t(v >> (24 - 8 * i)); }
inline void putf(uint8_t* b, uint32_t at, float v) { uint32_t u; std::memcpy(&u, &v, 4); put32(b, at, u); }
inline uint32_t get32(const uint8_t* b, uint32_t at) { return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3]; }

// the speed per logic step for an authored route (0: the character's own)
inline float step_speed(const story_notebook::ManifestPath& p) { return p.speed_given ? p.speed / kLogicRate : 0.f; }

// a hash of what the record's route is (points, modes, speed): a restored record keeps its guard
// state only while it still describes the same route
inline uint32_t route_hash(const story_notebook::ManifestPath& p, const story_notebook::WorldPoint& start) {
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { for (int i = 0; i < 4; i++) { h ^= (v >> (8 * i)) & 0xFF; h *= 16777619u; } };
    auto f = [&](float v) { uint32_t u; std::memcpy(&u, &v, 4); mix(u); };
    f(start.x); f(start.y); f(start.z);
    for (const auto& q : p.points) { f(q.x); f(q.y); f(q.z); }
    mix(uint32_t(p.points.size())); mix(p.loop | p.back_and_forth << 1 | p.pauses << 2 | p.speed_given << 3); f(p.speed);
    return h;
}

// The record for a route starting at `start`, at guest address `slot`, with its guard state cleared.
inline std::array<uint8_t, kSlotBytes> encode(const story_notebook::ManifestPath& p, const story_notebook::WorldPoint& start, uint32_t slot) {
    std::array<uint8_t, kSlotBytes> b{};
    const unsigned count = unsigned(std::min<size_t>(p.points.size() + 1, kMaxPoints));
    put16(b.data(), kHeader + 0, uint16_t(count));
    put16(b.data(), kHeader + 2, 0xFFFF);
    b[kHeader + 4] = 0xFF;
    b[kHeader + 5] = p.loop ? 1 : 0;
    b[kHeader + 6] = 0xFF;
    put32(b.data(), kHeader + 8, slot + kPoints);
    put32(b.data(), kMagicAt, kMagic);
    float lowest = start.y;
    for (unsigned i = 0; i < count; i++) {
        const auto& q = i == 0 ? start : p.points[i - 1];
        const uint32_t at = kPoints + i * 16;
        put32(b.data(), at, 0xFFFFFFFFu);
        putf(b.data(), at + 4, q.x); putf(b.data(), at + 8, q.y); putf(b.data(), at + 12, q.z);
        lowest = std::min(lowest, q.y);
    }
    putf(b.data(), kGood, start.x); putf(b.data(), kGood + 4, start.y); putf(b.data(), kGood + 8, start.z);
    putf(b.data(), kAnchor, 1e9f); putf(b.data(), kAnchor + 4, 0);
    put32(b.data(), kRandom, route_hash(p, start) | 1u);
    b[kBlocked] = kNoPoint;
    b[kConfig] = uint8_t((p.loop ? kLoop : 0) | (p.back_and_forth ? kBackAndForth : 0) | (p.pauses ? kPauses : 0));
    putf(b.data(), kSpeed, step_speed(p));
    put32(b.data(), kHash, route_hash(p, start));
    putf(b.data(), kLowest, lowest);
    return b;
}

// The guards, one logic step of a walking resident. Progress is getting closer to the point it walks to
// (sliding along a wall is no progress; `anchor[0]` is the closest it got since). A point it can't
// reach is skipped; the third skip of the same point (with no pass of it between), or three skips in a
// row with no progress between them, stops the walker.
struct Guard {
    float good[3]{}, anchor[2]{1e9f, 0};
    uint16_t stuck = 0;
    uint8_t skips = 0;
    Status status = Status::Walking;
    uint8_t blocked = kNoPoint;  // the point it skipped last
    uint8_t in_row = 0;          // skips since its last progress
};
enum class Act { None, NextPoint, Stop, ReturnToGood };
// `walking`: it is in its walk and nothing (Link, a talk) holds it; `target`, `distance`: the point it
// walks to and how far (across) it is from it; `passed`: the point it passed this step, or kNoPoint;
// `lowest`: the route's lowest point.
inline Act guard_step(Guard& g, float x, float y, float z, bool walking, unsigned target, float distance, unsigned passed, float lowest) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(distance)) return Act::None;
    if (y < lowest - kFallBelow) {
        g.status = Status::Fell;
        return Act::ReturnToGood;
    }
    if (y >= lowest - kGoodBelow) g.good[0] = x, g.good[1] = y, g.good[2] = z;
    if (passed != kNoPoint) {
        g.anchor[0] = 1e9f, g.stuck = 0;
        if (passed == g.blocked) g.blocked = kNoPoint, g.skips = 0;
        if (g.status == Status::Skipped && g.blocked == kNoPoint) g.status = Status::Walking;
        return Act::None;
    }
    if (!walking) {
        g.anchor[0] = 1e9f, g.stuck = 0;
        return Act::None;
    }
    if (g.anchor[0] >= 1e8f) {  // a new point to walk to: the distance to beat
        g.anchor[0] = distance, g.stuck = 0;
        return Act::None;
    }
    if (distance <= g.anchor[0] - kProgress) {
        g.anchor[0] = distance, g.stuck = 0, g.in_row = 0;
        return Act::None;
    }
    if (++g.stuck < kStuckSteps) return Act::None;
    g.stuck = 0, g.anchor[0] = 1e9f;
    if (target == g.blocked) ++g.skips;
    else g.blocked = uint8_t(target), g.skips = 1;
    if (g.skips >= kMaxSkips || ++g.in_row >= kMaxSkips) {
        g.status = Status::Stuck;
        return Act::Stop;
    }
    g.status = Status::Skipped;
    return Act::NextPoint;
}

// Whether a walker at (x, z) has passed `target`, walking to it from `previous`: it is past the line
// through the target across its way, or within `arrival` of it. (The game's own test measures across
// the line between the target's two neighbours; on a mod route with sharp corners a walker can then be
// past several points at once and turn on the spot. This one only looks at the way it walks.)
inline bool passed(float px, float pz, float tx, float tz, float x, float z, float arrival) {
    const double dx = double(tx) - px, dz = double(tz) - pz, len = std::hypot(dx, dz);
    const double rx = double(x) - tx, rz = double(z) - tz;
    if (std::hypot(rx, rz) <= arrival) return true;
    if (len < 1e-3) return true;
    return (rx * dx + rz * dz) / len > 0;
}
// the point a walker comes from on its way to `index` (count points; forward or back; loop wraps)
inline unsigned previous_point(unsigned index, unsigned count, bool forward, bool loop) {
    if (count < 2) return 0;
    if (forward) return index > 0 ? index - 1 : (loop ? count - 1 : 1);
    return index + 1 < count ? index + 1 : (loop ? 0 : count - 2);
}
inline constexpr float kArrival = 4.f;  // units

// The point after `index` (of `count`) going forward (`forward`) for a skip: a loop wraps, the other
// modes turn at the ends (once only turns once it is finished, which the caller decides).
struct Next { unsigned index; bool forward; bool end; };
inline Next next_point(unsigned index, unsigned count, bool forward, bool loop) {
    if (count < 2) return {0, forward, true};
    if (loop) return {(index + (forward ? 1 : count - 1)) % count, forward, false};
    if (forward) return index + 1 < count ? Next{index + 1, true, false} : Next{count >= 2 ? count - 2 : 0, false, true};
    return index > 0 ? Next{index - 1, false, false} : Next{1, true, true};
}

// the strolling walk time for a character with none of its own: 200..300 logic steps
inline uint16_t pause_walk(uint32_t& state) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    if (!state) state = 1;
    return uint16_t(kPauseWalkMin + state % (kPauseWalkMax - kPauseWalkMin + 1));
}

}  // namespace story_resident_route
