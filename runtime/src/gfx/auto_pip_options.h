// Display > GamePad screen > Song list while conducting: Show / Hide (#694). Only Automatic picture-in-picture
// reads it (auto_pip.h): with Hide the Wind Waker's song list no longer opens the overlay while Link conducts,
// the menus still do, and the PiP shortcut peeks at it. Saved through hostui (display.plist / settings.ini);
// WWHD_AUTO_PIP_BATON=0|1 overrides it at start (Hide / Show, not saved).
#pragma once
#include "display_modes.h"
#include "../overlay/hostui.h"
#include <cstdlib>
#include <string>

namespace gfx::auto_pip_options {
inline constexpr const char* kSettingKey = "auto-pip-song-list";
inline void load_options() {
    std::string saved;
    bool on = !hostui::get(kSettingKey, saved) || saved != "0";
    if (const char* e = getenv("WWHD_AUTO_PIP_BATON"); e && *e) on = atoi(e) != 0;
    g_auto_song_list.store(on, std::memory_order_relaxed);
}
inline void set_option(bool show) {
    g_auto_song_list.store(show, std::memory_order_relaxed);
    if (show) g_auto_peek = false;  // the song list shows by itself now
    hostui::set(kSettingKey, show ? "1" : "0");
}
}  // namespace gfx::auto_pip_options
