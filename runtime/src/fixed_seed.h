// Test aid WWHD_TEST_FIXED_SEED=1: repeatable runs for side-by-side comparisons.
//
// The game's state after a load is not a function of the state alone: its two random generators,
// cM_rnd's three seeds and the particle system's emitter seed (each new JPA emitter takes the next
// value of a linear congruential generator at 104B5730), are global and carry over from everything
// drawn before (title screen, wind lines, clouds, particle effects), and
// g_Counter.mCounter0 counts every game frame including loading screens, while g_Counter.mTimer
// counts only gameplay frames. The game reads mCounter0 (the wind-line spawn gate in rainWindMove,
// tree sway, light LOD), and how many frames the boot's loading takes depends on the host's speed,
// so two runs of the same build diverge from the boot on, as on the console.
//
// With the switch set, the seeds are set to fixed values and mCounter0 to mTimer + kCounterOffset:
// - at the first gameplay frame after boot;
// - after a full state load (memory and threads are the state's: at once);
// - after a portable state load twice: the old stage keeps running while the screen fades out, so
//   the values are written when its loading frames begin (mTimer stops: the old stage is gone, the
//   new one not yet created, its creation draws random numbers) and again at the new stage's first
//   gameplay frame (mTimer moves again), which also undoes a different number of loading frames.
// Scripted test aids timed in TV frames (WWHD_PRESS, WWHD_KEYS, WWHD_STICK, WWHD_RSTICK,
// WWHD_TEST_ORIGIN, WWHD_STATE_/PORTABLE_ SAVE_AT/LOAD_AT, WWHD_DUMP_FRAMES) then count from the first
// gameplay frame, which counts as TV frame kBootFrame (script_frame): the boot takes a different
// number of frames from run to run, so without this a press lands at another game moment. The
// renderer's own frame counter and WWHD_EXIT_AT_FRAME stay TV frames.
// Unset, nothing is written and script_frame is the TV frame. The logic below is free of guest
// memory access (runtime/tools/fixed_seed_test.cpp); fixed_seed.cpp holds the switch and the anchor.
#pragma once
#include <cstdint>
#include <cstdlib>

namespace fixed_seed {

constexpr uint32_t kSeeds[3] = {100, 100, 100};  // the values the game initialises cM_rnd with
constexpr uint32_t kParticleSeed = 0;            // the particle system's emitter seed
constexpr uint32_t kCounterOffset = 96;          // mCounter0 - mTimer (loading frames before gameplay)
constexpr uint64_t kBootFrame = 96;              // the TV frame the first gameplay frame counts as

// the switch (WWHD_TEST_FIXED_SEED) and the scripted-frame anchor (fixed_seed.cpp)
bool enabled();
void set_boot_frame(uint64_t tv);    // the TV frame of the first gameplay frame
uint64_t script_frame(uint64_t tv);  // the TV frame scripted test aids compare with

// WWHD_TEST_FIXED_SEED: on for any value other than empty or "0"
inline bool enabled_value(const char* v) { return v && *v && !(v[0] == '0' && v[1] == 0); }

struct Applied {
    uint32_t seeds_before[3], particle_seed_before;
    uint32_t counter0_before, counter0_after, timer;
};

// TV frame -> scripted frame: from the first gameplay frame (boot_tv; < 0 not seen yet) on,
// counted so that it is kBootFrame
inline uint64_t rebase(uint64_t tv, int64_t boot_tv) {
    if (boot_tv < 0 || tv < uint64_t(boot_tv)) return tv;
    return tv - uint64_t(boot_tv) + kBootFrame;
}

// Ld: uint32_t(uint32_t ea), St: void(uint32_t ea, uint32_t value); addresses of the build in use
template <class Ld, class St>
Applied apply(Ld ld, St st, uint32_t seeds_ea, uint32_t particle_seed_ea, uint32_t counter0_ea, uint32_t timer_ea) {
    Applied a{};
    for (int i = 0; i < 3; i++) {
        a.seeds_before[i] = ld(seeds_ea + 4 * i);
        st(seeds_ea + 4 * i, kSeeds[i]);
    }
    a.particle_seed_before = ld(particle_seed_ea);
    st(particle_seed_ea, kParticleSeed);
    a.timer = ld(timer_ea);
    a.counter0_before = ld(counter0_ea);
    a.counter0_after = a.timer + kCounterOffset;  // (wraps like the game's u32 counters)
    st(counter0_ea, a.counter0_after);
    return a;
}

// The boot application: once, at the first frame boundary that follows a gameplay frame
// (mTimer != 0), unless a load applied it already.
struct BootGate {
    bool done = false;
    bool due(uint32_t timer) {
        if (done || timer == 0) return false;
        done = true;
        return true;
    }
    void mark() { done = true; }
};

// After a portable state is applied (start), once per frame boundary with mTimer: kLoading when
// the scene change's loading frames begin, kArrived at the new stage's first gameplay frame;
// gives up (kTimedOut) when either takes longer than kMaxFrames.
struct LoadGate {
    enum Event { kNone, kLoading, kArrived, kTimedOut };
    static constexpr uint32_t kMaxFrames = 1800;
    int phase = 0;  // 0 idle, 1 old stage fading out, 2 loading
    uint32_t last = 0, frames = 0;
    void start(uint32_t timer) { phase = 1, last = timer, frames = 0; }
    Event step(uint32_t timer) {
        if (!phase) return kNone;
        if (++frames > kMaxFrames) {
            phase = 0;
            return kTimedOut;
        }
        if (phase == 1) {
            if (timer != last) {
                last = timer;
                return kNone;
            }
            phase = 2;
            frames = 0;
            return kLoading;
        }
        if (timer == last) return kNone;
        phase = 0;
        return kArrived;
    }
};

}  // namespace fixed_seed
