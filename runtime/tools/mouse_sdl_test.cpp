// Exercise event routing and capture transitions without opening a desktop window.
// The SDL window boundary is simulated; the production mouse event code is linked.
#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>
#include "mods/mods.h"
#include "input_map.h"
namespace input { void release_mouse_bindings() {} }
#include "platform/mouse_sdl.h"
#include "platform/keycodes.h"
static bool camera = true, wheel_enabled = true, relative = false;
static float dx = 0, dy = 0, wheel = 0;
static bool buttons[2]{};
extern "C" SDL_WindowID SDL_GetWindowID(SDL_Window*) { return 17; }
extern "C" bool SDL_SetWindowRelativeMouseMode(SDL_Window*, bool value) { relative = value; return true; }
extern "C" SDL_Window* SDL_GetKeyboardFocus() { return reinterpret_cast<SDL_Window*>(1); }
void log_msg(const char*, ...) {}
// the mouse as a gyro (motion/motion.h): the game aims while `aiming`
static bool gyro_mouse = false, aiming = false, overlay_open = false;
static float gx = 0, gy = 0;
namespace motion {
void mouse_motion(float x, float y) { if (gyro_mouse && aiming) { gx += x; gy += y; } }
bool mouse_drives_gyro() { return gyro_mouse && aiming; }
}
namespace overlay { bool captures() { return overlay_open; } }
namespace mods {
bool mouse_camera() { return camera; }
bool mouse_wheel_owned() { return wheel_enabled; }
void mouse_add(float x, float y) { dx += x; dy += y; }
void mouse_wheel(float y) { wheel += y; }
void mouse_button(int i, bool value) { buttons[i] = value; }
}
int main() {
    mods::mouse_init(reinterpret_cast<void*>(1));
    SDL_Event e{};
    auto capture = [&] {
        e = {}; e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.windowID = 17; e.button.button = SDL_BUTTON_LEFT;
        assert(mods::handle_mouse_event(e));
        assert(relative && mods::mouse_captured());
    };
    capture();
    e = {}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.windowID = 18;
    e.motion.xrel = 4; e.motion.yrel = -3;
    assert(!mods::handle_mouse_event(e)); assert(dx == 0 && dy == 0);
    e.motion.windowID = 17; assert(mods::handle_mouse_event(e));
    assert(dx == 4 && dy == -3);
    e = {}; e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.windowID = 17;
    e.button.button = SDL_BUTTON_RIGHT; assert(mods::handle_mouse_event(e)); assert(buttons[1]);
    e = {}; e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.windowID = 17;
    e.wheel.y = 2; e.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    assert(mods::handle_mouse_event(e)); assert(wheel == -2);
    e = {}; e.type = SDL_EVENT_WINDOW_FOCUS_LOST; e.window.windowID = 17;
    mods::handle_mouse_event(e);
    assert(!relative && !mods::mouse_captured() && !buttons[1]);
    capture(); assert(mods::host_key_down(kVK_Escape)); mods::update_mouse();
    assert(!relative && !mods::mouse_captured());
    capture(); camera = false; mods::update_mouse();
    assert(!relative && !mods::mouse_captured()); camera = true;
    capture(); e = {}; e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.windowID = 17; e.button.button = SDL_BUTTON_MIDDLE;
    assert(mods::handle_mouse_event(e)); assert(!relative && !mods::mouse_captured());
    auto mapping=input_map::Mapping::defaults();mapping.keys[input_map::kR][1]=input_map::kMouse3;
    input_map::set_current(mapping,false);capture();
    e={};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=17;e.button.button=SDL_BUTTON_MIDDLE;
    assert(mods::handle_mouse_event(e));assert(relative && mods::mouse_captured());
    assert(mods::host_key_down(kVK_Escape));mods::update_mouse();assert(!relative);
    input_map::set_current(input_map::Mapping::defaults(),false);
    // mouse gyro: the pointer is captured while the game aims and released after; movement goes to the
    // gyro then, not to the mouse camera
    gyro_mouse = true; mods::update_mouse(); assert(!relative);
    aiming = true; mods::update_mouse(); assert(relative && !mods::mouse_captured());
    dx = dy = 0;
    e = {}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.windowID = 17; e.motion.xrel = 5; e.motion.yrel = 2;
    assert(mods::handle_mouse_event(e)); assert(gx == 5 && gy == 2 && dx == 0 && dy == 0);
    overlay_open = true; mods::update_mouse(); assert(!relative);  // the settings overlay takes the pointer
    overlay_open = false; mods::update_mouse(); assert(relative);
    aiming = false; mods::update_mouse(); assert(!relative);
    // the mouse camera's own capture survives the end of an aim
    capture(); aiming = true; mods::update_mouse(); aiming = false; mods::update_mouse();
    assert(relative && mods::mouse_captured());
    assert(mods::host_key_down(kVK_Escape)); mods::update_mouse(); assert(!relative);
    gyro_mouse = false;
    mods::mouse_init(nullptr);
    puts("mouse_sdl_test: capture, relative motion, window routing, wheel, release and mouse gyro capture passed");
}
