// SPDX-License-Identifier: MPL-2.0
// Zoom while holding (Controls > Camera, issue #688): with Right stick up/down: Look up and down (#661), while a
// chosen Wii U button is held the right stick's up/down (and the mouse's) zooms as in the original game again. The
// choice names a Wii U button, so it follows the controls mapping on every controller and the keyboard:
//   kL   the Wii U's L (L1 / LB, Q by default). In play the game uses L only to turn the GamePad screen between
//        Items and Map, and dCamera_c never reads it (its "L" lock is the HD's ZL: d_camera_util.cpp updatePad reads
//        CPad_GET_ANALOG_L / mDoCPd_L_LOCK_BUTTON, the pad word's BUTTON_L, which the HD fills from ZL; measured in
//        #688's runs: a ZL press takes nextMode to 1 / 2, an L press changes no mode). So holding L cannot change
//        the camera's mode or targeting. To keep the page from turning on every zoom, an L that
//        reaches the game in play is held back while it is held; let go without having zoomed and the game gets
//        a short L press then (a tap still turns the page, on release).
//   kZL  ZL, the targeting button: the stick zooms while Link targets. Targeting is unchanged (ZL still reaches
//        the game): the game's own nextMode already lets a pushed stick run manualCamera while ZL is held, with
//        nothing to target (d_camera_mode.cpp, path 1 of chooseMode) and with a target (manualCamera's
//        CAMERA_MANUAL_LOCKON branch, d_camera_engine2.cpp), only the press itself ends it (mLockLJustActivated).
// Pure (no runtime state), so cannon_mouse_test covers it; mods/controller_play.cpp runs it on the game's pad
// reads (State::held -> mods::camera_zoom_held), mods/camera.cpp look_pitch_take reads that.
#pragma once

#include <cmath>
#include <cstdint>

#include "../input.h"

namespace camera_zoom_hold {

enum Choice : int { kOff = 0, kL = 1, kZL = 2, kChoiceCount };
constexpr int kDefault = kL;
// the short L press on release (logic steps): the game turns the page on its press edge
constexpr uint64_t kTapSteps = 2;
// a right stick tilted this far up or down counts as zooming (the manual camera starts its vertical rate there)
constexpr float kStickUsed = 0.25f;

inline uint32_t button(int choice) {
    return choice == kL ? input::kL : choice == kZL ? input::kZL : 0;
}

struct State {
    bool held = false;       // the zoom button is held now (Look up and down on): the stick zooms
    bool holding_l = false;  // kL: an L held back from the game since its press
    bool used = false;       // ... and the camera zoomed during that hold (no page turn on release)
    uint64_t tap_until = 0;  // kL: the game gets L until this step (the tap on release)
};

// One game pad read (not an interpolation repeat). `raw`: the buttons before the play rules (input_map::play_buttons),
// `out`: after them (an L a jump took is already gone there). `ry`: the right stick's y. `zoomed`: the camera zoomed
// through held() since the last call (camera.cpp). Returns the buttons the game gets.
inline uint32_t filter(State& s, int choice, bool look, bool in_play, uint32_t raw, uint32_t out, float ry, bool zoomed,
                       uint64_t now) {
    const uint32_t bit = look ? button(choice) : 0;
    s.held = bit && (raw & bit);
    if (choice != kL || !look) {
        s.holding_l = s.used = false;
        s.tap_until = 0;
        return out;
    }
    if (s.held) {
        if (!s.holding_l && in_play && (out & input::kL)) {  // the press: the game does not get it (yet)
            s.holding_l = true;
            s.used = false;
        }
        if (s.holding_l) {
            out &= ~input::kL;
            if (zoomed || std::fabs(ry) >= kStickUsed) s.used = true;
        }
    } else if (s.holding_l) {  // let go: a tap if nothing zoomed (and Link still plays)
        s.holding_l = false;
        if (!s.used && in_play) s.tap_until = now + kTapSteps;
        s.used = false;
    }
    if (now < s.tap_until) out |= input::kL;
    return out;
}

}  // namespace camera_zoom_hold
