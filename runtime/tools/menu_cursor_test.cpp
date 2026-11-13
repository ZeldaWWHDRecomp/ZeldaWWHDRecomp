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
    {
        // The #336 stop-gap (Restore): the reported state, the focus on an unlinked element with the focus cursor.
        Reading stuck = reading(0, false, "::StateID_Blank");
        stuck.focus = 0x10A00000;
        assert(focus_stuck(stuck));
        const Context none;
        Restore x;
        for (uint64_t s = 1000; s < 1000 + kMissingSteps; s++) assert(!x.update(stuck, none, s));
        assert(x.update(stuck, none, 1000 + kMissingSteps) && x.restores() == 1);
        // the same step again (an interpolated frame) or the next one: not again
        assert(!x.update(stuck, none, 1000 + kMissingSteps) && !x.update(stuck, none, 1001 + kMissingSteps));
        // the focus back on a linked element: nothing; stuck again: another second before the next one
        assert(!x.update(reading(0, true, "::StateID_LoopAll"), none, 1100));
        for (uint64_t s = 1200; s < 1200 + kMissingSteps; s++) assert(!x.update(stuck, none, s));
        assert(x.update(stuck, none, 1200 + kMissingSteps) && x.restores() == 2);
        // where it goes: the menu's default element, else the one it was last on, never one that isn't linked
        assert(restore_target(0x10B00000, true, 0x10C00000, true) == 0x10B00000);
        assert(restore_target(0x10B00000, false, 0x10C00000, true) == 0x10C00000);
        assert(restore_target(0, false, 0x10C00000, true) == 0x10C00000);
        assert(restore_target(0x10B00000, false, 0x10C00000, false) == 0);
        // A tab switch or an opening menu: the unlinked element changes (or goes) within the second: no restore.
        Restore t;
        for (uint64_t s = 0; s < 3 * kMissingSteps; s++) {
            Reading moving = stuck;
            moving.focus = 0x10A00000 + 0x100 * uint32_t(s / (kMissingSteps / 2));
            assert(!t.update(moving, none, 2000 + s));
        }
        Reading cleared = stuck;
        cleared.focus = 0;  // no focus (a sub window, a closing menu): not this state
        assert(!focus_stuck(cleared));
        for (uint64_t s = 0; s < 3 * kMissingSteps; s++) assert(!t.update(cleared, none, 3000 + s));
        // The chart grid (map tab): the fixed cursor on an unlinked grid cell: untouched.
        Reading grid = reading(1, false, "::StateID_LoopAll");
        grid.focus = 0x10D00000;
        assert(!focus_stuck(grid));
        for (uint64_t s = 0; s < 3 * kMissingSteps; s++) assert(!t.update(grid, none, 4000 + s));
        // Our settings entry arrow focused, or its page or slide out: untouched.
        Reading arrow = stuck;
        arrow.focus_on_arrow = true;
        assert(!focus_stuck(arrow));
        for (uint64_t s = 0; s < 3 * kMissingSteps; s++) assert(!t.update(arrow, none, 5000 + s));
        Context page;
        page.settings_page = true;
        for (uint64_t s = 0; s < 3 * kMissingSteps; s++) assert(!t.update(stuck, page, 6000 + s));
        // The overlay open: untouched, and the second starts again when it closes.
        Context overlay;
        overlay.overlay_open = true;
        for (uint64_t s = 0; s < 3 * kMissingSteps; s++) assert(!t.update(stuck, overlay, 7000 + s));
        for (uint64_t s = 0; s < kMissingSteps; s++) assert(!t.update(stuck, none, 8000 + s));
        assert(t.update(stuck, none, 8000 + kMissingSteps));
        // touch (directional input off) or a Game Over menu: no cursor on the TV by design, untouched
        Reading touch = stuck;
        touch.directional = 0;
        assert(!focus_stuck(touch));
        Reading over = stuck;
        over.requested = 1;
        assert(!focus_stuck(over));
        // At most kMaxRestores a session.
        Restore cap;
        uint64_t s = 10000;
        for (int i = 0; i < Restore::kMaxRestores; i++) {
            while (!cap.update(stuck, none, s)) s++;
            s++;
        }
        for (uint64_t e = s + 10 * kMissingSteps; s < e; s++) assert(!cap.update(stuck, none, s));
    }
    {
        // The cause: the map's chart grid moves the focus to Link's square only from one of its own squares.
        uint32_t squares[49];
        for (uint32_t i = 0; i < 49; i++) squares[i] = 0x1986ED7C + 0x230 * i;
        // the map shown with its cursor on the chart (a square has the focus): the game's move runs
        assert(grid_may_move_focus(squares[0], squares, 49));
        assert(grid_may_move_focus(squares[48], squares, 49));
        // after a scene change with the map not shown: the game gave the focus to the menu's tab button (or an
        // item); the square doesn't take it (the reporter's log: a map square as the item menu's focus)
        assert(!grid_may_move_focus(0x19701AFC, squares, 49));
        assert(!grid_may_move_focus(0x1985B450, squares, 49));  // the map tab's chart icon
        // no focus (a sub window, an opening menu): left alone too
        assert(!grid_may_move_focus(0, squares, 49));
        // cells without an element (0) never match a focus
        uint32_t empty[49] = {};
        assert(!grid_may_move_focus(0x19701AFC, empty, 49));
        // "Fix original game bugs" off: the game's move always runs, as in the original
        assert(grid_may_move_focus(0x19701AFC, squares, 49, false));
        assert(grid_may_move_focus(0, squares, 49, false));
        assert(grid_may_move_focus(squares[3], squares, 49, false));
    }
    {
        // "Fix original game bugs" off: the safety net never gives the focus back (the original's stuck state stays)
        Reading stuck = reading(0, false, "::StateID_Blank");
        stuck.focus = 0x1987004C;
        Context off;
        off.fix_game_bugs = false;
        Restore restore;
        for (uint64_t s = 100; s < 100 + 20 * kMissingSteps; s++) assert(!restore.update(stuck, off, s));
        assert(restore.restores() == 0);
        // on again: the second after gives it back
        Context on;
        uint64_t s = 1000;
        while (!restore.update(stuck, on, s)) s++;
        assert(s == 1000 + kMissingSteps && restore.restores() == 1);
    }
    std::puts("menu_cursor_test: ok");
    return 0;
}
