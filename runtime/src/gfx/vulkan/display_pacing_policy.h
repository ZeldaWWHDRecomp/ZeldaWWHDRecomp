// Display pacing (display_timing.h, WWHD_VK_DISPLAY_PACING): the on-screen time asked for each TV frame. Pure logic,
// no Vulkan: the unit test and the phone-trace replay (r5-pacing tools/pacing_replay.py) run the same rule.
//
// Measured on a Xiaomi 14 (120 Hz, record worker + 8 submission slots, even pacing on): the present calls are even
// (16.5-16.7 ms apart) but a frame reaches the screen 12-23 ms after its call (GPU work left, the compositor's latch),
// so whenever two frames straddle a refresh differently one stays 3 refreshes and the next 1 (the "3+1" pairs: 220
// a minute at the cliff). With VK_GOOGLE_display_timing the present asks for a time on the display's refresh grid,
// a frame interval after the previous frame's: a frame ready early waits in the compositor, so the frames land on
// every second refresh at 60 fps. The asked time is at least the present call plus the recent call-to-screen latency
// (quantile Q of the last 128 frames): a frame that cannot make its slot (a late call) gets the next refresh it can
// make, and the chain goes on from there.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gfxvk::display_pacing {
struct Policy {
    double quantile = 0.5;   // of the unpaced call-to-screen latency: the first latency target
    bool adapt = false;      // (WWHD_VK_DISPLAY_PACING bit 4) lower the target after a window without misses
    int64_t period = 0;      // refresh duration, ns (0: unknown, no pacing)
    int64_t grid = 0;        // a time on the refresh grid (an actual present time), ns
    int64_t last = 0;        // the previous frame's asked slot (a refresh time), 0: none
    int64_t target = 0;      // call-to-slot latency the asked slot keeps at least (0: not measured yet)
    std::array<int64_t, 128> latency{};
    int count = 0, next = 0;
    int windowFrames = 0, windowMisses = 0;
    uint64_t paced = 0, rebased = 0, unpaced = 0, misses = 0, raised = 0, lowered = 0;
    // Bounded latency (each 0 = off: the rule above, unchanged). Measured on the phone (land-cliff): without them the
    // target only ever goes up, half a refresh per miss, so a session's latency is the first target plus 4.2 ms for
    // every miss so far (19.8 -> 32.2 ms within one 75 s bench run, unbounded over a session).
    int64_t cap = 0;    // (WWHD_VK_DISPLAY_PACING_CAP, ms) a miss raises the target to at most base + CAP
    int decay = 0;      // (WWHD_VK_DISPLAY_PACING_DECAY, frames) after DECAY paced frames without a miss the target
                        // goes a quarter refresh back down, never below base (bit 4 instead goes down to one refresh,
                        // below what the frames need, and swings: lower -> misses -> raise)
    int64_t keep = 0;   // (WWHD_VK_DISPLAY_PACING_KEEP, ms) a chain slot is kept while it is at least the call +
                        // target - KEEP: a present call a few ms late keeps its slot instead of moving the chain a
                        // refresh later (the "screen latency up" 3-refresh frames)
    bool track = false; // (WWHD_VK_DISPLAY_PACING_GRID=1) each chain slot is snapped to the nearest refresh of the
                        // latest on-screen time: the driver's refresh duration (8.300-8.313 ms on the phone) is shorter
                        // than the screen's real one (8.333 ms over a run), so an untracked chain asks 0.05 ms earlier
                        // every frame, drifts off the present calls and is rebased a refresh later every ~120 frames
    int64_t base = 0;   // the first target (the measured latency quantile)
    int clean = 0;      // paced frames since the last miss (decay)
    uint64_t capped = 0, decayed = 0, kept = 0;
    static constexpr int kWindow = 120;
    void set_period(int64_t ns) { if (ns > 0) period = ns; }
    // a finished presentation: CALL = its present call, EARLIEST = the earliest it could have been shown,
    // ACTUAL = when it was shown, DESIRED = what it asked for (0: nothing; all ns, steady / CLOCK_MONOTONIC).
    // Only frames that asked for nothing measure the latency: with a desired time the reported earliest time is
    // the refresh before the asked one (measured: actual - earliest = one refresh in every paced frame).
    void feedback(int64_t call, int64_t earliest, int64_t actual, int64_t desired = 0) {
        if (actual > 0) grid = actual;
        if (!desired) {
            if (call > 0 && earliest > call) {
                latency[size_t(next)] = earliest - call;
                next = (next + 1) % int(latency.size());
                count = std::min(count + 1, int(latency.size()));
            }
            return;
        }
        if (!period) return;
        const bool miss = actual > desired + period;  // (asked: desired + half a refresh)
        if (miss) {  // a frame that did not make its slot: keep half a refresh more from now on
            ++misses; ++windowMisses;
            if (target) {
                target += period / 2; ++raised;
                if (cap && target > base + cap) { target = base + cap; ++capped; }
            }
            clean = 0;
        } else if (decay && target > base && ++clean >= decay) {
            target = std::max(base, target - period / 4); ++decayed;
            clean = 0;
        }
        if (++windowFrames >= kWindow) {
            if (adapt && windowMisses == 0 && target > period) { target -= period / 2; ++lowered; }
            windowFrames = windowMisses = 0;
        }
    }
    int64_t latency_quantile() const {
        std::array<int64_t, 128> v = latency;
        const int n = count;
        const int k = std::clamp(int(quantile * n), 0, n - 1);
        std::nth_element(v.begin(), v.begin() + k, v.begin() + n);
        return v[size_t(k)];
    }
    int64_t snap_up(int64_t t) const {  // the first refresh at or after T
        int64_t m = (t - grid) % period;
        if (m < 0) m += period;
        return m ? t + (period - m) : t;
    }
    int64_t snap_near(int64_t t) const {  // the nearest refresh to T
        int64_t m = (t - grid) % period;
        if (m < 0) m += period;
        return m * 2 < period ? t - m : t + (period - m);
    }
    // the present time to ask for (desiredPresentTime, ns) for a frame presented NOW whose frame interval is
    // INTERVAL ns (0: not paced); 0: ask for nothing (as without pacing)
    int64_t desired(int64_t now, int64_t interval) {
        const double ratio = period > 0 ? double(interval) / double(period) : 0;
        const int64_t k = std::llround(ratio);
        if (!period || !grid || (!target && count < 16) || interval <= 0 || k < 1 || std::fabs(ratio - double(k)) > 0.1) {
            last = 0; ++unpaced;
            return 0;
        }
        if (!target) base = target = latency_quantile();
        const int64_t step = k * period, earliest = now + target;
        int64_t slot = last ? (track ? snap_near(last + step) : last + step) : snap_up(earliest);
        if (keep && last && slot < earliest && slot >= earliest - keep) ++kept;
        else if (slot < earliest || slot > earliest + 2 * step) {  // late call (or a stale chain after a pause)
            if (last) ++rebased;
            slot = snap_up(earliest);
        }
        last = slot; ++paced;
        // half a refresh before the slot: the compositor shows it at that refresh, not the one before
        return slot - period / 2;
    }
};
}  // namespace gfxvk::display_pacing
