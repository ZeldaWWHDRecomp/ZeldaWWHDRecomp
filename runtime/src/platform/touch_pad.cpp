// Android on-screen controls (android/.../TouchControls.java): the buttons and sticks drawn over the
// game are a virtual SDL gamepad, so the game reads them like any controller (input_sdl.cpp opens
// it on SDL_EVENT_GAMEPAD_ADDED). The gamepad exists while the controls are shown.
#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <jni.h>
#include <mutex>
#include "../runtime.h"
#include "touch_face.h"
#include "phone_rumble.h"
#include "../gfx/display_modes.h"
#include "../overlay/hostui.h"
#include "../overlay/overlay.h"

namespace {
std::mutex g_mu;
SDL_JoystickID g_id = 0;
SDL_Joystick* g_pad = nullptr;

bool attach() {
    if (g_pad) return true;
    if (!SDL_WasInit(SDL_INIT_JOYSTICK)) return false;  // before input::init: try on the next touch
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "Touch controls";
    g_id = SDL_AttachVirtualJoystick(&desc);
    if (!g_id) { LOG("[input] touch controls: %s", SDL_GetError()); return false; }
    g_pad = SDL_OpenJoystick(g_id);
    if (!g_pad) { SDL_DetachVirtualJoystick(g_id); g_id = 0; return false; }
    LOG("[input] touch controls on");
    return true;
}
}  // namespace

// buttons: bit n is SDL_GamepadButton n, except the face buttons; face: bit 0..3 = the drawn A, B,
// X, Y (touch_face.h presses the pad buttons the mapping binds to them); sticks -1..1 (y down),
// triggers 0..1
extern "C" JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeSetPad(
    JNIEnv*, jclass, jint buttons, jint face, jfloat lx, jfloat ly, jfloat rx, jfloat ry, jfloat lt, jfloat rt) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!attach()) return;
    if (buttons || face || lx || ly || rx || ry || lt || rt) phone_rumble::last_input_touch = true;
    auto axis = [](float v) { return (Sint16)SDL_clamp(v * 32767.0f, -32768.0f, 32767.0f); };
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX, axis(lx));
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY, axis(ly));
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHTX, axis(rx));
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHTY, axis(ry));
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, axis(lt));
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, axis(rt));
    const uint32_t pressed = uint32_t(buttons) | touch_face::buttons(uint32_t(face), input_map::current());
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && b < 31; b++)
        SDL_SetJoystickVirtualButton(g_pad, b, (pressed >> b) & 1);
}

// the controls were hidden: the gamepad goes away (a physical controller is the only one again)
extern "C" JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeDetachPad(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lk(g_mu);
    phone_rumble::last_input_touch = false;
    if (!g_pad) return;
    SDL_CloseJoystick(g_pad);
    SDL_DetachVirtualJoystick(g_id);
    g_pad = nullptr;
    g_id = 0;
    LOG("[input] touch controls off");
}
#endif

#ifdef __ANDROID__
extern "C" JNIEXPORT jint JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeTouchState(JNIEnv*, jclass) {
    return (gfx::touch_buttons_visible() ? 1 : 0) | (gfx::g_touch_controls ? 2 : 0) | (overlay::captures() ? 4 : 0);
}
extern "C" JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeRevealButtons(JNIEnv*, jclass) {
    gfx::reveal_touch_buttons();
}
extern "C" JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeShowControls(JNIEnv*, jclass, jboolean shown) {
    gfx::g_touch_controls = shown;
    hostui::set("touchControls", shown ? "1" : "0");
}
#endif

#ifdef __ANDROID__
extern "C" JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeScreenDpi(JNIEnv*, jclass, jfloat dpi) {
    overlay::set_android_screen_dpi(dpi);
}
#endif

#ifdef __ANDROID__
extern "C" JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeControllerConnected(JNIEnv*, jclass, jboolean connected) {
    gfx::g_touch_controller = connected;
}
#endif
