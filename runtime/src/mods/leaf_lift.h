// SPDX-License-Identifier: MPL-2.0
// Deku Leaf: wave to rise (jump.cpp; off by default): the rules, pure so leaf_lift_test checks them without the game.
//
// While Link glides on the Deku Leaf (daPy_lk_c::procFanGlide, proc 0x93) a new press of the leaf's button waves
// it: the game makes a gust, his voice and the FAN_CHUTE_RISE sound, and for that step pulls him down with a frame
// of the normal gravity. The glide's own step only puts the glide's gravity back, so a step in which the gravity
// went down from the glide's is the wave. Then speed.y becomes at least the lift and the glide's gravity is kept,
// so he rises about a body height and glides on. The glide's magic drain stays the limit.
// The height is a percentage of the default lift's height (10 units of speed; the height grows with speed.y
// squared, so the lift scales with its square root).
// Held, the button waves on: the game waves only on a new press, so while he glides and the button is held it reads
// as let go for kDropSteps logic steps once kRepeatSteps steps have passed since the last wave and the last let-go.
// Counted in logic steps, so 30 fps, 60 fps (interpolated) and fast forward behave alike.
//
// From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (MPL-2.0), mods/jump.cpp leaf_after_step / leaf_pad (there
// counted in milliseconds: 300 ms between waves, 50 ms let go).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mods::leaf_lift {

constexpr uint32_t kProcFanGlide = 0x93;
constexpr float kBaseLift = 10.0f;
constexpr int kMinHeight = 25, kMaxHeight = 400;  // percent
constexpr uint64_t kRepeatSteps = 9;  // 300 ms at 30 logic steps a second
constexpr uint64_t kDropSteps = 2;    // let go for two steps: one full step's pad read sees it

inline float lift_for(int height_percent) {
    return kBaseLift * std::sqrt(float(std::clamp(height_percent, kMinHeight, kMaxHeight)) / 100.0f);
}

// after Link's step
struct Glide {
    float gravity = 0;  // the gravity after the last step while gliding (0: not gliding)
    uint64_t waves = 0;  // waves seen
};
struct Lifted {
    bool wave = false;  // this step was a wave: write speed_y and gravity
    float speed_y = 0, gravity = 0;
};
inline Lifted after_step(Glide& g, bool on, uint32_t proc, float gravity, float speed_y, int height_percent) {
    if (!on || proc != kProcFanGlide) {
        g.gravity = 0;
        return {};
    }
    const float was = g.gravity;
    g.gravity = gravity;
    if (was >= 0 || gravity >= was) return {};  // no wave in this step (or the glide's first step)
    g.gravity = was;
    g.waves++;
    return {true, std::max(speed_y, lift_for(height_percent)), was};
}

// the leaf's button held: whether this pad read lets it go (`step` = full logic steps so far)
struct Repeat {
    uint64_t seen_waves = ~0ull;
    uint64_t last = 0;        // the step of the last wave or let-go
    uint64_t drop_until = 0;  // let go before this step
};
inline bool drop(Repeat& r, bool on, bool gliding, bool held, uint64_t waves, uint64_t step) {
    if (waves != r.seen_waves) {  // a wave: the next let-go waits from here (a wave ends a let-go under way)
        r.seen_waves = waves;
        r.last = step;
        r.drop_until = 0;
    }
    if (!on || !gliding || !held) {
        r.last = step;  // a fresh hold waits as long as after a wave: its own press waves first
        r.drop_until = 0;
        return false;
    }
    if (step < r.drop_until) return true;
    if (step >= r.last + kRepeatSteps) {
        r.last = step;
        r.drop_until = step + kDropSteps;
        return true;
    }
    return false;
}

}  // namespace mods::leaf_lift
