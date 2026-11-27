// SPDX-License-Identifier: MPL-2.0
// Mouse wheel (Controls > Camera, issue #696): what the wheel does in the game window.
//   kFirstPerson  forward enters first person, backward leaves it (a one-step R3 press, camera.cpp filter_pad); the
//                 old "First-person shortcut" switch on (built-in mod "first-person", saved mod.first-person.enabled)
//   kZoom         the game's own third-person zoom: wheel up (forward) closer, down further
//   kBindings     the wheel is the WheelUp / WheelDown keyboard-and-mouse bindings (Controls), the old switch off
// kBindings is the default, as the old switch was off by default (and with nothing bound the wheel does nothing).
//
// Zoom: manualCamera (dCamera_c, 0250FDC8) zooms by its vertical rate f28 (+1 the stick fully up: closer and lower),
// each logic step by the style's steps: centre height, radius, latitude and field of view, each kept in the style's
// limits by limited_range_addition (manRangeAdd), with the wall check after it. A notch adds kStepsPerNotch steps of
// the full rate to `pending`; camera.cpp gives the camera at most the full rate per 30 Hz step (rate()) until it is
// used up, so a notch zooms as far as the stick held fully for kStepsPerNotch steps, at the stick's speed, and the
// game's limits, speed and wall checks stay. Trackpads' fractional scrolls add their fraction (a smooth zoom).
// Frame-rate independence: the logic runs at 30 Hz at 30 / 60 / 120 fps (interpolated), and the decomp build's true
// 60 / 120 (N = 2 / 4 steps a frame) gives each step of a 30 Hz frame the frame's rate, as the game's stick does
// (PORT_CAMERA_RATE @0251053C), and manRangeAdd takes x dt itself (frame_rate()).
// Pure (no runtime state), so cannon_mouse_test covers it.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace mouse_wheel {

enum Mode : int { kFirstPerson = 0, kZoom = 1, kBindings = 2, kModeCount };
constexpr int kDefault = kBindings;
inline const char* name(int m) { return m == kFirstPerson ? "first person" : m == kZoom ? "zoom" : "bindings"; }
// WWHD_MOUSE_WHEEL: 0 / 1 / 2 or first-person / zoom / bindings; -1 when it names none
inline int parse(const char* s) {
    if (!s || !*s) return -1;
    if (!std::strcmp(s, "0") || !std::strcmp(s, "first-person") || !std::strcmp(s, "fp")) return kFirstPerson;
    if (!std::strcmp(s, "1") || !std::strcmp(s, "zoom")) return kZoom;
    if (!std::strcmp(s, "2") || !std::strcmp(s, "bindings")) return kBindings;
    return -1;
}
// the old switch (the built-in mod "first-person") and the mode: on is First person; off leaves Zoom alone
inline int from_switch(int mode, bool on) { return on ? kFirstPerson : mode == kFirstPerson ? kBindings : mode; }
// the wheel's events go to the camera (First person, Zoom), not to the WheelUp / WheelDown bindings
inline bool owns_wheel(int mode) { return mode == kFirstPerson || mode == kZoom; }

// a notch: the full rate for this many logic steps (30 Hz)
constexpr float kStepsPerNotch = 4.0f;
// at most this much zoom waits (4 notches): a fast scroll does not keep the camera zooming long after it
constexpr float kMaxPending = 4 * kStepsPerNotch;
// zoom the camera did not take for this many steps (first person, an event, a menu, a camera without the zoom) is
// dropped, so it does not zoom later
constexpr uint64_t kDropSteps = 4;

struct Zoom {
    float pending = 0;      // full-rate steps still to give (+ closer, - further)
    uint64_t last = 0;      // the last step the wheel added or the camera took (0: never)
    uint32_t frame = ~0u;   // true 60: the 30 Hz frame of `frame_rate`
    float frame_rate = 0;   // ... and its rate
};
// the wheel moved by `notches` (> 0 forward, fractional from trackpads); turning the other way first stops the zoom
inline void add(Zoom& z, float notches, uint64_t now) {
    if (!(notches != 0) || !std::isfinite(notches)) return;
    if (z.pending * notches < 0) z.pending = 0;
    z.pending = std::clamp(z.pending + notches * kStepsPerNotch, -kMaxPending, kMaxPending);
    z.last = now;
}
inline void drop_stale(Zoom& z, uint64_t now) {
    if ((z.pending != 0 || z.frame_rate != 0) && now > z.last + kDropSteps) z.pending = z.frame_rate = 0;
}
// a zoom waits for the camera (true 60: also the rest of the frame that took the last of it)
inline bool wants(const Zoom& z) { return z.pending != 0 || z.frame_rate != 0; }
// one 30 Hz step's rate (|rate| <= 1), taken from pending
inline float rate(Zoom& z, uint64_t now) {
    const float r = std::clamp(z.pending, -1.0f, 1.0f);
    z.pending -= r;
    if (std::fabs(z.pending) < 1e-6f) z.pending = 0;
    if (r != 0) z.last = now;
    return r;
}
// true 60 (N steps a frame): the frame's rate on each of its steps (the game takes x dt per step)
inline float frame_rate(Zoom& z, uint32_t frame, uint64_t now) {
    if (z.frame != frame) {
        z.frame = frame;
        z.frame_rate = rate(z, now);
    } else if (z.frame_rate != 0) {
        z.last = now;
    }
    return z.frame_rate;
}
// the wheel's rate added to the game's own (the stick's, or the mouse's), within the stick's range (a full stick
// and the wheel zoom no faster than the full stick); a larger rate of the mouse's own stays as it is
inline double combine(double game, float wheel) {
    if (wheel == 0) return game;
    const double lim = std::max(1.0, std::fabs(game));
    return std::clamp(game + wheel, -lim, lim);
}

}  // namespace mouse_wheel
