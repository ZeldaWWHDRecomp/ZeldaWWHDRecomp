// Save-state conveniences (save_convenience.h): slots, At start, Auto save timer, settings file.
// Pure logic; no game files.
#include "save_convenience.h"
#include <cassert>

using namespace ss::convenience;

int main() {
    // own slots: never the player's 1..5 or crash recovery's 101..103
    assert(is_convenience(kLastSessionSlot) && !is_auto_save(kLastSessionSlot));
    for (int s = 111; s <= 113; s++) assert(is_auto_save(s) && is_convenience(s));
    for (int s : {0, 1, 5, 101, 102, 103, 109, 114}) assert(!is_convenience(s));

    // At start: default intro; round trip; unknown text is the default
    assert(parse_start("") == StartChoice{} && parse_start("intro") == StartChoice{});
    assert(parse_start("last").what == Start::kLastSession);
    assert(parse_start("slot3").what == Start::kSlot && parse_start("slot3").slot == 3);
    for (const char* bad : {"slot0", "slot6", "slot", "slot12", "LAST", "x"}) assert(parse_start(bad) == StartChoice{});
    for (const char* s : {"intro", "last", "slot1", "slot5"}) assert(format_start(parse_start(s)) == s);

    // Auto save interval: off / 2 / 5 / 10 only
    assert(parse_minutes("2") == 2 && parse_minutes("10") == 10 && parse_minutes("0") == 0);
    for (const char* bad : {"3", "-2", "", "5x", "60"}) assert(parse_minutes(bad) == 0);

    // the timer
    AutoSaveTimer t;
    assert(!t.due(0, 0, true));            // off
    assert(!t.due(0, 2, false));           // not in gameplay
    assert(!t.due(10, 2, true));           // gameplay begins: the interval starts now
    assert(!t.due(129, 2, true));
    assert(t.due(130, 2, true) && t.next == 1);
    assert(!t.due(131, 2, true));          // one request at a time
    t.done(131, true);
    assert(t.next == 2 && !t.due(200, 2, true) && t.due(251, 2, true));
    t.done(251, false);                    // refused (cutscene): retried after a short while
    assert(t.next == 2 && !t.due(260, 2, true) && t.due(266, 2, true));
    t.done(266, true);
    assert(t.next == 3);
    assert(t.due(386, 2, true)); t.done(386, true);
    assert(t.next == 1);                   // rotates through the three slots, oldest first
    assert(!t.due(400, 2, false));         // leaving gameplay resets the interval
    assert(!t.due(401, 2, true) && !t.due(520, 2, true) && t.due(521, 2, true));
    t.done(521, true);
    assert(!t.due(600, 0, true) && !t.due(700, 5, true) && t.due(1000, 5, true));  // switched off and on

    // the settings file: round trip, unknown and bad lines ignored, defaults otherwise
    Options o;
    assert(parse_options("").start == StartChoice{} && parse_options("").autosave_minutes == 0);
    o.start = {Start::kSlot, 4};
    o.autosave_minutes = 5;
    Options r = parse_options(format_options(o));
    assert(r.start == o.start && r.autosave_minutes == 5);
    r = parse_options("junk\r\nstart=last\r\nautosave=7\r\nother=1");
    assert(r.start.what == Start::kLastSession && r.autosave_minutes == 0);

    // Last session: on for players, off for scripted runs unless asked for
    assert(last_session_enabled(nullptr, false) && !last_session_enabled(nullptr, true));
    assert(!last_session_enabled("0", false) && last_session_enabled("1", true) && last_session_enabled("", false));
    return 0;
}
