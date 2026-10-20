// The world editor's pick: the first ground a ray meets, found with the game's ground check only
// (`ground(x, y, z, h)`: the height `h` of the highest ground at x/z at or below y; false: none).
//
// The ray is walked in steps. For each step the ground is looked up below the top of that step (the
// higher of its two ends, plus a little): the ray has met the ground when its far end is at or below
// that ground. The crossing is then narrowed with the same lookup height, so a thin surface (a pier,
// a roof, a bridge) above lower ground is found as well as solid terrain, and the nearest one wins.
// Header-only so the contract test (editor_test) runs it against a made-up world.
#pragma once

#include <algorithm>
#include <cmath>

#include "../mods/story_world_manifest.h"

namespace editor {

template <class Ground>
bool ray_ground(const story_notebook::WorldPoint& from, const story_notebook::WorldPoint& d, float range, Ground&& ground,
                story_notebook::WorldPoint& hit, double step = 60) {
    if (!(range > 0) || !(step > 0)) return false;
    auto at = [&](double t, int axis) { return axis == 0 ? from.x + d.x * t : axis == 1 ? from.y + d.y * t : from.z + d.z * t; };
    // the ground at the ray's point t, looked up from `top`; true when the ray is at or below it there
    auto below = [&](double t, double top, float& h) {
        return ground(at(t, 0), top, at(t, 2), h) && at(t, 1) <= double(h);
    };
    double prev = 0;
    for (double t = step;; t += step) {
        const double tt = std::min<double>(t, range);
        const double top = std::max(at(prev, 1), at(tt, 1)) + 1;
        float h = 0;
        if (below(tt, top, h)) {
            double lo = prev, hi = tt;
            float hh = h;
            for (int i = 0; i < 20 && hi - lo > 0.05; i++) {
                const double mid = (lo + hi) / 2;
                float hm = 0;
                if (below(mid, top, hm)) hi = mid, hh = hm;
                else lo = mid;
            }
            hit = {float(at(hi, 0)), hh, float(at(hi, 2))};
            return true;
        }
        prev = tt;
        if (tt >= range) return false;
    }
}

// The sea's surface as a place to put things: on the sea stage the water at y 0 is no ground the game's check
// finds (the open sea has none, or only the deep sea floor), so a ray that meets y 0 before any ground (or meets
// no ground at all within `range`) gives the point where it meets the water. `d` is a unit direction; `hit` is
// the ground hit when `has_hit`. True when `hit` became the sea point.
inline bool sea_pick(const story_notebook::WorldPoint& from, const story_notebook::WorldPoint& d, float range, bool has_hit,
                     story_notebook::WorldPoint& hit, float sea_level = 0) {
    if (!(d.y < 0) || !(from.y > sea_level)) return false;
    const double t = (double(sea_level) - from.y) / d.y;
    if (!(t >= 0) || t > range) return false;
    if (has_hit) {
        const double dx = double(hit.x) - from.x, dy = double(hit.y) - from.y, dz = double(hit.z) - from.z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) <= t) return false;  // the ground is nearer (an island above the water)
    }
    hit = {float(from.x + d.x * t), sea_level, float(from.z + d.z * t)};
    return true;
}

}  // namespace editor
