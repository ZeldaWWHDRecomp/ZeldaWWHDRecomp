// Per-frame breakdown rows in the stutter trace: frame_breakdown.h
#include "frame_breakdown.h"
#ifdef WWHD_VK_SMOOTH
#include "stutter_trace.h"
#include "runtime.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace gfxvk::frame_breakdown {
namespace {
struct Sum { uint64_t count = 0, first = 0, last = 0, total = 0; };
thread_local std::array<Sum, kParts> sums;  // (rows only from the render thread's frame_done)
constexpr const char* kNames[kParts] = {"fb_idle", "fb_swap", "fb_acquire", "fb_present", "fb_worker_wait", "fb_fence_wait"};
struct Hold { uint64_t frame = ~uint64_t{0}; int64_t ready = 0, due = 0; bool held = false; };
Hold pendingHold;  // render thread
}
bool enabled() {
    static const bool on = [] {
        const char* e = std::getenv("WWHD_VK_FRAME_BREAKDOWN");
        const bool requested = e && !std::strcmp(e, "1");
        const bool value = requested && stutter::trace().enabled();
        if (requested)
            LOG("[vulkan frame breakdown] %s: per-frame render-thread parts, even-pacing presents, game flips%s",
                value ? "on" : "off (needs WWHD_VK_STUTTER_TRACE)",
#ifdef __ANDROID__
                " and display times (VK_GOOGLE_display_timing) in the stutter trace"
#else
                " in the stutter trace"
#endif
            );
        return value;
    }();
    return on;
}
uint64_t now_ns() { return stutter::now(); }
void end(Part part, uint64_t started) {
    if (!started || part < 0 || part >= kParts) return;
    const uint64_t t = now_ns();
    Sum& s = sums[part];
    if (!s.count) s.first = started;
    ++s.count; s.last = t; s.total += t - started;
}
void frame_done(uint64_t frame) {
    if (!enabled()) return;
    for (int p = 0; p < kParts; ++p) {
        Sum& s = sums[p];
        if (s.count) stutter::trace().add(kNames[p], frame, s.count, s.first, s.last, 0, s.total);
        s = {};
    }
}
void hold_planned(uint64_t frame, int64_t ready, int64_t due, bool held) {
    if (enabled()) pendingHold = {frame, ready, due, held};
}
void hold_issued(uint64_t frame, int64_t at) {
    if (!enabled() || pendingHold.frame != frame) return;
    stutter::trace().add("fb_hold", frame, uint64_t(pendingHold.due), uint64_t(pendingHold.ready), uint64_t(at),
                         pendingHold.held ? 1 : 0);
    pendingHold = {};
}
void flip(uint64_t swap, int64_t swappedAt, int64_t dueAt, int64_t executedAt, bool inBetween) {
    if (enabled()) stutter::trace().add("fb_flip", swap, uint64_t(dueAt), uint64_t(swappedAt), uint64_t(executedAt), inBetween ? 1 : 0);
}
void game_wait(int site, int64_t start, int64_t end) {
    if (enabled()) stutter::trace().add("fb_gwait", 0, uint64_t(site), uint64_t(start), uint64_t(end), 0);
}
void game_vsync(int64_t start, int64_t end) {
    if (enabled()) stutter::trace().add("fb_gvsync", 0, 0, uint64_t(start), uint64_t(end), 0);
}
void game_pass(bool hold, int phase) {
    if (enabled()) { const uint64_t t = now_ns(); stutter::trace().add("fb_gpass", 0, uint64_t(phase), t, t, hold ? 1 : 0); }
}
void game_drop(int64_t elapsed, int64_t pass, int64_t budget) {
    if (enabled()) { const uint64_t t = now_ns(); stutter::trace().add("fb_gdrop", 0, uint64_t(elapsed), uint64_t(budget), t, 0, uint64_t(pass)); }
}
}  // namespace gfxvk::frame_breakdown
#endif
