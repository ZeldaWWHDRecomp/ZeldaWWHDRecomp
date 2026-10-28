// SPDX-License-Identifier: MPL-2.0
// Swipe camera (Android touch controls, Flexible style): a finger sliding on the right half of the
// screen turns the camera. The movement goes the mouse camera's way (camera.cpp: the manualCamera
// sites take degrees per logic step; first person and aiming get a proportional right stick), so this
// header only turns finger pixels into camera degrees. Pure, so camera_swipe_test checks the scaling.
//
// Scale: kDegreesPerInch at speed 1 (100 %), by physical distance (the display's DPI), so a swipe
// turns the camera as far on a small phone as on a tablet. While Link sails (daPy_lk_c mModeFlg
// ModeFlg_IN_SHIP) the boat factor multiplies it: the boat's wide camera needs longer swipes for the
// same turn. First person and item aiming (telescope, bow from the boat) keep the plain speed.
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0),
// ControlsView swipe camera and camera.cpp swipe_degrees; reimplemented for our design.
#pragma once
#include <cmath>

namespace mods::camera_swipe {

constexpr float kDegreesPerInch = 72.0f;  // at speed 1: a phone's right half (about 2.8 in) turns ~200 degrees
constexpr float kFallbackDpi = 400.0f;    // a DPI the display did not report sensibly
constexpr float kSpeedMin = 0.25f, kSpeedMax = 3.0f, kSpeedDefault = 1.0f;
constexpr float kBoatMin = 1.0f, kBoatMax = 4.0f, kBoatDefault = 1.0f;
// the mouse camera's nominal degrees per point (mods.cpp default): the fallback stick's scale
constexpr float kNominalSensitivity = 0.15f;

inline float clampf(float v, float lo, float hi, float fallback) {
    return std::isfinite(v) ? std::fmax(lo, std::fmin(hi, v)) : fallback;
}
inline float display_dpi(float dpi) { return std::isfinite(dpi) && dpi >= 72.0f && dpi <= 1000.0f ? dpi : kFallbackDpi; }
inline float speed(float s) { return clampf(s, kSpeedMin, kSpeedMax, kSpeedDefault); }
inline float boat(float b) { return clampf(b, kBoatMin, kBoatMax, kBoatDefault); }

// camera degrees for a finger movement of `pixels` (either axis) on a display of `dpi`
inline float degrees(float pixels, float dpi, float swipe_speed) {
    if (!std::isfinite(pixels)) return 0.0f;
    return pixels / display_dpi(dpi) * kDegreesPerInch * speed(swipe_speed);
}
// the factor for this logic step: the boat factor while Link sails and does not look through
// first person or aim an item, else 1
inline float step_factor(bool in_ship, bool looking, float boat_factor) {
    return in_ship && !looking ? boat(boat_factor) : 1.0f;
}

}  // namespace mods::camera_swipe
