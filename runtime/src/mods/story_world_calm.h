// SPDX-License-Identifier: MPL-2.0
// Calm water around a mod island (docs/world-objects.md "Calm water"): a static sea world object's
// "calm_water" zone lets the sea's waves die down as Link comes near, the way the game calms the sea at
// its own islands.
//
// The game's mechanism (d_a_sea, daSea_packet_c): the whole sea around the player is one wave field whose
// height is scaled by a "flat" factor between 0 (still water) and 1 (full waves). Each frame
// CalcFlatInterTarget works it out from where the player is: 0 inside a square whose wave height (the sea
// stage's MULT entry) is 0, as at Windfall, Outset or Dragon Roost, and in the neighbouring squares 0 up to
// 12800 units from such a square, then rising in a straight line to 1 at 25600 units. The visible sea,
// the water height Link, the boat and everything else read (sea_calcWave) and the boat's rocking all come
// from that one field, so they agree. With the factor at 0 the water lies at the sea's own still level
// (the sea actor's height + 1), the level of the game's own landings.
//
// A zone is a circle on the sea: still water within `radius` of its centre, the waves back in full
// `falloff` further out, in a straight line as the game's. The zones of every mod and the game's own
// value are combined by taking the smallest, so the order never matters. Without a zone nothing changes.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace story_world_calm {
inline constexpr float kGameFalloff = 12800;  // CalcFlatInterTarget: the ramp from still water to full waves
inline constexpr float kMinRadius = 100, kMaxRadius = 100000;  // up to one sea square (1 km)
inline constexpr float kMinFalloff = 800, kMaxFalloff = 25600;  // one sea grid cell up to the game's whole fade
inline constexpr float kMaxOffset = 100000;
inline constexpr unsigned kMaxPerMod = 8;
inline constexpr size_t kMaxZones = 64;

struct Settings {
    bool enabled = false;
    float radius = 0, falloff = kGameFalloff;
    float offset_x = 0, offset_z = 0;  // the centre, from the object's position, turned with it
    bool operator==(const Settings&) const = default;
};
struct Zone {
    float x = 0, z = 0, radius = 0, falloff = kGameFalloff;
};

// the zone of an object at (x, z) turned by yaw degrees about the vertical axis (as its collision turns)
inline Zone zone_of(const Settings& s, float x, float z, float yaw_degrees) {
    const double r = double(yaw_degrees) * 3.14159265358979323846 / 180.0, c = std::cos(r), n = std::sin(r);
    const double ox = double(s.offset_x) * c + double(s.offset_z) * n, oz = -double(s.offset_x) * n + double(s.offset_z) * c;
    return {float(double(x) + ox), float(double(z) + oz), s.radius, s.falloff};
}
// the wave factor one zone allows at (x, z): 0 inside, 1 from radius + falloff on
inline float factor(const Zone& zone, float x, float z) {
    const double dx = double(x) - zone.x, dz = double(z) - zone.z, d = std::sqrt(dx * dx + dz * dz) - zone.radius;
    if (!(d > 0)) return std::isfinite(d) ? 0.0f : 1.0f;
    if (!(zone.falloff > 0) || d >= zone.falloff) return 1.0f;
    return float(d / zone.falloff);
}
// the smallest factor of all zones at (x, z) (1: no zone near)
inline float combined(const Zone* zones, size_t count, float x, float z) {
    float f = 1.0f;
    for (size_t i = 0; i < count; i++) f = std::min(f, factor(zones[i], x, z));
    return f;
}
// the game's target with the zones: unchanged unless a zone wants calmer water
inline float apply(float game_target, const Zone* zones, size_t count, float x, float z) {
    if (!count || !std::isfinite(x) || !std::isfinite(z)) return game_target;
    const float f = combined(zones, count, x, z);
    return f < game_target ? f : game_target;
}

// set when a loaded or edited package declares a zone (story_actors); until then the sea's hook
// (story_world_calm.cpp) only runs the game's own function
inline std::atomic<bool>& declared_flag() {
    static std::atomic<bool> flag{false};
    return flag;
}
inline void note_declared() { declared_flag().store(true, std::memory_order_release); }
inline bool declared() { return declared_flag().load(std::memory_order_acquire); }
}  // namespace story_world_calm
