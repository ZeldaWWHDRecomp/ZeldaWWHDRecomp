// Frame interpolation of grass, tree and flower animation slots: a plant's own slot (hit, cut,
// walked through) blends while the same plant uses it on both steps; a slot that was free, or was
// taken over by another plant, is drawn at the new step (runtime/src/interp_slots.h, issue #251).
#include "../src/interp_slots.h"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace interp::slots;

int main() {
    // owners: the first active data entry per slot; shared slots (0-7) are nobody's
    {
        struct Plant { bool active; int slot; };
        const std::vector<Plant> plants = {{true, 3}, {true, 9}, {false, 10}, {true, 9}, {true, 71}, {true, 72}};
        std::vector<int32_t> own;
        owners((int)plants.size(), 72, [&](int i) { return plants[i].active; }, [&](int i) { return plants[i].slot; }, own);
        assert(own.size() == 72);
        assert(own[3] == -1);   // shared sway slot
        assert(own[9] == 1);    // first plant using it
        assert(own[10] == -1);  // inactive plant
        assert(own[71] == 4);
        // (slot 72 is out of range: ignored)
    }
    // the shared sway slots always blend while the record is valid
    for (int i = 0; i < kShared; i++) {
        assert(blend_slot(i, true, 0, 0, -1, -1));
        assert(!blend_slot(i, false, 1, 1, -1, -1));
    }
    // a cut tree's own slot: the same tree on both steps -> blended (its falling top moves on every
    // drawn frame)
    assert(blend_slot(20, true, 1, 1, 5, 5));
    // taken this step (free on the previous one): drawn at the new step
    assert(!blend_slot(20, true, 0, 1, -1, 5));
    // freed and taken by another plant within one step: drawn at the new step
    assert(!blend_slot(20, true, 1, 1, 5, 7));
    // released this step (the plant is back on a shared slot): nothing to blend
    assert(!blend_slot(20, true, 1, 0, 5, -1));
    // a record of another packet or an older step
    assert(!blend_slot(20, false, 1, 1, 5, 5));
    printf("interp_slots: all tests passed\n");
    return 0;
}
