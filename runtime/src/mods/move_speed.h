// Run/swim speed mod: the pure maths, so move_speed_test can check it without the game.
// mods.cpp owns the live state (button, mode, stamina, ramp) and calls these per logic step.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace mods {

// How the boost is engaged. Toggle is what the README already promised ("hold L3 (or toggle)").
enum class MoveMode { kHold, kToggle };

// Native keeps the game's clip; dash replaces its walk portion at low speeds (ANM_DASH is already
// used at full speed). Sprint adds a lean and arm drive to the native cycle, without replacing art.
enum class MoveAnim { kNative, kDash, kSprint };
constexpr uint32_t kAnmWalk = 1, kAnmDash = 2;  // daAlink_ANM ids, d_a_player_main_02.cpp:3555-3600
constexpr uint32_t kAnmSwim = 0x83;            // ANM_SWIMING, the forward surface swim cycle
inline uint32_t boost_anim(uint32_t anm, MoveAnim mode, bool boosting) {
    return boosting && mode == MoveAnim::kDash && anm == kAnmWalk ? kAnmDash : anm;
}
inline const char* move_anim_label(MoveAnim a) {
    return a == MoveAnim::kSprint ? "sprint" : a == MoveAnim::kDash ? "dash" : "native";
}
inline bool valid_move_anim_id(std::string_view id) { return id == "native" || id == "dash" || id == "sprint"; }
inline MoveAnim move_anim_from_id(std::string_view id) {
    return id == "sprint" ? MoveAnim::kSprint : id == "dash" ? MoveAnim::kDash : MoveAnim::kNative;
}

constexpr uint32_t kProcMove = 0x06;      // daPy_PROC PROC_MOVE
constexpr uint32_t kProcSwimMove = 0x37;  // daPy_PROC PROC_SWIM_MOVE
constexpr uint32_t kProcFrontRoll = 0x1E; // daPy_PROC PROC_FRONT_ROLL
inline bool is_move_proc(uint32_t proc) { return proc == kProcMove || proc == kProcSwimMove; }

// The defaults a fresh settings file gets when the mod is switched on.
constexpr float kDefaultLandFactor = 1.50f, kDefaultSwimFactor = 1.25f;
constexpr float kDefaultStaminaSeconds = 5.f, kDefaultCooldownSeconds = 3.f;

// 1 disables the boost in that state; at exactly 1 the 023FD39C site keeps the stock path.
inline float clamp_factor(float f) { return std::isfinite(f) ? std::clamp(f, 1.f, 4.f) : kDefaultLandFactor; }
// seconds of boosting on a full bar; 0 = no limit
inline float clamp_stamina_seconds(float s) { return std::isfinite(s) ? std::clamp(s, 0.f, 60.f) : kDefaultStaminaSeconds; }
// the bar's refill time, i.e. the wait before the boost can run again; 0 = none
inline float clamp_cooldown_seconds(float s) { return std::isfinite(s) ? std::clamp(s, 0.f, 60.f) : kDefaultCooldownSeconds; }

// The factor this step moves towards, before the ramp. `boosting` already folds in the button,
// the mode and the stamina; land/swim are the two dials.
inline float move_target(bool boosting, uint32_t proc, float land, float swim) {
    if (!boosting) return 1.f;
    if (proc == kProcMove) return clamp_factor(land);
    if (proc == kProcSwimMove) return clamp_factor(swim);
    return 1.f;
}

// Exponential approach: closes 1-exp(-dt/tau) of the remaining distance per step, so the curve is
// the same at 30 and 60 Hz. Snaps when close, so "not boosting" settles on exactly 1 and the site
// keeps its stock path (and all FP bits) as before.
inline float ramp_towards(float current, float target, float tau, float dt) {
    if (!(tau > 0.f) || !(dt > 0.f)) return target;
    const float next = current + (target - current) * (1.f - std::exp(-dt / tau));
    return std::fabs(next - target) < 1e-3f ? target : next;
}

// How fast the locomotion animation plays under a boost: the same factor as the movement, so the
// legs keep up with the ground instead of skating. 1 leaves the authored rate alone.
inline float animation_factor(float ramp) { return ramp == 1.f ? 1.f : clamp_factor(ramp); }

