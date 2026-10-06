// Host input: keyboard and game controllers, read as a Wii U GamePad.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace input {

// VPAD button bits (as returned in VPADStatus.hold)
enum : uint32_t {
    kA = 0x8000, kB = 0x4000, kX = 0x2000, kY = 0x1000,
    kL = 0x0020, kR = 0x0010, kZL = 0x0080, kZR = 0x0040,
    kPlus = 0x0008, kMinus = 0x0004, kHome = 0x0002,
    kUp = 0x0200, kDown = 0x0100, kLeft = 0x0800, kRight = 0x0400,
    kStickR = 0x00020000, kStickL = 0x00040000,
};

struct PadState {
    uint32_t buttons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0;  // -1..1, +y = up
    bool touch = false;
    float tx = 0, ty = 0;                  // touch position on the GamePad screen, 0..1 from top left
};

void set_touch(bool down, float x, float y);  // mouse on the GamePad window

// Which Wii U controller the keyboard and host controllers act as. In Pro Controller mode the
// GamePad stays connected (its screen and touch still work) but its buttons and sticks are idle.
bool pro_controller();
void set_pro_controller(bool on);

void init();       // main thread, after NSApplication exists
PadState read();   // any thread
void release_keys();                // forget held keys (another window took the keyboard)
void controller_values(float* v);   // main thread: input_map::kPadCount host controller inputs, 0..1
void host_controller_values(float* v);  // any thread: the latest controller_values (settings overlay)
void held_keys(bool* keys);         // 256 entries: keys held for the game (keyboard + WWHD_KEYS script)

// Ask the user for a line of text (software keyboard). Non-blocking: `done` runs on the
// main thread with ok = false when cancelled. Text is UTF-16.
void prompt_text(const std::u16string& initial, int max_len,
                 std::function<void(bool ok, std::u16string text)> done);

// The host drives the controllers' rumble motors (SDL host; the game's requests are kept by
// rumble.h). The AppKit host does not yet.
bool has_rumble();
// Any thread: the motors still at once and for good; the process ends (quit, exit, crash handler).
void stop_rumble_now();

}  // namespace input
