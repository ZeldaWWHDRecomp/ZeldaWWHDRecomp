// Frame interpolation of the screen fade, without game state (interp.cpp, mDoGph_calcFade hook;
// runtime/tools/interp_fade_test.cpp, ctest interp_fade).
//
// mDoGph_calcFade (025F06A8) advances the full-screen fade (l_graphics: mFadeRate += mFadeSpeed,
// clamped to 0..1; mFade off once the rate drops below 0) and applies its colour. The game calls it
// once per drawn frame, so with frame interpolation every fade ran 2x/4x/8x too fast (at 120 fps the
// fade-in at a stage entry took 0.22 s instead of 0.87 s). Gated here: the fade advances once per
// logic step (the step's first call), and every call shows it at the pass's fraction of that step
// (the original update run from the step's start with the speed scaled by the fraction), so the
// fade takes the same time at 30, 60, 120 and 240 fps and changes on every drawn frame. The exact
// state of the step is put back after each call. A fade the game starts or changes between two
// calls (fadeIn/fadeOut set rate, speed and the flag) starts a new gate.
#pragma once
#include <cstdint>

namespace interp::fade {

struct State {
    float rate = 0, speed = 0;
    uint8_t fade = 0;  // 1: fading (rate/speed drive the colour); 0: the brightness path, nothing advances
    bool operator==(const State& o) const { return rate == o.rate && speed == o.speed && fade == o.fade; }
};
struct Gate {
    uint64_t step = ~0ull;
    State before, after;  // the guest state at the step's start and after its one advance
};

// One call on a pass at fraction `frac` (0 < frac <= 1) of logic step `step`. `s` is the guest
// state; orig(s) runs the game's update on it (and applies the colour).
template <class Orig>
void call(Gate& g, uint64_t step, float frac, State& s, Orig&& orig) {
    if (g.step != step || !(s == g.after)) {  // the step's first call, or the game started a fade
        g.step = step;
        g.before = s;
        orig(s);
        g.after = s;
        if (frac >= 1.0f || !g.before.fade) return;
    }
    if (g.before.fade) {  // shown at frac: from the step's start with the speed scaled
        s.rate = g.before.rate;
        s.speed = frac * g.before.speed;
        s.fade = 1;
    } else {
        s = g.before;
    }
    orig(s);
    s = g.after;  // the exact state of the step
}

// The GameCube fade the HD code keeps (mDoGph_gInf_c::calcFade of the GC code, 0252F5A0; started by
// 0252F54C / 0252F590: the snapshot fade d_ovlp_fade3 and the event director's FADE): +0 rate (0..1,
// drawn as the HD wipe's texture scale), +4 speed, +C on. The HD display's per-frame child 13 calls it
// on every drawn pass, so it ran 2x/4x/8x too fast, and the logic that waits for it (d_ovlp_fade3,
// the event director, hdFaderMgr::isFading) went on at a different step than at 30 fps. Gated like the
// fader: advanced on the logic pass only, shown at the pass's fraction (the update run from the step's
// start with the speed scaled); the shown state stays in guest memory until the next call takes it
// back (`shown`), the speed stays exact. `s` is the guest state; orig(s) runs the game's update on it.
struct RateGate {
    uint64_t step = ~0ull;
    State before, after, shown;
    bool have = false;
};
template <class Orig>
void rate_call(RateGate& g, uint64_t step, bool logic, float frac, State& s, Orig&& orig) {
    if (g.have && s == g.shown) s = g.after;  // the step's exact state (nothing changed it since)
    if (logic || g.step != step || !(s == g.after)) {  // the step's advance, or the game started a fade
        g.before = s;
        orig(s);
        g.after = s;
        g.step = step;
        g.have = true;
    }
    g.shown = g.after;
    if (frac < 1.0f && g.before.fade) {
        State v = g.before;
        v.speed = frac * g.before.speed;
        orig(v);
        v.speed = g.after.speed;
        g.shown = v;
    }
    s = g.shown;
}

// The HD display's fader (the stage fades: dOvlpFd_startFadeInHD 02728CB8 starts it, 26 frames)
// is advanced by the per-frame function's child 13 (02728A74), which runs on every drawn pass:
// +0 state (0/1 idle, 2/3 fading), +4 duration << 16 | frame counter, +8 colour (bytes r, g, b, alpha;
// read when the fader layer is painted; while the fade counts, the child sets the alpha byte from the
// counter; mDoGph_setFadeColor 025F05F8 sets the colour: black for most stage changes, light grey
// A0A0A0 for the white fades of exits to the outside, other colours in cutscenes). Gated the same
// way: the child advances it on the logic pass only (on the other passes its advance is taken back),
// and while the fade counts on, every pass shows the alpha at its fraction between the alpha shown at
// the end of the previous step and the step's own.
// While the fader counts, the game stores 255 into its alpha between the passes (the title screen's
// exit before the child on every step, scene changes on every pass); the child overwrites it, so 30
// fps never shows it. Such a write is no change of the fade (#273: the gate took it for a new fade:
// it blended from 255, so every logic pass was far darker than the pass after it, and where the write
// came on every pass, the hold passes advanced the fade a second time). A fade the game starts or
// changes (state or counter written) is taken as it is and shown exactly for its first step; while
// the fader idles, an alpha the game writes is kept.
// The colour word is blended per byte (blend_rgba): blended as one number (a float of 0xA0A0A0xx has
// no room for the low byte), every blended pass of a white fade showed alpha 0 or 0x100 (#273: white
// fades flickered between the fade and no fade at all; black fades, 0x000000xx, were exact).
// a colour word (4 bytes) at fraction `frac` from `a` to `b`, per byte
inline uint32_t blend_rgba(uint32_t a, uint32_t b, float frac) {
    uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        const float x = (float)(a >> sh & 0xFF), y = (float)(b >> sh & 0xFF);
        r |= (uint32_t)((1.0f - frac) * x + frac * y + 0.5f) << sh;
    }
    return r;
}
struct Fader {
    uint32_t w[3] = {0, 0, 0};  // state, duration/counter, alpha
    bool operator==(const Fader& o) const { return w[0] == o.w[0] && w[1] == o.w[1] && w[2] == o.w[2]; }
};
struct FaderGate {
    uint64_t step = ~0ull;
    Fader before, after, shown;
    bool have = false;
    bool counting = false;  // the step's advance moved the counter (the alpha is the child's, from the counter)
    bool blend = false;     // the step's alpha is shown blended (the fade counts on from the previous step)
};
// One run of the child on a pass at fraction `frac` of logic step `step` (`logic`: the step's logic
// pass). `f` is the guest fader; child(f) runs the child on it. Leaves the shown state in f.
template <class Child>
void fader_call(FaderGate& g, uint64_t step, bool logic, float frac, Fader& f, Child&& child) {
    // the same fade as `o`: state and counter (and the alpha, unless the child sets it from the counter)
    auto same = [&](const Fader& o) { return f.w[0] == o.w[0] && f.w[1] == o.w[1] && (g.counting || f.w[2] == o.w[2]); };
    if (g.have && same(g.shown)) f = g.after;  // the step's exact state (nothing changed the fade since)
    if (logic || g.step != step || !same(g.after)) {
        // the fade continues from where the previous step left it
        const bool cont = g.have && f.w[0] == g.after.w[0] && f.w[1] == g.after.w[1];
        const uint32_t prev_alpha = g.after.w[2];
        g.before = f;
        child(f);
        g.after = f;
        g.step = step;
        g.have = true;
        g.counting = g.after.w[1] != g.before.w[1];
        g.blend = cont && g.counting;
        if (g.blend) g.before.w[2] = prev_alpha;
    } else {
        f = g.after;  // (an alpha written since: the child's own value)
        child(f);     // (the child's other work)
        f = g.after;  // its advance taken back: once per step
    }
    g.shown = g.after;
    if (frac < 1.0f && g.blend) g.shown.w[2] = blend_rgba(g.before.w[2], g.after.w[2], frac);
    f = g.shown;
}

