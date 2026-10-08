// SDL host (Windows, Linux, Android): the TV window's display refresh rate for frame interpolation
// (interp::output_fps caps 120/240 fps to it). Polled by the SDL main loop (gfx/vulkan/backend.cpp).
// The AppKit host reports it in gfx/display.mm.
//
// Android: phones with 90/120 Hz screens often run an app at 60 Hz unless it asks for more. While
// frame interpolation draws more than 60 fps, the game asks for a display mode with at least that
// rate (WwhdActivity.requestRefreshRate), and leaves the choice to the system again otherwise. The
// rate in use is read from the display itself (SDL's value is only updated with the surface size).
#include "display_rate.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "../interp.h"
#include "../runtime.h"

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace display_rate {
namespace {
#ifdef __ANDROID__
// static float/void methods of WwhdActivity (the game's activity class)
jclass activity_class(JNIEnv* env) {
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!activity) return nullptr;
    jclass cls = env->GetObjectClass(activity);
    env->DeleteLocalRef(activity);
    return cls;
}
float call_float(const char* name) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return 0;
    jclass cls = activity_class(env);
    if (!cls) return 0;
    float v = 0;
    if (jmethodID m = env->GetStaticMethodID(cls, name, "()F")) v = env->CallStaticFloatMethod(cls, m);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        v = 0;
    }
    env->DeleteLocalRef(cls);
    return v;
}
void request(float hz) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return;
    jclass cls = activity_class(env);
    if (!cls) return;
    if (jmethodID m = env->GetStaticMethodID(cls, "requestRefreshRate", "(F)V")) env->CallStaticVoidMethod(cls, m, (jfloat)hz);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
}
#endif
}  // namespace

void poll(SDL_Window* tv) {
#ifdef __ANDROID__
    (void)tv;
    // the rate interpolation wants: the chosen 120/240 fps, capped to what the display offers
    static int requested = -1, maxHz = 0;
    if (!maxHz) maxHz = (int)std::lround(call_float("maxRefreshRate"));
    const int want = interp::mode() == 1 && interp::fps() > 60 && maxHz > 60 ? std::min(interp::fps(), maxHz) : 0;
    if (want != requested) {
        requested = want;
        request((float)want);
        if (want) LOG("[display] %d fps frame interpolation: asking for a %d Hz display mode (the display offers up to %d Hz)", interp::fps(), want, maxHz);
    }
    interp::set_display_hz((int)std::lround(call_float("displayRefreshRate")));
#else
    const SDL_DisplayID id = tv ? SDL_GetDisplayForWindow(tv) : SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* m = id ? SDL_GetCurrentDisplayMode(id) : nullptr;
    interp::set_display_hz(m ? (int)std::lround(m->refresh_rate) : 0);
#endif
}
}  // namespace display_rate
