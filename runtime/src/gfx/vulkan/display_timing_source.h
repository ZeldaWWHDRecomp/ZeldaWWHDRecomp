// Whether VK_GOOGLE_display_timing can drive display pacing on this swapchain (display_timing.h), and the safe way to
// read its past presentation times. Pure logic, no Vulkan: display_timing_source_test.cpp runs the same rules.
//
// In Steam's Gaming Mode the extension does not come from the driver but from gamescope's WSI layer
// (VK_LAYER_FROG_gamescope_wsi, which adds it to every device). Two facts about that layer (gamescope source,
// layer/VkLayer_FROG_gamescope_wsi.cpp):
// - up to gamescope 3.16.31 its vkGetPastPresentationTimingGOOGLE erased as many stored timings as the CALLER's array
//   holds, not as many as it returned ("timings.erase(begin, begin + originalCount)"; fixed in 3.17.0 by commit
//   9a25be6, 2026-10-10). Asking for 16 while fewer were stored moved the layer's std::vector end before its start:
//   every later call moved it 16 entries further back and every presentation the compositor reported was written
//   there, so the layer wrote into the heap below its buffer, frame after frame (issue #178: the Steam Deck crashed at
//   start in Gaming Mode, VK_ERROR_DEVICE_LOST, since display pacing became the Deck's default). fetch() first asks
//   for the count (0 in, so even the old layer erases nothing) and then for at most that many: the old layer then
//   erases exactly what it returned;
// - its vkGetRefreshCycleDurationGOOGLE answers 16.667 ms until the compositor has sent the swapchain its refresh
//   cycle (an event read on a later call), so a refresh duration read right after the swapchain was made is not the
//   screen's (the OLED Deck runs at 90 Hz). Source::refresh() trusts a duration only when it matches the display's
//   refresh rate (SDL) or, with no rate known, when it is not that placeholder; until then nothing is paced.
// Source also stops asking for present times when the timings never come back or come back implausible: pacing then
// stays with the CPU-side even pacing (interp.cpp, held presents in backend.cpp), the Deck's pacing without the
// extension. On Android nothing here applies (checks off: the drivers' own extension, as measured).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gfxvk::display_timing_source {
// Past presentation timings into DATA (CAPACITY entries): first the count (0 in), then at most that many. QUERY is
// called as query(uint32_t* count, T* data) -> R (data null: the count). N: the entries written. Returns the second
// call's result, or the count call's when it failed or nothing is stored (then N is 0).
template <class R, class T, class Query>
R fetch(Query&& query, T* data, uint32_t capacity, uint32_t& n, R success) {
    n = 0;
    uint32_t available = 0;
    const R counted = query(&available, static_cast<T*>(nullptr));
    if (counted != success || !available) return counted;
    n = std::min(available, capacity);
    return query(&n, data);
}

enum class State { Probing, Usable, Unusable };

struct Source {
    static constexpr int64_t kLayerPlaceholder = 16'666'666;  // gamescope's layer before the refresh cycle arrives
    static constexpr int64_t kMinRefresh = 2'000'000, kMaxRefresh = 50'000'000;  // 500 Hz .. 20 Hz
    static constexpr uint64_t kProbe = 300;     // presents with a present time before the timings must have come back
    static constexpr uint64_t kConfirm = 16;    // matched plausible timings that confirm the source
    static constexpr uint64_t kImplausible = 8; // implausible timings that rule it out while probing
    static constexpr uint64_t kSilent = 600;    // presents without a timing after which a usable source is dropped
    static constexpr int64_t kLate = 1'000'000'000;  // a present shown more than 1 s after its call: implausible

    bool checks = true;      // false (Android): always usable, every refresh duration used, as before
    bool gamescope = false;  // the extension comes from gamescope's WSI layer (Gaming Mode)
    State state = State::Probing;
    const char* reason = "";
    int64_t refreshNs = 0;   // the last refresh duration read
    bool refreshTrusted = false;
    uint64_t asked = 0, matched = 0, implausible = 0, silent = 0;

    bool chain() const { return state != State::Unusable; }  // ask for present times (and read them back)
    // a refresh duration read (NS) with the display's refresh rate HZ (0: unknown): true when the period to pace on
    // changed (period() then gives it; 0: do not pace)
    bool refresh(int64_t ns, int hz) {
        const bool before = refreshTrusted;
        const int64_t old = refreshNs;
        refreshNs = ns;
        refreshTrusted = trusted(ns, hz);
        return before != refreshTrusted || (refreshTrusted && old != ns);
    }
    int64_t period() const { return refreshTrusted ? refreshNs : 0; }
    bool trusted(int64_t ns, int hz) const {
        if (!checks) return ns > 0;
        if (ns < kMinRefresh || ns > kMaxRefresh) return false;
        if (hz > 0) {
            const double screen = 1e9 / double(ns);
            return std::fabs(screen - hz) <= std::max(2.0, 0.03 * hz);
        }
        return !gamescope || ns != kLayerPlaceholder;
    }
    // a present that asked for a present time (chain_present); true when the state changed
    bool asked_one() {
        ++asked; ++silent;
        if (!checks) return false;
        if (state == State::Probing && asked >= kProbe && !matched)
            return rule_out("no past presentation timings came back");
        if (state == State::Usable && silent >= kSilent) return rule_out("past presentation timings stopped coming back");
        return false;
    }
    // a past timing of one of our presents: CALL its present call, ACTUAL when it was shown (0: not reported), all ns
    // on the same clock; true when the state changed
    bool timing(int64_t call, int64_t actual) {
        silent = 0;
        if (!checks) { ++matched; return false; }
        if (actual <= 0 || actual + kMaxRefresh < call || actual > call + kLate) {
            ++implausible;
            if (state == State::Probing && implausible >= kImplausible) return rule_out("implausible present times");
            return false;
        }
        ++matched;
        if (state == State::Probing && matched >= kConfirm) { state = State::Usable; return true; }
        return false;
    }
    bool rule_out(const char* why) {
        state = State::Unusable;
        reason = why;
        return true;
    }
};
}  // namespace gfxvk::display_timing_source
