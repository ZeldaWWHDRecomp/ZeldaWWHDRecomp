// dsweep_scene.h: the D-sweep holds no archive across the title's scene changes (issue #686): while the fade's overlap
// request is up, for the settle time after it, and after a gap in the steps it sees (the idle intro draws no actor).
#include "dsweep_scene.h"
#include <cassert>
#include <cstdio>

int main() {
    dsweep::SceneGuard g;
    // the title, no change: nothing held
    for (uint64_t s = 400; s < 2325; ++s) {
        g.update(s, false);
        assert(!g.hold(s));
    }
    // the fade to the intro: held while the request is up
    for (uint64_t s = 2325; s < 2400; ++s) {
        g.update(s, true);
        assert(g.hold(s));
    }
    // the intro draws no actor: the next step the sweep sees is much later, the request still up, then gone
    g.update(2573, true);
    assert(g.hold(2573));
    g.update(2602, false);
    assert(g.hold(2602));  // settling
    for (uint64_t s = 2603; s < 2602 + 120; ++s) {
        g.update(s, false);
        assert(g.hold(s));
    }
    g.update(2602 + 120, false);
    assert(!g.hold(2602 + 120));
    // a gap alone (no request seen) counts as a change: held for the settle time after it
    for (uint64_t s = 2723; s < 3000; ++s) g.update(s, false);
    assert(!g.hold(2999));
    g.update(3100, false);
    assert(g.hold(3100) && g.hold(3219) && !g.hold(3220));
    // a few skipped steps (a busy frame) are no change
    dsweep::SceneGuard h;
    h.update(10, false);
    h.update(14, false);
    assert(!h.hold(14));
    // before the first update nothing is held
    dsweep::SceneGuard fresh;
    assert(!fresh.hold(0) && !fresh.hold(5000));
    std::puts("dsweep_scene: ok");
    return 0;
}
