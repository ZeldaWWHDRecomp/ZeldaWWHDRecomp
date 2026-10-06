// What the settings overlay needs from the host (window system) it runs in. Two implementations:
//   AppKit host (macOS, Metal and Vulkan): gfx/overlay_appkit.mm, on top of menu.mm and display.mm
//   SDL host (Vulkan-only builds: Windows, Linux, macOS test builds): gfx/vulkan/overlay_sdl.cpp
// Reads may come from any thread; changes are made on the main thread (post()), as the menus do.
#pragma once
#include <functional>
#include <string>

namespace hostui {

void choose_mod_source(bool folder, std::function<void(std::string)> chosen);
void post(std::function<void()> fn);  // run on the main thread (soon, in order)

// persistent settings: display.plist (AppKit) or <config dir>/settings.ini (SDL host). Test runs
// (WWHD_NO_HOST_INPUT) neither read nor write the user's file.
bool get(const char* key, std::string& value);
void set(const char* key, const std::string& value);

// graphics options that the host keeps (and saves) itself
float res_scale();                 // internal resolution (as the menu shows it)
void set_res_scale(float s);
void graphics_changed();           // an option changed: the AppKit host saves its options and refreshes the title
int scale_filter();                // 0 smooth, 1 sharp, 2 integer
void set_scale_filter(int f);
bool scale_filter_available();

// display
bool fullscreen();
void set_fullscreen(bool on);
int drc_modes();                   // GamePad screen modes (gfx/display_modes.h): 4 (window, picture-in-picture,
                                   // auto, off), 5 (and GamePad only) or 0
bool drc_mode_offered(int m);      // the host can show this mode (no separate window on Android)
int drc_mode();
void set_drc_mode(int m);
int pip_corner();                  // picture-in-picture: 0 top left, 1 top right, 2 bottom left, 3 bottom right
void set_pip_corner(int c);
float pip_size();                  // fraction of the TV picture's width
void set_pip_size(float s);
float pip_opacity();
void set_pip_opacity(float o);
bool drc_available();              // there is a GamePad screen to show or hide
bool drc_shown();
void show_drc(bool on);
void set_pro_controller(bool on);  // Input: keyboard and controllers act as a Pro Controller (or the GamePad)

const char* name();                // "AppKit" or "SDL"

// SDL host only (gfx/vulkan/overlay_sdl.cpp, called by its main loop)
void run_posted();                 // the functions post()ed since the last call
void load_saved_options();         // start-up: graphics and GamePad screen options saved in settings.ini
void toggle_drc();                 // Ctrl+G: show / hide the GamePad screen in the current mode
void drc_window_closed();          // the GamePad window's close button (window mode: hidden until shown again)
void tv_fullscreen_changed();      // the TV window may have entered or left full screen (remembered, as on macOS)

}  // namespace hostui
