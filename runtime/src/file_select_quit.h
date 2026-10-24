// Host-only file-select Quit UI. No guest state is changed.
#pragma once
#include "input.h"
#include <cstdint>
struct Cpu;
namespace file_select_quit {
struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};
inline constexpr Rect kQuitBox{520, 605, 240, 100};

// Logical overlay coordinates, after HiDPI conversion. Rendering and pointer
// routing must use the same TV rectangle, including letterboxing and dual-screen layouts.
struct View {
    float width = 1280, height = 720;
    Rect tv{0, 0, 1280, 720};
    float scale() const { return tv.h / 720.f; }
    float origin_x() const { return tv.x + (tv.w - 1280 * scale()) * .5f; }
    float origin_y() const { return tv.y; }
    bool contains(float nx, float ny) const {
        if (width <= 0 || height <= 0 || tv.w <= 0 || tv.h <= 0)
            return false;
        const float px = nx * width, py = ny * height;
        return tv.contains(px, py) && kQuitBox.contains((px - origin_x()) / scale(), (py - origin_y()) / scale());
    }
};

// Input routing is kept independent of the renderer for regression tests.
struct Routing {
    struct Event {
        bool open = false, dismiss = false;
        uint32_t pressed = 0;
    };
    bool x_held = false;
    bool filter(input::PadState &pad, bool visible) {
        const bool down = (pad.buttons & input::kX) != 0;
        const bool pressed = visible && down && !x_held;
        x_held = down;
        if (visible)
            pad.buttons &= ~input::kX;
        return pressed;
    }
    Event route(input::PadState &pad, bool eligible, bool shown, bool confirming) {
        const uint32_t raw = pad.buttons | (pad.lx < -.5f ? input::kLeft : 0) | (pad.lx > .5f ? input::kRight : 0);
        if (!eligible || !shown) {
            previous = raw;
            x_held = (raw & input::kX) != 0;
            release = 0;
            // A presentation stall hides the UI temporarily; only loss of the
            // native context or the setting dismisses its pending confirmation.
            return {false, !eligible, 0};
        }
        const bool open = filter(pad, true) && !confirming;
        uint32_t pressed = 0;
        if (confirming || open) {
            pressed = raw & ~previous;
            release |= raw;
            pad = input::PadState{};
        } else {
            release &= raw;
            pad.buttons &= ~release;
        }
        previous = raw;
        return {open, false, pressed};
    }

  private:
    uint32_t previous = 0, release = 0;
};
void observe(Cpu *cpu); // guest draw thread, at the title background's draw
void filter(input::PadState &pad);
bool visible();
bool confirming();
bool pointer(float x, float y); // normalized full-overlay coordinates
void set_tv_region(float x, float y, float width, float height, bool visible); // physical presentation pixels
void frame(float width, float height);
void backend_destroyed();
bool image_texture(uint64_t id); // native layout images encoded as sRGB
} // namespace file_select_quit
