// Frame interpolation math (runtime/src/interp_pacing.h): blend fractions at 60/120/240 fps, the
// blends at arbitrary fractions, the display cap, and paced scheduling (in-between frames per step,
// dropped and skipped passes) in a simulation of the pass loop on the virtual vsync, and the paced
// steps at 120/240 fps on the virtual vsync that flips late frames when ready (step slots, issue #164).
#include "../src/interp_pacing.h"
#include "../src/gx2/vsync_pacing.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

using namespace interp::pacing;

static int g_failed = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                            \
        }                                                                          \
    } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(std::fabs(double(a) - double(b)) <= (eps))

static void test_fractions() {
    // 60 fps: logic pass halfway, the hold pass is the record pass
    CHECK(pass_fraction(0, 1, false) == 0.5f);
    CHECK(pass_fraction(1, 1, false) == 1.0f);
    // 120 fps: 1/4, 2/4, 3/4, 1
    for (int k = 0; k <= 3; k++) CHECK(pass_fraction(k, 3, false) == float(k + 1) / 4.0f);
    // 240 fps: k/8
    for (int k = 0; k <= 7; k++) CHECK(pass_fraction(k, 7, false) == float(k + 1) / 8.0f);
    // an exact step: drawn at the new step, its record pass too
    CHECK(pass_fraction(0, 1, true) == 1.0f);
    CHECK(pass_fraction(1, 1, true) == 1.0f);
    // a step planned with fewer in-between frames spaces them evenly (n = 2: 1/3, 2/3, 1)
    CHECK_NEAR(pass_fraction(0, 2, false), 1.0 / 3.0, 1e-7);
    CHECK_NEAR(pass_fraction(1, 2, false), 2.0 / 3.0, 1e-7);
    CHECK(pass_fraction(2, 2, false) == 1.0f);
}

static void test_blends() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1000.0f, 1000.0f);
    for (int i = 0; i < 10000; i++) {
        const float a = u(rng), b = u(rng);
        // bit-identical to the original halfway arithmetic at 1/2 (60 fps frames unchanged)
        const float h = lerp_f(a, b, 0.5f), o = 0.5f * (a + b);
        CHECK(std::memcmp(&h, &o, 4) == 0);
        // other fractions: linear, exact at the ends
        for (int k = 0; k <= 8; k++) {
            const float t = k / 8.0f;
            if (t == 0.5f) continue;
            CHECK_NEAR(lerp_f(a, b, t), a + (b - a) * double(t), 1e-3);
        }
        CHECK(lerp_f(a, b, 1.0f) == b);
        CHECK(lerp_f(a, b, 0.0f) == a);
    }
    // s16 angles: halfway as before, other fractions the short way round across the wrap
    for (int a = -32768; a < 32768; a += 997)
        for (int b = -32768; b < 32768; b += 1009) {
            const int16_t d = (int16_t)(b - a);
            CHECK(lerp_s16((int16_t)a, (int16_t)b, 0.5f) == (int16_t)(a + d / 2));
            CHECK(lerp_s16((int16_t)a, (int16_t)b, 1.0f) == (int16_t)b);
        }
    CHECK(lerp_s16(32000, -32000, 0.25f) == (int16_t)(32000 + 384));   // +1536 the short way, a quarter of it
    CHECK(lerp_s16(-32000, 32000, 0.75f) == (int16_t)(-32000 - 1152));
    // colours
    CHECK(lerp_u8(0, 255, 0.5f) == 128);
    CHECK(lerp_u8(0, 255, 0.25f) == 64);
    CHECK(lerp_u8(200, 100, 0.125f) == 188);
    CHECK(lerp_u8(10, 20, 1.0f) == 20);
}

// slerp: the blended direction advances at constant angular speed (k/8 of the angle), and at 1/2
// the weights are the original nlerp's (1, 1)
static void test_slerp() {
    float wa, wb;
    slerp_weights(0.3f, 0.5f, wa, wb);
    CHECK(wa == 1.0f && wb == 1.0f);
    for (double ang : {0.01, 0.3, 0.785, 1.5}) {
        const float a[2] = {1, 0}, b[2] = {(float)std::cos(ang), (float)std::sin(ang)};
        for (int k = 1; k < 8; k++) {
            const float t = k / 8.0f;
            slerp_weights(a[0] * b[0] + a[1] * b[1], t, wa, wb);
            const float x = wa * a[0] + wb * b[0], y = wa * a[1] + wb * b[1];
            CHECK_NEAR(std::atan2(y, x), t * ang, 1e-4);
            if (t != 0.5f) CHECK_NEAR(std::hypot(x, y), 1.0, 1e-4);  // unit length without normalising
        }
    }
    slerp_weights(1.0f, 0.25f, wa, wb);  // no angle: linear
    CHECK_NEAR(wa, 0.75, 1e-6);
    CHECK_NEAR(wb, 0.25, 1e-6);
}

static void test_cap() {
    CHECK(cap_fps(240, 120, true) == 120);  // ProMotion
    CHECK(cap_fps(240, 60, true) == 60);
    CHECK(cap_fps(120, 60, true) == 60);
    CHECK(cap_fps(120, 144, true) == 120);
    CHECK(cap_fps(240, 144, true) == 120);
    CHECK(cap_fps(240, 165, true) == 120);
    CHECK(cap_fps(240, 240, true) == 240);
    CHECK(cap_fps(240, 239, true) == 240);  // 239.76 Hz modes
    CHECK(cap_fps(120, 119, true) == 120);  // 119.88 Hz
    CHECK(cap_fps(120, 90, true) == 60);    // 90 Hz phones: 60 (frames per step must be 2, 4 or 8)
    CHECK(cap_fps(60, 30, true) == 60);     // never below 60
    CHECK(cap_fps(240, 0, true) == 240);    // unknown rate: as chosen
    CHECK(cap_fps(240, 60, false) == 240);  // no vsync (mailbox, immediate): as chosen
    CHECK(cap_fps(100, 240, true) == 60);   // invalid choices round down
}

