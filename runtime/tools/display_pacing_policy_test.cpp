// Display pacing rule (gfx/vulkan/display_pacing_policy.h): the desired present time each TV frame asks for.
#include "gfx/vulkan/display_pacing_policy.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <utility>
#include <tuple>
#include <algorithm>
#include <vector>

using gfxvk::display_pacing::Policy;
static constexpr int64_t kMs = 1'000'000;
static constexpr int64_t kRefresh = 8'333'333;  // 120 Hz
static constexpr int64_t kFrame = 16'683'333;   // even pacing at 60 fps (two vsyncs of the 59.94 Hz grid)

static Policy measured(int64_t latency) {
    Policy p;
    p.set_period(kRefresh);
    for (int i = 0; i < 16; ++i) p.feedback(1000 * kMs + i * kFrame, 1000 * kMs + i * kFrame + latency, 1000 * kMs + i * kFrame + latency);
    return p;
}
int main() {
    {  // nothing is asked before the latency is known, or without a refresh period
        Policy p;
        assert(p.desired(10 * kMs, kFrame) == 0);
        p.set_period(kRefresh);
        p.feedback(1, 2, 3);
        assert(p.desired(10 * kMs, kFrame) == 0);  // (one sample of 16)
    }
    {  // 60 fps on a 120 Hz screen: slots two refreshes apart, each at least the call + the latency target
        Policy p = measured(15 * kMs);
        int64_t call = 2000 * kMs, prev = 0;
        for (int i = 0; i < 50; ++i, call += kFrame) {
            const int64_t d = p.desired(call, kFrame);
            assert(d);
            const int64_t slot = d + kRefresh / 2;
            assert(slot >= call + p.target);
            assert((slot - p.grid) % kRefresh == 0);
            if (prev) assert(slot - prev == 2 * kRefresh || slot - prev == 3 * kRefresh);  // (3: the 59.94 Hz drift)
            prev = slot;
        }
        assert(p.rebased <= 1);
    }
    {  // a late present call takes the next refresh it can make; the chain goes on from there
        Policy p = measured(15 * kMs);
        const int64_t call = 3000 * kMs;
        const int64_t a = p.desired(call, kFrame) + kRefresh / 2;
        const int64_t b = p.desired(call + kFrame + 12 * kMs, kFrame) + kRefresh / 2;  // 12 ms late
        assert(b >= call + kFrame + 12 * kMs + p.target && b > a + 2 * kRefresh && p.rebased == 1);
        const int64_t c = p.desired(call + 2 * kFrame + 12 * kMs, kFrame) + kRefresh / 2;
        assert(c - b == 2 * kRefresh);
    }
    {  // a frame interval that is no whole number of refreshes (90 Hz) is not paced
        Policy p = measured(15 * kMs);
        p.set_period(11'111'111);
        assert(p.desired(4000 * kMs, kFrame) == 0 && p.unpaced == 1);
    }
    {  // paced frames do not feed the latency estimate (their earliest time is the refresh before the asked one);
       // a missed slot raises the target by half a refresh; the adaptive bit lowers it after a clean window
        Policy p = measured(15 * kMs);
        const int64_t d = p.desired(5000 * kMs, kFrame);
        const int64_t target = p.target;
        p.feedback(5000 * kMs, d + kRefresh / 2 - kRefresh, d + kRefresh / 2, d);  // shown as asked
        assert(p.target == target && p.count == 16);
        p.feedback(5000 * kMs, d + kRefresh, d + kRefresh / 2 + kRefresh, d);  // a refresh late
        assert(p.target == target + kRefresh / 2 && p.misses == 1);
        Policy q = measured(15 * kMs);
        q.adapt = true;
        const int64_t e = q.desired(6000 * kMs, kFrame), t0 = q.target;
        for (int i = 0; i < Policy::kWindow; ++i) q.feedback(6000 * kMs, e - kRefresh / 2, e + kRefresh / 2, e);
        assert(q.target == t0 - kRefresh / 2 && q.lowered == 1);
        Policy r = measured(15 * kMs);
        const int64_t f = r.desired(6000 * kMs, kFrame), t1 = r.target;
        for (int i = 0; i < Policy::kWindow; ++i) r.feedback(6000 * kMs, f - kRefresh / 2, f + kRefresh / 2, f);
        assert(r.target == t1);  // (without the adaptive bit only higher)
    }
    {  // simulated: frames ready 11-19 ms after their (even) present calls; a compositor that shows a frame at its
       // asked refresh or when it is ready: unpaced, frames straddle refreshes (1 and 3 refresh intervals); paced at
       // the 90th percentile, every interval is two refreshes apart from the 59.94 Hz drift
        std::mt19937 rng(7);
        std::uniform_int_distribution<int64_t> ready(11 * kMs, 19 * kMs);
        Policy p;
        p.set_period(kRefresh);
        p.quantile = 0.9;
        int64_t grid = 0;
        auto snap = [&](int64_t t) { int64_t m = (t - grid) % kRefresh; return m ? t + kRefresh - m : t; };
        std::vector<int64_t> paced, unpaced;
        for (int i = 0; i < 2000; ++i) {
            const int64_t call = 10000 * kMs + i * kFrame, r = call + ready(rng);
            const int64_t d = p.desired(call, kFrame);
            const int64_t shown = d ? std::max(snap(r), d + kRefresh / 2) : snap(r);
            p.feedback(call, snap(r), shown, d);
            (d ? paced : unpaced).push_back(shown);
        }
        int uneven = 0;
        for (size_t i = 1; i < paced.size(); ++i) {
            const int64_t k = (paced[i] - paced[i - 1] + kRefresh / 2) / kRefresh;
            uneven += k != 2;
        }
        std::printf("display pacing simulation: %d of %zu paced intervals not two refreshes\n", uneven, paced.size() - 1);
        assert(uneven * 100 < int(paced.size()) * 2);  // (< 2 %: the drift and the misses above the quantile)
    }
    {  // bounded latency, cap: misses raise the target to at most base + CAP
        Policy p = measured(15 * kMs);
        p.cap = kRefresh / 2;
        const int64_t d = p.desired(7000 * kMs, kFrame), base = p.target;
        assert(p.base == base);
        for (int i = 0; i < 10; ++i) p.feedback(7000 * kMs, d + kRefresh, d + kRefresh / 2 + kRefresh, d);  // misses
        assert(p.target == base + kRefresh / 2 && p.raised == 10 && p.capped == 9);
        Policy u = measured(15 * kMs);  // (without the cap: 10 misses = 5 refreshes more, the unbounded climb)
        const int64_t e = u.desired(7000 * kMs, kFrame);
        for (int i = 0; i < 10; ++i) u.feedback(7000 * kMs, e + kRefresh, e + kRefresh / 2 + kRefresh, e);
        assert(u.target == u.base + 10 * (kRefresh / 2));
    }
    {  // bounded latency, decay: DECAY frames without a miss take a quarter refresh off, never below the base
        Policy p = measured(15 * kMs);
        p.decay = 10;
        const int64_t d = p.desired(8000 * kMs, kFrame), base = p.target;
        p.feedback(8000 * kMs, d + kRefresh, d + kRefresh / 2 + kRefresh, d);  // one miss: + half a refresh
        assert(p.target == base + kRefresh / 2);
        for (int i = 0; i < 9; ++i) p.feedback(8000 * kMs, d - kRefresh / 2, d + kRefresh / 2, d);
        assert(p.target == base + kRefresh / 2);
        p.feedback(8000 * kMs, d - kRefresh / 2, d + kRefresh / 2, d);
        assert(p.target == base + kRefresh / 2 - kRefresh / 4 && p.decayed == 1);
        for (int i = 0; i < 100; ++i) p.feedback(8000 * kMs, d - kRefresh / 2, d + kRefresh / 2, d);
        assert(p.target == base && p.decayed == 2);  // (the floor)
    }
    {  // bounded latency, keep: a call KEEP late keeps its slot; later than that it is rebased as before
        Policy p = measured(15 * kMs);
        p.keep = 3 * kMs;
        int64_t call = 9000 * kMs;
        const int64_t a = p.desired(call, kFrame) + kRefresh / 2;
        const int64_t room = a - (call + p.target);  // (the slot's slack over the target)
        call += kFrame + room + 2 * kMs;  // 2 ms past the slot's limit
        const int64_t b = p.desired(call, kFrame) + kRefresh / 2;
        assert(b == a + 2 * kRefresh && p.kept == 1 && p.rebased == 0);
        call += kFrame + 2 * kMs;  // and 4 ms (> KEEP) past it
        const int64_t c = p.desired(call, kFrame) + kRefresh / 2;
        assert(c > b + 2 * kRefresh && p.rebased == 1);
    }
    {  // bounded latency, grid tracking: a refresh duration 0.3 % short (the phone's driver: 8.31 vs 8.333 ms) and
       // a screen that shows a frame at the first real refresh at or after its ask: the untracked chain drifts off
       // the present calls and is rebased again and again; the tracked one stays on the screen's grid
        auto run = [](bool track) {
            Policy p = measured(15 * kMs);
            p.set_period(8'310'000);
            p.track = track;
            const int64_t real = kRefresh, g0 = 1000 * kMs;
            auto up = [&](int64_t t) { int64_t m = (t - g0) % real; if (m < 0) m += real; return m ? t + real - m : t; };
            int64_t prev = 0; int uneven = 0;
            for (int i = 0; i < 3000; ++i) {
                const int64_t call = 20000 * kMs + i * (2 * real), d = p.desired(call, kFrame);
                const int64_t shown = up(std::max(d, call + 12 * kMs));
                if (prev && shown - prev != 2 * real) ++uneven;
                prev = shown;
                p.feedback(call, shown - real, shown, d);
            }
            return std::make_pair(uneven, p.rebased);
        };
        const auto [u0, r0] = run(false);
        const auto [u1, r1] = run(true);
        std::printf("grid tracking: uneven %d -> %d, rebased %llu -> %llu of 3000\n", u0, u1, (unsigned long long)r0, (unsigned long long)r1);
        assert(r1 <= 1 && u1 <= u0 && r0 > 10);
    }
    {  // the Smooth frame pacing default (WWHD_VK_DISPLAY_PACING=11: bit 8 = GRID 1, CAP 4.2 ms, DECAY 300): a 40-minute
       // session at 60 fps with a slow frame (ready 25 ms after its call) every ~500 frames stays within base + CAP,
       // ends back at the base and keeps its frames two refreshes apart; without bit 8 the target climbs to what the
       // slowest frames need and stays there
        auto session = [](bool bounded) {
            std::mt19937 rng(11);
            std::uniform_int_distribution<int64_t> ready(11 * kMs, 16 * kMs);
            std::uniform_int_distribution<int> slow(0, 499);
            Policy p = measured(15 * kMs);
            if (bounded) { p.track = true; p.cap = int64_t(4.2 * kMs); p.decay = 300; }
            const int64_t g0 = 1000 * kMs;
            auto up = [&](int64_t t) { int64_t m = (t - g0) % kRefresh; if (m < 0) m += kRefresh; return m ? t + kRefresh - m : t; };
            int64_t maxTarget = 0, prev = 0; int uneven = 0, n = 0;
            for (int i = 0; i < 40 * 3600; ++i) {
                const int64_t call = 50000 * kMs + i * kFrame, d = p.desired(call, kFrame);
                const int64_t r = call + (slow(rng) ? ready(rng) : 25 * kMs);
                const int64_t shown = up(std::max(d, r));
                p.feedback(call, shown - kRefresh, shown, d);
                maxTarget = std::max(maxTarget, p.target);
                if (i > 3600) { ++n; uneven += prev && shown - prev != 2 * kRefresh; }
                prev = shown;
            }
            return std::make_tuple(maxTarget, p.target, p.base, 100.0 * uneven / n);
        };
        const auto [m1, t1, b1, u1] = session(true);
        const auto [m0, t0, b0, u0] = session(false);
        std::printf("40-minute session: bounded target max %.1f end %.1f (base %.1f), uneven %.2f%%; unbounded max %.1f ms, uneven %.2f%%\n",
                    m1 / 1e6, t1 / 1e6, b1 / 1e6, u1, m0 / 1e6, u0);
        assert(m1 <= b1 + int64_t(4.2 * kMs) && t1 <= b1 + kRefresh / 2 && u1 < 1.5);
        assert(m0 > b0 + kRefresh && t0 == m0);  // (up to what the slowest frames need, for good)
    }
    std::printf("display_pacing_policy_test: ok\n");
    return 0;
}
