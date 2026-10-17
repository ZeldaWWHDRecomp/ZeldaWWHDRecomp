// GamePad screen modes and the TV window's layout, shared by both window hosts: the AppKit host
// (display.mm: macOS, Metal and Vulkan) and the SDL host (gfx/vulkan: Windows, Linux, Android, macOS
// test builds). The hosts keep their own windows, menus and settings files; what is decided here is
// the same everywhere: the mode, the picture-in-picture anchor / size / opacity, where the pictures
// go in the TV window (display_plan, display.h), the automatic overlay and the mapping of clicks on
// the GamePad picture inside the TV window to GamePad touches.
//
// Modes (WWHD_DRC_MODE at start, not saved):
//   window   the GamePad picture in its own window (hosts with a GamePad window)
//   pip      picture-in-picture: the GamePad picture inside the TV window
//   auto     GamePad menus while open; follows the game's Off-TV Play routing
//   off      not shown (the game keeps drawing it)
//   gamepad  only the GamePad picture, fitted to the TV window, like a Wii U played without a TV
//            (Wind Waker HD's Off-TV Play, Minus, puts the game itself on it); a click or touch on
//            it touches the GamePad
#pragma once
#include <atomic>
#include <cstdint>

#include "display.h"
#include "auto_pip.h"

namespace gfx {

enum DrcMode { kDrcWindow, kDrcPip, kDrcAuto, kDrcOff, kDrcGamePad, kDrcModeCount };
enum Filter { kSmooth, kSharp, kInteger };
extern const char* const kModeNames[kDrcModeCount];  // window, pip, auto, off, gamepad
enum PipAnchor { kPipTL, kPipTC, kPipTR, kPipCL, kPipCC, kPipCR, kPipBL, kPipBC, kPipBR, kPipCustom };
extern const char* const kAnchorNames[10];
// Gameplay capture at 1280x720: rupees at y=670..696 (93–97%); reserve
// everything below 86% to clear the counter and its shadow across aspects.
inline constexpr float kPipRupeeBottom = 0.86f;
// Gameplay HUD extents at 720p: sword/D-pad ends near y=158, actions near y=215.
inline constexpr float kPipLeftHudBottom = 0.24f;
inline constexpr float kPipRightHudBottom = 0.32f;
struct PipPosition { int anchor; float x, y; };
float pip_clamp(float value, float lo, float hi, float fallback);
int pip_load_anchor(const char* anchor, const char* legacy);
Box pip_box(Box tv, float aspect, float size, PipPosition position);
PipPosition pip_place(Box tv, float aspect, float size, float x, float y, bool snap = true);
PipPosition pip_position();
void pip_set_position(PipPosition position);
extern const char* const kFilterNames[3];             // smooth, sharp, integer

// the options (read from any thread; changed on the host's main thread)
extern std::atomic<int> g_mode;
extern std::atomic<int> g_pip_anchor;
extern std::atomic<float> g_pip_x, g_pip_y;
extern std::atomic<bool> g_pip_preview;
extern std::atomic<float> g_pip_size;      // fraction of the TV picture's width, 0.1..0.5
extern std::atomic<float> g_pip_opacity;   // 0.2..1
extern std::atomic<int> g_filter;          // picture scaling (the SDL host mirrors gfxvk::scale_filter)
extern std::atomic<bool> g_shown;          // window / pip modes: GamePad screen shown (Cmd+G, Pro Controller)
extern std::atomic<bool> g_auto_pin;       // auto mode: Cmd+G keeps the overlay up
extern std::atomic<double> g_auto_until;   // auto mode: closing grace expires at this time (display_now)
extern std::atomic<float> g_drc_aspect;    // of the GamePad image, for the GamePad window's touch
extern std::atomic<bool> g_has_drc_window; // the host made a GamePad window (window mode possible)
// a physical second screen (Android) switched off by the player while it was connected: Off chosen, or the
// GamePad screen hidden (#222). Not the single-screen mode, which is Off by default on Android.
extern std::atomic<bool> g_second_screen_off;
extern std::atomic<bool> g_second_display_available; // capability, independent of live surface
// Android start: the saved Off choice read by Java before the native settings load, so a saved Off never
// acquires the second screen for a moment (#222). Ignored once load_second_screen_off ran.
void seed_second_screen_off(bool off);
void load_second_screen_off(bool off);  // the saved choice from settings.ini (hostui::load_saved_options)

int find_name(const char* const* names, int n, const char* s, int def);  // index of s, def if absent
double display_now();  // seconds, monotonic (the automatic overlay's clock)
bool drc_mode_offered(int m);  // window: the host has a window or an available second display
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
bool drc_screen_off();             // switched off (Off, or hidden in window mode): a physical second screen
                                   // (Android) shows black and ignores touches (issue #222)
bool drc_screen_shown(bool drc_window_visible);  // show/hide control state; auto: whether the overlay is pinned
void display_show_drc(bool on);    // show / hide the GamePad screen; auto: set / clear the pin
void display_set_mode(int m);      // a new mode (shown again unless off; auto starts unpinned)
// The optional Plus-button pause view uses the same read-only game state, with no GPU sampling.
// On by default on Android; WWHD_DRC_PAUSE=0|1. A manually chosen view stays.
void display_plus_pressed();
int display_saved_mode();
bool display_gamepad_state_needed();
void display_read_gamepad_ui(); // guest thread, once at swap; returns immediately outside auto/pause
void display_gamepad_ui(GamePadUiState state); // publish a host-only snapshot

// pixel rectangles in the target (drawable), top-left origin
struct Layout { Box tv, pip; bool pip_on = false; bool drc_only = false; float scale = 1; };
// the TV window's layout: TV picture (tw x th) scaled to fit dw x dh, GamePad picture (pw x ph) in
// the selected anchor (pip_on) or alone, fitted (drc_only)
Layout layout(float dw, float dh, float tw, float th, float pw, float ph, bool pip_on, bool drc_only = false);
// a point in the TV window (0..1 from top left) on the GamePad picture -> touch position (0..1),
// from the last display_plan; clamp_outside: a drag that left the picture keeps touching its edge
bool overlay_hit(float nx, float ny, float* tx, float* ty, bool clamp_outside = false);
void display_input_plan(const PresentPlan& plan); // publish the logical canvas used by the compositor
// the main picture of the last TV layout (the TV picture, or the GamePad picture in GamePad-only
// mode), 0..1 of the TV window from the top left; false before the first layout (the settings
// overlay's text prompt places itself on it)
bool main_picture(float* x, float* y, float* w, float* h);

// Touch screens (Android; WWHD_VIEW_BUTTON=1 elsewhere for tests): a button in the TV window's top
// left corner. A tap cycles the views offered there (picture-in-picture, GamePad only, TV only);
// what a long press does is the host's (Android: 60 fps on / off).
extern std::atomic<int> g_touch_buttons;
extern std::atomic<bool> g_touch_controller, g_touch_controls;
bool touch_buttons_visible();
void reveal_touch_buttons();
bool view_button_enabled();
bool view_button_hit(float nx, float ny);  // a point in the TV window (0..1) on the button
int next_view();                           // the mode a tap switches to (the host sets and saves it)

}  // namespace gfx
