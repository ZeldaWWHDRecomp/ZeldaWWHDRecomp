// Dynamic resolution decisions (runtime/src/dynres_core.h): levels, steps down when GPU-bound, no
// change when the CPU is the limit, hysteresis on the way up, the doubled hold after an undone step,
// skipped windows, the test budget and the GPU busy union.
#include "dynres_core.h"
#include <cstdio>
#include <cstdlib>

using namespace dynres;

static int failures = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); \
            failures++;                                                  \
        }                                                                \
    } while (0)

// a half-second window at `fps` frames a second with the GPU busy `share` of the time
static Window win(double fps, double share, double target = 30, double seconds = 0.5) {
    Window w;
    w.seconds = seconds;
    w.frames = int(fps * seconds + 0.5);
    w.busyMs = share * seconds * 1000.0;
    w.targetFps = target;
    return w;
}

int main() {
    // levels and the minimum
    CHECK(lowest_level(0) == 0);
    CHECK(lowest_level(1) == 0);
    CHECK(lowest_level(0.75f) == 2);
    CHECK(lowest_level(0.5f) == 4);
    CHECK(lowest_level(0.3f) == 4);
    CHECK(lowest_level(0.8f) == 1);  // the lowest level at or above the minimum
    CHECK(kLevels[4] == 0.5f && 1280 * kLevels[1] == 1120 && 720 * kLevels[3] == 450);

    // off: never changes
    {
        Controller c;
        c.reset(0);
        for (int i = 0; i < 20; i++) CHECK(!c.decide(win(20, 1.0)).changed);
        CHECK(c.factor() == 1.0f);
    }
    // GPU-bound: one level down, the next window is not judged, then another level, down to the minimum
    {
        Controller c;
        c.reset(lowest_level(0.75f));
        Decision d = c.decide(win(27, 0.97));
        CHECK(d.changed && c.level == 1);
        d = c.decide(win(27, 0.97));  // settling
        CHECK(!d.judged && c.level == 1);
        d = c.decide(win(27, 0.97));
        CHECK(d.changed && c.level == 2);
        c.decide(win(27, 0.97));
        d = c.decide(win(27, 0.97));
        CHECK(!d.changed && c.level == 2);  // the minimum
    }
    // far below the frame rate: still one level at a time (the renderers keep only neighbouring levels)
    {
        Controller c;
        c.reset(4);
        Decision d = c.decide(win(18, 0.99));
        CHECK(d.changed && c.level == 1);
        c.decide(win(18, 0.99));  // settling
        d = c.decide(win(18, 0.99));
        CHECK(d.changed && c.level == 2);
    }
    // the CPU is the limit (GPU mostly idle): no change; at the frame rate with a busy GPU: no change
    {
        Controller c;
        c.reset(4);
        for (int i = 0; i < 10; i++) CHECK(!c.decide(win(20, 0.5)).changed);
        for (int i = 0; i < 10; i++) CHECK(!c.decide(win(30, 0.95)).changed);
        CHECK(c.level == 0);
    }
    // going up: needs room at the next level for two windows and the hold (2 s) since the step down
    {
        Controller c;
        c.reset(4);
        c.decide(win(25, 0.99));  // down to 0.875 at t=0.5
        CHECK(c.level == 1);
        c.decide(win(30, 0.5));   // settling (t=1.0)
        Decision d = c.decide(win(30, 0.5));  // room 1 (t=1.5)
        CHECK(d.judged && !d.changed);
        d = c.decide(win(30, 0.5));  // room 2, but only 1.5 s since the step down
        CHECK(!d.changed);
        d = c.decide(win(30, 0.5));  // 2.0 s
        CHECK(d.changed && c.level == 0);
    }
    // no step up when the next level would not fit: 0.70 at 0.875 predicts 0.91 at 1x
    {
        Controller c;
        c.reset(4);
        c.decide(win(25, 0.99));
        for (int i = 0; i < 20; i++) c.decide(win(30, 0.70));
        CHECK(c.level == 1);
        // 0.60 predicts 0.78: up
        for (int i = 0; i < 4; i++) c.decide(win(30, 0.60));
        CHECK(c.level == 0);
    }
    // a step up undone within 4 s doubles the hold
    {
        Controller c;
        c.reset(4);
        c.decide(win(25, 0.99));  // down
        c.decide(win(30, 0.5));
        for (int i = 0; i < 3; i++) c.decide(win(30, 0.5));
        CHECK(c.level == 0 && c.hold == 2.0);
        c.decide(win(30, 0.5));    // settling after the step up
        c.decide(win(25, 0.99));   // down again 1 s after the step up
        CHECK(c.level == 1 && c.hold == 4.0);
        c.decide(win(30, 0.5));
        int windows = 0;
        while (c.level == 1 && windows < 40) { c.decide(win(30, 0.5)); windows++; }
        CHECK(windows >= 7);  // 4 s at half-second windows
        CHECK(c.level == 0);
    }
    // a long window (paused, loading) and too few frames are not judged
    {
        Controller c;
        c.reset(4);
        CHECK(!c.decide(win(25, 0.99, 30, 3.0)).judged);
        Window w = win(25, 0.99);
        w.frames = 2;
        CHECK(!c.decide(w).judged);
        Window none = win(25, 0.99);
        none.busyMs = 0;  // no GPU timing
        CHECK(!c.decide(none).judged && c.level == 0);
    }
    // test budget: GPU ms per frame against the budget, whatever the frame rate
    {
        Controller c;
        c.reset(4);
        Window w = win(60, 0.3);  // 5 ms per frame
        w.budgetMs = 4;
        CHECK(c.decide(w).changed && c.level == 1);
        c.decide(w);
        w.budgetMs = 2;  // 2.5x the budget: still one level
        CHECK(c.decide(w).changed && c.level == 2);
        w.budgetMs = 50;
        int windows = 0;
        while (c.level > 0 && windows < 100) { c.decide(w); windows++; }
        CHECK(c.level == 0);
    }
    // the GPU busy union: overlaps count once, gaps not at all
    {
        BusyUnion u;
        u.add(0, 10);
        u.add(5, 15);
        u.add(20, 30);
        u.add(22, 25);  // inside an earlier interval
        CHECK(u.take() == 25);
        CHECK(u.take() == 0);
        u.add(40, 35);  // empty
        CHECK(u.busyNs == 0);
        u.add(1, 2);  // slightly before the last end: already covered
        CHECK(u.busyNs == 0);
        u.add(5e9, 6e9);
        CHECK(u.take() == 1e9);
        u.add(1000, 3000);  // far before (a new clock): the end starts over
        CHECK(u.take() == 2000);
    }
    if (failures) {
        std::fprintf(stderr, "dynres_test: %d failures\n", failures);
        return 1;
    }
    std::printf("dynres_test: ok\n");
    return 0;
}
