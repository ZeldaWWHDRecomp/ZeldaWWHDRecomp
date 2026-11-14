// VK_GOOGLE_display_timing observation and display pacing: display_timing.h
#include "display_timing.h"
#ifdef WWHD_NATIVE_DRAW
#include "display_pacing_policy.h"
#include "frame_breakdown.h"
#include "smooth_defaults.h"
#include "stutter_trace.h"
#include "runtime.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace gfxvk::display_timing {
namespace {
PFN_vkGetPastPresentationTimingGOOGLE pastTiming = nullptr;
PFN_vkGetRefreshCycleDurationGOOGLE refreshCycle = nullptr;
VkSwapchainKHR periodFor = VK_NULL_HANDLE;
std::mutex mutex;  // (presents: render thread; held presents too)
struct Issued { uint64_t frame; int64_t call, desired; };
std::unordered_map<uint32_t, Issued> issued;
uint32_t nextId = 0;
uint32_t interval = 0;
display_pacing::Policy policy;
int64_t lastActual = 0;
std::array<uint64_t, 6> refreshes{};  // on-screen intervals: 1, 2, 3, 4, 5+ refreshes, other
uint64_t shown = 0, missedAsk = 0, errors = 0;
}
int mode() {  // WWHD_VK_DISPLAY_PACING: bit 1 pacing, 2 one more swapchain image, 4 adaptive latency target,
              // 8 bounded latency (the GRID, CAP and DECAY defaults below)
    static const int value = [] {
        const char* e = std::getenv("WWHD_VK_DISPLAY_PACING");
        const int bits = e && *e ? std::atoi(e) : smooth_defaults::display_pacing();  // (smooth_defaults.h)
        return (bits & 1) ? bits & 15 : 0;
    }();
    return value;
}
// Bounded latency (display_pacing_policy.h): each part its own variable, bit 8 sets the measured defaults (phone
// closed-loop model over the land-cliff runs, r5-pacing/latfix): the chain on the screen's own refresh grid, a miss
// raises the target to at most half a refresh above the measured latency, 300 frames without a miss take a quarter
// refresh back off it. KEEP stays off by default (no gain in the model, more uneven frames when frames are slow).
static void bounded_latency() {
    const bool bundle = mode() & 8;
    auto num = [](const char* name, double fallback) {
        const char* e = std::getenv(name);
        return e && *e ? std::max(0.0, std::atof(e)) : fallback;
    };
    policy.track = num("WWHD_VK_DISPLAY_PACING_GRID", bundle ? 1 : 0) != 0;
    policy.cap = int64_t(num("WWHD_VK_DISPLAY_PACING_CAP", bundle ? 4.2 : 0) * 1e6);
    policy.decay = int(num("WWHD_VK_DISPLAY_PACING_DECAY", bundle ? 300 : 0));
    policy.keep = int64_t(num("WWHD_VK_DISPLAY_PACING_KEEP", 0) * 1e6);
    if (policy.track || policy.cap || policy.decay || policy.keep)
        LOG("[vulkan display pacing] bounded latency%s: refresh grid tracking %s, target cap %.1f ms above the measured "
            "latency, decay after %d frames without a miss, keep a slot %.1f ms (0: off)", bundle ? " (bit 8)" : "",
            policy.track ? "on" : "off", policy.cap / 1e6, policy.decay, policy.keep / 1e6);
}
bool pacing() {
    static const bool on = [] {
#ifdef __linux__  // (Android and Linux: VK_GOOGLE_display_timing where the driver has it)
        const bool value = mode() & 1;
#else
        const bool value = false;
#endif
        policy.adapt = mode() & 4;
        if (const char* q = std::getenv("WWHD_VK_DISPLAY_PACING_Q"); q && *q) policy.quantile = std::clamp(std::atof(q), 0.0, 1.0);
        if (value) bounded_latency();
        if (value) LOG("[vulkan display pacing] on (%s): TV frames ask for an on-screen time on the "
                       "refresh grid one frame interval apart (latency quantile %.2f; one more swapchain image %s, adaptive latency %s)",
                       std::getenv("WWHD_VK_DISPLAY_PACING") ? "WWHD_VK_DISPLAY_PACING" : "Smooth frame pacing", policy.quantile, (mode() & 2) ? "on" : "off", (mode() & 4) ? "on" : "off");
        return value;
    }();
    return on;
}
bool wanted() {
#ifdef __linux__
    return pacing() || frame_breakdown::enabled();
#else
    return false;
#endif
}
void device_created(VkDevice device, bool extensionEnabled) {
    if (!wanted()) return;
    if (extensionEnabled) {
        pastTiming = reinterpret_cast<PFN_vkGetPastPresentationTimingGOOGLE>(vkGetDeviceProcAddr(device, "vkGetPastPresentationTimingGOOGLE"));
        refreshCycle = reinterpret_cast<PFN_vkGetRefreshCycleDurationGOOGLE>(vkGetDeviceProcAddr(device, "vkGetRefreshCycleDurationGOOGLE"));
    }
    LOG("[vulkan frame breakdown] display times: %s%s", pastTiming ? "VK_GOOGLE_display_timing" :
        extensionEnabled ? "no vkGetPastPresentationTimingGOOGLE" : "VK_GOOGLE_display_timing unavailable",
        pacing() && !pastTiming ? " (display pacing off)" : "");
}
void set_interval(uint32_t ns) { interval = ns; }
uint32_t image_count(uint32_t count, uint32_t maxCount) {
    if (!pacing() || !pastTiming || !(mode() & 2)) return count;
    return maxCount ? std::min(count + 1, maxCount) : count + 1;
}
const void* chain_present(const void* pNext, PresentTimes& storage, uint64_t frame) {
    if (!pastTiming) return pNext;
    std::lock_guard lock(mutex);
    if (issued.size() >= 64) return pNext;  // (nothing comes back: stop asking)
    const int64_t call = int64_t(stutter::now());
    const int64_t desired = pacing() ? policy.desired(call, int64_t(interval)) : 0;
    storage.time.presentID = ++nextId;
    storage.time.desiredPresentTime = uint64_t(std::max<int64_t>(desired, 0));  // 0: as soon as possible
    storage.info.swapchainCount = 1;
    storage.info.pTimes = &storage.time;
    storage.info.pNext = pNext;
    issued[storage.time.presentID] = {frame, call, desired};
    return &storage.info;
}
void poll(VkDevice device, VkSwapchainKHR swapchain) {
    if (!pastTiming || !swapchain) return;
    if (refreshCycle && swapchain != periodFor) {
        VkRefreshCycleDurationGOOGLE cycle{};
        if (refreshCycle(device, swapchain, &cycle) == VK_SUCCESS && cycle.refreshDuration) {
            policy.set_period(int64_t(cycle.refreshDuration));
            LOG("[vulkan display timing] refresh %.3f ms", cycle.refreshDuration / 1e6);
        }
        periodFor = swapchain;
    }
    std::array<VkPastPresentationTimingGOOGLE, 16> past{};
    uint32_t n = uint32_t(past.size());
    const VkResult r = pastTiming(device, swapchain, &n, past.data());
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) {
        if (++errors == 1) LOG("[vulkan display timing] vkGetPastPresentationTimingGOOGLE: %d", int(r));
        return;
    }
    std::lock_guard lock(mutex);
    for (uint32_t i = 0; i < n; ++i) {
        const auto& p = past[i];
        auto it = issued.find(p.presentID);
        if (it == issued.end()) continue;
        const Issued is = it->second;
        issued.erase(it);
        policy.feedback(is.call, int64_t(p.earliestPresentTime), int64_t(p.actualPresentTime), is.desired);
        if (frame_breakdown::enabled())
            stutter::trace().add("fb_display", is.frame, p.earliestPresentTime, uint64_t(is.call), p.actualPresentTime,
                                 is.desired > 0 ? 1 : 0, p.presentMargin);
        if (is.desired > 0 && int64_t(p.actualPresentTime) > is.desired + policy.period) ++missedAsk;
        if (lastActual && policy.period && int64_t(p.actualPresentTime) > lastActual) {
            const int64_t k = std::llround(double(int64_t(p.actualPresentTime) - lastActual) / double(policy.period));
            ++refreshes[size_t(k >= 1 && k <= 4 ? k - 1 : k >= 5 ? 4 : 5)];
        }
        if (p.actualPresentTime) lastActual = int64_t(p.actualPresentTime);
        ++shown;
    }
    if (issued.size() >= 64) issued.clear();  // (a swapchain change lost them)
}
void report(double frames) {
    if (!pastTiming || frames <= 0) return;
    uint64_t total = 0;
    for (auto v : refreshes) total += v;
    if (!total) return;
    LOG("[vulkan display timing] on screen: %llu frames; intervals in refreshes 1: %.1f%%, 2: %.1f%%, 3: %.1f%%, 4: %.1f%%, "
        "5+: %.1f%%; display pacing %s (paced %llu, rebased %llu, unpaced %llu, shown after the asked refresh %llu; latency target "
        "%.1f ms, raised %llu, lowered %llu)",
        (unsigned long long)shown, 100.0 * refreshes[0] / total, 100.0 * refreshes[1] / total, 100.0 * refreshes[2] / total,
        100.0 * refreshes[3] / total, 100.0 * refreshes[4] / total, pacing() ? "on" : "off",
        (unsigned long long)policy.paced, (unsigned long long)policy.rebased, (unsigned long long)policy.unpaced,
        (unsigned long long)missedAsk, policy.target / 1e6, (unsigned long long)policy.raised, (unsigned long long)policy.lowered);
    if (policy.track || policy.cap || policy.decay || policy.keep)
        LOG("[vulkan display pacing] bounded latency: base %.1f ms, capped %llu, decayed %llu, kept %llu",
            policy.base / 1e6, (unsigned long long)policy.capped, (unsigned long long)policy.decayed, (unsigned long long)policy.kept);
    policy.capped = policy.decayed = policy.kept = 0;
    refreshes = {}; shown = missedAsk = 0; policy.paced = policy.rebased = policy.unpaced = policy.raised = policy.lowered = 0;
}
}  // namespace gfxvk::display_timing
#endif
