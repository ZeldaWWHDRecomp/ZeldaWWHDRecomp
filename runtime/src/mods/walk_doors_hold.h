// SPDX-License-Identifier: MPL-2.0
// Walk-through doors (walk_doors.cpp): the far room's new actors wait as during the game's door scene.
//
// Opening a door clears the far room's "hidden" flag (dDoor_info_c::openInitCom), and the room's actors are created
// again (d_s_room.cpp objectSetCheck: a hidden room's actors are deleted, a shown room's are created). In the game this
// happens inside the door scene, where only the scene's staff move (dEvt_control_c::moveApproval): by the time the
// room's actors first run, every one of them has finished its create, and the ones that do not belong there (an
// Obj_Try whose chk_appear says no) are gone. Opened without the scene they would run at once, next to actors still
// being made or removed. In the Tower of the Gods hall this let the statue pads (Obj_Correct CrTrM1 / CrTrM2) see the
// "placed statue" stand-ins (Obj_Try Hmon1d / Hmon2d) before those removed themselves: the pads set switches 5 and 6,
// the gate and water-spout scenes (htetu1_open, hha_close) started with Link still in the side room, and hha_close
// never ended (issue #529).
//
// So the actors that appear in the far room after a walk-through opening do not run for a short time: at least
// kMinSteps logic steps, and as long as new ones keep appearing (kQuietSteps without a new one), at most kMaxSteps
// (the door scene itself lasts longer than that). Only actors made after the opening (process id above every id seen
// before it) whose room is the far room are held; Link, the door and everything already there run as usual.
#pragma once
#include <cstdint>
#include <unordered_set>

namespace mods {
class WalkDoorHold {
public:
    static constexpr uint32_t kMinSteps = 15, kQuietSteps = 5, kMaxSteps = 60;
    // a door opened by walk-through into `room` (its hidden flag was set): hold its new actors from `step` on
    void start(int room, uint32_t step) {
        room_ = room, from_ = step, last_new_ = step, floor_ = max_id_;
        held_.clear();
    }
    void reset() {
        room_ = -1, max_id_ = 0;
        held_.clear();
    }
    bool active(uint32_t step) {
        if (room_ < 0) return false;
        const uint32_t since = step - from_;
        if (since < kMinSteps || (step - last_new_ < kQuietSteps && since < kMaxSteps)) return true;
        room_ = -1;
        held_.clear();
        return false;
    }
    // a process about to run: true = it does not run this step. `actor_room` < -1 for a process that is not an actor
    // (or is Link, or a door being walked through)
    bool hold(uint32_t id, int actor_room, uint32_t step) {
        const bool on = active(step);
        if (!on || id <= floor_ || actor_room != room_) {
            if (id > max_id_ && (!on || actor_room != room_)) max_id_ = id;
            return false;
        }
        if (held_.insert(id).second) last_new_ = step;
        return true;
    }
    int room() const { return room_; }

private:
    int room_ = -1;
    uint32_t from_ = 0, last_new_ = 0, floor_ = 0, max_id_ = 0;
    std::unordered_set<uint32_t> held_;
};
}  // namespace mods
