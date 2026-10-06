// GamePad screen modes and the TV window's layout, shared by both window hosts: the AppKit host
// (display.mm: macOS, Metal and Vulkan) and the SDL host (gfx/vulkan: Windows, Linux, Android, macOS
// test builds). The hosts keep their own windows, menus and settings files; what is decided here is
// the same everywhere: the mode, the picture-in-picture corner / size / opacity, where the pictures
// go in the TV window (display_plan, display.h), the automatic overlay and the mapping of clicks on
// the GamePad picture inside the TV window to GamePad touches.
//
// Modes (WWHD_DRC_MODE at start, not saved):
//   window   the GamePad picture in its own window (hosts with a GamePad window)
//   pip      picture-in-picture: the GamePad picture in a corner of the TV window
//   auto     picture-in-picture for a few seconds when the GamePad picture changes a lot
//   off      not shown (the game keeps drawing it)
//   gamepad  only the GamePad picture, fitted to the TV window, like a Wii U played without a TV
//            (Wind Waker HD's Off-TV Play, Minus, puts the game itself on it); a click or touch on
//            it touches the GamePad
#pragma once
#include <atomic>
#include <cstdint>

#include "display.h"

namespace gfx {

enum DrcMode { kDrcWindow, kDrcPip, kDrcAuto, kDrcOff, kDrcGamePad, kDrcModeCount };
enum Filter { kSmooth, kSharp, kInteger };
extern const char* const kModeNames[kDrcModeCount];  // window, pip, auto, off, gamepad
extern const char* const kCornerNames[4];             // tl, tr, bl, br (bit 0: right, bit 1: bottom)
extern const char* const kFilterNames[3];             // smooth, sharp, integer

// the options (read from any thread; changed on the host's main thread)
extern std::atomic<int> g_mode;
extern std::atomic<int> g_corner;
extern std::atomic<float> g_pip_size;      // fraction of the TV picture's width, 0.1..0.5
extern std::atomic<float> g_pip_opacity;   // 0.2..1
extern std::atomic<int> g_filter;          // picture scaling (the SDL host mirrors gfxvk::scale_filter)
extern std::atomic<bool> g_shown;          // window / pip modes: GamePad screen shown (Cmd+G, Pro Controller)
extern std::atomic<bool> g_auto_pin;       // auto mode: Cmd+G keeps the overlay up
extern std::atomic<double> g_auto_until;   // auto mode: overlay up until this time (display_now)
extern std::atomic<float> g_drc_aspect;    // of the GamePad image, for the GamePad window's touch
extern std::atomic<bool> g_has_drc_window; // the host made a GamePad window (window mode possible)

int find_name(const char* const* names, int n, const char* s, int def);  // index of s, def if absent
double display_now();  // seconds, monotonic (the automatic overlay's clock)
bool drc_mode_offered(int m);  // window: the host has a GamePad window
// start-up overrides for tests, after the saved options were read (not saved):
// WWHD_DRC_MODE, WWHD_DRC_PIP=br:0.25[:0.85], WWHD_SCALE_FILTER
void display_env_overrides();
// Full screen is remembered: the TV window starts as it was left (the hosts save tvFullScreen when it
// enters or leaves full screen, display.plist / settings.ini). Whether to switch it to full screen at
// start: WWHD_FULLSCREEN=0|1 overrides the saved state for this start (a session started with it does
// not save its full-screen state: display_fullscreen_env); test runs (WWHD_NO_HOST_INPUT) switch only
// with WWHD_FULLSCREEN=1 and hidden windows never do. Logs the decision.
bool display_start_fullscreen(bool saved, bool hidden_windows);
bool display_fullscreen_env();  // WWHD_FULLSCREEN is set
// debug: WWHD_TEST_DRC_MODE=3400:gamepad,3600:pip switches the mode at those frames (as the settings
// overlay does); the mode to switch to at this frame, -1 for none (host main thread)
int display_test_mode(uint64_t frame);

// state changes of the modes (the hosts then show or hide their GamePad window and save)
bool drc_window_wanted();          // window mode and shown
bool pip_shown_now();              // the overlay is up (pip / auto modes)
bool drc_screen_shown(bool drc_window_visible);  // the "Show GamePad screen" state in this mode
void display_show_drc(bool on);    // show / hide the GamePad screen in the current mode
void display_set_mode(int m);      // a new mode (shown again unless off; auto starts unpinned)
void display_touched();            // a touch on the overlay: auto mode keeps it up 2 s longer

// pixel rectangles in the target (drawable), top-left origin
struct Layout { Box tv, pip; bool pip_on = false; bool drc_only = false; float scale = 1; };
// the TV window's layout: TV picture (tw x th) scaled to fit dw x dh, GamePad picture (pw x ph) in
// the overlay corner (pip_on) or alone, fitted (drc_only)
Layout layout(float dw, float dh, float tw, float th, float pw, float ph, bool pip_on, bool drc_only = false);
// a point in the TV window (0..1 from top left) on the GamePad picture -> touch position (0..1),
// from the last display_plan; clamp_outside: a drag that left the picture keeps touching its edge
bool overlay_hit(float nx, float ny, float* tx, float* ty, bool clamp_outside = false);
// the main picture of the last TV layout (the TV picture, or the GamePad picture in GamePad-only
// mode), 0..1 of the TV window from the top left; false before the first layout (the settings
// overlay's text prompt places itself on it)
bool main_picture(float* x, float* y, float* w, float* h);

// Touch screens (Android; WWHD_VIEW_BUTTON=1 elsewhere for tests): a button in the TV window's top
// left corner. A tap cycles the views offered there (picture-in-picture, GamePad only, TV only);
// what a long press does is the host's (Android: 60 fps on / off).
bool view_button_enabled();
bool view_button_hit(float nx, float ny);  // a point in the TV window (0..1) on the button
int next_view();                           // the mode a tap switches to (the host sets and saves it)

}  // namespace gfx