static void test_plan_and_next() {
    const int64_t budget = 35'333'333, vs = 16'683'333;  // ns
    CHECK(plan_in_between(3, budget, vs / 2, vs / 2) == 3);        // 120 fps passes fit
    CHECK(plan_in_between(7, budget, vs / 4, vs / 4) == 7);        // 240 fps passes fit
    CHECK(plan_in_between(7, budget, vs / 2, vs / 2) == 3);        // 240 fps on a 120 Hz-bound machine: 120
    CHECK(plan_in_between(7, budget, vs, vs) == 1);                // 60 Hz-bound: 60
    CHECK(plan_in_between(3, budget, 25'000'000, vs / 2) == 1);    // slow logic pass, cheap holds: the record pass
    CHECK(plan_in_between(7, budget, 12'000'000, vs / 4) == 5);    // 240 fps: (35.3 - 12) / 4.17
    CHECK(plan_in_between(3, budget, 200'000'000, vs) == 1);       // at least one
    CHECK(plan_in_between(1, budget, 1, 1) == 1);                  // at most n_max
    CHECK(plan_in_between(0, budget, 0, 0) == 1);
    // 60 fps (n = 1): the original rule, in-between pass if it fits, dropped otherwise
    CHECK(next_pass(0, 1, 16'000'000, 16'000'000, budget) == Next::kHold);
    CHECK(next_pass(0, 1, 20'000'000, 16'000'000, budget) == Next::kDrop);
    CHECK(next_pass(1, 1, 0, 0, budget) == Next::kStepDone);
    // 120 fps: all remaining passes fit / only the record pass fits / nothing fits
    CHECK(next_pass(0, 3, 8'000'000, 8'000'000, budget) == Next::kHold);
    CHECK(next_pass(1, 3, 20'000'000, 8'000'000, budget) == Next::kRecord);
    CHECK(next_pass(1, 3, 30'000'000, 8'000'000, budget) == Next::kDrop);
    CHECK(next_pass(2, 3, 26'000'000, 8'000'000, budget) == Next::kHold);  // 26 + 2 x 8 > 35.3: wait, 2 remain
    CHECK(next_pass(3, 3, 99'000'000, 8'000'000, budget) == Next::kStepDone);
    // kRecord: skip as few blended passes as make the rest fit, at least the record pass is left
    CHECK(skip_to(1, 7, 10'000'000, 4'400'000, budget) == 2);  // (35.3 - 10) / 4.4 = 5 of 6 left fit: one skipped
    CHECK(skip_to(0, 7, 34'000'000, 4'400'000, budget) == 6);  // only the record pass
    CHECK(skip_to(3, 3, 0, 1, budget) == 2);                   // (nothing left to skip)
}

// The pass loop of interp.cpp on the virtual vsync of gx2_core.cpp, paced: every pass costs `cost`
// ms of work and then flips on the next tick of the virtual vsync (1/fps of 59.94 Hz x 60).
struct SimResult {
    double steps_per_s, frames_per_s, holds_per_step, exact_share, record_share;
};
static SimResult simulate(int fps, double logic_ms, double hold_ms, double seconds = 30, bool paced = true, double spike_at = -1,
                          double spike_ms = 0) {
    const double kStep = 33.333333, kBudget = kStep + 2.0, period = 16.683333 * 60.0 / fps, kProbeAfter = 10000;
    const int n_max = fps / 30 - 1;
    double t = 0, last_logic = -1e9, logic_avg = period, hold_avg = period, last_hold = 0;
    bool probe = false;
    bool wait_step = false, prev_record = false;
    long steps = 0, frames = 0, holds = 0, exact = 0, records = 0;
    auto run_pass = [&](double cost, double& avg) {  // work, then the flip waits for the next vsync tick
        const double start = t;
        if (spike_at >= 0 && t >= spike_at) {  // one hitch
            cost += spike_ms;
            spike_at = -1;
        }
        t = std::ceil((t + cost) / period - 1e-9) * period;
        avg = probe ? t - start : update_average(int64_t(avg * 1e6), int64_t((t - start) * 1e6)) / 1e6;
        probe = false;
        frames++;
    };
    while (t < seconds * 1000) {
        // logic pass
        if (paced && wait_step && t < last_logic + kStep) t = last_logic + kStep;
        wait_step = false;
        last_logic = t;
        const bool is_exact = paced && !prev_record;
        exact += is_exact;
        int n = paced ? plan_in_between(n_max, int64_t(kBudget * 1e6), int64_t(logic_avg * 1e6), int64_t(hold_avg * 1e6)) : n_max;
        if (is_exact) n = 1;
        steps++;
        run_pass(logic_ms, logic_avg);
        prev_record = false;
        for (int phase = 0;;) {
            // as interp.cpp: after 10 s without an in-between pass, one probe if a vsync tick fits
            const bool try_probe = paced && phase == 0 && t - last_hold >= kProbeAfter;
            const double hold_est = try_probe ? period : hold_avg;
            Next next = paced ? next_pass(phase, n, int64_t((t - last_logic) * 1e6), int64_t(hold_est * 1e6), int64_t(kBudget * 1e6))
                              : (phase >= n ? Next::kStepDone : Next::kHold);
            if (next == Next::kStepDone) {
                prev_record = true;
                records++;
                if (n_max > 1) wait_step = true;
                break;
            }
            if (next == Next::kDrop) {
                wait_step = true;
                break;
            }
            phase = next == Next::kRecord ? skip_to(phase, n, int64_t((t - last_logic) * 1e6), int64_t(hold_est * 1e6), int64_t(kBudget * 1e6)) + 1 : phase + 1;
            holds++;
            probe = try_probe;
            last_hold = t;
            run_pass(hold_ms, hold_avg);
        }
    }
    const double s = t / 1000;
    return {steps / s, frames / s, double(holds) / steps, double(exact) / steps, double(records) / steps};
}

