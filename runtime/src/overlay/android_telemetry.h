// Battery handling adapted from GreenNaugahyde's PerfStats.java, 9551a250 (MPL-2.0).
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <jni.h>
#include <chrono>
#include <string>

namespace overlay {
// Render thread only. At most one JNI/public-API poll per second, only for the overlay/report.
inline std::string android_telemetry() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next{};
    static std::string cached = "Thermal: n/a; battery: n/a";
    const auto now = clock::now();
    if (now < next) return cached;
    next = now + std::chrono::seconds(1);
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return cached;
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!activity) return cached;
    jclass cls = env->GetObjectClass(activity);
    env->DeleteLocalRef(activity);
    if (!cls) { if (env->ExceptionCheck()) env->ExceptionClear(); return cached; }
    jmethodID method = env->GetStaticMethodID(cls, "performanceThermals", "()Ljava/lang/String;");
    jstring result = method ? (jstring)env->CallStaticObjectMethod(cls, method) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); result = nullptr; }
    if (result) {
        const char* chars = env->GetStringUTFChars(result, nullptr);
        if (chars) { cached = chars; env->ReleaseStringUTFChars(result, chars); }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(result);
    }
    env->DeleteLocalRef(cls);
    return cached;
}
}
#endif
