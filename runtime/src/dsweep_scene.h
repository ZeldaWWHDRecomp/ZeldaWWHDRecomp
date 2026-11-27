// D-sweep (dsweep.cpp): the title's scene changes, seen once per logic step (issue #686).
//
// A title sweep runs in the title's scene (stage sea_T), and the title plays its idle story intro about every 2200
// logic steps: a scene change with a fade (an overlap request, *101F36CC, up for about 250 steps), the intro, then the
// title again. Leaving the title releases its stage resources, among them the sea's Stage archive that the title
// holds (sea_Stage), and the release of a "Stage" archive (ResourceControlHD::deletePathRes) destroys every object
// heap and stage heap of the resource control. The sweep's sea LOD archives live in heaps made in those (seen in
// measurements: their heaps' vtables reset to the base heap's), so one the sweep held across the intro was released
// later on heaps destroyed already:
// "indirect branch to unknown address 00000000 (lr=02607610)" in deletePathRes (the heap's destroy slot, after the
// destructor reset its vtable). Models the sweep made from the title's own sea_Stage drew the released archive's data
// (a read of 0x28 in j3ddesc::submit, sea_Stage:vr_sky.bmd). It depended on when the intro came, so on how busy the
// machine was: a different sea archive each time.
//
// The sweep holds no archive across a scene change: while one is under way (and for the settle time after it, as
// after a stage change) it starts no new archive, and the one it has is given up (its models go, it is released as
// usual) and tried again after it. A gap in the steps the sweep sees (no actor draws: the intro) counts as a scene
// change too.
#pragma once
#include <cstdint>

namespace dsweep {

struct SceneGuard {
    uint64_t settle = 120;   // steps after a change before a new archive (the stage's light state settles)
    uint64_t gap = 30;       // steps without an update that count as a scene change (the intro draws no actor)
    bool pending = false;    // a scene change is under way
    bool seen = false;       // one has happened (end_step is valid)
    uint64_t end_step = 0;   // ... and ended at this step
    uint64_t last_step = 0;  // the last update
    bool any = false;        // there was an update before

    // once per logic step the sweep sees: the step and whether a scene change request is up
    void update(uint64_t step, bool request) {
        if (any && step - last_step > gap) seen = true, end_step = step;  // the steps between were someone else's
        any = true, last_step = step;
        if (request) {
            pending = true;
            return;
        }
        if (pending) pending = false, seen = true, end_step = step;
    }
    // no archive may be held or started at this step
    bool hold(uint64_t step) const { return pending || (seen && step - end_step < settle); }
};

}  // namespace dsweep