static void test_simulation() {
    // cheap passes: every in-between frame drawn, 30 logic steps a second (29.97 on the 59.94 Hz grid)
    SimResult r = simulate(120, 3, 2);
    std::printf("120 fps, cheap: %.2f steps/s, %.1f fps, %.2f in-between per step\n", r.steps_per_s, r.frames_per_s, r.holds_per_step);
    CHECK_NEAR(r.steps_per_s, 29.97, 0.05);
    CHECK_NEAR(r.holds_per_step, 3.0, 0.03);
    CHECK_NEAR(r.frames_per_s, 119.9, 0.6);
    r = simulate(240, 2, 1.5);
    std::printf("240 fps, cheap: %.2f steps/s, %.1f fps, %.2f in-between per step\n", r.steps_per_s, r.frames_per_s, r.holds_per_step);
    CHECK_NEAR(r.steps_per_s, 29.97, 0.05);
    CHECK_NEAR(r.holds_per_step, 7.0, 0.05);  // (the first steps plan fewer while the averages settle)
    r = simulate(60, 5, 3);
    CHECK_NEAR(r.steps_per_s, 29.97, 0.05);
    CHECK_NEAR(r.holds_per_step, 1.0, 0.01);
    // 240 fps where a pass costs 6 ms (two ticks): 120 fps drawn, 3 in-between frames, full speed
    r = simulate(240, 6, 6);
    std::printf("240 fps, 6 ms passes: %.2f steps/s, %.1f fps, %.2f in-between per step\n", r.steps_per_s, r.frames_per_s, r.holds_per_step);
    CHECK(r.steps_per_s > 29.5);
    CHECK(r.holds_per_step >= 2.9);
    CHECK(r.record_share > 0.99);  // every step keeps its record pass: no exact (doubled) frames
    // 120 fps with 10 ms passes: each flips two ticks later (16.7 ms), so 60 fps evenly, full speed
    r = simulate(120, 10, 10);
    std::printf("120 fps, 10 ms passes: %.2f steps/s, %.1f fps, %.2f in-between per step, %.0f%% exact\n", r.steps_per_s, r.frames_per_s,
                r.holds_per_step, 100 * r.exact_share);
    CHECK(r.steps_per_s > 29.5);
    CHECK(r.holds_per_step >= 0.99);
    CHECK(r.exact_share < 0.01);
    // 240 fps with 12 ms logic and 3 ms in-between passes: (35.3 - 12.5) / 4.17 = 5 in-between frames
    r = simulate(240, 12, 3);
    std::printf("240 fps, 12 ms logic, 3 ms holds: %.2f steps/s, %.1f fps, %.2f in-between per step\n", r.steps_per_s, r.frames_per_s,
                r.holds_per_step);
    CHECK(r.steps_per_s > 29.5);
    CHECK(r.holds_per_step >= 4.9);
    // a slow logic pass (25 ms) at 120 fps: the record pass fits (one tick), nothing else: still 30 steps/s
    r = simulate(120, 24, 4);
    std::printf("120 fps, 24 ms logic: %.2f steps/s, %.1f fps, %.2f in-between per step, %.0f%% exact\n", r.steps_per_s, r.frames_per_s,
                r.holds_per_step, 100 * r.exact_share);
    CHECK(r.steps_per_s > 29.5);
    CHECK(r.exact_share < 0.01);
    // too slow for any in-between frame: logic only, at full speed (paced) ...
    r = simulate(120, 30, 30);
    std::printf("120 fps, 30 ms passes: %.2f steps/s, %.1f fps, %.2f in-between per step\n", r.steps_per_s, r.frames_per_s, r.holds_per_step);
    CHECK(r.steps_per_s > 28.5);
    CHECK(r.holds_per_step < 0.1);
    // expensive in-between passes (30 ms) after a cheap logic pass: none fits, and the probe every 10 s
    // costs at most a step
    r = simulate(120, 5, 30, 60);
    std::printf("120 fps, 5 ms logic, 30 ms holds: %.2f steps/s, %.1f fps, %.3f in-between per step\n", r.steps_per_s, r.frames_per_s,
                r.holds_per_step);
    CHECK(r.steps_per_s > 29.5);
    CHECK(r.holds_per_step < 0.01);
    // one 70 ms hitch in an in-between pass: back to all in-between frames within a few steps
    r = simulate(120, 3, 2, 30, true, 5000, 70);
    std::printf("120 fps, cheap, one 70 ms hitch: %.2f steps/s, %.1f fps, %.2f in-between per step\n", r.steps_per_s, r.frames_per_s,
                r.holds_per_step);
    CHECK(r.holds_per_step > 2.95);
    CHECK(r.steps_per_s > 29.8);
    CHECK(update_average(8'000'000, 80'000'000) == (3 * 8'000'000 + 16'000'000) / 4);
    CHECK(update_average(8'000'000, 4'000'000) == 7'000'000);
    // ... while unpaced the game slows down (each step draws all its passes)
    r = simulate(120, 30, 30, 30, false);
    CHECK(r.steps_per_s < 10);
}


