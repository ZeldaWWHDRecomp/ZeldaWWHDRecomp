// SPDX-License-Identifier: MPL-2.0
// Camera extras (controls plan P7): the walking direction kept while the camera turns, the camera swinging
// behind Link, the direct camera as fast on the boat as on foot, and a pinch for the field of view. Pure, so
// steer_test checks the angle maths without the game; steer.cpp runs it (and camera.cpp the camera side).
//
// Walking direction. The game moves Link where the left stick points as seen from the camera, worked out again
// every step (daPy_lk_c::setStickData 023F32E4: m34DC +0x6924 = the stick's angle, m34E8 +0x6930 = m34DC + the
// camera's angle, the way he turns to and walks), so while the stick is held every camera turn turns him too.
// With this option, from the moment the stick is pushed the camera's turns are added up and taken off again:
// the stick works as if the camera were still where it was. Modes:
//   kPlayerTurns  only the turns the player makes (right stick, mouse, swipe, the swing below) are kept off;
//                 the camera's own following still turns him, as in the game
//   kStickHeld    every camera turn is kept off while the stick is held
// Re-sync: once the stick moves more than a few degrees from where it was when the camera last turned (the
// player steers again, by what they see) the kept angle is dropped; letting go of the stick ends it too. Half a
// second after the camera stopped turning, the kept angle eases away over about a second (Link curves back to
// the camera's frame). Left as the game does it: events, Z-targeting (strafing stays camera relative), the boat.
//
// Camera swing. While Link runs (the stick more than half pushed, for a moment) and the player has left the
// camera alone for the delay, the camera turns towards his back: a share of the angle still off each step,
// 0.5..4 degrees, so it eases in; running towards the camera it waits the delay again before turning (a short
// turn-around doesn't spin the view), and near 180 degrees it keeps the way it began turning.
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0), mods/steer.cpp
// and camera.cpp; reimplemented.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace mods::steer {

enum Mode : int { kOff = 0, kPlayerTurns = 1, kStickHeld = 2 };

constexpr float kDegPerUnit = 180.0f / 32768.0f;  // s16 angle units
inline int16_t units(float deg) { return int16_t(std::lround(deg / kDegPerUnit)); }

struct Options {
    int mode = kOff;
    bool swing = false;
    uint64_t swing_delay = 30;  // steps the camera is left alone before it swings (1 s)
    float resync_deg = 20;      // the stick moves this far: relative to the camera again
};

constexpr uint64_t kEaseAfter = 15;   // steps after the last camera turn before the kept angle eases away
constexpr float kEaseMinDeg = 2;      // ... at least this much per step, else a tenth of it
constexpr int16_t kTurnJitter = 8;    // camera turns smaller than this (0.04 degrees) are no turn

// One step of setStickData, while Link could walk (`active`: the option is on, the stick past the game's own
// threshold, no event, no targeting, not in the boat). Returns the angle to take off the walking direction
// (r9 = stick + camera - offset), 0 for the game's own.
class Direction {
public:
    int16_t step(bool active, int16_t cam, int16_t stick, bool player_turn, bool swinging, uint64_t now, const Options& o) {
        if (!active || o.mode == kOff) {
            hold_ = false, offset_ = 0;
            return 0;
        }
        if (!hold_) hold_ = true, prev_cam_ = cam, offset_ = 0, anchor_ = stick, turned_ = now;
        const int16_t turn = int16_t(cam - prev_cam_);
        prev_cam_ = cam;
        if (o.mode == kStickHeld || player_turn) {
            offset_ = int16_t(offset_ + turn);
            if (std::abs(int(turn)) > kTurnJitter) turned_ = now;
        }
        if (offset_ != 0 && now > turned_ + kEaseAfter && now != eased_) {  // (once per logic step)
            eased_ = now;
            const int mag = std::abs(int(offset_)), d = std::max(mag / 10, int(units(kEaseMinDeg)));
            offset_ = mag <= d ? 0 : int16_t(offset_ > 0 ? offset_ - d : offset_ + d);
        }
        // the anchor follows the stick while the player turns the camera (not the swing's turns: steering
        // during or after a swing re-syncs)
        if (player_turn && turn != 0 && !swinging) anchor_ = stick;
        else if (std::abs(int(int16_t(stick - anchor_))) > int(units(o.resync_deg))) offset_ = 0, anchor_ = stick;
        return offset_;
    }
    int16_t offset() const { return offset_; }
    bool holding() const { return hold_; }

private:
    bool hold_ = false;
    int16_t prev_cam_ = 0, offset_ = 0, anchor_ = 0;
    uint64_t turned_ = 0, eased_ = ~0ull;
};

// The swing: degrees to turn the camera's angle this step (towards Link's back), 0 none.
// face: the way Link faces; cam: the camera's angle (behind him: equal); dist: the stick's distance (0..1).
class Swing {
public:
    static constexpr float kRun = 0.5f;          // the stick at least this far ...
    static constexpr uint64_t kRunSteps = 10;    // ... this long
    static constexpr float kNear = 8, kFar = 135, kKeepWay = 150;  // degrees
    float step(bool active, int16_t face, int16_t cam, float dist, bool player_camera, uint64_t now, const Options& o) {
        if (player_camera) user_ = now;
        if (!active || !o.swing || o.mode == kOff || dist < kRun) {
            run_since_ = ~0ull, far_since_ = ~0ull, way_ = 0;
            return 0;
        }
        if (run_since_ == ~0ull) run_since_ = now;
        if (now < run_since_ + kRunSteps || now < user_ + o.swing_delay) return 0;
        const float deg = float(int16_t(face - cam)) * kDegPerUnit;
        if (std::fabs(deg) <= kFar) far_since_ = ~0ull;
        else if (far_since_ == ~0ull) far_since_ = now;
        if (std::fabs(deg) < kNear || (far_since_ != ~0ull && now < far_since_ + o.swing_delay)) {
            way_ = 0;
            return 0;
        }
        int way = deg > 0 ? 1 : -1;
        if (std::fabs(deg) > kKeepWay && way_ != 0) way = way_;  // no flip-flop around 180
        way_ = way;
        const float off = way * deg < 0 ? 360.0f - std::fabs(deg) : std::fabs(deg);  // the way it turns
        return way * std::clamp(off * 0.08f, 0.5f, 4.0f);
    }

private:
    uint64_t user_ = 0, run_since_ = ~0ull, far_since_ = ~0ull;
    int way_ = 0;
};

// The direct camera's turning on the boat: the boat's camera turns per speed unit by its own f25; on foot it is
// 8 (camera.cpp). This factor makes a stick push turn the boat camera as fast as on foot.
constexpr float kFootF25 = 8.0f;
inline float boat_speed_factor(float f25) { return f25 > 0.01f && std::isfinite(f25) ? std::clamp(kFootF25 / f25, 0.25f, 8.0f) : 1.0f; }

// A pinch on the picture (Android): fingers apart zoom in (a narrower field of view). The field of view scale
// when the fingers are `d` apart, from `start` when they were `d0` apart.
constexpr float kFovMin = 0.75f, kFovMax = 2.5f;  // aspect::set_fov_scale's range (the settings page's)
inline float pinch_fov(float start, float d0, float d) {
    if (!(d0 > 1) || !(d > 1)) return start;
    const float v = start * d0 / d;
    return std::clamp(std::round(v * 20.0f) / 20.0f, kFovMin, kFovMax);
}

}  // namespace mods::steer
