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

}  // namespace pacing
}  // namespace interp
