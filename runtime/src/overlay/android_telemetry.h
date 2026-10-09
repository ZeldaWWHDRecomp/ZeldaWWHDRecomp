// Battery handling adapted from GreenNaugahyde's PerfStats.java, 9551a250 (MPL-2.0).
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace overlay {
struct AndroidTelemetry {
    double busy = -1;
    std::vector<std::pair<std::string, double>> temperatures;
    static bool number(const std::filesystem::path& path, double& value) {
        FILE* f = fopen(path.string().c_str(), "r");
        if (!f) return false;
        bool ok = fscanf(f, "%lf", &value) == 1 && std::isfinite(value);
        fclose(f); return ok;
    }
    void read(const std::filesystem::path& root = "/sys") {
        busy = -1; temperatures.clear();
        // kgsl reports busy and total time over the same sampling interval.
        auto kgsl = root / "class/kgsl/kgsl-3d0/gpubusy";
        if (FILE* f = fopen(kgsl.string().c_str(), "r")) {
            double active = 0, total = 0;
            if (fscanf(f, "%lf %lf", &active, &total) == 2 && total > 0 && active >= 0 && active <= total)
                busy = 100 * active / total;
            fclose(f);
        }
        // Some Mali/MediaTek kernels expose a percentage through devfreq. Restrict to GPU nodes.
        std::error_code ec;
        if (busy < 0) for (const auto& entry : std::filesystem::directory_iterator(root / "class/devfreq", ec)) {
            auto name = entry.path().filename().string();
            if (name.find("gpu") == std::string::npos && name.find("mali") == std::string::npos) continue;
            double value;
            if (number(entry.path() / "load", value) && value >= 0 && value <= 100) { busy = value; break; }
        }
        ec.clear();
        for (const auto& entry : std::filesystem::directory_iterator(root / "class/thermal", ec)) {
            if (!entry.path().filename().string().starts_with("thermal_zone")) continue;
            char type[80] = {};
            FILE* f = fopen((entry.path() / "type").string().c_str(), "r");
            if (!f) continue;
            bool ok = fscanf(f, "%79s", type) == 1; fclose(f);
            std::string name(type);
            if (!ok || (name.find("gpu") == std::string::npos && name.find("cpu") == std::string::npos &&
                        name.find("soc") == std::string::npos)) continue;
            double value;
            if (number(entry.path() / "temp", value) && value >= -40000 && value <= 200000)
                temperatures.emplace_back(name, value / 1000);
            if (temperatures.size() == 8) break;
        }
    }
};
}

namespace overlay {
// Shared by the overlay and copied report. Unreadable optional sysfs values stay omitted.
inline std::string format_android_telemetry(const AndroidTelemetry& telemetry,
                                          const std::string& public_readings) {
    std::string result = public_readings;
    char line[160];
    if (telemetry.busy >= 0) {
        snprintf(line, sizeof line, "\nGPU busy %.0f%%", telemetry.busy);
        result += line;
    }
    for (const auto& [name, value] : telemetry.temperatures) {
        snprintf(line, sizeof line, "\n%s %.1f C", name.c_str(), value);
        result += line;
    }
    return result;
}
}
#ifdef __ANDROID__
#include <SDL3/SDL.h>
#include <jni.h>
#include <chrono>
#include <string>

namespace overlay {
// Called by the render-thread-only cache below; public API failures do not hide legacy values.
inline std::string android_public_thermals() {
    std::string cached = "Thermal: n/a; battery: n/a";
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
// Render thread only. Overlay and report share one poll per second, only while needed.
inline std::string android_telemetry() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next{};
    static std::string cached = "Thermal: n/a; battery: n/a";
    const auto now = clock::now();
    if (now < next) return cached;
    next = now + std::chrono::seconds(1);
    AndroidTelemetry telemetry;
    telemetry.read(); // Existing devel kgsl/devfreq and gpu/cpu/soc paths, unchanged.
    cached = format_android_telemetry(telemetry, android_public_thermals());
    return cached;
}
}
#endif
