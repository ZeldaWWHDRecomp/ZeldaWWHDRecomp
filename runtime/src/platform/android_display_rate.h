// Android surface cadence policy and request deduplication, independent of SDL/JNI for testing.
#pragma once
#include <cstdint>

namespace android_display_rate {
inline int requested_hz(int mode, int interpolationFps, bool disabled) {
    if (disabled) return 0; // zero clears a previous request and lets Android choose
    // True 60 and original 30 both fit a 60 Hz panel. Use the chosen interpolation rate rather
    // than output_fps(), which is capped to the current display and cannot raise its own cap.
    // 40 fps asks for 120 Hz, a multiple of 40 (every frame 3 refreshes): a Xiaomi 14 asked for 40 Hz
    // (and given its 40 Hz or 120 Hz mode as the preferred one) stayed at 60 Hz, where 40 fps is uneven;
    // 120 Hz requests are granted there. A panel without 120 Hz gets what it has (the settings then
    // say so when that is not a multiple of 40).
    if (mode == 1 && interpolationFps == 40) return 120;
    return mode == 1 && interpolationFps > 60 ? interpolationFps : 60;
}
// The display's rate shows the requested one: at least it above 60 Hz; a whole multiple of it below
// (40 fps: 40, 80, 120 Hz; 60 Hz is not)
inline bool reached(int actualHz, int requestedHz) {
    if (requestedHz > 60) return actualHz >= requestedHz - 1;
    const int r = actualHz % requestedHz;
    return actualHz >= requestedHz - 1 && (r <= 1 || r >= requestedHz - 1);
}
class RequestState {
    uintptr_t window_ = 0;
    unsigned revision_ = 0;
    int hz_ = -1;
public:
    bool changed(uintptr_t window, unsigned revision, int hz) {
        if (!window) {
            window_ = 0; // even if Android reuses the address, a later surface must get its request
            return false;
        }
        if (window == window_ && revision == revision_ && hz == hz_) return false;
        window_ = window;
        revision_ = revision;
        hz_ = hz;
        return true;
    }
};
// After a request the system may keep a lower rate (vendor per-app policies). If the read-back
// rate stays below the request for 3 s, the request is sent again, at most twice per request.
class Resend {
    int64_t sentMs_ = -1;
    int hz_ = 0;
    int resends_ = 0;
public:
    static constexpr int64_t kDelayMs = 3000;
    static constexpr int kMaxResends = 2;
    void sent(int64_t nowMs, int hz) { sentMs_ = nowMs; hz_ = hz; resends_ = 0; }
    bool due(int64_t nowMs, int actualHz) {
        if (sentMs_ < 0 || hz_ <= 0 || hz_ == 60 || actualHz <= 0 || reached(actualHz, hz_)) return false;
        if (resends_ >= kMaxResends || nowMs - sentMs_ < kDelayMs) return false;
        ++resends_;
        sentMs_ = nowMs;
        return true;
    }
};
} // namespace android_display_rate
