// SPDX-License-Identifier: MPL-2.0
// One screen (handheld) mode (controls plan P2b): the device is the Wii U GamePad in Off-TV Play from the
// first frame, for a phone or a handheld that has one screen. The rules here are pure (one_screen_test); the
// runtime is one_screen.cpp (the game's screen mode, the file select's controller picker, the pause menu's
// Options, Minus, the view).
//
// - The game's screen mode (hdInputManager +0x1D0: 0 TV only, 1 TV play with the GamePad's own screen, 2 Off-TV
//   Play, 3 the picture on both, the title screen's): while the mode is on and the controller is the GamePad,
//   every request for TV play becomes Off-TV Play, and the title's "both" too (one picture, drawn once).
// - No "GamePad or Pro Controller?" at the file select (the GamePad is taken as chosen); Back on the save list
//   goes back to the title screen, as the picker's own Back would.
// - Plus or Minus pauses with the item menu: the game never sees Minus (in Off-TV Play it would switch back to
//   TV play, which has no meaning on one screen); a short Minus is a Plus for the game when it is let go, and
//   held, it is the Settings shortcut as before (hold Select).
// - Wii U leftovers hidden: the Controller item of the pause menu's Options, the GamePad's slide-conducting
//   guide over the picture.
// - The window shows the GamePad picture (the game itself in Off-TV Play).
// - Touches: on the GamePad picture the game's own touch screen works in its menus (drag items onto X / Y / R,
//   the map, the bottles); in play, a finger landing on no on-screen control follows the touch style (the
//   floating sticks, the swipe camera) and the talks by touch, as on the TV picture.
//
// Defaults (user decision 2026-10-28): on for Android, unless the player chose the Pro Controller before; an
// option on the Steam Deck and on desktops, off there. Decided at start (the game's screen mode is set while
// it starts); the old setup (the Pro Controller, or the GamePad with the game's own behaviour) stays.
// A device with a second screen in use for the GamePad picture (a dual-screen handheld such as the AYN Thor:
// the Android Presentation display, GamePadDisplay.java) already has both pictures: off there by default, so
// the TV and GamePad pictures stay apart and Minus is the game's own Off-TV Play switch, as on the Wii U.
// The player's own choice (oneScreen in settings.ini) always wins.
//
// Idea from SwitchWakerHD (thanks @l0nkDev, its #5, MPL-2.0), reimplemented: addresses and the game's
// behaviour re-checked in our build.
#pragma once
#include <cstdint>

namespace one_screen {

// second_screen: a second physical screen shows the GamePad picture at start (second_screen_in_use)
inline bool default_on(bool android, bool chose_pro_controller, bool second_screen) {
    return android && !chose_pro_controller && !second_screen;
}
// the start choice: the player's saved oneScreen (saved_set: the key holds 0 or 1) wins over the default
inline bool start_on(bool saved_set, bool saved_on, bool android, bool chose_pro_controller, bool second_screen) {
    return saved_set ? saved_on : default_on(android, chose_pro_controller, second_screen);
}
// a second screen counts while it is there (seen when the activity started, or published by the display host)
// and the player has not switched the GamePad screen off on it (#222: then the device is one screen again)
inline bool second_screen_in_use(bool present_at_start, bool available, bool switched_off) {
    return (present_at_start || available) && !switched_off;
}

enum Mode : int { kTvOnly = 0, kTvPlay = 1, kOffTv = 2, kBoth = 3 };
// what the mode setter gets asked for, and what it may store, while the mode is active
inline int requested_mode(bool active, int mode) { return active && mode == kTvPlay ? kOffTv : mode; }
inline int stored_mode(bool active, int mode) { return active && mode == kBoth ? kOffTv : mode; }

// Minus as Plus (VPAD bits; input.h): one pad read per logic step
constexpr uint32_t kMinus = 0x0004, kPlus = 0x0008;
constexpr uint64_t kSettingsHoldSteps = 15;  // the Settings shortcut's 0.5 s hold (overlay/settings_shortcut.h)
constexpr uint64_t kPlusSteps = 2;
class MinusAsPlus {
public:
    uint32_t filter(uint32_t buttons, uint64_t now) {
        const bool minus = (buttons & kMinus) != 0;
        buttons &= ~kMinus;
        if (minus && !held_) held_ = true, since_ = now;
        if (!minus && held_) {
            held_ = false;
            if (now - since_ < kSettingsHoldSteps) plus_until_ = now + kPlusSteps;
        }
        if (now < plus_until_) buttons |= kPlus;
        return buttons;
    }
    void reset() { *this = MinusAsPlus{}; }

private:
    bool held_ = false;
    uint64_t since_ = 0, plus_until_ = 0;
};

// a finger on the GamePad picture (the whole screen in this mode) is the game's own touch screen while a game
// menu shows on it, or while the on-screen controls are hidden or off; else it is the touch controls' and
// the talks' (touch_style.h free_touch, touch_talk.h)
inline bool picture_is_game_touch(bool game_menu, bool controls_shown) { return game_menu || !controls_shown; }

// The pause menu's Options without the Controller item. The US game lists 6 items: Target Type, Camera,
// First-Person Camera, Controller, Gyroscope, UI Display (descriptors 24 bytes each; their values one byte each
// at +0x104 of the window). Shown order: the Controller item moved last and the count lowered to 5, so it is
// never shown or reached; code that uses fixed offsets (the saver, the Controller callback) gets the game's
// order around its call.
constexpr int kOptionItems = 6, kControllerItem = 3;
constexpr int kShownFromGame[kOptionItems] = {0, 1, 2, 4, 5, 3};  // shown slot i holds the game's item kShownFromGame[i]
template <class T>
void to_shown(T (&v)[kOptionItems]) {
    T g[kOptionItems];
    for (int i = 0; i < kOptionItems; i++) g[i] = v[i];
    for (int i = 0; i < kOptionItems; i++) v[i] = g[kShownFromGame[i]];
}
template <class T>
void to_game(T (&v)[kOptionItems]) {
    T s[kOptionItems];
    for (int i = 0; i < kOptionItems; i++) s[i] = v[i];
    for (int i = 0; i < kOptionItems; i++) v[kShownFromGame[i]] = s[i];
}

}  // namespace one_screen
