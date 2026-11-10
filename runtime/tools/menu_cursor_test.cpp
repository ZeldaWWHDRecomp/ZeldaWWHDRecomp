// The item menu's selection cursor watch (runtime/src/mods/menu_cursor.h, issue #336), without the game.
#include <cassert>
#include <cstdio>

#include "mods/menu_cursor.h"

using namespace mods::menu_cursor;
using Change = Watch::Change;

static Reading reading(int active, bool linked, const char* state) {
    Reading r;
    r.directional = 1, r.active = active, r.focus_linked = linked, r.state = state, r.requested = 0;
    return r;
}

int main() {
    // The focus cursor is drawn on an element of the menu's event manager.
    assert(shown(reading(0, true, "::StateID_LoopAll")));
    assert(shown(reading(0, true, "::StateID_InAll")));
    // The map tab's chart grid: the fixed cursor on a grid cell (never linked) is the map's normal state
    // (closing the menu on the grid and opening it again gave a false #336 line before).
    assert(shown(reading(1, false, "::StateID_LoopAll")));
    // #336 as a player's log shows it: the focus cursor with the focus on an element that isn't linked.
    assert(!shown(reading(0, false, "::StateID_Blank")));
    assert(!shown(reading(0, false, "::StateID_OutAll")));
    assert(!shown(reading(0, false, "::StateID_LoopAll")));
    // directional input off (touch) or a Game Over menu: no cursor on the TV
    Reading r = reading(0, true, "::StateID_LoopAll");
    r.directional = 0;
    assert(!shown(r));
    r = reading(0, true, "::StateID_LoopAll");
    r.requested = 1;
    assert(!shown(r));
    {
        // An opening menu has its focus unlinked for a step: no line.
        Watch w;
        assert(w.update(false, 100) == Change::None);
        assert(w.update(true, 101) == Change::None && !w.lost());
        // Missing for a second: one line, once.
        for (uint64_t s = 200; s < 200 + kMissingSteps; s++) assert(w.update(false, s) == Change::None);
        assert(w.update(false, 200 + kMissingSteps) == Change::Lost && w.lost() && w.since() == 200);
        assert(w.update(false, 300) == Change::None);
        // Closed and opened again still without it: no second line; drawn again: one line.
        w.reset();
        assert(w.update(false, 400) == Change::None && w.update(false, 400 + kMissingSteps) == Change::None);
        w.reset();
        assert(w.update(true, 500) == Change::Back && !w.lost());
        assert(w.update(true, 501) == Change::None);
    }
    std::puts("menu_cursor_test: ok");
    return 0;
}
