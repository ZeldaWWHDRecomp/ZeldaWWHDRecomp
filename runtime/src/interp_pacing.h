// Frame interpolation math without game state: blend fractions, the pass schedule of a logic step,
// paced decisions and the display cap. interp.cpp and interp_fx.cpp use it; runtime/tools/
// interp_pacing_test.cpp tests it (ctest interp_pacing).
//
// A logic step S -> S+1 is drawn in n + 1 passes: the logic pass (phase 0) and hold passes 1..n.
// Hold passes 1..n-1 are blended like the logic pass; hold pass n, the record pass, draws S+1
// exactly and records it as the "before" of the next step's blended frames. n is the step's number
// of in-between frames: fps / 30 - 1 at most (1 at 60 fps, 3 at 120, 7 at 240).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace interp {
namespace pacing {

// Blends at fraction t (0 = the previous step, 1 = the new one). At t = 1/2 they keep the original
// halfway arithmetic, so 60 fps draws bit-identical frames to the halfway-only code.
// (Other fractions as (1-t) a + t b: exact at both ends, unlike a + (b - a) t.)
inline float lerp_f(float a, float b, float t) { return t == 0.5f ? 0.5f * (a + b) : (1.0f - t) * a + t * b; }
// s16 angle from a towards b, the short way round
inline int16_t lerp_s16(int16_t a, int16_t b, float t) {
    const int16_t d = (int16_t)(b - a);
    return t == 0.5f ? (int16_t)(a + d / 2) : (int16_t)(a + (int)std::lround(d * t));
}
inline uint8_t lerp_u8(uint8_t a, uint8_t b, float t) {
    return t == 0.5f ? (uint8_t)((a + b + 1) / 2) : (uint8_t)std::lround(a + (b - a) * t);
}
// Weights of two unit vectors / quaternions for a blend at t whose cosine is cosang (>= 0 after
// choosing the short way): slerp (constant angular speed over the step's frames; nlerp would bunch
// the in-between frames towards the middle). At t = 1/2 both weights are 1 (nlerp, exact there, and
// the original arithmetic); the caller normalises.
inline void slerp_weights(float cosang, float t, float& wa, float& wb) {
    if (t == 0.5f) {
        wa = wb = 1.0f;
        return;
    }
    const float ang = std::acos(std::min(1.0f, std::max(-1.0f, cosang)));
    if (ang > 1e-4f) {
        const float s = std::sin(ang);
        wa = std::sin((1 - t) * ang) / s;
        wb = std::sin(t * ang) / s;
    } else {
        wa = 1 - t;
        wb = t;
    }
}

// Blend fraction of a pass. phase 0 = logic pass, k = hold pass k; n = the step's in-between
// frames; exact = the step is drawn without blending (no record pass before it).
inline float pass_fraction(int phase, int n, bool exact) {
    if (phase == 0) return exact ? 1.0f : 1.0f / float(n + 1);
    return phase < n ? float(phase + 1) / float(n + 1) : 1.0f;
}

// Frame rates frame interpolation offers: 60, 120, 240 (anything else rounds down to one of them)
inline int valid_fps(int f) { return f >= 240 ? 240 : f >= 120 ? 120 : 60; }

// The frame rate drawn for a chosen one on a display with refresh rate hz (0: unknown) when
// presenting waits for the display's vsync: the highest offered rate the display can show (a
// 144 Hz display gets 120, a 90 Hz one 60), never below 60. Without vsync, or with the rate unknown,
// the chosen rate.
inline int cap_fps(int fps, int hz, bool vsync) {
    fps = valid_fps(fps);
    if (!vsync || hz <= 0) return fps;
    const int shown = hz >= 238 ? 240 : hz >= 118 ? 120 : 60;  // (119.88 Hz and 239.76 Hz modes count)
    return std::min(fps, shown);
}

// Paced interpolation: how many in-between frames the logic pass plans for its step: as many hold
// passes as fit into the step's budget after the logic pass, at the recent durations of each kind
// (they differ: a logic pass runs the game, a hold pass only draws; one average for both kept the
// hold passes out for good once a slow logic pass alone filled the budget). At least one, at most
// n_max. All times in one unit (and a pass never takes less than one vsync of its rate: the
// measured durations include the wait for it).
inline int plan_in_between(int n_max, int64_t budget, int64_t logic_pass, int64_t hold_pass) {
    n_max = std::max(1, n_max);
    hold_pass = std::max<int64_t>(hold_pass, 1);
    const int64_t fit = (budget - std::max<int64_t>(logic_pass, 0)) / hold_pass;
    return int(std::clamp<int64_t>(fit, 1, n_max));
}

// Even frame delivery: the in-between frames a step may have so that its frames divide the display's
// refreshes per step evenly (refreshes: whole refreshes in a 33.37 ms step, 4 at 120 Hz, 2 at 60 Hz,
// 8 at 240 Hz; 0: unknown). On a 120 Hz screen 3 frames a step (90 fps) would stay 1, 1 and 2
// refreshes: uneven by construction. The most of at most n that divide evenly, at least 1.
inline int even_in_between(int n, int refreshes) {
    if (refreshes <= 0) return std::max(1, n);
    for (int k = std::max(1, n); k > 1; --k)
        if (refreshes % (k + 1) == 0) return k;
    return 1;
}

// The running average of a pass duration (3/4 old, 1/4 new). A single sample counts at most twice
// the average: one hitch (a shader compiled, a scene loaded) would otherwise make in-between passes
// look unaffordable, and while none are drawn none are measured (measured: one 76 ms pass kept
// them all out for 10 s). A lasting change still gets through within a few passes.
inline int64_t update_average(int64_t avg, int64_t sample) {
    if (avg > 0) sample = std::min(sample, 2 * avg);
    return (avg * 3 + sample) / 4;
}

// What follows a pass of a step (paced): phase just ended (0 = logic pass), n = the step's planned
// in-between frames, elapsed = time since the logic pass started, pass = recent duration of a hold
// pass.
enum class Next {
    kStepDone,    // the record pass ended the step: the next pass is a logic pass
    kHold,        // the next hold pass of the step
    kRecord,      // skip the remaining blended hold passes: the next pass is the record pass
    kDrop,        // no pass fits: the rest of the step is dropped, the next logic pass waits for its time
};
inline Next next_pass(int phase, int n, int64_t elapsed, int64_t pass, int64_t budget) {
    if (phase >= n) return Next::kStepDone;
    const int remaining = n - phase;  // blended hold passes left + the record pass
    if (elapsed + pass * remaining <= budget) return Next::kHold;
    // The record pass goes first: without it the next step has no "before" and is drawn exactly
    // (its logic pass and record pass would then show the same picture twice).
    if (remaining > 1 && elapsed + pass <= budget) return Next::kRecord;
    return Next::kDrop;
}

// A probe of the in-between pass cost (after a long time without in-between passes, and the first
// in-between pass of all) replaces the estimate: the scene may be lighter now. One that hit a hitch
// (more than twice the estimate) counts twice the estimate and asks for another probe soon
// (retry): at the start the first in-between pass met one (90-200 ms, measured here and in the
// #164 report) and kept in-between frames out until the next probe 10 s later.
inline int64_t probe_estimate(int64_t avg, int64_t pass, bool& retry) {
    retry = avg > 0 && pass > 2 * avg;
    return retry ? 2 * avg : pass;
}

// Next::kRecord (not all passes left fit): the phase the step goes on from, so that the next hold
// pass is the one after it. As few blended passes are skipped as make the rest fit (elapsed, pass,
// budget as for next_pass), at least the record pass is left. (Skipping every blended pass up to
// the record pass left gaps of 15-25 ms at 240 fps where in-between passes take a little longer than
// a frame, a few times a second: issue #164.)
inline int skip_to(int phase, int n, int64_t elapsed, int64_t pass, int64_t budget) {
    pass = std::max<int64_t>(pass, 1);
    const int64_t fit = std::clamp<int64_t>((budget - elapsed) / pass, 1, std::max(1, n - phase));
    return n - int(fit);
}

// Paced logic pass at 120/240 fps with a step slot (gx2/vsync_pacing.h: when the step's logic frame
// is due on the virtual vsync). The pass starts one drawn frame (`frame`) before the slot, as it does
// after a record pass, so its frame makes the slot: it waits for that time if it is early and `wait`
// (the step's passes are done), keeps it if less than a step late (the elapsed time counted from it
// makes the step plan fewer in-between frames, and the game catches up), and starts its own schedule
// when later than that (the caller then drops the slot). `start`: the step's start, which its elapsed time counts from.
enum class StepStart { kWait, kKeep, kLate };
struct StepPlan {
    int64_t start;
    StepStart how;
};
inline StepPlan slot_step_start(int64_t now, int64_t slot, int64_t frame, int64_t step, bool wait) {
    const int64_t start = slot - frame;
    if (wait && now < start) return {start, StepStart::kWait};
    if (now - start < step) return {start, StepStart::kKeep};
    return {now, StepStart::kLate};
}

// Even frame delivery (on by default on Android; WWHD_INTERP_EVEN_PACING=0/1). The passes of a step
// take different times: a logic pass runs the game, an in-between pass only draws. When the game thread is the bottleneck
// (a Xiaomi 14 with the record worker on: logic pass 19-20 ms, in-between pass 12-14 ms) frames
// were ready 20 and 13 ms apart, and its 120 Hz screen showed a third of the 60 fps frames for 1 or
// 3 refreshes instead of 2. Here a frame that is ready early is held on the CPU side (its present
// call waits; the game and the renderer go on) until one frame interval after the previous frame;
// a frame that is late goes out at once. Nothing feeds back into the game's timing.
// - interval: the frame's intended distance to the previous frame (the step's length over its
//   frames: 16.68 ms at 60 fps); 0 turns the schedule off (30 fps, uncapped, no interpolation).
// - Each slot is the previous frame's time plus the interval less a small leak (1/128 interval):
//   the late kind of frame (the logic frame) then anchors the schedule, and a schedule pushed late
//   by one slow frame comes back to the frames' own times within about a hundred frames instead of
//   keeping that delay for good. In the steady state the intervals alternate between interval -
//   leak and interval + leak (16.55 / 16.81 ms at 60 fps: both two 120 Hz refreshes). A smaller leak
//   keeps the schedule nearer the latest the logic frames come (less jitter on screen, a little
//   more latency); a simulation with a 120 Hz display: 1/32 92%, 1/64 94%, 1/128 95% of the frames
//   on screen for two refreshes (without the schedule 56%).
// - A frame is never held longer than one interval (no more than one frame of added latency): one
//   that would wait longer goes out at once and starts the schedule anew.
struct EvenSchedule {
    int64_t last = 0;      // the previous frame's present time
    int64_t interval = 0;  // its interval
    bool valid = false;
    void reset() { valid = false; }
    int64_t leakDivisor = 128;  // the leak per frame: interval / leakDivisor
    int64_t leak(int64_t frameInterval) const { return frameInterval / std::max<int64_t>(1, leakDivisor); }
    // the present time of a frame ready at `ready`
    int64_t plan(int64_t ready, int64_t frameInterval) {
        interval = frameInterval;
        if (frameInterval <= 0) {
            valid = false;
            return ready;
        }
        int64_t t = ready;
        if (valid) {
            const int64_t slot = last + frameInterval - leak(frameInterval);
            if (slot > ready && slot - ready <= frameInterval) t = slot;
        }
        last = t;
        valid = true;
        return t;
    }
    // the frame planned for `due` went out at `at`. A little late (the presenting thread was busy, a
    // timer woke late) keeps the schedule: counted from the late time, every late wake-up would push
    // the following frames later for good (measured: holds grew to a whole interval). Much later
    // (more than 1/8 interval) or early (the next frame forced it out) counts from `at`.
    void presented(int64_t due, int64_t at) {
        if (valid && (at < due || at - due > interval / 8)) last = at;
    }
};

}  // namespace pacing
}  // namespace interp
