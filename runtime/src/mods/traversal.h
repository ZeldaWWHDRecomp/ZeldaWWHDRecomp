// Traversal mods (issues #95, #96): faster ladders, ivy, ledge hanging, faster sidling, sailing speed per sail and
// automatic wind with the normal sail. The pure maths, so traversal_test can check it without the
// game; traversal.cpp owns the hooks, traversal_settings.cpp the live switches and factors.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mods::traversal {

// ---- factors: one dial per mod, 1x..3x; a fresh settings file starts at 1.5x ----
constexpr float kMinFactor = 1.f, kMaxFactor = 3.f, kDefaultFactor = 1.5f;
inline float clamp_factor(float f) { return std::isfinite(f) ? std::clamp(f, kMinFactor, kMaxFactor) : kDefaultFactor; }
// The factor a hook applies: exactly 1 (the stock path) while the mod is off.
inline float active_factor(bool on, float f) { return on ? clamp_factor(f) : 1.f; }

// ---- Link's procedures (daPy_lk_c::daPy_PROC; the WWHD ids match the GameCube ones) ----
constexpr uint32_t kProcWHideMove = 0x15;       // PROC_WHIDE_MOVE: sidling along a wall
constexpr uint32_t kProcLadderUpStart = 0x38;   // LADDER_UP_START, UP_END, DOWN_START, DOWN_END
constexpr uint32_t kProcLadderMove = 0x3C;      // LADDER_MOVE: one rung up or down
inline bool is_ladder_proc(uint32_t p) { return p >= kProcLadderUpStart && p <= kProcLadderMove; }
constexpr uint32_t kProcClimbUpStart = 0x3D;    // CLIMB_UP_START, DOWN_START, MOVE_UP_DOWN, MOVE_SIDE: ivy
constexpr uint32_t kProcClimbMoveSide = 0x40;
inline bool is_ivy_proc(uint32_t p) { return p >= kProcClimbUpStart && p <= kProcClimbMoveSide; }

// Ivy and ledge hanging (idea from GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0): their procedures
// take the clip rate from daPy_lk_c::getLadderMoveAnmSpeed (ivy up / down; ladders too),
// getClimbMoveAnmSpeed (ivy sideways) and getHangMoveAnmSpeed (along a ledge while hanging), both in
// their _init and on every step, and the clips move Link. The hooks scale what the getter returns.
// Ladders keep their own path (setFrameCtrl and procLadderMove above), so the shared ladder getter
// scales only in the ivy procedures and never compounds with it.
enum class AnmGetter { kLadder, kClimb, kHang };
inline float anm_getter_factor(AnmGetter g, uint32_t proc, float ivy, float hang) {
    switch (g) {
        case AnmGetter::kLadder: return is_ivy_proc(proc) ? ivy : 1.f;
        case AnmGetter::kClimb: return ivy;
        case AnmGetter::kHang: return hang;
    }
    return 1.f;
}

// How fast daPy_lk_c::setFrameCtrl should play an animation Link starts in procedure `proc`.
// Ladders move Link by the animation's root motion (posMove, m34C2 = 5), so a faster clip climbs
// faster with hands and feet in step. Sidling blends the foot motion with mNormalSpeed; the clip
// and the speed are scaled together (see traversal.cpp). Every other procedure: 1.
inline float anim_rate_factor(uint32_t proc, float ladder, float sidle) {
    if (is_ladder_proc(proc)) return ladder;
    if (proc == kProcWHideMove) return sidle;
    return 1.f;
}

// procLadderMove writes the stick-dependent rate into mFrameCtrlUnder[0] on every step while the
// rung animation runs (|rate| >= 0.01 on entry) and Link stays on the ladder. Only then is the
// stored rate the authored one that the mod scales; otherwise the procedure either left the
// running clip alone or started the next rung through procLadderMove_init (already scaled by
// setFrameCtrl), and scaling again would compound.
constexpr float kRateStopped = 0.01f;  // the procedure's own "the clip ended" threshold
inline bool ladder_rate_rewritten(float rate_on_entry, uint32_t proc_before, uint32_t proc_after) {
    return proc_before == kProcLadderMove && proc_after == kProcLadderMove && std::fabs(rate_on_entry) >= kRateStopped;
}

// Between two rungs the stock procedure spends one step with the clip stopped: it plays the footstep
// (setLadderFootSe, counting the timer m34D0 from 1 to 0) and only on the next step starts the next
// rung (changeLadderMoveProc). That rest step is a fixed cost per rung, so a faster clip alone would
// climb slower than its factor (8 steps per rung at 1x; 5 at a 2x clip, 1.6x). With the mod on the
// next rung starts in the step that played the footstep: 4 steps at 2x. This tells the hook that
// the step just run was that rest step.
inline bool ladder_rest_step(float rate_on_entry, int16_t timer_before, int16_t timer_after, uint32_t proc_before,
                             uint32_t proc_after) {
    return proc_before == kProcLadderMove && proc_after == kProcLadderMove && std::fabs(rate_on_entry) < kRateStopped &&
           timer_before > 0 && timer_after == timer_before - 1 && timer_after == 0;
}
// Steps per rung for a 10-frame rung clip at `rate` frames per step: the init step shows frame 0,
// the clip ends when it reaches its last frame, then the rest step (unless merged).
inline int ladder_steps_per_rung(float rate, bool merge_rest) {
    int steps = 1;
    for (float frame = 0.f; frame < 9.999f; frame = std::min(frame + rate, 9.999f)) ++steps;
    return merge_rest ? steps - 1 : steps;
}

// ---- the ship (daShip_c::procSteerMove 0247E854) ----
// The game's target speed is windPower = min(2 * wind power, 1) * windFactor(angle) * K, with K = 55
// for the normal sail and 100 for the Swift Sail. The mod multiplies that target by the factor of
// the sail in use; the initial push (1.2x target) and the slow approach follow. The jump height
// (speed.y = clamp(windPower, 15, 40)) keeps the stock value.
inline float sail_target(float stock_target, bool swift, float normal, float swift_factor) {
    return stock_target * (swift ? swift_factor : normal);
}

// The Swift Sail's wind: every steering step it calls dKyw_tact_wind_set(0, angle) with the
// boat's heading turned into the wind system's angle and rounded down to one of 8 directions:
// `subfic r12, y, 0x5000; rlwinm r0, r12, 0, 0, 18; extsh r4, r0` (0247E9EC..0247E9F8).
inline int16_t follow_wind_angle(int16_t shape_angle_y) {
    return (int16_t)(uint16_t)((uint32_t)(0x5000 - (int32_t)shape_angle_y) & 0xFFFFE000u);
}

}  // namespace mods::traversal
