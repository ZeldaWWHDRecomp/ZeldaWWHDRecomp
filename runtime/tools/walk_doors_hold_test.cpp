// SPDX-License-Identifier: MPL-2.0
// walk_doors_hold.h: which processes wait after a walk-through door shows a hidden room (issue #529)
#include "mods/walk_doors_hold.h"
#include <cassert>
#include <cstdint>
int main() {
    using mods::WalkDoorHold;
    WalkDoorHold h;
    // nothing held before an opening; the highest id seen so far is noted
    for (uint32_t id = 1; id <= 100; id++) assert(!h.hold(id, 0, 10));
    h.start(0, 1000);
    // actors already there (ids up to 100) run, whatever their room
    assert(!h.hold(50, 0, 1000) && !h.hold(100, 0, 1001));
    // a new actor of the shown room waits; one of another room, Link / the door / non-actors (-2) run
    assert(h.hold(101, 0, 1001));
    assert(!h.hold(102, 3, 1001) && !h.hold(103, -2, 1001) && !h.hold(104, -1, 1001));
    // at least kMinSteps steps
    assert(h.hold(101, 0, 1000 + WalkDoorHold::kMinSteps - 1));
    // new actors keep appearing: held kQuietSteps past the last one
    const uint32_t late = 1000 + WalkDoorHold::kMinSteps - 1;
    assert(h.hold(105, 0, late));
    assert(h.hold(101, 0, late + WalkDoorHold::kQuietSteps - 1));
    assert(!h.hold(101, 0, late + WalkDoorHold::kQuietSteps));
    assert(h.room() < 0);  // over
    // a room that keeps making actors is released after kMaxSteps
    h.start(5, 2000);
    for (uint32_t s = 0; s < WalkDoorHold::kMaxSteps; s++) assert(h.hold(200 + s, 5, 2000 + s));
    assert(!h.hold(999, 5, 2000 + WalkDoorHold::kMaxSteps));
    // released actors are known now: a later opening holds only newer ones
    for (uint32_t id = 200; id < 260; id++) assert(!h.hold(id, 5, 3000));
    h.start(5, 4000);
    assert(!h.hold(259, 5, 4000) && h.hold(1000, 5, 4000));
    // a save state load forgets everything
    h.reset();
    assert(!h.hold(2000, 5, 4001) && h.room() < 0);
    return 0;
}
