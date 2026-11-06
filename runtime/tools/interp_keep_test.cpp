// SPDX-License-Identifier: MPL-2.0
// Kept actors' put-back (runtime/src/interp_keep.h, issue #446): a draw on a pass without logic that
// hands state to the next execute is undone, but the word selecting an HD draw's GPU buffer set stays
// as the draw left it, so that no two consecutive draws fill the same buffers (the GPU may still be
// reading the previous pass's set). Simulates an actor with a sail packet over a 120 fps step
// (logic pass + three passes without logic) and the next steps.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/interp_keep.h"

namespace {
constexpr uint32_t kActor = 0x40000000, kWords = 0x400, kFirst = 0xF8 / 4;
constexpr uint32_t kHandOff = 0x200 / 4;  // a draw-to-execute hand-off (as the grass cutter's +0xA0C)
constexpr uint32_t kSet = 0x9F4 / 4;      // the sail packet's GPU set selector
uint32_t mem[kWords];

// the game's draw: fills the selected set and flips the selector (the HD code's word = (word == 0)),
// and moves a flag (the word before the hand-off) into the hand-off, clearing the flag
int draw() {
    const int set = (int)mem[kSet];
    mem[kSet] = mem[kSet] == 0;
    mem[kHandOff] = mem[kHandOff - 1];
    mem[kHandOff - 1] = 0;
    return set;
}

// one logic step of `passes` draws: the logic pass draws in place, every later pass is kept and put back
std::vector<int> step(int passes, bool keep_sets, interp::DrawSideWords& side) {
    std::vector<int> sets;
    mem[kHandOff - 1] = 5;   // the flag, set by the step's logic
    sets.push_back(draw());  // the logic pass: hand-off 5 for the next execute, as at 30 fps
    for (int p = 1; p < passes; p++) {
        uint32_t saved[kWords];
        memcpy(saved, mem, sizeof mem);
        sets.push_back(draw());
        if (keep_sets) side.add(kActor + 4 * kSet);
        std::vector<uint32_t> keep;
        side.indices_in(kActor, kWords, keep);
        interp::put_back_words(mem, saved, kWords, kFirst, keep);
        side.clear();
        assert(mem[kHandOff] == 5);  // the hand-off survives every pass without logic
    }
    return sets;
}
}  // namespace

int main() {
    interp::DrawSideWords side;
    // registration: aligned, unique, inside the actor only
    side.add(kActor + 4 * kSet);
    side.add(kActor + 4 * kSet);
    side.add(kActor + 4 * kSet + 2);
    side.add(kActor + 4 * kWords);
    side.add(kActor - 4);
    assert(side.size() == 3);  // (unaligned: dropped; outside the actor: kept, not used for it)
    std::vector<uint32_t> keep;
    side.indices_in(kActor, kWords, keep);
    assert(keep.size() == 1 && keep[0] == kSet);
    side.clear();
    assert(side.size() == 0);

    for (int passes : {2, 4, 8}) {  // 60, 120, 240 fps
        // as before the fix: the selector is put back too, and a later pass refills the set the pass
        // before it handed to the GPU
        memset(mem, 0, sizeof mem);
        bool reused = false;
        int last = -1;
        for (int s = 0; s < 3; s++)
            for (int set : step(passes, false, side)) reused |= set == last, last = set;
        assert(reused);
        // fixed: every draw fills the other set
        memset(mem, 0, sizeof mem);
        last = -1;
        for (int s = 0; s < 3; s++)
            for (int set : step(passes, true, side)) {
                assert(set != last);
                last = set;
            }
    }
    puts("interp_keep: GPU buffer set selectors stay as the draws left them, hand-offs are put back PASS");
}
