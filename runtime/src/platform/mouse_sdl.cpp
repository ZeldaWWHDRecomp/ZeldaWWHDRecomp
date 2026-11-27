#include "mouse_sdl.h"
#include "keycodes.h"
#include "../mods/mods.h"
#include "../motion/motion.h"
#include "../overlay/overlay.h"
#include "../runtime.h"
#include <atomic>
#include "../input_map.h"
#include <cstdlib>

namespace mods {
namespace {
SDL_Window* tv = nullptr;
std::atomic<bool> captured{false};
std::atomic<bool> release_requested{false};
void release_now() {
    release_requested.store(false);
    if (!captured.exchange(false)) return;
    if (tv) SDL_SetWindowRelativeMouseMode(tv, false);
    input::release_mouse_bindings();
    mouse_button(0, false);
    mouse_button(1, false);
    LOG("[mods] mouse released");
}
}
bool mouse_captured() { return captured.load(); }
void mouse_release() { release_requested.store(true); }
void mouse_init(void* window) { release_now(); tv = static_cast<SDL_Window*>(window); }
bool host_key_down(uint16_t code) {
    if (code == kVK_Escape && mouse_captured()) { mouse_release(); return true; }
    return false;
}
// the mouse as a gyro (motion.h): while the game aims the pointer is captured, so that Steam Input's
// gyro-to-mouse (or the mouse) never stops at the screen's edge; it is released when the aim ends
static bool gyro_capture = false;
static void update_gyro_capture() {
    const bool want = tv && motion::mouse_drives_gyro() && !overlay::captures() && SDL_GetKeyboardFocus() == tv &&
                      !getenv("WWHD_NO_HOST_INPUT");
    if (want && !gyro_capture) {
        gyro_capture = true;
        if (!captured.load() && SDL_SetWindowRelativeMouseMode(tv, true)) LOG("[gyro] mouse captured while the game aims");
    } else if (!want && gyro_capture) {
        gyro_capture = false;
        if (!captured.load() && tv) SDL_SetWindowRelativeMouseMode(tv, false);
    }
}
void update_mouse() {
    if (release_requested.load() || (captured.load() && !mouse_camera())) release_now();
    update_gyro_capture();
}
bool handle_mouse_event(const SDL_Event& event) {
    update_mouse();
    if (!tv || getenv("WWHD_NO_HOST_INPUT")) return false;
    const SDL_WindowID id = SDL_GetWindowID(tv);
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && event.window.windowID == id) release_now();
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == id &&
        event.key.scancode == SDL_SCANCODE_ESCAPE && captured.load()) {
        release_now(); return true;
    }
    // the mouse as a gyro: every movement in the game window (motion.h keeps it only while the game aims)
    if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.windowID == id) {
        motion::mouse_motion(event.motion.xrel, event.motion.yrel);
        if (motion::mouse_drives_gyro()) return true;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.windowID == id && mouse_wheel_owned()) {
        float y = event.wheel.y;
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;
        mouse_wheel(y); return true;
    }
    if (!mouse_camera()) return false;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.windowID == id &&
        event.button.button == SDL_BUTTON_LEFT && !captured.load()) {
        if (SDL_SetWindowRelativeMouseMode(tv, true)) {
            captured.store(true);
            LOG("[mods] mouse captured (Esc releases; unbound middle click also releases)");
            return true;
        }
        return false;
    }
    if (!captured.load()) return false;
    if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.windowID == id) {
        mouse_add(event.motion.xrel, event.motion.yrel); return true;
    }
    if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.windowID == id) {
        const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (event.button.button == SDL_BUTTON_LEFT) mouse_button(0, down);
        else if (event.button.button == SDL_BUTTON_RIGHT) mouse_button(1, down);
        else if (event.button.button == SDL_BUTTON_MIDDLE && down && input_map::key_users(input_map::current(), input_map::kMouse3).empty()) release_now();
        return true;
    }
    return false;
}
}
