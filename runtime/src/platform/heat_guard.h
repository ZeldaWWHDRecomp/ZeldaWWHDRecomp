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
