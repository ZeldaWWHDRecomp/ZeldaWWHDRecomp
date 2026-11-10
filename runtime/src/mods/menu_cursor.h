// Issue #336: when the item menu's selection cursor is missing. Pure rules, so menu_cursor_test can check
// them without the game; hud_gate.cpp (menu_cursor_watch) reads the game's state once per frame.
//
// The game draws a cursor of its cursor manager (sCursorMgrInstance +0x18: 0 the focus cursor, 1 the fixed
// cursor) while its directional input is on (sInputOptions +0x14), no Game Over menu is requested
// (sIngameUi +0x23D) and the cursor is in its LoopAll / InAll state. The focus cursor follows the event
// manager's focus (sEventMgr +0x70) only while that element is linked into the event manager
// (hdScreenCursor_c::blankExecute / loopAllExecute: its +0xC is the manager). The fixed cursor belongs to the
// map tab's chart grid, whose cells are never linked: a grid cell as the focus is the map's normal state there.
#pragma once
#include <cstdint>
#include <string>

namespace mods::menu_cursor {

struct Reading {
    int directional = -1;      // the game's directional input (1 on)
    bool focus_linked = false;  // the focus is an element of the menu's event manager
    int active = -1;           // the active cursor: 0 focus, 1 fixed
    std::string state;         // that cursor's state name
    int requested = 0;         // mMenuRequested (Game Over)
};

inline bool shown(const Reading& r) {
    const bool drawn = r.state == "::StateID_LoopAll" || r.state == "::StateID_InAll";
    if (r.directional != 1 || r.requested != 0 || !drawn) return false;
    return r.active == 1 || (r.active == 0 && r.focus_linked);
}

// Missing for this many logic steps in a row (1 s) before a line says so: an opening menu, a page switch or a
// sub window has its focus unlinked for a step or two.
constexpr uint64_t kMissingSteps = 30;

// Feeds one reading per logic step of an open item menu; says when the cursor has been missing for
// kMissingSteps (Lost) and when it is back after that (Back). reset() when the menu is not up.
class Watch {
public:
    enum class Change { None, Lost, Back };
    Change update(bool is_shown, uint64_t step) {
        if (is_shown) {
            since_ = 0;
            if (!lost_) return Change::None;
            lost_ = false;
            return Change::Back;
        }
        if (!since_) since_ = step ? step : 1;
        if (lost_ || step < since_ + kMissingSteps) return Change::None;
        lost_ = true;
        return Change::Lost;
    }
    // The menu closed: a cursor still lost stays reported as lost, so the next opening tells whether it is back
    void reset() { since_ = 0; }
    uint64_t since() const { return since_; }
    bool lost() const { return lost_; }
private:
    uint64_t since_ = 0;
    bool lost_ = false;
};

}  // namespace mods::menu_cursor
