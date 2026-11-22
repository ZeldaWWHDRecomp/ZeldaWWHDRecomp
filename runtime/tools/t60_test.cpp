// True 60 Hz logic (decomp build): the step schedule on frame interpolation's passes, the 30 Hz fallback's hand-over
// and the pad latch (runtime/src/t60/t60.cpp).
#include <cassert>
#include <cstdarg>
#include <cstdio>

#include "t60/t60.h"
#include "t60/t60_draw_hist.h"
#include "t60/t60_test_rng_key.h"

void log_msg(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
}

static uint64_t g_clock = 0;  // interp's 30 Hz step clock
static void logic(bool interp, int passes) { t60::pass_begin(interp, false, 0, passes, ++g_clock); }
static void hold(int phase, int passes) { t60::pass_begin(true, true, phase, passes, g_clock); }
static void check(bool tick30, bool sub, uint32_t n) {
    assert(t60::tick30() == tick30 && wwhd_t60_tick30 == (tick30 ? 1u : 0u));
    assert(t60::sub_step() == sub);
    assert(wwhd_t60_steps == n);
    assert(wwhd_t60_dt == 1.0f);  // every pass starts outside converted code
    // the frame's one input read (2026-11-15): on its first step: the logic pass of an N = 1 frame, else the first
    // in-between step (a sub-step's own `first` is checked in main, where the step index is known)
    if (tick30) assert(wwhd_t60_first == (n == 1 ? 1u : 0u));
    if (!tick30 && !sub) assert(wwhd_t60_first == 0);
    if (tick30) assert(wwhd_t60_stepk == n);  // (the step within the frame, N on its tick30 step)
}

// draw alignment (t60_draw.h): a converted process's states stamped with t60::step_time() of the step that made them,
// drawn at the interpolated world's time W = clock - 1 + pass_t(): at N = P (30 N fps) always exactly one of its
// steps, N - 1 steps back from its newest; in a 30 Hz fallback frame (it ran on tick30 only) blended like the world
static void test_draw_hist() {
    using namespace t60::draw::hist;
    for (int n : {2, 4}) {
        std::deque<std::pair<double, int>> h;
        int state = 0;  // the process's state = the number of steps it ran
        for (int frame = 1; frame <= 6; frame++) {
            for (int phase = 0; phase < n; phase++) {
                // pass `phase` of the frame whose logic pass made the clock `frame`: the logic pass (phase 0) runs the
                // tick30 step ending `frame`, in-between pass k the step k of the next frame (time frame + k/n)
                const double t = phase == 0 ? frame : frame + (double)phase / n;
                stamp(h, t, ++state);
                const double w = frame - 1.0 + (double)(phase + 1) / n;  // pass_t() = (phase + 1) / P, P = n
                const Pick p = pick(h, w);
                if (state > n) {  // (once the history reaches back to the world)
                    assert(p.a == p.b && p.f == 0.0f);
                    assert(std::fabs(h[p.a].first - w) < kEps);
                    assert(h[p.a].second == state - (n - 1));  // drawn N - 1 steps back
                }
            }
        }
    }
    // a fallback frame: the process ran on tick30 steps only (frames 1, 2): at P = 2 the logic pass of frame 2 draws
    // halfway between them, the in-between pass frame 2 itself
    std::deque<std::pair<double, int>> h;
    stamp(h, 1.0, 10);
    stamp(h, 2.0, 20);
    stamp(h, 2.0, 20);  // (drawn again on the in-between pass, no step: the same stamp)
    assert(h.size() == 2);
    Pick p = pick(h, 1.5);
    assert(p.a == 0 && p.b == 1 && p.f == 0.5f);
    p = pick(h, 2.0);
    assert(p.a == 1 && p.b == 1 && p.f == 0.0f);
    p = pick(h, 0.5);  // (before the history: the first kept step)
    assert(p.a == 0 && p.b == 0);
    stamp(h, 1.0, 30);  // (an older stamp: a save state, the clock went back) starts over
    assert(h.size() == 1 && h[0].second == 30);
    // A whole-form actor (state = its frame-end pose, 10 x frame) runs its frame on the tick30 step; its earlier
    // steps are picture steps (not kept): every pass draws it between its frame ends, never the in-between step's state
    for (int n : {2, 4}) {
        std::deque<std::pair<double, int>> w;
        for (int frame = 1; frame <= 6; frame++)
            for (int phase = 0; phase < n; phase++) {
                const bool picture = phase != 0;
                const double t = picture ? frame + (double)phase / n : frame;
                const bool any = record(w, t, picture, [&] { return picture ? -1 : 10 * frame; });  // (-1: never drawn)
                assert(any);
                for (const auto& st : w) assert(st.second != -1);
                const double at = frame - 1.0 + (double)(phase + 1) / n;
                const Pick p = pick(w, at);
                if (frame >= 2) {  // the frame ends frame - 1 and frame: at (phase + 1) / n between them; the last pass exact
                    if (phase + 1 < n) assert(w[p.a].second == 10 * (frame - 1) && w[p.b].second == 10 * frame &&
                                              std::fabs(p.f - (float)(phase + 1) / n) < 1e-6f);
                    else assert(p.a == p.b && w[p.a].second == 10 * frame);
                }
            }
    }
    {  // a picture step before anything was kept: nothing to draw from (drawn as it is)
        std::deque<std::pair<double, int>> w;
        assert(!record(w, 1.5, true, [] { return 0; }) && w.empty());
    }
    printf("t60 draw history ok\n");
}