// One step of the bar. `dt` in seconds, `seconds` of boosting on a full bar (0 = unlimited).
// Both snap on the ends: accumulating dt/seconds in binary floats never lands exactly on 0 or 1,
// and the exhausted/refilled transitions must be exact.
inline float drain_stamina(float s, float seconds, float dt) {
    if (!(seconds > 0.f)) return 1.f;
    const float next = s - dt / seconds;
    return next < 1e-4f ? 0.f : std::min(next, 1.f);
}
inline float refill_stamina(float s, float seconds, float dt) {
    if (!(seconds > 0.f)) return 1.f;
    const float next = s + dt / seconds;
    return next > 1.f - 1e-4f ? 1.f : std::max(next, 0.f);
}

// The cooldown: how long the emptied bar takes to come back, i.e. the wait before the boost can run
// again. It is the bar's refill time, so both dials mean something (6 s of boost, 3 s of cooldown =
// a 9 s cycle); 0 refills at once, so the boost is always available.
inline float refill_stamina_over(float s, float cooldown_seconds, float dt) {
    return cooldown_seconds > 0.f ? refill_stamina(s, cooldown_seconds, dt) : 1.f;
}

// "Roll, then hold A": a boost button choice next to L3/R3/L/R/ZL/ZR. Not a controller button: a
// value no VPAD button uses, so it saves and restores like the others (mod.move-speed.button).
constexpr uint32_t kMoveButtonRollA = 0x80000000u;
constexpr uint32_t kButtonA = 0x8000u;  // VPAD A (input::kA)
// A fresh A press: held this many logic steps at most when the roll begins (the game rolls on the
// press, a step or two later), so A held from before (a talk, a pick-up) never counts.
constexpr unsigned kRollPressSteps = 4;

// The A choice's state machine, one call per logic step with A and Link's procedure. A while
// running rolls exactly as the game does; if A is still held when that roll ends in running again,
// the boost runs until A is let go (or he stops running). Only a roll started from running by a
// fresh press counts: no roll from standing, A held from before, or a roll in which A was let go.
// Land only: there is no roll in water, so swimming never boosts with this choice.
struct RollHoldA {
    uint32_t prev_proc = 0xFFFFFFFFu;
    unsigned a_steps = 0;   // steps A has been held without a break
    bool rolling = false;   // a qualifying roll is under way with A still held
    bool active = false;    // the boost is held
    bool step(bool a, uint32_t proc) {
        a_steps = a ? std::min(a_steps + 1, 1000u) : 0;
        if (proc == kProcFrontRoll) {
            if (prev_proc != kProcFrontRoll)  // the roll begins: from a run, on a fresh press
                rolling = prev_proc == kProcMove && a && a_steps <= kRollPressSteps;
            if (!a) rolling = false;
            active = false;
        } else {
            if (rolling && proc == kProcMove && a) active = true;  // the roll ended in running, A held
            rolling = false;
            if (!a || proc != kProcMove) active = false;
        }
        prev_proc = proc;
        return active;
    }
    void reset() { *this = RollHoldA{}; }
};

// The boost button choices by name (WWHD_MOD_MOVE_BUTTON, the test override): L3, R3, L, R, ZL,
// ZR or roll-a; 0 for anything else.
inline uint32_t move_button_from_id(std::string_view id) {
    if (id == "L3") return 0x00040000u;
    if (id == "R3") return 0x00020000u;
    if (id == "L") return 0x0020u;
    if (id == "R") return 0x0010u;
    if (id == "ZL") return 0x0080u;
    if (id == "ZR") return 0x0040u;
    if (id == "roll-a") return kMoveButtonRollA;
    return 0;
}

// Whether the boost's bar wants to be on screen this step (before its one-second hold and fade, and
// before the game's menus, events and messages hide it: hud_visibility.h). Only a limited boost has
// a bar to show: with no limit (0 s) there is nothing to run out, and "Show boost bar" off hides it
// for good. The boost itself does not depend on the bar.
inline bool move_bar_wanted(bool bar_setting, float limit_seconds, bool boosting, float stamina) {
    if (!bar_setting || !(limit_seconds > 0.f)) return false;
    return boosting || stamina < 1.f;
}

}  // namespace mods
