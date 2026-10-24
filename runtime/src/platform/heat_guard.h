// Android heat guard: when the device forecasts it is close to its thermal limit, 60 fps (frame
// interpolation) pauses and the game keeps a steady 30 fps until it cools down (platform/perf_hint.cpp).
// The limits depend on the kind of device (HeatGuardDevice.java tells native code which one) and on
// the player's Graphics > "Heat guard" setting. Pure policy here, so runtime/tools/heat_guard_test.cpp
// covers it.
#pragma once
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace heat_guard {
// The values HeatGuardDevice.java passes (keep both in step).
enum DeviceClass { kPhone = 0, kFanHandheld = 1 };
// The player's choice, saved as "heatGuard" (normal | relaxed | off).
enum Setting { kNormal = 0, kRelaxed = 1, kOff = 2 };

struct Limits {
    bool active;   // false: never pauses 60 fps for heat
    float pause;   // headroom at or above which 60 fps pauses
    float resume;  // headroom below which it resumes
};

// Phones and unknown devices: 0.72 / 0.60. Tuned on fanless phones (a Galaxy S25 Ultra already
// halved its clocks at 0.76). Actively cooled handhelds: 1.00 / 0.90. An AYANEO Pocket S2 Pro with its
// fan forecast 0.73 within a minute; an AYANEO EVO and a KONKR Pocket FIT Elite played at 0.90-0.96
// (the EVO up to 1.13) at about 70 C with 60 fps running well (#247). Relaxed is one step above the
// device's normal limits: 0.90 / 0.80 on phones, 1.10 / 1.00 on handhelds.
inline Limits limits(DeviceClass device, Setting setting) {
    if (setting == kOff) return {false, 0.0f, 0.0f};
    if (device == kFanHandheld) return setting == kRelaxed ? Limits{true, 1.10f, 1.00f} : Limits{true, 1.00f, 0.90f};
    if (setting == kRelaxed) return {true, 0.90f, 0.80f};
    return {true, 0.72f, 0.60f};
}

// One step of the hysteresis: whether 60 fps is paused after this headroom reading. A missing reading
// (NaN: no thermal service) keeps the state; an inactive guard never pauses.
inline bool next_paused(bool paused, float headroom, Limits l) {
    if (!l.active) return false;
    if (std::isnan(headroom)) return paused;
    if (!paused) return headroom >= l.pause;
    return !(headroom < l.resume);
}

// Temperature check for fan-cooled handhelds (#254). An AYANEO Pocket EVO forecasts headroom 1.09-1.38
// during normal 60 fps play (and Android reports SEVERE right after boot) at a 30-31 C battery, with its
// CPU sensors at 52-77 C, GPU sensors at 45-62 C and its fan far from full speed: its headroom does not
// mean heat. So on a fan handheld the headroom pause also needs a real temperature: the battery at
// 40 C or more (the setup's battery limits start at 41-42 C) or the hottest CPU/GPU sensor at 90 C or
// more (Snapdragon cores sit in the 70s under gaming load and throttle only near their 95-105 C junction
// limits). While paused, 60 fps resumes early once both readings are clearly cool (battery below 38 C
// and CPU/GPU below 80 C), subject to the sustained-cooling timer below. Phones keep the headroom
// rule (tuned on fanless phones, which throttle by skin temperature before their cores get that hot),
// and missing temperatures cannot confirm a new handheld pause.
struct Temperatures {
    float battery = NAN;       // C, NaN when unreadable
    float hottest_core = NAN;  // hottest CPU/GPU thermal zone, C, NaN when unreadable
};
constexpr float kHotBattery = 40.0f, kHotCore = 90.0f;    // confirm a pause
constexpr float kCoolBattery = 38.0f, kCoolCore = 80.0f;  // resume despite a high headroom
inline bool known(Temperatures t) { return !std::isnan(t.battery) || !std::isnan(t.hottest_core); }
inline bool hot(Temperatures t) { return t.battery >= kHotBattery || t.hottest_core >= kHotCore; }
// every readable value below its cool limit (and at least one readable)
inline bool cool(Temperatures t) {
    return known(t) && !(t.battery >= kCoolBattery) && !(t.hottest_core >= kCoolCore);
}
// Whether this device class checks the temperatures (fan-cooled handhelds only).
inline bool checks_temperatures(DeviceClass device) { return device == kFanHandheld; }

// next_paused with the temperature check: on a fan handheld with readable temperatures a pause needs
// hot(), and a pause can end once cool(). Elsewhere it is the headroom rule above.
inline bool next_paused(bool paused, float headroom, Limits l, DeviceClass device, Temperatures t) {
    const bool next = next_paused(paused, headroom, l);
    if (!next || !checks_temperatures(device)) return next;
    if (!known(t)) return paused;
    if (!paused) return hot(t);
    return !cool(t);
}

// Release only after five seconds of consecutive cool samples. Calls use monotonic milliseconds;
// missing headroom or a sampling gap cannot count as evidence of sustained cooling. Off is immediate.
constexpr int64_t kClearMillis = 5000;
struct State {
    bool paused = false;
    int64_t cool_since = -1, last_sample = -1;
    bool update(float headroom, Limits l, DeviceClass device, Temperatures t, int64_t now) {
        if (!l.active) { paused = false; cool_since = -1; last_sample = now; return false; }
        const bool next = next_paused(paused, headroom, l, device, t);
        if (!paused) {
            paused = next;
            cool_since = -1;
        } else {
            const bool clear = !std::isnan(headroom) && !next;
            if (!clear || now < last_sample || (last_sample >= 0 && now - last_sample > 2000))
                cool_since = -1;
            if (clear) {
                if (cool_since < 0) cool_since = now;
                if (now - cool_since >= kClearMillis) { paused = false; cool_since = -1; }
            }
        }
        last_sample = now;
        return paused;
    }
};

// "normal" / "relaxed" / "off" (also 0 / 1 / 2); anything else is Normal, the safe default.
inline Setting parse(const char* v) {
    if (!v) return kNormal;
    if (!std::strcmp(v, "relaxed") || !std::strcmp(v, "1")) return kRelaxed;
    if (!std::strcmp(v, "off") || !std::strcmp(v, "2")) return kOff;
    return kNormal;
}
inline const char* name(Setting s) { return s == kRelaxed ? "relaxed" : s == kOff ? "off" : "normal"; }
inline const char* device_name(DeviceClass d) { return d == kFanHandheld ? "handheld (fan)" : "phone"; }

inline DeviceClass to_device(int v) { return v == kFanHandheld ? kFanHandheld : kPhone; }
inline Setting to_setting(int v) { return v == kRelaxed ? kRelaxed : v == kOff ? kOff : kNormal; }

// Shared state: the device class from Java, the setting (WWHD_HEAT_GUARD wins over the saved one).
inline std::atomic<int> device{kPhone};
inline std::atomic<int> choice{parse(std::getenv("WWHD_HEAT_GUARD"))};
inline bool env_override() { return std::getenv("WWHD_HEAT_GUARD") != nullptr; }
}  // namespace heat_guard
