// Android on-screen controls (android/.../TouchControls.java): the buttons and sticks drawn over the
// game are a virtual SDL gamepad, so the game reads them like any controller (input_sdl.cpp opens
// it on SDL_EVENT_GAMEPAD_ADDED). The gamepad exists while the controls are shown.
#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <jni.h>
#include <mutex>
#include "../runtime.h"

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

// buttons: bit n is SDL_GamepadButton n; sticks -1..1 (y down), triggers 0..1
extern "C" JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeSetPad(
    JNIEnv*, jclass, jint buttons, jfloat lx, jfloat ly, jfloat rx, jfloat ry, jfloat lt, jfloat rt) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!attach()) return;
    auto axis = [](float v) { return (Sint16)SDL_clamp(v * 32767.0f, -32768.0f, 32767.0f); };
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX, axis(lx));
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY, axis(ly));
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHTX, axis(rx));
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHTY, axis(ry));
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, axis(lt));
    SDL_SetJoystickVirtualAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, axis(rt));
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && b < 31; b++)
        SDL_SetJoystickVirtualButton(g_pad, b, (buttons >> b) & 1);
}

// the controls were hidden: the gamepad goes away (a physical controller is the only one again)
extern "C" JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeDetachPad(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_pad) return;
    SDL_CloseJoystick(g_pad);
    SDL_DetachVirtualJoystick(g_id);
    g_pad = nullptr;
    g_id = 0;
    LOG("[input] touch controls off");
}
#endif
