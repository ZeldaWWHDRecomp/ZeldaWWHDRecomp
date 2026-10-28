// In-game settings overlay (Dear ImGui), drawn on top of the TV window's picture by both renderers.
//
// The Settings shortcut (default Esc, alternate F1) opens and closes it (Esc closes; with a captured mouse
// the first Esc only frees the mouse); on macOS also Cmd+, (app menu > Settings..., fixed; most Mac keyboards
// send F1 only with Fn unless the top row is set to standard function keys); on a controller, hold Select/Minus for half a second or press
// Home. While it is open the game sees no buttons (it keeps running) and the overlay takes keyboard,
// mouse and controller. Tabs: Saves, Graphics, Display, Audio, Mods, Controls, Language / About. Every option
// calls the same functions as the macOS menu bar (which stays in sync) or the SDL host's shortcuts.
// When the game asks for text, the overlay shows its text prompt (text_entry.h) with the same input rules.
// Ports add or hide tabs, and plug in fonts, shortcuts, a system keyboard and layers over the game,
// through extensions.h (registered at start-up).
//
// Threads: hosts feed input on their main thread; the renderer builds and draws the UI on its render
// thread once per TV present (frame()); option changes go back to the main thread (hostui::post).
//
// Test switches:
//   WWHD_TEST_OVERLAY=open[:<tab>][@<frame>]   open the overlay (tab: saves, graphics, display, audio, mods,
//                                              controls, about, or an added tab's id) at TV frame <frame>
//                                              (default 1)
//   WWHD_TEST_MOD_DISABLE/WWHD_TEST_MOD_REMOVE=<id>  normal package actions in headless runs
//   WWHD_TEST_CATALOGUE_INSTALL=<id>         refresh and install through the normal catalogue UI worker
//                                              (headless tests only; open the Mods tab separately)
//   WWHD_TEST_MOD_SETUP=<id>               drive required setup steps with normal APIs/trust
//   WWHD_TEST_GAME_SOURCES=<absolute JSON>  game-ID -> local path; setup diagnostics require
//                                              NO_HOST_INPUT and explicit MOD_MANAGER_DIR too
//   WWHD_TEST_OVERLAY=perf                     only the performance overlay
//   WWHD_TEST_PERF_REPORT=<file>                export the clipboard report in a headless test
//   WWHD_TEST_PERF_REPORT_AT=<frame>            export at this presented frame (default 300)
//   WWHD_TEST_PERF_REPORT=<file>@<frame>,...    several reports, each at its presented frame
//   WWHD_TEST_PROFILING=1@<frame> (or 0@)       set Graphics > Detailed performance profiling at a TV frame
//   WWHD_TEST_BG_SHADERS=fast@<frame> (or 0@)   set Graphics > Shader compile at a TV frame
#pragma once
#include <cstdint>
#include <string>

struct ImDrawData;

namespace overlay {

// Host input thread, every input poll (the AppKit host: the overlay's frame): the settings shortcut
// (Home, Select held) and the platform's shortcuts (extensions.h).
void poll_controller_shortcuts(const float* values);
// Native Controls capture also owns controller presses until they are released.
void external_binding_capture(bool active);
// A new controller (no bindings of its own, not declined) was first used: offer the controller setup
// (controller_setup.h). Opens Settings for the question. Any thread.
void offer_controller_setup(const std::string& guid, const std::string& name);
// A platform shortcut took the input since the last call (and clears that). Hosts count such a poll as
// captured, as the press that opens or closes the menu: the inputs held stay from the game until each one
// is released (release_gate.h). Always false without platform shortcuts.
bool shortcut_took_input();
bool is_open();             // any thread
enum class OpenReason { API, F1, Esc, Home, View, B, CloseButton, LoadSlot, Focus };
struct KeyOrigin { uint32_t window = 0; uint64_t timestamp = 0; const char* name = "host"; };
void activate_shortcut(int action, KeyOrigin origin = {});
void set_open(bool open, OpenReason reason = OpenReason::API, KeyOrigin origin = {});   // any thread
// the game sees no input: the overlay has it (captures()). Controller inputs held when it closes stay
// from the game until each is released (the hosts' input pumps, release_gate.h).
bool blocks_input();
// the overlay has keyboard, mouse and controller: the menu is open or the game's text prompt shows
// (text_entry.h); hosts route the pointer by this
bool captures();
bool dialog_open();  // any thread: a dialog or a list is open in Settings (Esc and Android's Back close it)
// the world editor's overlays (extensions.h InputLayer): a layer is active while the menu is closed, and
// takes the pointer now (its mode, or the pointer over one of its windows)
bool pointer_layer_active();
bool layer_takes_pointer();
// the renderer draws the overlay (frame() ran within the last second): the text prompt can show
bool alive();
bool perf_shown();          // performance overlay (FPS, frame time) on
void set_perf_shown(bool on);

// ---- host input (main thread)
// Key codes are macOS virtual key codes (kVK_*); the SDL host maps to them (platform/keycodes.h).
enum Mods : int { kShift = 1, kCtrl = 2, kAlt = 4, kSuper = 8 };
// Release UI keys on host focus loss without changing the menu or shortcut latch.
void focus_lost();
// Returns true when the overlay used the key: the Settings key toggles, Esc frees a captured mouse first,
// and every key while it is open.
bool key(int code, bool down, bool repeat, int mods, KeyOrigin origin = {});
// pointer in the TV window, normalised to its content area (0..1 from the top left); return true while
// the overlay is open (the event is the overlay's, not the game's or the mouse camera's)
// typed characters (UTF-8, after the key event that produced them); true while the overlay is open
bool text(const char* utf8);
// the system clipboard's text, read by the host just before it passes Ctrl/Cmd+V on (main thread)
void set_clipboard_text(const char* utf8);
// a text field of the overlay has the keyboard (hosts turn on text input / the on-screen keyboard)
bool wants_text();
bool mouse_move(float nx, float ny, bool touch = false);
bool pointer_shortcut(int code, bool down); // physical mouse buttons/wheel, before game mouse handling
bool mouse_button(int button, bool down, bool touch = false);  // 0 left, 1 right, 2 middle
// Test aid for scripted hidden runs (the world editor's WWHD_TEST_EDITOR_DRAG): the pointer at (nx, ny) in 0..1
// of the picture and the left button, as if the host had sent them (also with WWHD_NO_HOST_INPUT).
void test_pointer(float nx, float ny, bool down);
void set_stereo_pointer_visible(bool visible); // host: pointer is inside the TV window
bool mouse_wheel(float dx, float dy);      // in lines, +y = away from the user
void set_android_screen_dpi(float dpi);  // physical Android display DPI
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
