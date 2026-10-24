// SDL host (Windows, Linux, Android): the TV window's display refresh rate for frame interpolation
// (interp::output_fps caps 120/240 fps to it). Polled by the SDL main loop (gfx/vulkan/backend.cpp).
// The AppKit host reports it in gfx/display.mm.
//
// Android requests a cadence matching the chosen frame-rate setting (at least 60 Hz), once per
// setting or native surface change. The system may choose another mode. Actual refresh is read
// back for interpolation's cap, never used as the request (which would trap 120 fps at 60 Hz).
#include "display_rate.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "../interp.h"
#include "../runtime.h"

#ifdef __ANDROID__
#include <jni.h>
#include <android/native_window.h>
#include <atomic>
#include <cstdlib>
#include "android_display_rate.h"
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
void call_void_int(const char* name, int value) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return;
    jclass cls = activity_class(env);
    if (!cls) return;
    if (jmethodID m = env->GetStaticMethodID(cls, name, "(I)V")) env->CallStaticVoidMethod(cls, m, (jint)value);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(cls);
}
std::atomic<unsigned> surfaceRevision{0};
void request(SDL_Window* tv, int actualHz) {
    static const bool disabled = [] {
        const char* value = std::getenv("WWHD_NO_60HZ");
        return value && std::atoi(value) != 0;
    }();
    static android_display_rate::RequestState state;
    static android_display_rate::Resend resend;
    auto* window = tv ? static_cast<ANativeWindow*>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(tv), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr)) : nullptr;
    const int hz = android_display_rate::requested_hz(interp::mode(), interp::fps(), disabled);
    if (window) interp::set_requested_display_hz(hz);  // for the performance report
    const int64_t now = (int64_t)SDL_GetTicks();
    const bool again = window && resend.due(now, actualHz);
    if (!state.changed(reinterpret_cast<uintptr_t>(window), surfaceRevision.load(), hz) && !again) return;
    // the player's explicit choice: game content at exactly this rate, switching modes if needed
    // (the default, seamless-only strategy let the system keep 60 Hz with 120 fps chosen)
    const int result = ANativeWindow_setFrameRateWithChangeStrategy(window, static_cast<float>(hz),
        ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE, ANATIVEWINDOW_CHANGE_FRAME_RATE_ALWAYS);
    if (!again) resend.sent(now, hz);
    // the activity's preferred display mode follows the chosen rate (not each surface): above 60 Hz
    // a mode of the current resolution with at least that rate, else the system's choice
    static int preferredHz = -1;
    if (window && hz != preferredHz) {
        preferredHz = hz;
        call_void_int("preferDisplayMode", hz);
    }
    if (result == 0)
        LOG("[display] requested %d Hz for Android surface%s%s", hz, disabled ? " (system choice)" : "",
            again ? " again (the display stayed at a lower rate)" : "");
    else LOG("[display] Android frame-rate request %d Hz failed (%d); system choice retained", hz, result);
}
#endif
}  // namespace

void surface_changed() {
#ifdef __ANDROID__
    ++surfaceRevision;
#endif
}

void poll(SDL_Window* tv) {
#ifdef __ANDROID__
    static int actualHz = 0;  // the previous poll's read-back (0: not known yet)
    request(tv, actualHz);
    actualHz = (int)std::lround(call_float("displayRefreshRate"));
    interp::set_display_hz(actualHz);
#else
    const SDL_DisplayID id = tv ? SDL_GetDisplayForWindow(tv) : SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* m = id ? SDL_GetCurrentDisplayMode(id) : nullptr;
    interp::set_display_hz(m ? (int)std::lround(m->refresh_rate) : 0);
#endif
}
}  // namespace display_rate
