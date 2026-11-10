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
    uint32_t focus = 0;        // the focused element (sEventMgr +0x70), 0 none
    bool focus_on_arrow = false;  // its cursor pane is the settings entry arrow's (game_settings_page.cpp)
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

// Issue #336 stop-gap, until the trigger is found: the item menu up with its focus on an element that isn't
// linked into the menu's event manager (so the focus cursor stays in ::StateID_Blank and the menu doesn't
// move) is the stuck state players report. After a second of it, hud_gate.cpp logs the [menu-cursor] line
// naming the element (that line is what is to find the trigger) and then gives the focus back to the menu's
// default element through the game's own setter. Never in the normal cases:
//   the chart grid (map tab): the fixed cursor (active 1) on a grid cell, never linked: shown() is true
//   no focus at all (a sub window or an opening/closing menu clears it): not this state, left alone
//   the settings entry arrow focused, or its page or slide out (game_settings_page.cpp)
//   the overlay open
//   tab switches, opening and closing: their focus is unlinked for a step or two, and the element must be
//   the same one for the whole second
struct Context {
    bool overlay_open = false;   // the port's overlay (Settings, Esc)
    bool settings_page = false;  // game_settings_page::page_open(): the entry arrow, page or slide has the controller
};

// The reading is the stuck state the safety net acts on (the context aside).
inline bool focus_stuck(const Reading& r) {
    return r.directional == 1 && r.requested == 0 && r.active == 0 && r.focus != 0 && !r.focus_linked &&
           !r.focus_on_arrow;
}

// Feeds one reading per logic step of an open item menu; says when to give the focus back (true once per
// stuck element: should the game's state take it again, the next second gives it back again, at most
// kMaxRestores a session). reset() when the menu is not up.
class Restore {
public:
    static constexpr int kMaxRestores = 20;
    bool update(const Reading& r, const Context& c, uint64_t step) {
        if (!focus_stuck(r) || c.overlay_open || c.settings_page) { reset(); return false; }
        if (r.focus != focus_ || !since_) focus_ = r.focus, since_ = step ? step : 1;
        if (step < since_ + kMissingSteps || restores_ >= kMaxRestores) return false;
        restores_++;
        since_ = step ? step : 1;  // another second before the next one
        return true;
    }
    void reset() { since_ = 0, focus_ = 0; }
    int restores() const { return restores_; }
private:
    uint64_t since_ = 0;
    uint32_t focus_ = 0;
    int restores_ = 0;
};

// The element the focus goes back to: the menu's default for its window (hdMenuMain_c mChildren[0x11] on the
// item window, mChildren[0x28] on the other one, as the game focuses them when a window slides in), else the
// element the focus was last on in this opening; 0 (none) unless it is linked into the menu's event manager.
inline uint32_t restore_target(uint32_t default_focus, bool default_linked, uint32_t last_good, bool last_linked) {
    if (default_focus && default_linked) return default_focus;
    if (last_good && last_linked) return last_good;
    return 0;
}

}  // namespace mods::menu_cursor
