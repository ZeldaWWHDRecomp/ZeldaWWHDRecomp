// SPDX-License-Identifier: MPL-2.0
// The Settings overlay's game menu sounds (runtime/src/overlay/menu_sounds.h): which sound each menu event
// plays, the rate limits, and when nothing may play.
#include <cstdio>
#include <cstring>

#include "overlay/menu_sounds.h"

using namespace menu_sounds;

static int failures = 0;
#define CHECK(c)                                                                      \
    do {                                                                              \
        if (!(c)) {                                                                   \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);     \
            failures++;                                                               \
        }                                                                             \
    } while (0)

static Frame open_at(double t, int tab = 0) {
    Frame f;
    f.open = true, f.tab = tab, f.time = t;
    return f;
}

int main() {
    // the labels are the game's (as the item menu's settings page plays them)
    CHECK(!strcmp(label(Sound::Cursor), "SE_MENU_CURSOR"));
    CHECK(!strcmp(label(Sound::Change), "SE_MENU_OPTION_SWITCH"));
    CHECK(!strcmp(label(Sound::Decide), "SE_MENU_DECIDE_S"));
    CHECK(!strcmp(label(Sound::Cancel), "SE_MENU_CANCEL"));
    CHECK(!strcmp(label(Sound::Tab), "SE_MENU_TAB_SWITCH"));
    CHECK(!strcmp(label(Sound::Open), "SE_MENU_OPTION_IN"));
    CHECK(!strcmp(label(Sound::Close), "SE_MENU_OPTION_OUT"));
    CHECK(label(Sound::None) == nullptr);

    {  // opening, moving, choosing, changing, tabs, back, closing
        Tracker t;
        Frame closed;
        CHECK(t.step(closed) == Sound::None);              // closed from the start: nothing
        CHECK(t.step(open_at(1)) == Sound::Open);
        CHECK(t.step(open_at(1.02)) == Sound::None);       // nothing happened
        CHECK(t.step(open_at(1.03, 5)) == Sound::None);    // the tab Settings shows just after opening: no tab sound
        CHECK(t.step(open_at(1.04, 0)) == Sound::None);
        Frame f = open_at(1.1);
        f.moved = true;
        CHECK(t.step(f) == Sound::Cursor);
        f = open_at(1.12), f.moved = true;
        CHECK(t.step(f) == Sound::None);                   // the cursor repeating faster than kCursorGap
        f = open_at(1.2), f.moved = true;
        CHECK(t.step(f) == Sound::Cursor);
        f = open_at(1.3), f.activated = true;
        CHECK(t.step(f) == Sound::Decide);                 // a button
        f = open_at(1.4), f.activated = true, f.edited = true;
        CHECK(t.step(f) == Sound::Change);                 // a checkbox: its value changed
        f = open_at(1.45), f.edited = true;
        CHECK(t.step(f) == Sound::None);                   // a slider dragged: not every frame
        f = open_at(1.6), f.edited = true;
        CHECK(t.step(f) == Sound::Change);
        CHECK(t.step(open_at(1.7, 2)) == Sound::Tab);      // another tab (L / R, a click on it)
        Frame tab_click = open_at(1.8, 3);
        tab_click.activated = true;
        CHECK(t.step(tab_click) == Sound::Tab);            // the tab wins over the click
        f = open_at(1.9, 3), f.popups = 1, f.activated = true;
        CHECK(t.step(f) == Sound::Decide);                 // a list or dialog opened
        f = open_at(2.0, 3), f.popups = 0, f.back = true;
        CHECK(t.step(f) == Sound::Cancel);                 // ... closed by B / Esc
        f = open_at(2.1, 3), f.popups = 1;
        CHECK(t.step(f) == Sound::Decide);
        f = open_at(2.2, 3), f.popups = 0, f.activated = true;
        CHECK(t.step(f) == Sound::Decide);                 // ... closed by choosing in it
        f = open_at(2.3, 3), f.back = true;
        CHECK(t.step(f) == Sound::Cancel);                 // B that closes nothing else (a capture, the quit question)
        closed.time = 2.4, closed.back = true;
        CHECK(t.step(closed) == Sound::Close);             // Settings closed (B, Esc, the close button)
        closed.back = false, closed.time = 2.5;
        CHECK(t.step(closed) == Sound::None);
        closed.moved = closed.activated = true;
        CHECK(t.step(closed) == Sound::None);              // closed: nothing it sees plays
        CHECK(t.step(open_at(3)) == Sound::Open);
    }
    {  // opened on the first frame the tracker sees (a test switch, Settings at start): its opening sound
        Tracker t;
        CHECK(t.step(open_at(0, 4)) == Sound::Open);
        CHECK(t.step(open_at(0.1, 4)) == Sound::None);
    }
    {  // the gate
        Gate g;
        g.game_sound_ready = true;
        CHECK(audible(g));
        Gate off = g;
        off.option = false;
        CHECK(!audible(off));                               // the option off
        Gate back = g;
        back.focused = false;
        CHECK(!audible(back));                              // the window in the background
        Gate muted = g;
        muted.master_volume = 0;
        CHECK(!audible(muted));                             // muted
        Gate early = g;
        early.game_sound_ready = false;
        CHECK(!audible(early));                             // the game's sound system not up yet (boot)
    }
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("menu_sounds: ok\n");
    return 0;
}
