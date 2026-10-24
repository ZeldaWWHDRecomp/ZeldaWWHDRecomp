// One-shot "not drawn this frame" requests on actors, across the passes of a logic step.
//
// Game logic can hide an actor for a single frame by setting fopAcStts_NODRAW_e in its actor status
// (fopAc_ac_c +0x2E0, bit 0x01000000). fopAc_Draw (025D4654) honours the bit and clears it after
// every draw, so the request has to be made again by the next step's logic. The talk camera does
// this on every step of an over-the-shoulder shot: the listener, usually Link, is hidden
// (dCamera_c::talktoCamera, talkShoulder).
// With frame interpolation and true 60 the in-between (hold) passes draw without logic: nothing sets
// the bit again after the logic pass's draw consumed it, so the actor was drawn on every in-between
// frame and hidden on the logic frames - it flickered at 60/120/240 fps (issue #126).
// The bookkeeping here gives every pass of a step the visibility the step's logic asked for: the
// logic (full) pass records which actors it drew with the bit set, and the hold passes of the same
// step set it again before their draw (which then clears it, as on the logic pass). Requests a hold
// pass makes itself (true 60's 60 Hz processes) are kept as they are.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace interp {

class OneShotNoDraw {
public:
    static constexpr uint32_t kStatus = 0x2E0;      // fopAc_ac_c::actor_status (WWHD)
    static constexpr uint32_t kNoDraw = 0x01000000;  // fopAcStts_NODRAW_e

    // fopAc_Draw of `actor` with actor status `status` on a pass of logic step `step` (a hold pass
    // when `hold`): returns the status the draw must see
    uint32_t on_draw(uint32_t actor, uint32_t status, bool hold, uint64_t step) {
        if (!hold) {
            if (step != step_) {
                step_ = step;
                hidden_.clear();
            }
            if ((status & kNoDraw) && std::find(hidden_.begin(), hidden_.end(), actor) == hidden_.end())
                hidden_.push_back(actor);
            return status;
        }
        if (step == step_ && std::find(hidden_.begin(), hidden_.end(), actor) != hidden_.end())
            return status | kNoDraw;
        return status;
    }
    // The draw hook's order on a pass (interp.cpp, fopAc_Draw): an actor the step's logic pass did not
    // draw is saved first (`keep`, when `kept`: actor_keep, put back after the pass has been painted),
    // then the step's request is made again (when `active`). Saved after it, the put-back handed the
    // request on to the next logic pass as if that step's logic had made it: the next hold passes
    // made it again, were put back with it again, and so on, so an actor the logic hid once stayed
    // hidden (issue #283: Link after a conversation). Returns the status the draw must see.
    template <class Keep>
    uint32_t before_draw(uint32_t actor, uint32_t status, bool hold, uint64_t step, bool active, bool kept, Keep&& keep) {
        if (kept) keep();
        return active ? on_draw(actor, status, hold, step) : status;
    }
    // nothing recorded is valid any more (a save state was loaded, interpolation was switched)
    void reset() {
        step_ = ~0ull;
        hidden_.clear();
    }
    size_t hidden_count() const { return hidden_.size(); }

private:
    uint64_t step_ = ~0ull;
    std::vector<uint32_t> hidden_;  // actors the step's logic pass drew with the bit set
};

}  // namespace interp