// Byte values a drawing function steps once per call (one per drawn frame): the fade-out of fallen
// trees (dTree_packet_c::update 025CA6EC, run in the draw on every pass: chaseAlpha(alpha, 0, 8) per
// fallen tree, 4x too fast at 120 fps). Gated like the fader: the call advances them on the logic
// pass only (its advance taken back on the other passes), every pass shows them at its fraction
// between the step's start and end; `owner` identifies the object (another one: a new gate).
template <int N>
struct BytesGate {
    uint64_t step = ~0ull;
    uint32_t owner = 0;
    bool have = false;
    uint8_t before[N], after[N], shown[N];
};
template <int N, class Child>
void bytes_call(BytesGate<N>& g, uint32_t owner, uint64_t step, bool logic, float frac, uint8_t* v, Child&& child) {
    const bool same = g.have && g.owner == owner;
    if (same)
        for (int i = 0; i < N; i++)
            if (v[i] == g.shown[i]) v[i] = g.after[i];  // the step's exact values (nothing changed them since)
    if (logic || !same || g.step != step) {
        for (int i = 0; i < N; i++) g.before[i] = v[i];
        child(v);
        for (int i = 0; i < N; i++) g.after[i] = v[i];
        g.step = step;
        g.owner = owner;
        g.have = true;
    } else {
        child(v);  // (the call's other work)
        for (int i = 0; i < N; i++) v[i] = g.after[i];
    }
    for (int i = 0; i < N; i++) {
        const uint8_t a = g.before[i], b = g.after[i];
        g.shown[i] = frac < 1.0f && a != b ? (uint8_t)((1.0f - frac) * a + frac * b + 0.5f) : b;
        v[i] = g.shown[i];
    }
}