// Per-frame holds: a hold asked during frame F applies from frame F + 1 on, fixed within a frame; not asked again: gone
static void test_hold() {
    wwhd_t60_frame = 10;
    assert(!wwhd_t60_held(7));
    wwhd_t60_hold30_request(7);
    assert(!wwhd_t60_held(7));  // (not within the frame it was asked in)
    wwhd_t60_frame = 11;
    assert(wwhd_t60_held(7) && !wwhd_t60_held(8));
    wwhd_t60_hold30_request(7);  // (asked again in 11: still held in 12)
    wwhd_t60_frame = 12;
    assert(wwhd_t60_held(7));
    wwhd_t60_frame = 13;  // (not asked in 12)
    assert(!wwhd_t60_held(7));
    printf("t60 hold ok\n");
}

// WWHD_T60_TEST_CONVERT's list: several profiles convert together (Link and the camera: "168,0x1DC")
static void test_convert_list() {
    int l[9];
    assert(t60_parse_convert_list("168,0x1DC", l, 8) == 168 && l[0] == 168 && l[1] == 0x1DC && l[2] == 0);
    assert(t60_parse_convert_list("0x1DC", l, 8) == 0x1DC && l[1] == 0);
    assert(t60_parse_convert_list("168, 476 ,", l, 8) == 168 && l[1] == 476 && l[2] == 0);
    assert(t60_parse_convert_list("1,2,3,4,5,6,7,8,9", l, 8) == 1 && l[7] == 8 && l[8] == 0);  // at most 8
    assert(t60_parse_convert_list("x", l, 8) == 0 && l[0] == 0);
    wwhd_t60_test_convert = -1;
    assert(wwhd_t60_test_converted(0x1DC) && wwhd_t60_test_converted(1));
    wwhd_t60_test_convert = kT60TestWindowClosed;  // WWHD_T60_TEST_CONVERT_FROM before its time: nothing converted
    assert(!wwhd_t60_test_converted(0x1DC) && !wwhd_t60_test_converted(1));
    wwhd_t60_test_convert = 0;
    assert(!wwhd_t60_test_converted(168));  // (no WWHD_T60_TEST_CONVERT in the test's environment)
    // WWHD_T60_TEST_UNCONVERT (no-conversion controls): unset in the test's environment -> nothing excluded
    assert(wwhd_t60_test_unconvert == 0 && !wwhd_t60_test_unconverted(0xD6) && !wwhd_t60_test_unconverted(0));
    assert(t60_test_unconvert_set("0xD6,0xD5") == 0xD6 && wwhd_t60_test_unconverted(0xD6) && wwhd_t60_test_unconverted(0xD5));
    assert(!wwhd_t60_test_unconverted(0xA8) && !wwhd_t60_test_unconverted(0));
    assert(t60_test_unconvert_set("") == 0 && !wwhd_t60_test_unconverted(0xD6));
    assert(t60_test_unconvert_set(nullptr) == 0 && !wwhd_t60_test_unconverted(0xD5));
    printf("t60_test: convert list ok\n");
}

// Frame-level work on the first step, on by default: the play scene's execute on the frame's first step, so the world flag
// is cleared when that step begins, not on the tick30 step of an N > 1 frame; the frame-level work's pass is that step
static void test_scene_first() {
    assert(t60_scene_first_from_env(nullptr) == 1);  // (unset: on)
    assert(t60_scene_first_from_env("0") == 0 && t60_scene_first_from_env("1") == 1);
    assert(wwhd_t60_scene_first == 1);  // (the test's environment has no WWHD_T60_SCENE_FIRST)
    t60::set_n(2);
    wwhd_t60_world60 = 1;
    logic(true, 2);  // (a frame planned at N = 1: its logic pass is its only step and clears the flag)
    check(true, false, 1);
    assert(wwhd_t60_world60 == 0 && !t60::frame_work_hold_pass());
    wwhd_t60_world60 = 1;  // (the play scene: no event, menu, ...)
    hold(1, 2);            // the next frame at N = 2: its first step clears the flag for its own play scene
    check(false, true, 2);
    assert(wwhd_t60_first == 1 && wwhd_t60_world60 == 0 && t60::frame_work_hold_pass());
    wwhd_t60_world60 = 1;  // (set by that step's play scene)
    logic(true, 2);        // ... and the tick30 step keeps it: the play scene ran on the first step
    check(true, false, 2);
    assert(wwhd_t60_world60 == 1 && !t60::frame_work_hold_pass());
    hold(1, 2);            // (the next frame at N again)
    check(false, true, 2);
    logic(true, 2);
    check(true, false, 2);
    wwhd_t60_world60 = 0;  // (an event started: no N for the next frame)
    hold(1, 2);
    check(false, false, 1);
    assert(!t60::frame_work_hold_pass());
    logic(true, 2);
    check(true, false, 1);
    t60::set_n(1);
    logic(false, 1);  // (back to the default N = 1 for the schedule tests below)
    check(true, false, 1);
    printf("t60_test: scene first (14f) ok\n");
}

