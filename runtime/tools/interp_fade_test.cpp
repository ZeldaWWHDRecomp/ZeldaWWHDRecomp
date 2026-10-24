// Frame interpolation of the screen fade: the same duration at 30, 60, 120 and 240 fps, a change on
// every drawn frame, and a fade started between two calls (runtime/src/interp_fade.h).
#include "../src/interp_fade.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace interp::fade;

namespace {
// mDoGph_calcFade's update (m_Do_graphic.cpp): the alpha it shows, or -1 on the brightness path
float g_shown = -1;
void calc_fade(State& s) {
    if (!s.fade) { g_shown = -1; return; }
    float rate = s.rate + s.speed;
    if (rate < 0.0f) { rate = 0.0f; s.fade = 0; }
    else if (rate > 1.0f) rate = 1.0f;
    s.rate = rate;
    g_shown = rate;
}
struct Run { int steps_to_end; std::vector<float> shown; };
// a fade-in (rate 1 -> 0 at speed -1/26) drawn with `frames` frames per logic step
Run fade_in(int frames) {
    Gate g;
    State s{1.0f, -1.0f / 26.0f, 1};
    Run r{-1, {}};
    for (uint64_t step = 1; step < 100 && r.steps_to_end < 0; step++)
        for (int k = 1; k <= frames; k++) {
            if (frames == 1) calc_fade(s);  // interpolation off: the hook calls the game directly
            else call(g, step, (float)k / frames, s, calc_fade);
            r.shown.push_back(g_shown);
            if (!s.fade && r.steps_to_end < 0) r.steps_to_end = (int)step;
        }
    return r;
}
}  // namespace

// the display fader's per-frame update (child 13): count up to the duration, alpha (the colour word's
// low byte) from the count
void fader_child(Fader& f) {
    if (f.w[0] != 2 && f.w[0] != 3) return;  // idle (0, 1): nothing counts
    const uint32_t dur = f.w[1] >> 16, n = (f.w[1] & 0xFFFF) + 1;
    f.w[1] = (dur << 16) | n;
    f.w[2] = (f.w[2] & 0xFFFFFF00u) | (n * 255 / dur);
    if (n >= dur) f.w[0] = 0;
}
// a stage fade (26 frames at 30 fps) drawn with `frames` frames per step: steps until done, alphas shown.
// `poke`: the game stores 255 into the alpha before the child, 1: on the logic pass (the title screen's
// exit), 2: on every pass (scene changes) (#273); the fader idles at alpha 255 before the fade starts
// `rgb`: the fade colour (mDoGph_setFadeColor: 0 black, A0A0A0 the white fades of exits); shown: alpha
Run fader_run(int frames, int poke = 0, uint32_t rgb = 0, std::vector<uint32_t>* words = nullptr) {
    FaderGate g;
    Fader f;
    f.w[0] = 1;
    f.w[2] = rgb << 8 | (poke ? 255 : 0);
    if (frames > 1) fader_call(g, 0, true, 1.0f / frames, f, fader_child);  // idle before the fade
    f.w[0] = 3;  // the game starts the fade-out
    f.w[1] = 26u << 16;
    Run r{-1, {}};
    for (uint64_t step = 1; step < 100 && r.steps_to_end < 0; step++)
        for (int k = 1; k <= frames; k++) {
            if (f.w[0] && (poke == 2 || (poke == 1 && k == 1))) f.w[2] |= 255;  // the game, before the child
            if (frames == 1) fader_child(f);
            else fader_call(g, step, k == 1, (float)k / frames, f, fader_child);
            r.shown.push_back((float)(f.w[2] & 0xFF));
            if (words) words->push_back(f.w[2]);
            if (k == frames && f.w[0] == 0 && r.steps_to_end < 0) r.steps_to_end = (int)step;
        }
    return r;
}

