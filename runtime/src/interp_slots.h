// Frame interpolation of the grass, tree and flower animation slots, without game state
// (interp_fx.cpp sway_calc; runtime/tools/interp_slots_test.cpp, ctest interp_slots).
//
// Each packet (dGrass_packet_c, dTree_packet_c, dFlower_packet_c) keeps an array of animation slots.
// Slots 0-7 are the shared wind sway, written by the packet's calc every step and used by every
// plant at rest. A plant that is hit, cut or walked through takes a slot of its own (newAnm: the
// first slot >= 8 whose state byte is 0), plays its hit animation there (grass and flowers bend
// back, a tree shakes, or its cut top falls and tilts) and returns to a shared slot at the end
// (state byte 0 again). Those slots are logic too (the calc runs in the world-system block, once per
// step), and were drawn at 30 fps (issue #251: the falling tops of cut trees).
// A slot of its own is blended between two steps only when the same plant used it on both: a slot
// that was free on the previous step, or that another plant took over in between, is drawn at the
// new step (never blended from another plant's animation). The shared slots always blend.
#pragma once
#include <cstdint>
#include <vector>

namespace interp::slots {

constexpr int kShared = 8;  // slots 0-7: the shared wind sway

// The plant using each slot: index of the first data entry (in the packet's data array) whose
// animation index is that slot, -1 for none. `active(i)` and `slot(i)` read data entry i.
template <class Active, class Slot>
void owners(int entries, int slots, Active&& active, Slot&& slot, std::vector<int32_t>& out) {
    out.assign(slots, -1);
    for (int i = 0; i < entries; i++) {
        if (!active(i)) continue;
        const int s = slot(i);
        if (s >= kShared && s < slots && out[s] < 0) out[s] = i;
    }
}

// Is slot `i` blended between the recorded step and this one? `valid`: the record is of this packet
// and of the previous step. A slot of its own blends only while in use (state byte != 0) on both
// steps by the same plant.
inline bool blend_slot(int i, bool valid, uint8_t prev_state, uint8_t state, int32_t prev_owner, int32_t owner) {
    if (!valid) return false;
    if (i < kShared) return true;
    return prev_state != 0 && state != 0 && owner >= 0 && prev_owner == owner;
}

}  // namespace interp::slots