// Float values a drawing function advances once per call, the same way: Link's eye texture offset
// (daPy_matAnm_c::calc 02413E80, from the material calculation of every drawn pass: its morph towards
// the target 1/(n+1) of the way per call ran 2x/4x/8x per logic step) and the HD projection zoom that
// camera_draw eases towards 1 (cLib_addCalc, every drawn pass). The call advances them on the logic
// pass only; elements in `blend` (bit i: element i) are shown at the pass's fraction between the
// step's start and end, the others (the call's own memory, e.g. the previous offset the morph starts
// from) keep the step's exact value.
template <int N>
struct FloatsGate {
    uint64_t step = ~0ull;
    uint32_t owner = 0;
    bool have = false;
    float before[N], after[N], shown[N];
};
template <int N, class Child>
void floats_call(FloatsGate<N>& g, uint32_t owner, uint64_t step, bool logic, float frac, uint32_t blend, float* v, Child&& child) {
    const bool same = g.have && g.owner == owner;
    if (same)
        for (int i = 0; i < N; i++)
            if (v[i] == g.shown[i]) v[i] = g.after[i];  // the step's exact values (nothing changed them since)
    if (logic || !same || g.step != step) {
        for (int i = 0; i < N; i++) g.before[i] = v[i];
        child(v);
        for (int i = 0; i < N; i++) g.after[i] = v[i];
        g.step = step;
        g.owner = owner;
        g.have = true;
    } else {
        child(v);  // (the call's other work)
        for (int i = 0; i < N; i++) v[i] = g.after[i];
    }
    for (int i = 0; i < N; i++) {
        const float a = g.before[i], b = g.after[i];
        g.shown[i] = frac < 1.0f && a != b && (blend >> i & 1) ? (1.0f - frac) * a + frac * b : b;
        v[i] = g.shown[i];
    }
}

// A texture scroll that wraps (0..period): the previous step's value moved by whole periods to the
// side of the new one, so a blend between them goes the short way (0.98 -> 0.02 passes 1.0, not 0.5).
// The blended value may lie outside 0..period; a texture translation repeats, so it draws the same.
inline float unwrap_near(float prev, float cur, float period) {
    if (!(period > 0.0f)) return prev;
    for (int i = 0; i < 4 && cur - prev > 0.5f * period; i++) prev += period;
    for (int i = 0; i < 4 && prev - cur > 0.5f * period; i++) prev -= period;
    return prev;
}

}  // namespace interp::fade
