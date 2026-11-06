// SPDX-License-Identifier: MPL-2.0
// Online co-op: far players on a small sea radar (GAPS C13; coop_radar.cpp).
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace mods::coop::radar {

constexpr float kRange = 25000;  // units from Link to the radar's rim
struct Far { float x, z; bool party; bool pvp = false; };  // pvp: a player who can fight now (C28: red)
struct Dot { float x, y; bool party, rim; bool pvp = false; };  // -1..1 from the radar's centre, +y up (the camera's view); rim: further out
// the dots for these far players, seen from `me` with the camera's heading `view_yaw` (radians, atan2 of its forward x, z)
inline std::vector<Dot> place(const float me[3], float view_yaw, const std::vector<Far>& far) {
    std::vector<Dot> out;
    const float s = std::sin(view_yaw), c = std::cos(view_yaw);
    for (const Far& f : far) {
        const float dx = f.x - me[0], dz = f.z - me[2];
        // up: the camera's forward (s, c on x, z); right: its right (-c, s), as cross(forward, up)
        float rx = -dx * c + dz * s, ry = dx * s + dz * c;
        const float d = std::hypot(rx, ry) / kRange;
        if (d > 1) rx /= d, ry /= d;
        out.push_back({rx / kRange, ry / kRange, f.party, d > 1, f.pvp});
    }
    return out;
}
// (C28) a safe zone's border on the radar: its centre as a Dot (may be far outside), its radius in radar units (1 = rim)
struct Ring { float x, y, r; };
inline std::vector<Ring> rings(const float me[3], float view_yaw, const std::vector<Far>& centres, const std::vector<float>& radii) {
    std::vector<Ring> out;
    const float s = std::sin(view_yaw), c = std::cos(view_yaw);
    for (size_t i = 0; i < centres.size() && i < radii.size(); i++) {
        const float dx = centres[i].x - me[0], dz = centres[i].z - me[2];
        const float rx = -dx * c + dz * s, ry = dx * s + dz * c;
        if (std::hypot(rx, ry) - radii[i] > kRange) continue;  // (no part of it on the radar)
        out.push_back({rx / kRange, ry / kRange, radii[i] / kRange});
    }
    return out;
}
// each step (game thread): the far players now (none: the radar hides); at sea only, as the settings say. With PvP
// (C28) also the safe zones' borders (`zones`: centres and radii), which keep the radar shown
void step(uint32_t link, const std::vector<Far>& far);
void set_zones(const std::vector<Far>& centres, const std::vector<float>& radii);
// main (before the overlay seals)
void register_overlay();

}  // namespace mods::coop::radar
