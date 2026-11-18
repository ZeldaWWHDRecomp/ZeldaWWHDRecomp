// Frame interpolation: children of the per-frame function on hold passes (runtime/src/interp_hold.h).
// The message manager's frame end (child 11, dMesg_core_c::update) advances once per logic step at 30,
// 60, 120 and 240 fps, so button glyph animations in message boxes play at game speed.
#include "../src/interp_hold.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

using namespace interp::hold;

namespace {
// dMesg_core_c::update (025F77B0): frame counter mod 10000, three cyclic glyph timers
struct Mesg {
    uint32_t frame = 0, timers[3] = {0, 0, 0};
    void update() {
        frame = (frame + 1) % 10000;
        static const uint32_t period[3] = {160, 40, 80};
        for (int i = 0; i < 3; i++)
            if (++timers[i] >= period[i]) timers[i] -= period[i];
    }
};
// `steps` logic steps drawn with `passes` passes each (pass 1: the logic pass, the rest hold passes);
// the per-frame function calls child 11 on every pass, the hook skips it where skip says so
Mesg run(int steps, int passes) {
    Mesg m;
    for (int s = 0; s < steps; s++)
        for (int p = 0; p < passes; p++) {
            const bool hold = p > 0;
            if (!(hold && !runs_on_hold(kDefaultRunMask, kMessageUpdateChild))) m.update();
        }
    return m;
}
}  // namespace

int main() {
    // the message manager's frame end is held; child 0 always runs
    assert(!runs_on_hold(kDefaultRunMask, kMessageUpdateChild));
    assert(runs_on_hold(0, 0));
    // held: 1, 4, 5 (HD menus and systems), 7 (start-up loading screen, #300), 11 (message frame end);
    // every other child runs (6: light counts cleared before child 8 fills them again)
    for (int i = 0; i < 15; i++) {
        const bool held = i == 1 || i == 4 || i == 5 || i == 7 || i == 11;
        assert(runs_on_hold(kDefaultRunMask, i) == !held);
    }
    // the glyph timers and the frame counter: the same after 1000 logic steps at 30/60/120/240 fps
    const Mesg ref = run(1000, 1);
    assert(ref.frame == 1000);
    for (int passes : {2, 4, 8}) {
        const Mesg m = run(1000, passes);
        assert(m.frame == ref.frame);
        for (int i = 0; i < 3; i++) assert(m.timers[i] == ref.timers[i]);
    }
    puts("interp_hold: ok");
    return 0;
}
