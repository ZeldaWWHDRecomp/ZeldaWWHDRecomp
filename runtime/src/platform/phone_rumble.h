// Android phone fallback policy. Hardware output lives in WwhdActivity/PhoneRumble.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
namespace phone_rumble {
// Controls > Rumble on (saved as "phoneRumble", the index):
//   kOff       the controller's motors only (this device never vibrates)
//   kTouch     Automatic: this device while you play by touch, the controller's motors otherwise
//   kFallback  Automatic, and this device also for a controller without rumble motors
//   kDevice    this device only (the controller's motors stay still)
//   kBoth      both at once
// (kDevice, kBoth: P6 of the controls plan, idea from the GreenNaugahyde Android fork)
enum Mode { kOff, kTouch, kFallback, kDevice, kBoth, kModeCount };
inline Mode parse(const char* v) {
    if (!v || !std::strcmp(v, "touch") || !std::strcmp(v, "1")) return kTouch;
    if (!std::strcmp(v, "fallback") || !std::strcmp(v, "2")) return kFallback;
    if (!std::strcmp(v, "device") || !std::strcmp(v, "3")) return kDevice;
    if (!std::strcmp(v, "both") || !std::strcmp(v, "4")) return kBoth;
    return kOff;  // invalid overrides fail closed
}
inline const char* label(Mode m) {
    switch (m) {
    case kOff: return "Controller";
    case kTouch: return "Automatic";
    case kFallback: return "Automatic, this device for a controller without rumble";
    case kDevice: return "This device";
    case kBoth: return "Both";
    default: return "";
    }
}
// whether the controllers' motors run (the game's rumble still decides how)
inline bool controller_allowed(Mode mode) { return mode != kDevice; }
inline bool touch_selected(bool controlsVisible, bool lastTouch) {
    return controlsVisible && lastTouch;
}
inline bool allowed(Mode mode, bool touchActive, bool hasControllerRumble) {
    if (mode == kDevice || mode == kBoth) return true;
    return mode != kOff && (touchActive || (mode == kFallback && !hasControllerRumble));
}
inline std::atomic<Mode> choice{parse(std::getenv("WWHD_PHONE_RUMBLE"))};
inline std::atomic<bool> last_input_touch{false};
inline bool env_override() { return std::getenv("WWHD_PHONE_RUMBLE") != nullptr; }
// Bound JNI/service work for sustained rumble, without delaying an explicit stop. Every
// nonzero request expires on Android after 150ms even if the native main loop stops running.
class Refresh {
    uint16_t last_ = 0;
    uint64_t time_ = 0;
public:
    bool send(uint16_t level, uint64_t now) {
        if (level == last_ && (!level || now - time_ < 100)) return false;
        last_ = level; time_ = now; return true;
    }
};
}
