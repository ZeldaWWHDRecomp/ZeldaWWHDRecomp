// SPDX-License-Identifier: MPL-2.0
// Game menu sounds in the port's Settings overlay (controls plan P8, H2): moving the cursor, choosing,
// changing an option, switching tabs, going back and opening / closing Settings play the game's own menu
// sound effects (SE_MENU_*), the ones its pause menu and the item menu's settings page play.
//
// Pure, so menu_sounds_test checks every rule without the game: Tracker turns what the overlay saw in one
// frame (Frame: Dear ImGui's state after the window was built) into at most one sound, and audible() says
// whether it may play. The port (mods/menu_sounds.cpp) queues it from the render thread and plays it on
// the game thread through the game's own named-sound call (playSe), so it is routed like every other game
// sound: the game's mix and output (TV, GamePad, both), the master volume, and in TV play the GamePad-only
// sound option (#218) with its volume when the game sends the menu sounds to the GamePad.
//
// Option audioMenuSounds (settings.ini; WWHD_MENU_SOUNDS=0|1 for tests, not saved then): on by default.
// No sound while the window is in the background or the game's audio is muted (master volume 0).
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0), its
// MenuSounds.java (the GamePad menu sounds in its options); reimplemented: no decoding of the sound archive,
// the game plays its own sounds.
#pragma once
#include <cstdint>

namespace menu_sounds {

enum class Sound : uint8_t { None, Cursor, Change, Decide, Cancel, Tab, Open, Close };

// the game's sound label (hd sound interface: playSe(name)), as the item menu's settings page uses them
inline const char* label(Sound s) {
    switch (s) {
    case Sound::Cursor: return "SE_MENU_CURSOR";
    case Sound::Change: return "SE_MENU_OPTION_SWITCH";
    case Sound::Decide: return "SE_MENU_DECIDE_S";
    case Sound::Cancel: return "SE_MENU_CANCEL";
    case Sound::Tab: return "SE_MENU_TAB_SWITCH";
    case Sound::Open: return "SE_MENU_OPTION_IN";
    case Sound::Close: return "SE_MENU_OPTION_OUT";
    case Sound::None: break;
    }
    return nullptr;
}

// what the overlay saw this frame
struct Frame {
    bool open = false;        // Settings is shown
    int tab = -1;             // the open tab
    bool moved = false;       // the keyboard / controller cursor moved to another item (ImGui NavJustMovedToId)
    bool activated = false;   // an item was pressed: a click, a tap, A or Enter (ImGui ActiveIdIsJustActivated)
    bool edited = false;      // a value changed this frame (ImGui ActiveIdHasBeenEditedThisFrame)
    int popups = 0;           // open popups and dialogs
    bool back = false;        // B or Esc pressed
    double time = 0;          // seconds
};

// at most this often: the cursor held on a direction repeats, a slider being dragged edits every frame
constexpr double kCursorGap = 0.05, kChangeGap = 0.12;
// Settings shows its last (or a requested) tab a frame or two after opening: no tab sound over the opening's
constexpr double kOpenSettle = 0.25;

class Tracker {
public:
    Sound step(const Frame& f) {
        const Frame was = last_;
        const bool first = !seen_;
        last_ = f, seen_ = true;
        if (first) {
            opened_at_ = f.time;
            return f.open ? Sound::Open : Sound::None;
        }
        if (f.open != was.open) {
            if (f.open) opened_at_ = f.time;
            return f.open ? Sound::Open : Sound::Close;
        }
        if (!f.open) return Sound::None;
        if (f.tab != was.tab) return f.time - opened_at_ < kOpenSettle ? Sound::None : Sound::Tab;
        if (f.popups < was.popups) return f.back ? Sound::Cancel : Sound::Decide;  // a dialog or list closed
        if (f.edited) return gap(f.time, change_at_, kChangeGap) ? Sound::Change : Sound::None;
        if (f.activated || f.popups > was.popups) return Sound::Decide;
        if (f.back) return Sound::Cancel;  // B that closes nothing (Settings itself closes with Close above)
        if (f.moved) return gap(f.time, cursor_at_, kCursorGap) ? Sound::Cursor : Sound::None;
        return Sound::None;
    }

private:
    static bool gap(double now, double& at, double min) {
        if (now - at < min) return false;
        at = now;
        return true;
    }
    Frame last_;
    bool seen_ = false;
    double cursor_at_ = -1e9, change_at_ = -1e9, opened_at_ = -1e9;
};

// whether a sound may play now
struct Gate {
    bool option = true;        // the "Menu sounds" option
    bool focused = true;       // the window is in front (Android: the app)
    float master_volume = 1;   // 0: muted
    bool game_sound_ready = false;  // the game's sound system is up (its sound manager exists)
};
inline bool audible(const Gate& g) { return g.option && g.focused && g.master_volume > 0 && g.game_sound_ready; }

}  // namespace menu_sounds
