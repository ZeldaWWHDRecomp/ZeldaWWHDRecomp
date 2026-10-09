// In-game settings overlay (Dear ImGui), drawn on top of the TV window's picture by both renderers.
//
// F1 opens and closes it (Esc closes); on macOS also Cmd+, (app menu > Settings...; most Mac keyboards
// send F1 only with Fn unless the top row is set to standard function keys); on a controller, hold Select/Minus for half a second or press
// Home. While it is open the game sees no buttons (it keeps running) and the overlay takes keyboard,
// mouse and controller. Tabs: Saves, Graphics, Display, Mods, Controls, Language / About. Every option
// calls the same functions as the macOS menu bar (which stays in sync) or the SDL host's shortcuts.
// When the game asks for text, the overlay shows its text prompt (text_entry.h) with the same input rules.
//
// Threads: hosts feed input on their main thread; the renderer builds and draws the UI on its render
// thread once per TV present (frame()); option changes go back to the main thread (hostui::post).
//
// Test switches:
//   WWHD_TEST_OVERLAY=open[:<tab>][@<frame>]   open the overlay (tab: saves, graphics, display, mods,
//                                              controls, about) at TV frame <frame> (default 1)
//   WWHD_TEST_MOD_DISABLE/WWHD_TEST_MOD_REMOVE=<id>  normal package actions in headless runs
//   WWHD_TEST_CATALOGUE_INSTALL=<id>         refresh and install through the normal catalogue UI worker
//                                              (headless tests only; open the Mods tab separately)
//   WWHD_TEST_MOD_SETUP=<id>               drive required setup steps with normal APIs/trust
//   WWHD_TEST_GAME_SOURCES=<absolute JSON>  game-ID -> local path; setup diagnostics require
//                                              NO_HOST_INPUT and explicit MOD_MANAGER_DIR too
//   WWHD_TEST_OVERLAY=perf                     only the performance overlay
//   WWHD_TEST_PERF_REPORT=<file>                export the clipboard report in a headless test
//   WWHD_TEST_PERF_REPORT_AT=<frame>            export at this presented frame (default 300)
#pragma once
#include <cstdint>

struct ImDrawData;

namespace overlay {

bool is_open();             // any thread
void set_open(bool open);   // any thread
// the game sees no input: the overlay is open, or was just closed and buttons are still held
bool blocks_input();
// the overlay has keyboard, mouse and controller: the menu is open or the game's text prompt shows
// (text_entry.h); hosts route the pointer by this
bool captures();
// the renderer draws the overlay (frame() ran within the last second): the text prompt can show
bool alive();
bool perf_shown();          // performance overlay (FPS, frame time) on
void set_perf_shown(bool on);

// ---- host input (main thread)
// Key codes are macOS virtual key codes (kVK_*); the SDL host maps to them (platform/keycodes.h).
enum Mods : int { kShift = 1, kCtrl = 2, kAlt = 4, kSuper = 8 };
// returns true when the overlay used the key: F1 toggles, and every key while it is open
bool key(int code, bool down, bool repeat, int mods);
// pointer in the TV window, normalised to its content area (0..1 from the top left); return true while
// the overlay is open (the event is the overlay's, not the game's or the mouse camera's)
bool mouse_move(float nx, float ny);
bool mouse_button(int button, bool down);  // 0 left, 1 right, 2 middle
bool mouse_wheel(float dx, float dy);      // in lines, +y = away from the user
void set_density(float pixels_per_point);  // TV window backing scale (HiDPI)

// ---- render thread, once per TV present
// Builds the UI for a TV window of pw x ph pixels; null when there is nothing to draw this frame.
// The draw data stays valid until the next call (a frame may be drawn into several targets).
// renderer_init runs once, after the Dear ImGui context exists (the renderer's backend setup).
ImDrawData* frame(float pw, float ph, void (*renderer_init)());
// The same draw data for a target with sRGB encoding: vertex colours converted to linear once per
// frame (the Metal backend draws colours as they are; the Vulkan renderer converts in its shader).
void linearize_colors(ImDrawData* d,bool fresh_copy=false);

}  // namespace overlay
