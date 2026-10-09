// Unit tests for the run/swim speed mod maths (runtime/src/mods/move_speed.h). Pure functions only:
// the live state (button, mode, stamina, ramp) is mods.cpp and needs no game either.
//   make -C build/cmake move_speed_test && ./build/cmake/move_speed_test
#include "mods/move_speed.h"
#include <cassert>
#include <cmath>
#include <limits>

using namespace mods;

static void test_distance() {
    // The site multiplies the horizontal speed by the factor, composed with true60's dt. One second
    // of movement must be exactly `factor` times the unboosted distance at any rate and logic mode.
    for (auto proc : {kProcMove, kProcSwimMove})
        for (float factor : {1.25f, 1.5f, 2.f, 4.f})
            for (int rate : {30, 60, 120, 240})
                for (bool true60 : {false, true}) {
                    float distance = 0;
                    const int logic = true60 ? 60 : 30;
                    for (int frame = 0; frame < rate; ++frame)
                        for (int n = frame * logic / rate; n < (frame + 1) * logic / rate; ++n)
                            distance += 10.f * (true60 ? .5f : 1.f) * move_target(true, proc, factor, factor);
                    assert(std::abs(distance / 300.f - factor) < 1e-6f);
                }
}

static void test_target() {
    // only the two movement procedures boost, and only while boosting
    assert(move_target(true, kProcMove, 2.f, 3.f) == 2.f);
    assert(move_target(true, kProcSwimMove, 2.f, 3.f) == 3.f);
    assert(move_target(false, kProcMove, 2.f, 3.f) == 1.f);
    assert(move_target(false, kProcSwimMove, 2.f, 3.f) == 1.f);
    for (uint32_t proc = 0; proc < 256; ++proc)
        if (!is_move_proc(proc)) assert(move_target(true, proc, 4.f, 4.f) == 1.f);
    // a dial of 1 means "no boost in this state": the site then keeps its stock path
    assert(move_target(true, kProcMove, 1.f, 4.f) == 1.f);
    assert(move_target(true, kProcSwimMove, 4.f, 1.f) == 1.f);
}

static void test_clamp() {
    assert(clamp_factor(std::numeric_limits<float>::quiet_NaN()) == 1.50f);
    assert(clamp_factor(std::numeric_limits<float>::infinity()) == 1.50f);
    assert(clamp_factor(0.f) == 1.f);
    assert(clamp_factor(100.f) == 4.f);
    assert(clamp_factor(.5f) == 1.f);
    assert(clamp_stamina_seconds(std::numeric_limits<float>::quiet_NaN()) == 5.f);
    assert(clamp_stamina_seconds(-1.f) == 0.f);
    assert(clamp_stamina_seconds(1000.f) == 60.f);
    assert(clamp_cooldown_seconds(std::numeric_limits<float>::quiet_NaN()) == 3.f);
    assert(clamp_cooldown_seconds(-1.f) == 0.f);
    assert(clamp_cooldown_seconds(1000.f) == 60.f);
    // the defaults a fresh settings file gets
    assert(kDefaultLandFactor == 1.50f && kDefaultSwimFactor == 1.25f);
    assert(kDefaultStaminaSeconds == 5.f && kDefaultCooldownSeconds == 3.f);
}

static void test_ramp() {
    const float dt = 1.f / 30.f;
    // reaches the target, monotonically, from either side
    float up = 1.f, down = 4.f;
    for (int i = 0; i < 300; ++i) {
        float next = ramp_towards(up, 4.f, .15f, dt);
        assert(next >= up && next <= 4.f);
        up = next;
        next = ramp_towards(down, 1.f, .15f, dt);
        assert(next <= down && next >= 1.f);
        down = next;
    }
    assert(up == 4.f && down == 1.f);  // snapped, so "off" is exactly 1
    // ~63% of the way after one time constant
    assert(std::abs(ramp_towards(1.f, 4.f, .15f, .15f) - (1.f + 3.f * (1.f - std::exp(-1.f)))) < 1e-6f);
    // half steps advance half as far, so the curve is rate independent
    const float full = ramp_towards(1.f, 4.f, .15f, dt);
    const float half = ramp_towards(ramp_towards(1.f, 4.f, .15f, dt / 2), 4.f, .15f, dt / 2);
    assert(std::abs(full - half) < 1e-3f);
    // a zero time constant jumps
    assert(ramp_towards(1.f, 4.f, 0.f, dt) == 4.f);
    assert(ramp_towards(1.f, 4.f, .15f, 0.f) == 4.f);
}

static void test_animation() {
    // the animation plays at the movement's factor, and 1 is the authored rate untouched
    assert(animation_factor(1.f) == 1.f);
    assert(animation_factor(1.5f) == 1.5f);
    assert(animation_factor(4.f) == 4.f);
    assert(animation_factor(100.f) == 4.f);
    assert(animation_factor(std::numeric_limits<float>::quiet_NaN()) == 1.50f);
}

static void test_stamina() {
    const float dt = 1.f / 30.f;
    // a full bar lasts its stated seconds of boosting
    float s = 1.f;
    for (int i = 0; i < 4 * 30; ++i) s = drain_stamina(s, 4.f, dt);
    assert(s == 0.f);
    s = 1.f;
    for (int i = 0; i < 4 * 30 - 1; ++i) s = drain_stamina(s, 4.f, dt);
    assert(s > 0.f && s <= dt / 4.f + 1e-6f);
    // clamps
    assert(drain_stamina(.01f, 4.f, 1.f) == 0.f);
    assert(refill_stamina(.99f, 4.f, 1.f) == 1.f);
    // 0 seconds = no limit: the bar stays full, so the boost never runs out
    assert(drain_stamina(.5f, 0.f, dt) == 1.f);
    assert(refill_stamina(.5f, 0.f, dt) == 1.f);
    // refills at the same rate
    s = 0.f;
    for (int i = 0; i < 4 * 30; ++i) s = refill_stamina(s, 4.f, dt);
    assert(s == 1.f);
}

static void test_cooldown() {
    const float dt = 1.f / 30.f;
    // the cooldown is the bar's refill time: an empty bar is full again after exactly that long
    float s = 0.f;
    for (int i = 0; i < 3 * 30 - 1; ++i) s = refill_stamina_over(s, 3.f, dt);
    assert(s < 1.f);
    s = refill_stamina_over(s, 3.f, dt);
    assert(s == 1.f);
    // 0 fills at once, so the boost is always ready
    assert(refill_stamina_over(0.f, 0.f, dt) == 1.f);
    assert(refill_stamina_over(.4f, 0.f, dt) == 1.f);
    // a full bar stays full
    assert(refill_stamina_over(1.f, 3.f, dt) == 1.f);
}

int main() {
    test_distance();
    test_target();
    test_clamp();
    test_ramp();
    test_animation();
    test_stamina();
    test_cooldown();
    return 0;
}
