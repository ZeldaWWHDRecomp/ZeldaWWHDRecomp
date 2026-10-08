// Screenshots for players: the Screenshot binding (F10 by default, rebindable in the Controls tab /
// window like the GamePad inputs, also to a controller input) saves the TV picture as a PNG.
//
// What is saved: the TV picture of the frame shown when the key was pressed, as the game drew it, at
// the internal resolution and aspect ratio (1280x720 at 1x 16:9, 2560x1080 at 1.5x 21:9, ...), after
// the game's own post-processing and the gameplay mods' HUD, with FXAA when it is on (the window's
// scaling filter is left out: the picture is not resized). Never the settings overlay, the notices or
// the performance overlay. Optionally (setting "Also save the GamePad screen", off by default) the
// GamePad picture as a second file while it is shown (GamePad window or picture-in-picture).
// Where: <user data>/screenshots, next to the states folder: ~/Library/Application Support/wwhd,
// %APPDATA%\WWHD or ~/.config/wwhd for source builds, data/user in a release folder (portable.txt);
// WWHD_SCREENSHOT_DIR overrides. Files: WindWakerHD_YYYY-MM-DD_HH-MM-SS[_n].png (local time; _2, _3 ...
// when several land in the same second), the GamePad picture as ..._GamePad.png.
//
// Both renderers compose the picture on the GPU into an RGBA8 image (display-encoded sRGB values, as
// the frame dumps, WWHD_DUMP_FRAMES) in the frame's own command buffer, read it back once the GPU is
// done with that frame (Metal: completion handler; Vulkan: the submission's fence, polled at swaps) and
// hand it to a worker thread that encodes and writes the PNG: the frame is never stalled for it.
//
// Test: WWHD_TEST_SCREENSHOT=600,900 takes screenshots at those TV frames (as the key would);
// WWHD_SCREENSHOT_GAMEPAD=0|1 sets the GamePad option for that start (test runs do not read settings).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace screenshot {

// any thread: take a screenshot of the next frame shown
void request();
// hosts: a key went down (not a repeat) in a game window; true if it is bound to Screenshot (taken)
bool key_down(int code);
// hosts: the controller inputs (input_map::Pad values, 0..1), polled; a press of the bound one takes one
void poll_controller(const float* values);

// renderers, at each swap (render thread): true if this frame is to be saved; then `tv` (and `gamepad`
// when the option is on, else empty) are the files to write
bool take(uint64_t frame, std::string& tv, std::string& gamepad);
bool gamepad_too();
void set_gamepad_too(bool on);  // the setting (saved by the host: hostui "screenshotGamePad")

// renderers: the picture is ready (RGBA8, or BGRA8 with `bgra`; display-encoded; `stride` bytes per row;
// `pixels` stays valid while `owner` lives; null: no picture after all, the name is released). Encoding and writing happen on the worker thread; the TV
// file shows a notice when written. `frame` is for the log.
void write_async(const std::string& path, uint32_t width, uint32_t height, size_t stride, const uint8_t* pixels,
                 std::shared_ptr<const void> owner, uint64_t frame, bool tv, bool bgra = false);

std::string dir();  // (created when a screenshot is taken)
// on the way out (main thread): wait (up to 10 s) for the screenshots taken to be written; the
// render thread keeps running meanwhile (Vulkan reads them back at its swaps)
void finish();
std::string last_file();  // the newest screenshot written ("" none yet)

// PNG (RGB, 8 bit, sRGB chunk) of RGBA8 (BGRA8: bgra) pixels; false on a write error
bool write_png(const std::string& path, uint32_t width, uint32_t height, size_t stride, const uint8_t* rgba, bool bgra = false);

}  // namespace screenshot
