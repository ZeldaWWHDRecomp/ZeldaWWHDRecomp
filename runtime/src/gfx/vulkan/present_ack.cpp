// Late acknowledgment of the TV present submission: present_ack.h
#include "present_ack.h"
#ifdef WWHD_VK_SMOOTH
#include "runtime.h"
#include "smooth_defaults.h"
#include <cstdlib>
#include <cstring>

namespace gfxvk::present_ack {
namespace {
bool armed = false;  // render thread
void* deferredSlot = nullptr;
uint64_t deferredSerial = 0;
uint64_t deferrals = 0, settles = 0;
}
bool enabled() {
    static const bool on = [] {
        const char* e = std::getenv("WWHD_VK_LATE_PRESENT_ACK");
        const bool value = e ? !std::strcmp(e, "1") : smooth_defaults::late_present_ack();  // (smooth_defaults.h)
        if (value) LOG("[vulkan late present ack] on (%s): the render thread waits for the record "
                       "worker's TV present submission only right before that frame's present", e ? "WWHD_VK_LATE_PRESENT_ACK" : "default");
        return value;
    }();
    return on;
}
void arm(bool on) { armed = on && enabled(); }
bool take_armed() { const bool was = armed; armed = false; return was; }
void defer(void* slot, uint64_t serial) {
    deferredSlot = slot; deferredSerial = serial; ++deferrals;
}
void settle(void (*resolve)(void* slot, uint64_t serial)) {
    if (!deferredSlot) return;
    void* slot = deferredSlot;
    deferredSlot = nullptr;  // (first: resolve may present nothing, but never twice)
    ++settles;
    resolve(slot, deferredSerial);
}
void report(double frames) {
    if (!enabled() || frames <= 0) return;
    LOG("[vulkan late present ack] TV present submissions acknowledged at their present: %.2f/frame (deferred %.2f/frame)",
        settles / frames, deferrals / frames);
    deferrals = settles = 0;
}
}  // namespace gfxvk::present_ack
#endif