// keyed test RNG frame key (t60_test_rng_key.h): counts from the last restart, so a load that takes one record step
// less in B (e.g. 22 vs 21 loading frames) keys the frames after it alike
static void test_keyed_frame() {
    T60KeyedFrame none;
    assert(none.key(500, 0) == 500);                  // before the origin: the absolute frame
    assert(none.key(130, 100) == 30);                 // no restart since the origin: the scenario frame (as before)
    T60KeyedFrame a, b;
    for (T60KeyedFrame* k : {&a, &b}) k->restart(90, 0);  // a pre-origin restart (boot) does not count
    a.restart(100, 100), b.restart(100, 100);         // the origin reseed
    assert(a.key(130, 100) == 30 && b.key(130, 100) == 30);
    for (uint32_t f = 200; f <= 222; f++) a.restart(f, 100);  // A: loading frames 200..221 + the first frame after
    for (uint32_t f = 200; f <= 221; f++) b.restart(f, 100);  // B: one loading frame less
    for (uint32_t k = 0; k < 50; k++) assert(a.key(222 + k, 100) == b.key(221 + k, 100));
    assert(a.key(225, 100) != 3);                    // another epoch than the frames after the origin (key 3)
    const uint64_t first = a.key(230, 100);
    for (uint32_t f = 400; f <= 405; f++) a.restart(f, 100);  // a second load
    assert(a.key(413, 100) != first && a.key(413, 100) == ((uint64_t)2 << 32 | 8));
    a.restart(600, 700);                              // a new origin: epochs start again
    assert(a.key(610, 700) == 10);
    // the old key (frame - origin) for comparison: one frame apart after B's shorter load
    assert((uint64_t)(222 + 5 - 100) != (uint64_t)(221 + 5 - 100));
}

