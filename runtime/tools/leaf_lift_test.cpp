// Deku Leaf: wave to rise (runtime/src/mods/leaf_lift.h): the wave found after Link's step, the lift and its height,
// off and other procedures untouched, and the held button's let-go every kRepeatSteps logic steps.
#include "../src/mods/leaf_lift.h"

#include <cstdio>

using namespace mods::leaf_lift;

static int g_failed = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                            \
        }                                                                          \
    } while (0)

int main() {
    constexpr float kGlide = -0.5f, kNormal = -2.5f;  // the glide's gravity, a frame of the normal one (the wave)
    {  // a glide: steps with the glide's gravity, a wave, gliding on
        Glide g;
        CHECK(!after_step(g, true, kProcFanGlide, kGlide, -3.f, 100).wave);  // the glide's first step
        CHECK(!after_step(g, true, kProcFanGlide, kGlide, -3.f, 100).wave);
        const Lifted l = after_step(g, true, kProcFanGlide, kNormal, -3.f, 100);
        CHECK(l.wave && l.speed_y == kBaseLift && l.gravity == kGlide && g.waves == 1);
        CHECK(!after_step(g, true, kProcFanGlide, kGlide, 8.f, 100).wave);  // the glide's gravity back: no wave
        CHECK(after_step(g, true, kProcFanGlide, kNormal, 12.f, 100).speed_y == 12.f);  // already rising faster: kept
        CHECK(g.waves == 2);
    }
    {  // off, or another procedure (falling, landing): nothing, and the glide starts over
        Glide g;
        after_step(g, true, kProcFanGlide, kGlide, -3.f, 100);
        CHECK(!after_step(g, false, kProcFanGlide, kNormal, -3.f, 100).wave);
        CHECK(g.gravity == 0);
        after_step(g, true, kProcFanGlide, kGlide, -3.f, 100);
        CHECK(!after_step(g, true, 0x2A, kNormal, -3.f, 100).wave);
        CHECK(!after_step(g, true, kProcFanGlide, kNormal, -3.f, 100).wave);  // a new glide's first step is no wave
    }
    {  // the height: the lift's square root, clamped
        CHECK(lift_for(100) == kBaseLift);
        CHECK(lift_for(400) == 2 * kBaseLift);
        CHECK(lift_for(25) == kBaseLift / 2);
        CHECK(lift_for(1000) == lift_for(400) && lift_for(0) == lift_for(25));
        Glide g;
        after_step(g, true, kProcFanGlide, kGlide, -3.f, 400);
        CHECK(after_step(g, true, kProcFanGlide, kNormal, -3.f, 400).speed_y == 2 * kBaseLift);
    }
    {  // held: the first press waves; then a let-go of kDropSteps steps every kRepeatSteps steps after the last wave
        Repeat r;
        uint64_t waves = 0, step = 100;
        CHECK(!drop(r, true, true, true, waves, step));  // the press itself
        waves = 1;  // the game waved
        int drops = 0, first = -1;
        for (int i = 1; i <= 40; i++) {
            const bool d = drop(r, true, true, true, waves, step + i);
            if (d && first < 0) first = i;
            drops += d;
            if (d && !drop(r, true, true, true, waves, step + i)) CHECK(false);  // the same step reads the same
            if (first >= 0 && i == first + int(kDropSteps)) waves++;  // the button reads held again: a new press, a wave
        }
        CHECK(first == 1 + int(kRepeatSteps));  // kRepeatSteps after the wave (seen the step after the press)
        CHECK(drops >= 3 * int(kDropSteps));
        // not held, not gliding or off: never let go
        Repeat q;
        for (int i = 0; i < 30; i++) {
            CHECK(!drop(q, true, true, false, 0, step + i));
            CHECK(!drop(q, true, false, true, 0, step + i));
            CHECK(!drop(q, false, true, true, 0, step + i));
        }
        // a fresh hold waits as long as after a wave
        Repeat h;
        drop(h, true, true, false, 0, 500);
        for (uint64_t i = 1; i < kRepeatSteps; i++) CHECK(!drop(h, true, true, true, 0, 500 + i));
        CHECK(drop(h, true, true, true, 0, 500 + kRepeatSteps));
    }
    if (g_failed) {
        std::fprintf(stderr, "leaf_lift_test: %d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("leaf_lift_test: ok\n");
    return 0;
}