// Paced steps at 120/240 fps on the virtual vsync of gx2_core.cpp (gx2/vsync_pacing.h: a frame that
// is late flips when ready, less than a vsync late keeps the grid), with the pass loop of interp.cpp:
// a pass starts when the previous frame has flipped, its frame is ready `cost` later and flips on its
// tick (sleep timers wake up to 0.3 ms late) or, if late, when ready. Step slots (the schedule since
// issue #164): a step's logic frame is due on its slot, and the logic pass starts one drawn frame
// before it (slot_step_start), a pass that does not fit skips as few blended passes as needed
// (skip_to), and a probe that met a hitch is repeated (probe_estimate). v0.2.13, for reference
// (slots = false): a 30.000 Hz wall clock of its own (a step that waited starts at its due time, a
// step less than a step late keeps the schedule), skips to the record pass, probes replace.
struct StepSim {
    std::vector<int64_t> flips, steps, holds;  // flip times; logic pass starts; in-between flips (ns)
    int skips = 0;                       // steps that skipped or dropped in-between frames
    double steps_per_s(int64_t from) const {
        const auto a = std::lower_bound(steps.begin(), steps.end(), from);
        return double(steps.end() - a - 1) * 1e9 / double(steps.back() - *a);
    }
    double flips_per_s(int64_t from) const {
        const auto a = std::lower_bound(flips.begin(), flips.end(), from);
        return double(flips.end() - a - 1) * 1e9 / double(flips.back() - *a);
    }
    // flip intervals off the drawn rate after `from`: more than 1.4 or less than 0.6 of a tick
    int uneven(int64_t from, int64_t tick) const {
        int n = 0;
        for (size_t i = 1; i < flips.size(); i++)
            if (flips[i] >= from) n += flips[i] - flips[i - 1] > tick * 14 / 10 || flips[i] - flips[i - 1] < tick * 6 / 10;
        return n;
    }
    int64_t max_interval(int64_t from) const {
        int64_t m = 0;
        for (size_t i = 1; i < flips.size(); i++)
            if (flips[i] >= from) m = std::max(m, flips[i] - flips[i - 1]);
        return m;
    }
    // in-between frames drawn between from and to, of the 7 (240 fps) or 3 (120 fps) a step could have
    double drawn_share(int64_t from, int64_t to, int nMax) const {
        const auto c = [from, to](const std::vector<int64_t>& v) { return std::count_if(v.begin(), v.end(), [&](int64_t x) { return x >= from && x < to; }); };
        const auto s = c(steps);
        return s ? double(c(holds)) / double(s * nMax) : 0;
    }
    // the most logic steps in any second after `from` (a game running faster than 30 steps/s)
    int max_steps_per_second(int64_t from) const {
        int m = 0;
        for (size_t i = 0, j = 0; i < steps.size(); i++) {
            if (steps[i] < from) continue;
            j = std::max(j, i);
            while (j < steps.size() && steps[j] < steps[i] + 1'000'000'000) j++;
            m = std::max(m, int(j - i));
        }
        return m;
    }
};
// render: GPU time per frame after the game's swap (the flip waits for it; the pacing decisions are
// taken at the swap, as in interp.cpp, and so do not see it)
static StepSim simulate_steps(int fps, const std::function<int64_t(int)>& logicCost, const std::function<int64_t(int)>& holdCost,
                              double seconds, bool slots = true, int64_t render = 0, bool probeRetry = true) {
    using namespace vsync_pacing;
    constexpr Clock kClock{16'683'333, 4};
    const uint64_t g = fps >= 240 ? 1 : 2;  // granule: ticks per drawn frame
    const int nMax = fps / 30 - 1;
    const int64_t tick = kClock.granule_ns(g), step = slots ? 2 * kClock.period : 33'333'333, budget = step + 2'000'000;
    // (std::uniform_int_distribution is implementation-defined: libstdc++ and libc++ draw different
    // numbers from the same engine, so the simulation would differ between platforms)
    std::mt19937 rng(11);
    const auto uniform = [&rng](int64_t hi) { return int64_t(rng() % uint32_t(hi + 1)); };  // 0..hi
    const auto wakeDist = [&uniform](std::mt19937&) { return uniform(300'000); };
    const auto pollDist = [&uniform](std::mt19937&) { return uniform(250'000); };
    StepSim r;
    int64_t shift = 0, slot = -1, t = kClock.time(64, 0), lastLogic = 0, logicAvg = 16'000'000, holdAvg = 16'000'000;
    uint64_t last = 64;
    bool wait = false, haveLogic = false, prevRecord = false;
    int logicN = 0, holdN = 0;
    // the probe of the in-between pass cost (interp.cpp): after 10 s without in-between passes (the
    // first one of all is a probe), or a second after a probe that met a hitch
    constexpr int64_t kProbeAfter = 10'000'000'000, kProbeRetry = 1'000'000'000;
    int64_t lastHold = -kProbeAfter, retryAt = -1;
    // a pass that started at `start`: its frame flips (and the next pass starts) at the returned time
    int64_t swap = 0;
    auto pass = [&](int64_t start, int64_t cost, bool isStep) {
        swap = start + cost;
        const int64_t ready = swap + render;
        uint64_t due = last + g;
        if (slots && isStep) due = step_due(kClock, due, slot, shift, g);
        const int64_t dueTime = kClock.time(due, shift);
        const int64_t flip = ready <= dueTime ? dueTime + wakeDist(rng) : ready + pollDist(rng);
        last = flip_executed(kClock, shift, due, flip, g);
        if (slots && isStep) slot = next_slot(slot >= 0 ? slot : dueTime, flip, step, tick);
        r.flips.push_back(flip);
        return flip;
    };
    while (t < int64_t(seconds * 1e9)) {
        // logic pass (paced_pass_start)
        int64_t start = t;
        if (slots && slot >= 0) {
            const StepPlan p = slot_step_start(t, kClock.time(slot_tick(kClock, slot, shift, g), shift), tick, step, wait);
            lastLogic = p.start;
            if (p.how == StepStart::kWait) start = p.start + wakeDist(rng);
            if (p.how == StepStart::kLate) slot = -1;  // (gx2::drop_step_slot)
        } else if (wait && haveLogic && t < lastLogic + step) {
            lastLogic += step;
            start = lastLogic + wakeDist(rng);
        } else {
            const bool keep = !slots && wait && haveLogic && t - (lastLogic + step) < step;
            lastLogic = keep ? lastLogic + step : t;
        }
        wait = false;
        haveLogic = true;
        r.steps.push_back(start);
        int n = plan_in_between(nMax, budget, logicAvg, holdAvg);
        if (!prevRecord) n = 1;  // an exact step (interp.cpp: blends only after a record pass)
        prevRecord = false;
        t = pass(start, logicCost(logicN++), true);
        logicAvg = update_average(logicAvg, t - start);
        bool probe = false;
        for (int phase = 0;;) {  // after_pass (at the swap)
            const int64_t decided = render ? swap : t;
            const bool tryProbe = phase == 0 && (decided - lastHold >= kProbeAfter || (retryAt >= 0 && decided >= retryAt));
            const int64_t holdEst = tryProbe ? tick : holdAvg;
            const Next next = next_pass(phase, n, decided - lastLogic, holdEst, budget);
            if (next == Next::kStepDone) {
                prevRecord = true;
                wait = true;
                break;
            }
            if (next != Next::kHold) r.skips++;
            if (next == Next::kDrop) {
                wait = true;
                break;
            }
            // (v0.2.13 skipped every blended pass up to the record pass)
            phase = next != Next::kRecord ? phase + 1 : slots ? skip_to(phase, n, decided - lastLogic, holdEst, budget) + 1 : n;
            probe = tryProbe;
            if (probe) retryAt = -1;
            const int64_t holdStart = t;
            t = pass(holdStart, holdCost(holdN++), false);
            r.holds.push_back(t);
            lastHold = t;
            if (probe) {
                bool retry = false;
                holdAvg = probeRetry && slots ? probe_estimate(holdAvg, t - holdStart, retry) : t - holdStart;
                if (retry) retryAt = t + kProbeRetry;
                probe = false;
            } else {
                holdAvg = update_average(holdAvg, t - holdStart);
            }
        }
    }
    return r;
}

static void test_steps_on_vsync() {
    constexpr int64_t kMs = 1'000'000, kTick240 = 16'683'333 / 4, kTick120 = 16'683'333 / 2;
    const auto ms = [](double v) { return [v](int) { return int64_t(v * 1e6); }; };
    const int64_t from = 3'000 * kMs;  // after the averages settle
    // issue #164, 240 fps on a fast computer (2 ms logic, 1.5 ms in-between passes): every frame on
    // its vsync, 8 a step, 29.97 steps a second. The v0.2.13 wall clock (30.000 Hz) drifted against the
    // grid: frames bunched up after logic passes that slept past their vsync, and in-between frames
    // were skipped every few seconds.
    StepSim a = simulate_steps(240, ms(2), ms(1.5), 60);
    std::printf("240 fps, fast, step slots: %.3f steps/s, %.2f flips/s, %d uneven intervals, %d skips, max %.2f ms\n", a.steps_per_s(from),
                a.flips_per_s(from), a.uneven(from, kTick240), a.skips, a.max_interval(from) / 1e6);
    CHECK_NEAR(a.steps_per_s(from), 29.97, 0.01);
    CHECK_NEAR(a.flips_per_s(from), 239.76, 0.1);
    CHECK(a.uneven(from, kTick240) == 0);
    CHECK(a.skips <= 2);  // (the first steps, while the averages settle)
    StepSim old = simulate_steps(240, ms(2), ms(1.5), 60, false);
    std::printf("240 fps, fast, v0.2.13 schedule: %.3f steps/s, %.2f flips/s, %d uneven intervals, %d skips, max %.2f ms\n",
                old.steps_per_s(from), old.flips_per_s(from), old.uneven(from, kTick240), old.skips, old.max_interval(from) / 1e6);
    CHECK(old.uneven(from, kTick240) > 500);  // (the reported micro-stutter, for reference)
    // in-between passes close to a tick (3.5 ms logic, 3.5 ms in-between): still every frame on time
    a = simulate_steps(240, ms(3.5), ms(3.5), 60);
    std::printf("240 fps, 3.5 ms passes: %.3f steps/s, %.2f flips/s, %d uneven, %d skips\n", a.steps_per_s(from), a.flips_per_s(from),
                a.uneven(from, kTick240), a.skips);
    CHECK_NEAR(a.steps_per_s(from), 29.97, 0.01);
    CHECK(a.uneven(from, kTick240) == 0);
    // a logic pass longer than a tick (6 ms, in-between 2 ms): it flips late, which the in-between
    // frames make up for; full speed, all 8 frames
    a = simulate_steps(240, ms(6), ms(2), 60);
    std::printf("240 fps, 6 ms logic: %.3f steps/s, %.2f flips/s, %d skips\n", a.steps_per_s(from), a.flips_per_s(from), a.skips);
    CHECK_NEAR(a.steps_per_s(from), 29.97, 0.01);
    CHECK(a.flips_per_s(from) > 239);
    // passes a little over a tick (5 ms: a loaded computer at 240 fps): a late logic pass drops only
    // the in-between frames that no longer fit, not all blended frames of the step
    a = simulate_steps(240, ms(5), ms(5), 60);
    old = simulate_steps(240, ms(5), ms(5), 60, false);
    std::printf("240 fps, 5 ms passes: %.3f steps/s, %.1f flips/s, %d skips (v0.2.13 schedule: %.1f flips/s, %d skips)\n",
                a.steps_per_s(from), a.flips_per_s(from), a.skips, old.flips_per_s(from), old.skips);
    CHECK(a.steps_per_s(from) > 29.9);
    CHECK(a.flips_per_s(from) >= old.flips_per_s(from) - 1);
    // 120 fps, fast: 4 frames a step on their vsyncs
    a = simulate_steps(120, ms(2), ms(1.5), 60);
    old = simulate_steps(120, ms(2), ms(1.5), 60, false);
    std::printf("120 fps, fast: %.3f steps/s, %.2f flips/s, %d uneven (v0.2.13 schedule: %d)\n", a.steps_per_s(from), a.flips_per_s(from),
                a.uneven(from, kTick120), old.uneven(from, kTick120));
    CHECK_NEAR(a.steps_per_s(from), 29.97, 0.01);
    CHECK_NEAR(a.flips_per_s(from), 119.88, 0.05);
    CHECK(a.uneven(from, kTick120) == 0);
    // slower computers (issue #125): the game keeps its speed and draws what fits, as with the v0.2.13
    // schedule (6.7 ms a frame: a computer that draws ~150 frames a second; 16 ms: a little over 60;
    // 30 ms: too slow for any in-between frame)
    for (int fps : {240, 120}) {
        for (double cost : {6.7, 10.0, 16.0, 30.0}) {
            a = simulate_steps(fps, ms(cost), ms(cost), 60);
            old = simulate_steps(fps, ms(cost), ms(cost), 60, false);
            std::printf("%d fps, %.1f ms passes: %.3f steps/s, %.1f flips/s (v0.2.13 schedule: %.3f, %.1f)\n", fps, cost,
                        a.steps_per_s(from), a.flips_per_s(from), old.steps_per_s(from), old.flips_per_s(from));
            CHECK(a.steps_per_s(from) > 29.9);
            CHECK(a.flips_per_s(from) > old.flips_per_s(from) - 1);
            CHECK(a.max_steps_per_second(from) <= 31);
        }
    }
    // the render thread's time on top (15 ms a frame after a 2.5 ms pass, the docs/performance.md #125
    // rows; the pacing decisions do not see it): full speed, the frames of the old schedule. (The game
    // queues more than this model: there the logic frames flipped up to 36 ms after their slots.)
    for (int fps : {240, 120}) {
        a = simulate_steps(fps, ms(2.5), ms(2.5), 60, true, 15 * kMs);
        old = simulate_steps(fps, ms(2.5), ms(2.5), 60, false, 15 * kMs);
        std::printf("%d fps, render +15 ms: %.3f steps/s, %.1f flips/s (v0.2.13 schedule: %.3f, %.1f)\n", fps, a.steps_per_s(from),
                    a.flips_per_s(from), old.steps_per_s(from), old.flips_per_s(from));
        CHECK(a.steps_per_s(from) > 29.9);
        CHECK(a.flips_per_s(from) > old.flips_per_s(from) - 1);
    }
    // the #164 report (v0.2.13, RTX 5080, 240 Hz): in-between passes of ~4.3 ms (a 4.17 ms frame), the
    // GPU ~1.5 ms behind the swap. Skipping every blended pass up to the record pass when the rest did
    // not fit left gaps of 15-25 ms a few times a second (his log: p99 20-24 ms in most windows).
    {
        const auto gaps = [&](const StepSim& x) {
            int n = 0;
            for (size_t i = 1; i < x.flips.size(); i++) n += x.flips[i] >= from && x.flips[i] - x.flips[i - 1] > 15 * kMs;
            return n / ((x.flips.back() - from) / 1e9);
        };
        for (double hold : {4.1, 4.2, 4.3}) {
            a = simulate_steps(240, ms(4.3 - 1.5), ms(hold - 1.5), 60, true, 1'500'000);
            old = simulate_steps(240, ms(4.3 - 1.5), ms(hold - 1.5), 60, false, 1'500'000);
            std::printf("240 fps, #164 report (%.1f ms in-between passes): %.3f steps/s, %.1f flips/s, %.2f gaps over 15 ms a second "
                        "(v0.2.13: %.1f flips/s, %.2f gaps a second)\n", hold, a.steps_per_s(from), a.flips_per_s(from), gaps(a),
                        old.flips_per_s(from), gaps(old));
            CHECK(a.steps_per_s(from) > 29.9);
            CHECK(a.flips_per_s(from) > 210);
            CHECK(gaps(a) < 0.2);
            CHECK(gaps(old) > 1);  // (the report)
        }
        // the report's start: a first in-between pass (a probe) of 201 ms. In-between frames within two
        // seconds, as many as later on (seconds 12-19, after the next 10 s probe; these passes are
        // a little over a frame, so ~90 % of 7 a step), not only after that probe.
        const auto firstHitch = [](int i) { return int64_t(i == 0 ? 201'000'000 : 2'800'000); };
        a = simulate_steps(240, ms(2.8), firstHitch, 20, true, 1'500'000);
        old = simulate_steps(240, ms(2.8), firstHitch, 20, true, 1'500'000, false);
        const int64_t t0 = a.steps.front(), o0 = old.steps.front();
        const double early = a.drawn_share(t0 + 2000 * kMs, t0 + 9000 * kMs, 7), later = a.drawn_share(t0 + 12000 * kMs, t0 + 19000 * kMs, 7);
        std::printf("240 fps, #164 start (201 ms probe): in-between frames drawn in seconds 2-9: %.1f%%, 12-19: %.1f%% (without the retry: "
                    "%.0f%%)\n", 100 * early, 100 * later, 100 * old.drawn_share(o0 + 2000 * kMs, o0 + 9000 * kMs, 7));
        CHECK(later > 0.8);
        CHECK(early > later - 0.03);
        CHECK(old.drawn_share(o0 + 2000 * kMs, o0 + 9000 * kMs, 7) < 0.05);  // (the report: 0 % until the frame rate was changed)
    }
    // slower than a step (40 ms logic passes): the game runs at the computer's pace (25 steps a
    // second), without waiting for a slot on top of it
    a = simulate_steps(240, ms(40), ms(40), 30);
    std::printf("240 fps, 40 ms passes: %.3f steps/s\n", a.steps_per_s(from));
    CHECK(a.steps_per_s(from) > 24.5);
    // the start (issue #164): slow logic passes while the game loads (20 of 100 ms), then fast. The
    // game neither stalls nor runs faster than 30 steps a second to catch up, and within a second
    // every frame is on its vsync. (The simulation has no probe of the in-between pass cost after 10 s
    // without any, so the in-between passes are cheap from the start.)
    for (int fps : {240, 120}) {
        a = simulate_steps(fps, [](int i) { return i < 20 ? 100'000'000 : 2'000'000; }, ms(1.5), 30);
        const int64_t loaded = a.steps[20], tick = fps == 240 ? kTick240 : kTick120;  // the first fast logic pass
        std::printf("%d fps, start: %d steps at most in a second, %.3f steps/s and %d uneven intervals after a second\n", fps,
                    a.max_steps_per_second(a.steps.front()), a.steps_per_s(loaded + 1000 * kMs), a.uneven(loaded + 1000 * kMs, tick));
        CHECK(a.steps[19] - a.steps[0] < 19 * 105 * kMs);  // the slow steps at their own pace
        CHECK(a.max_steps_per_second(a.steps.front()) <= 31);
        CHECK_NEAR(a.steps_per_s(loaded + 1000 * kMs), 29.97, 0.01);
        CHECK(a.uneven(loaded + 1000 * kMs, tick) == 0);
    }
    // one 100 ms hitch in an in-between pass at 240 fps: no catch-up faster than 30 steps a second, and
    // every frame back on its vsync within a second
    a = simulate_steps(240, ms(2), [](int i) { return int64_t(i == 3000 ? 100'000'000 : 1'500'000); }, 40);
    {
        int64_t hitch = 0;
        for (size_t i = 1; i < a.flips.size(); i++)
            if (a.flips[i] - a.flips[i - 1] > 50 * kMs) hitch = a.flips[i];
        std::printf("240 fps, one 100 ms hitch: %d steps at most in a second, %d uneven intervals after a second\n",
                    a.max_steps_per_second(from), a.uneven(hitch + 1000 * kMs, kTick240));
        CHECK(hitch > 0);
        CHECK(a.max_steps_per_second(from) <= 31);
        CHECK(a.uneven(hitch + 1000 * kMs, kTick240) == 0);
    }
    // slot_step_start: early and waiting: wait for one frame before the slot; less than a step late:
    // keep; later: a new schedule
    CHECK(slot_step_start(10, 100, 20, 1000, true).how == StepStart::kWait && slot_step_start(10, 100, 20, 1000, true).start == 80);
    CHECK(slot_step_start(10, 100, 20, 1000, false).how == StepStart::kKeep);
    CHECK(slot_step_start(500, 100, 20, 1000, true).how == StepStart::kKeep && slot_step_start(500, 100, 20, 1000, true).start == 80);
    CHECK(slot_step_start(1080, 100, 20, 1000, true).how == StepStart::kLate && slot_step_start(1080, 100, 20, 1000, true).start == 1080);
}

// Even frame delivery (EvenSchedule): frames ready unevenly (logic pass 20 ms, in-between pass 13.4
// ms, a step of 33.37 ms) go out evenly; late frames at once; a hitch does not leave lasting delay;
// a frame is never held longer than one interval; interval 0 passes every frame through.
static void test_even_in_between() {
    // 120 Hz: 4 refreshes a step: 1 or 3 in-between frames, never 2 (90 fps: 1, 1, 2 refreshes)
    CHECK(even_in_between(3, 4) == 3);
    CHECK(even_in_between(2, 4) == 1);
    CHECK(even_in_between(1, 4) == 1);
    // 240 Hz: 8 refreshes: 7, 3 or 1
    CHECK(even_in_between(7, 8) == 7);
    CHECK(even_in_between(6, 8) == 3);
    CHECK(even_in_between(4, 8) == 3);
    // 60 Hz: 2 refreshes: 1; 144 Hz (4.8 -> 5 refreshes): only 1 (5 is prime)
    CHECK(even_in_between(1, 2) == 1);
    CHECK(even_in_between(3, 5) == 1);
    // 180 Hz: 6 refreshes: 5, 2 or 1
    CHECK(even_in_between(3, 6) == 2);
    // unknown display: as planned
    CHECK(even_in_between(2, 0) == 2);
    CHECK(even_in_between(0, 0) == 1);
}
static void test_even_schedule() {
    constexpr int64_t kMs = 1'000'000;
    const int64_t step = 2 * 16'683'333, interval = step / 2;
    std::mt19937 rng(7);
    std::uniform_int_distribution<int64_t> jitter(-kMs / 2, kMs / 2);
    // ready times of the game's frames: logic at step start + 20 ms, in-between at + 33.4 - 0.3 ms
    auto ready_at = [&](int i, int64_t extra) {
        const int64_t s = (i / 2) * step;
        return (i % 2 ? s + step - kMs / 3 : s + 20 * kMs) + jitter(rng) + extra;
    };
    EvenSchedule e;
    std::vector<int64_t> out, ready;
    for (int i = 0; i < 600; ++i) {
        const int64_t r = ready_at(i, i == 300 ? 9 * kMs : 0);  // one 9 ms hitch on a logic frame
        const int64_t t = e.plan(r, interval);
        CHECK(t >= r);
        CHECK(t - r <= interval);  // never more than one frame of added latency
        ready.push_back(r);
        out.push_back(t);
    }
    // without pacing, intervals alternate ~20 / ~13 ms; with it, every interval is within 1.5 ms of
    // the frame interval except right at the hitch
    int uneven = 0, unevenBefore = 0;
    double maxHold = 0;
    for (size_t i = 21; i < out.size(); ++i) {
        const int64_t d = out[i] - out[i - 1];
        if (std::llabs(d - interval) > 3 * kMs / 2 && !(i >= 300 && i <= 302)) uneven++;
        if (std::llabs((ready[i] - ready[i - 1]) - interval) > 3 * kMs / 2) unevenBefore++;
        if (i > 450) maxHold = std::max(maxHold, double(out[i] - ready[i]) / kMs);
    }
    std::printf("even schedule: %d uneven intervals of %zu (before: %d); hold %.1f ms at most 150 frames after a 9 ms hitch\n",
                uneven, out.size() - 21, unevenBefore, maxHold);
    CHECK(unevenBefore > 400);
    CHECK(uneven <= 6);
    // after the hitch the schedule comes back to the frames' own times: in-between frames wait about
    // the difference of the passes, logic frames almost not at all
    CHECK(maxHold < 5.0);
    // the game's rate is kept: the last frame goes out within one interval of its ready time
    CHECK(out.back() - ready.back() <= interval);
    // late frames go out at once
    {
        EvenSchedule l;
        l.plan(0, interval);
        CHECK(l.plan(interval + 5 * kMs, interval) == interval + 5 * kMs);
        // an early frame waits for its slot, the next one counts from that slot
        CHECK(l.plan(interval + 6 * kMs, interval) == interval + 5 * kMs + interval - EvenSchedule{}.leak(interval));
    }
    // a frame that would wait longer than an interval starts anew (a burst after a pause or a forced present)
    {
        EvenSchedule l;
        l.plan(100 * kMs, interval);
        l.presented(100 * kMs, 140 * kMs);  // the presenting thread was busy: counts from the actual time
        CHECK(l.plan(141 * kMs, interval) == 140 * kMs + interval - EvenSchedule{}.leak(interval));
        EvenSchedule m;
        m.plan(100 * kMs, interval);
        m.presented(100 * kMs, 200 * kMs);  // (a clock jump: the slot would be more than an interval away)
        CHECK(m.plan(150 * kMs, interval) == 150 * kMs);
        // a little late keeps the slot; forced out early counts from then
        EvenSchedule n;
        n.plan(100 * kMs, interval);
        n.presented(100 * kMs, 101 * kMs);
        CHECK(n.plan(105 * kMs, interval) == 100 * kMs + interval - EvenSchedule{}.leak(interval));
        n.presented(100 * kMs + interval - EvenSchedule{}.leak(interval), 110 * kMs);
        CHECK(n.plan(111 * kMs, interval) == 110 * kMs + interval - EvenSchedule{}.leak(interval));
    }
    // the presenting thread wakes up 0.8 ms late every time: the holds stay what the passes need
    // (3-4 ms for the in-between frame), they do not grow towards a whole interval
    {
        EvenSchedule w;
        double worstHold = 0;
        for (int i = 0; i < 600; ++i) {
            const int64_t r = ready_at(i, 0);
            const int64_t due = w.plan(r, interval);
            const int64_t at = due > r ? due + 4 * kMs / 5 : due;
            w.presented(due, at);
            if (i > 100) worstHold = std::max(worstHold, double(at - r) / kMs);
        }
        std::printf("even schedule, presents 0.8 ms late: hold %.1f ms at most\n", worstHold);
        CHECK(worstHold < 5.5);
    }
    // interval 0 (30 fps, no interpolation): every frame at once, and the next paced frame starts anew
    {
        EvenSchedule z;
        z.plan(0, interval);
        CHECK(z.plan(2 * kMs, 0) == 2 * kMs);
        CHECK(z.plan(3 * kMs, interval) == 3 * kMs);
    }
    // 120 fps (n = 3): logic pass 12 ms, in-between passes 7 ms each in a 33.37 ms step
    {
        EvenSchedule q;
        const int64_t iv = step / 4;
        int64_t prev = 0, worst = 0;
        for (int i = 0; i < 400; ++i) {
            const int64_t s = (i / 4) * step, k = i % 4;
            const int64_t r = k == 0 ? s + 12 * kMs : s + 12 * kMs + k * 7 * kMs;
            const int64_t t = q.plan(r, iv);
            if (i > 20) worst = std::max<int64_t>(worst, std::llabs((t - prev) - iv));
            prev = t;
        }
        std::printf("even schedule at 120 fps: worst interval error %.2f ms\n", worst / 1e6);
        CHECK(worst <= 4 * EvenSchedule{}.leak(iv) + 1);  // the step's leaks add up on its logic frame
    }
}

int main() {
    test_fractions();
    test_blends();
    test_slerp();
    test_cap();
    test_plan_and_next();
    test_simulation();
    test_steps_on_vsync();
    test_even_schedule();
    test_even_in_between();
    if (g_failed) {
        std::fprintf(stderr, "interp_pacing_test: %d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("interp_pacing_test: all checks passed\n");
    return 0;
}
