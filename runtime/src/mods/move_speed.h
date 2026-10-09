// Run/swim speed mod: the pure maths, so move_speed_test can check it without the game.
// mods.cpp owns the live state (button, mode, stamina, ramp) and calls these per logic step.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mods {

// How the boost is engaged. Toggle is what the README already promised ("hold L3 (or toggle)").
enum class MoveMode { kHold, kToggle };

constexpr uint32_t kProcMove = 0x06;      // daPy_PROC PROC_MOVE
constexpr uint32_t kProcSwimMove = 0x37;  // daPy_PROC PROC_SWIM_MOVE
inline bool is_move_proc(uint32_t proc) { return proc == kProcMove || proc == kProcSwimMove; }

// 1 disables the boost in that state; at exactly 1 the 023FD39C site keeps the stock path.
inline float clamp_factor(float f) { return std::isfinite(f) ? std::clamp(f, 1.f, 4.f) : 1.5f; }
// seconds of boosting on a full bar; 0 = no limit
inline float clamp_stamina_seconds(float s) { return std::isfinite(s) ? std::clamp(s, 0.f, 60.f) : 4.f; }

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

}  // namespace mods