int main() {
    test_keyed_frame();
    test_draw_hist();
    test_hold();
    test_convert_list();
    test_scene_first();
    wwhd_t60_scene_first = 0;  // the schedule below: the 30 Hz world flag's original form (WWHD_T60_SCENE_FIRST=0)
    // default N = 1: every logic pass is a tick30 step of a 1-step frame, in-between passes are no steps
    logic(false, 1);
    check(true, false, 1);
    assert(!t60::wants_unpaced() && t60::wants_fps() == 0);
    wwhd_t60_world60 = 1;
    // True 60 fps input: a logic pass is always a step; at N = 1 an in-between pass is none (interp.cpp t60_input_step reads on
    // every pass at N = 1 without asking: as before)
    assert(t60::next_pass_step(false, false, 0, 1) && t60::next_pass_step(true, false, 0, 2));
    assert(!t60::next_pass_step(true, true, 1, 2));
    hold(1, 2);
    check(false, false, 1);

    t60::set_n(2);
    assert(t60::wants_unpaced() && t60::wants_fps() == 60);
    // 60 fps interpolation: the play scene of a logic pass allows N for the next frame, whose first step is this
    // frame's in-between pass and whose tick30 step is the next logic pass
    logic(true, 2);
    check(true, false, 1);
    assert(wwhd_t60_world60 == 0);  // (cleared for this logic pass's play scene)
    wwhd_t60_world60 = 1;            // (the play scene: no event, menu, ...)
    // the controller's read at the start of the next pass (2026-11-15): that in-between pass is the next frame's first
    // step, so it reads; the logic pass after it does not
    assert(t60::next_pass_first(true, true, 1, 2));
    assert(t60::next_pass_step(true, true, 1, 2));  // (N 2, sub 0 reads)
    assert(t60::next_pass_step_offset(true, true, 1, 2) == 0.0);  // (WWHD_T60_TEST_INPUT_STEP: its frame's start)
    hold(1, 2);
    assert(t60::next_pass_step_offset(true, false, 0, 2) == 0.5);  // (the tick30 step: half a frame in)
    assert(!t60::next_pass_first(true, false, 0, 2));
    assert(t60::next_pass_step(true, false, 0, 2));  // (... and the logic pass reads too: every step)
    assert(t60::next_pass_first(false, false, 0, 1) == false);  // (interp off mid-frame at N = 2: not first either)
    check(false, true, 2);
    assert(wwhd_t60_first == 1);  // (the frame's first step: its input read)
    assert(t60::record_step(g_clock) == g_clock + 1);  // (its records belong to the frame the next logic pass ends)
    wwhd_t60_dt = 0.5f;  // (a converted execute left it set: the next pass starts at 1 again)
    logic(true, 2);
    check(true, false, 2);
    assert(t60::record_step(g_clock) == g_clock);
    // an event starts on this tick30 step (world60 stays 0): the next frame runs at 1, from its first step on
    assert(!t60::next_pass_first(true, true, 1, 2));  // (its in-between pass is no step: the logic pass reads)
    assert(!t60::next_pass_step(true, true, 1, 2));
    hold(1, 2);
    assert(t60::next_pass_first(true, false, 0, 2));
    assert(t60::next_pass_step(true, false, 0, 2));
    check(false, false, 1);
    logic(true, 2);
    check(true, false, 1);
    // and back: the hand-over is always at a frame boundary (the frame's steps never mix 1 and N)
    wwhd_t60_world60 = 1;
    hold(1, 2);
    check(false, true, 2);
    assert(wwhd_t60_first == 1);  // (the frame's first step: its input read)
    logic(true, 2);
    check(true, false, 2);
    // 120 fps (4 passes): the frame's first step on pass 2, passes 1 and 3 are drawn only
    wwhd_t60_world60 = 1;
    assert(!t60::next_pass_step(true, true, 1, 4));  // (drawn-only passes do not read)
    hold(1, 4);
    check(false, false, 2);
    assert(t60::next_pass_step(true, true, 2, 4));
    hold(2, 4);
    check(false, true, 2);
    assert(wwhd_t60_first == 1);  // (the frame's first step: its input read)
    assert(!t60::next_pass_step(true, true, 3, 4));
    hold(3, 4);
    check(false, false, 2);
    assert(t60::next_pass_step(true, false, 0, 4));
    assert(t60::next_pass_step_offset(true, false, 0, 4) == 0.5);  // (N 2 over 4 passes)
    logic(true, 4);
    check(true, false, 2);
    // interpolation off, or too few passes (40 fps): 30 Hz
    wwhd_t60_world60 = 1;
    logic(false, 1);
    check(true, false, 1);
    // N = 4 at 120 fps: a step on every pass
    t60::set_n(4);
    assert(t60::wants_fps() == 120);
    wwhd_t60_world60 = 1;
    for (int k = 1; k < 4; k++) {
        assert(t60::next_pass_step(true, true, k, 4));  // (every step reads)
    assert(t60::next_pass_step_offset(true, true, k, 4) == (k - 1) / 4.0);
        hold(k, 4);
        check(false, true, 4);
        assert(wwhd_t60_first == (k == 1 ? 1u : 0u));  // (one input read per frame: its first step only)
        assert(wwhd_t60_stepk == (uint32_t)k);  // (step k of the frame)
    }
    logic(true, 4);
    check(true, false, 4);
    // N = 4 with only 2 passes (display capped at 60 Hz): 30 Hz
    wwhd_t60_world60 = 1;
    assert(!t60::next_pass_step(true, true, 1, 2));
    hold(1, 2);
    check(false, false, 1);
    logic(true, 2);

    // pad: every read polls; the game sees button changes on live reads, a press between two of them once
    t60::set_n(2);
    wwhd_t60_world60 = 1;
    hold(1, 2);
    assert(t60::poll_every_read());
    const float sticks[4] = {0.25f, -0.5f, 0, 1};
    t60::pad_poll(0, sticks, true);
    assert(t60::game_buttons(0) == 0);
    t60::pad_poll(0x8000, sticks, false);  // A pressed on an in-between read
    assert(wwhd_t60_pad.hold == 0x8000 && wwhd_t60_pad.trig == 0x8000 && wwhd_t60_pad.stick[1] == -0.5f);
    t60::pad_poll(0, sticks, true);        // ... and released by the next live read: the game still sees it once
    assert(wwhd_t60_pad.trig == 0 && wwhd_t60_pad.hold == 0);
    assert(t60::game_buttons(0) == 0x8000);
    t60::pad_poll(0, sticks, true);
    assert(t60::game_buttons(0) == 0);     // (and released on the read after)
    t60::pad_poll(0x10, sticks, false);    // a press still held at the live read: the read's own state
    t60::pad_poll(0x10, sticks, true);
    assert(t60::game_buttons(0x10) == 0x10);
    assert(t60::game_buttons(0) == 0);
    printf("t60_test: ok\n");
    return 0;
}
