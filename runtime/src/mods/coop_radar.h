// SPDX-License-Identifier: MPL-2.0
// Online co-op: far players on a small sea radar (GAPS C13; coop_radar.cpp).
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace mods::coop::radar {

constexpr float kRange = 25000;  // units from Link to the radar's rim
struct Far { float x, z; bool party; };
struct Dot { float x, y; bool party, rim; };  // -1..1 from the radar's centre, +y up (the camera's view); rim: further out
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
        out.push_back({rx / kRange, ry / kRange, f.party, d > 1});
    }
    return out;
}
// each step (game thread): the far players now (none: the radar hides); at sea only, as the settings say
void step(uint32_t link, const std::vector<Far>& far);
// main (before the overlay seals)
void register_overlay();

}  // namespace mods::coop::radar