// dTree update's fade-out of a fallen tree: chaseAlpha(alpha, 0, 8) per call
void tree_update(uint8_t* a) { a[0] = a[0] > 8 ? (uint8_t)(a[0] - 8) : 0; }
int tree_fade_steps(int frames, std::vector<int>& shown) {
    BytesGate<1> g;
    uint8_t a[1] = {255};
    for (int step = 1; step < 200; step++)
        for (int k = 1; k <= frames; k++) {
            if (frames == 1) tree_update(a);
            else bytes_call(g, 1, step, k == 1, (float)k / frames, a, tree_update);
            shown.push_back(a[0]);
            if (k == frames && a[0] == 0) return step;
        }
    return -1;
}

int main() {
    // fallen trees fade out in the same time at every frame rate, a step on every drawn frame
    {
        std::vector<int> s30, s60, s120;
        const int n30 = tree_fade_steps(1, s30), n60 = tree_fade_steps(2, s60), n120 = tree_fade_steps(4, s120);
        assert(n30 == 32 && n60 == n30 && n120 == n30);
        for (size_t i = 0; i < s30.size(); i++) assert(s120[4 * i + 3] == s30[i]);
        for (size_t i = 1; i < s120.size(); i++) assert(s120[i] < s120[i - 1]);
    }
    // the display fader: the same duration at every frame rate, a new alpha on every drawn frame
    {
        const Run a30 = fader_run(1), a60 = fader_run(2), a120 = fader_run(4), a240 = fader_run(8);
        assert(a30.steps_to_end == 26);
        assert(a60.steps_to_end == 26 && a120.steps_to_end == 26 && a240.steps_to_end == 26);
        for (size_t i = 0; i < a30.shown.size(); i++) assert(a120.shown[4 * i + 3] == a30.shown[i]);
        // (the step that starts the fade shows its alpha exactly; from then on a new alpha every frame)
        for (size_t i = 1; i < a120.shown.size(); i++) assert(a120.shown[i] > a120.shown[i - 1] || (i < 4 && a120.shown[i] == a120.shown[i - 1]));
        // #273: the game stores 255 into the alpha before the child on every step: the fade looks the
        // same (it used to blend from 255: every logic pass far darker than the pass after it)
        // (written on every pass, the hold passes advanced the fade a second time: twice as fast)
        for (int poke : {1, 2}) {
            for (int n : {2, 3, 4, 8}) {
                const Run p = fader_run(n, poke), q = fader_run(n);
                assert(p.steps_to_end == 26 && p.shown == q.shown);
                for (size_t i = 1; i < p.shown.size(); i++) assert(p.shown[i] >= p.shown[i - 1]);
            }
            const Run p30 = fader_run(1, poke);
            assert(p30.shown == a30.shown);
        }
        // an alpha the game writes while the fader idles is kept (the child does not set it then)
        {
            FaderGate g;
            Fader f;
            f.w[0] = 1;
            fader_call(g, 1, true, 0.5f, f, fader_child);
            fader_call(g, 1, false, 1.0f, f, fader_child);
            f.w[2] = 255;  // the next step's logic
            fader_call(g, 2, true, 0.5f, f, fader_child);
            assert(f.w[0] == 1 && f.w[2] == 255);
            fader_call(g, 2, false, 1.0f, f, fader_child);
            assert(f.w[0] == 1 && f.w[2] == 255);
            fader_call(g, 3, true, 0.5f, f, fader_child);
            assert(f.w[2] == 255);
        }
        // #273: a white fade (colour A0A0A0, exits to the outside) looks like the black one: the same
        // alphas on every pass, the colour kept (blended as one number, the colour word lost its low
        // byte: every blended pass showed alpha 0 or 0x100, the fade flickered on and off)
        for (int poke : {0, 1, 2})
            for (int n : {1, 2, 3, 4, 8}) {
                std::vector<uint32_t> words;
                const Run w = fader_run(n, poke, 0xA0A0A0, &words), b = fader_run(n, poke);
                assert(w.steps_to_end == 26 && w.shown == b.shown);
                for (uint32_t x : words) assert(x >> 8 == 0xA0A0A0);
            }
        assert(blend_rgba(0xA0A0A009, 0xA0A0A013, 0.5f) == 0xA0A0A00E);
        assert(blend_rgba(0x000000FF, 0xFFFFFF00, 0.5f) == 0x80808080);
        assert(blend_rgba(0x12345678, 0x9ABCDEF0, 1.0f) == 0x9ABCDEF0 && blend_rgba(0x12345678, 0x9ABCDEF0, 0.0f) == 0x12345678);
        // ungated (the child on every pass), as before: 4 times as fast
        Fader f;
        f.w[0] = 3;
        f.w[1] = 26u << 16;
        int frames = 0;
        while (f.w[0]) { fader_child(f); frames++; }
        assert(frames == 26);  // 26 drawn frames: 0.22 s at 120 fps instead of 0.87 s
    }
    const Run r30 = fade_in(1), r60 = fade_in(2), r120 = fade_in(4), r240 = fade_in(8);
    // the same number of logic steps (the same time) at every frame rate
    assert(r30.steps_to_end > 20);
    assert(r60.steps_to_end == r30.steps_to_end && r120.steps_to_end == r30.steps_to_end && r240.steps_to_end == r30.steps_to_end);
    // the frame that ends each step shows the 30 fps value
    for (size_t i = 0; i < r30.shown.size(); i++) {
        assert(std::fabs(r120.shown[4 * i + 3] - r30.shown[i]) < 1e-6f);
        assert(std::fabs(r60.shown[2 * i + 1] - r30.shown[i]) < 1e-6f);
    }
    // at 120 fps the fade changes on every drawn frame (it used to run 4x as fast)
    for (size_t i = 1; i + 4 < r120.shown.size(); i++)
        if (r120.shown[i] > 0 && r120.shown[i - 1] > 0) assert(r120.shown[i] < r120.shown[i - 1]);
    // a fade the game starts between two calls of a step restarts the gate
    {
        Gate g;
        State s{0, 0, 0};
        call(g, 1, 0.25f, s, calc_fade);          // nothing fading
        s = State{0.0f, 0.1f, 1};                  // fadeOut started by the step's logic
        call(g, 1, 0.5f, s, calc_fade);
        assert(std::fabs(s.rate - 0.1f) < 1e-6f && s.fade == 1);  // advanced once, kept exact
        call(g, 1, 0.75f, s, calc_fade);
        assert(std::fabs(g_shown - 0.075f) < 1e-6f);                // shown at 3/4 of the step
        assert(std::fabs(s.rate - 0.1f) < 1e-6f);
        call(g, 2, 0.25f, s, calc_fade);
        assert(std::fabs(s.rate - 0.2f) < 1e-6f);                   // the next step advances once
        assert(std::fabs(g_shown - 0.125f) < 1e-6f);
    }
    // rate_call: the GameCube fade (0252F5A0: rate += speed, clamped to 0..1, off at the end) advances
    // once per logic step at every frame rate; the logic of the next step always finds the 30 fps
    // state; every drawn frame shows a new rate while it moves
    {
        auto gc_calc = [](State& s) {
            if (!s.fade) return;
            const float r = s.rate + s.speed;
            if (r < 0.0f) s.rate = 0.0f;
            else if (r <= 1.0f) s.rate = r;
            else { s.rate = 1.0f; s.fade = 0; }
        };
        auto run = [&](int frames, std::vector<State>& logic_sees, std::vector<float>& shown) {
            RateGate g;
            State s{0.0f, 0.0f, 0};
            for (uint64_t step = 1; step <= 40; step++) {
                if (step == 3) s = State{0.0f, 0.06f, 1};  // the step's logic starts the fade (0252F54C)
                logic_sees.push_back(s);
                for (int k = 1; k <= frames; k++) {
                    if (frames == 1) gc_calc(s);
                    else rate_call(g, step, k == 1, (float)k / frames, s, gc_calc);
                    shown.push_back(s.rate);
                }
            }
        };
        std::vector<State> l30, l60, l120, l40;
        std::vector<float> s30, s60, s120;
        run(1, l30, s30);
        run(2, l60, s60);
        run(4, l120, s120);
        assert(l60 == l30 && l120 == l30);
        for (size_t i = 0; i < s30.size(); i++) assert(s60[2 * i + 1] == s30[i] && s120[4 * i + 3] == s30[i]);
        for (size_t i = 4 * 3 + 1; i < s120.size() && s120[i] < 1.0f; i++) assert(s120[i] > s120[i - 1]);
        // the speed stays exact on every pass (the guest's), a fade the game starts mid-step restarts
        RateGate g;
        State s{0.0f, 0.1f, 1};
        rate_call(g, 1, true, 0.5f, s, gc_calc);
        assert(std::fabs(s.rate - 0.05f) < 1e-6f && s.speed == 0.1f && g.after.rate == 0.1f);
        rate_call(g, 1, false, 1.0f, s, gc_calc);
        assert(s.rate == 0.1f);
        s = State{1.0f, -0.25f, 1};  // the next step's logic starts a fade-in
        rate_call(g, 2, true, 0.5f, s, gc_calc);
        assert(g.after.rate == 0.75f && std::fabs(s.rate - 0.875f) < 1e-6f);
    }
    // floats_call: a value eased once per call (the HD projection zoom, cLib_addCalc towards 1 by at
    // most 0.1 per call) advances once per logic step at every frame rate, and blended passes show it
    // between the step's start and end
    {
        auto ease = [](float* v) { v[0] = v[0] - 1.0f > 0.1f ? v[0] - 0.1f : 1.0f; };
        auto run = [&](int frames) {
            FloatsGate<1> g;
            float z = 1.3333334f;
            std::vector<float> shown;
            for (uint64_t step = 1; step <= 6; step++)
                for (int k = 0; k < frames; k++) {
                    floats_call(g, 7, step, k == 0, float(k + 1) / frames, 1u, &z, ease);
                    shown.push_back(z);
                }
            return shown;
        };
        const auto s30 = run(1), s120 = run(4), s60 = run(2);
        assert(s30.size() == 6 && s30[0] < 1.3333334f && s30[5] == 1.0f);
        for (size_t i = 0; i < s30.size(); i++) {
            assert(s120[4 * i + 3] == s30[i]);  // the step's last frame: the 30 fps value
            assert(s60[2 * i + 1] == s30[i]);
        }
        for (size_t i = 1; i < 12; i++) assert(s120[i] < s120[i - 1]);  // moves on every frame while easing
        // not blended (bit clear): every pass shows the step's exact value
        FloatsGate<2> g;
        float v[2] = {0.0f, 0.0f};
        auto inc = [](float* x) { x[0] += 1.0f; x[1] += 1.0f; };
        floats_call(g, 1, 1, true, 0.25f, 2u, v, inc);
        assert(v[0] == 1.0f && v[1] == 0.25f);
        floats_call(g, 1, 1, false, 0.5f, 2u, v, inc);
        assert(v[0] == 1.0f && v[1] == 0.5f);  // advanced once only
        floats_call(g, 1, 2, true, 0.25f, 2u, v, inc);
        assert(v[0] == 2.0f && v[1] == 1.25f);
        // another owner starts a new gate
        float w[2] = {5.0f, 5.0f};
        floats_call(g, 2, 2, false, 0.5f, 2u, w, inc);
        assert(w[0] == 6.0f && w[1] == 5.5f);
    }
    // unwrap_near: a wrapping texture scroll blends the short way
    assert(unwrap_near(0.98f, 0.02f, 1.0f) == 0.98f - 1.0f);
    assert(unwrap_near(0.02f, 0.98f, 1.0f) == 0.02f + 1.0f);
    assert(unwrap_near(0.40f, 0.45f, 1.0f) == 0.40f);
    assert(unwrap_near(0.40f, 0.45f, 0.0f) == 0.40f);
    printf("interp_fade: all tests passed (fade-in %d steps, stage fader 26 steps at 30/60/120/240 fps)\n", r30.steps_to_end);
    return 0;
}
