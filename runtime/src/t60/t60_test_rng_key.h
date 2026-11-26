// The keyed test RNG's frame key (WWHD_TEST_RNG_KEYED_MODE=frame; t60/t60_test_rng.cpp), header-only for t60_test.
// The key counts 30 Hz frames from the last draw-counter restart (savestate.cpp apply_fixed_seed: every loading frame of
// a load / stage change / test warp, the first frame after it, the origin reseed), not from the scenario origin: the
// two runs of a pair can spend a different number of record steps in a load (e.g. a warp's load 22 frames in A, 21 in
// B, game clocks equal), so a key from the origin put every keyed draw after the warp one frame apart (Link idle false
// failures). A load's restarts on consecutive frames are one load; each load after the
// origin starts a new epoch, so the frames of another stage hash to other values. Before any restart since the origin
// the key is the scenario frame (frame - origin), as before.
#pragma once
#include <cstdint>

struct T60KeyedFrame {
    uint64_t origin = 0;   // the scenario origin the epochs count from
    bool have = false;     // a restart seen since that origin
    uint32_t base = 0;     // the frame of the last restart
    uint32_t epoch = 0;    // loads since the origin (consecutive restarts are one load)
    void restart(uint32_t frame, uint64_t o) {
        if (o != origin) origin = o, have = false, epoch = 0;
        if (have && frame - base > 2) epoch++;  // a later load (not the next frame of the same one)
        have = true, base = frame;
    }
    uint64_t key(uint32_t frame, uint64_t o) const {
        if (!o) return frame;
        if (!have || o != origin) return (uint64_t)((int64_t)frame - (int64_t)o);
        return ((uint64_t)epoch << 32) ^ (uint64_t)(uint32_t)(frame - base);
    }
};
