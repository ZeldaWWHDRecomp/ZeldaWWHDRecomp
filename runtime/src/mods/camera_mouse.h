// Mouse movement is measured in points per camera logic step (30 Hz in every display mode).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mods::camera_mouse {
// HD procedure IDs from the item-aim initializers. 0x86..0x89 are boarding/sailing/paddling,
// not aiming; 0x8A..0x8D are telescope/boomerang/hookshot/bow on the boat.
inline constexpr bool aiming_item(uint32_t proc) {
    return proc == 0x76 || proc == 0x7D || proc == 0x80 || proc == 0x81 ||
           proc == 0x83 || proc == 0x84 || (proc >= 0x8A && proc <= 0x8D) ||
           proc == 0x94 || proc == 0x95;
}
// procShipCannon_init (023F1B10) selects 0x8E. Require the ship flag as well so stale
// procedure state cannot send camera movement into walking or ordinary boat steering.
inline constexpr bool cannon_aim(uint32_t proc, bool in_ship) { return in_ship && proc == 0x8E; }
inline float yaw_speed(float points, float sensitivity, double yaw_step) {
    if (!(yaw_step > 0.01)) yaw_step = 8.0;
    return static_cast<float>(-points * sensitivity / (0.92f * yaw_step));
}
inline float fallback_stick(float points) {
    return std::fmax(-1.0f, std::fmin(1.0f, points * 0.08f));
}

// First person and every item aim (dCamera_c::CalcSubjectAngle 02506964, issue #558). Per step the game turns the
// view by
//   yaw   = yaw_max * clamp(x * k, -1, 1)               degrees right, relative to Link's facing (Link turns with it)
//   pitch = pitch_max * clamp(m388 + y * k, -1, 1)      degrees down, absolute (m388: the pitch so far / pitch_max)
// where x, y are the GamePad's turn since the previous step x 30 (its gyro input; y negated while swimming, so that
// tilting the GamePad up still looks up), yaw_max, pitch_max the subject style's camera parameters 24 and 19, and k
// its parameter 21 (speed), lowered while the telescope or Picto Box zooms in (k x (1 - slow x zoom)). Both angles
// become the game's s16 units (65536 a turn) by truncation towards zero (cM_deg2s 02006694), and the camera rebuilds
// m388 from Link's body angle in whole units: each step loses the fraction of a unit (0.0055 degrees) of its turn.
//
// subject_step gives the x and y to add so that the view lands on the whole unit nearest to where the mouse has
// turned it so far: equal points give equal degrees in every view, exactly, slow or fast. The sub-unit rest (at most
// half a unit) and a flick beyond the game's yaw_max per step carry to the next step. zoom = k_eff / k: the game's
// own slow-down while zoomed in stays (as with the stick and the gyro); its limits (the pitch range, crawling, a low
// eye) still apply.
constexpr double kUnitsPerDegree = 65536.0 / 360.0;
struct SubjectStep {
    float x = 0, y = 0;                 // to add to the game's x and y (before its swim inversion of y)
    double rest_yaw = 0, rest_pitch = 0;  // units carried to the next step
};
// The value to give the game so that its truncation lands on the whole unit `to` (a value half a unit beyond it,
// away from zero, so that float rounding cannot change the result)
inline double landing(double to) { return to == 0 ? 0 : to + std::copysign(0.5, to); }
// right / down: degrees (down: the view looks down, after the player's inversion); rest_*: the previous step's rest;
// m388: the game's pitch so far (-1..1); k_eff: the speed the game uses this step (speed x zoom slow-down)
inline SubjectStep subject_step(double right, double down, double rest_yaw, double rest_pitch, double k_eff,
                                double yaw_max, double pitch_max, double m388, bool swim) {
    SubjectStep r;
    if (!(k_eff > 1e-6) || !(yaw_max > 1e-3) || !(pitch_max > 1e-3)) return r;  // not a subject style: nothing
    // yaw: a turn of its own each step, at most yaw_max
    const double want_yaw = right * kUnitsPerDegree + rest_yaw, max_yaw = std::floor(yaw_max * kUnitsPerDegree);
    const double to_yaw = std::clamp(std::nearbyint(want_yaw), -max_yaw, max_yaw);
    r.rest_yaw = want_yaw - to_yaw;
    // at the limit a little beyond it, so that the game's clamp (not float rounding) decides: exactly yaw_max
    const double fed_yaw = std::fabs(to_yaw) >= max_yaw ? std::copysign(yaw_max * kUnitsPerDegree * 1.001, to_yaw)
                                                        : landing(to_yaw);
    r.x = static_cast<float>(fed_yaw / kUnitsPerDegree / (yaw_max * k_eff));
    // pitch: absolute, from where the game is now; nothing carried past its range
    // the whole unit the game shows now (its m388 is that unit, or the half-unit landing value when not rebuilt)
    const double range = pitch_max * kUnitsPerDegree, now = m388 * range;
    const double shown = std::trunc(now + std::copysign(1e-3, now));
    const double want_pitch = shown + (swim ? -down : down) * kUnitsPerDegree + rest_pitch;  // swimming: as the gyro
    const double to_pitch = std::nearbyint(want_pitch);
    r.rest_pitch = std::fabs(to_pitch) < std::floor(range) ? want_pitch - to_pitch : 0;
    const double fed = (landing(to_pitch) - now) / kUnitsPerDegree / (pitch_max * k_eff);
    r.y = static_cast<float>(swim ? -fed : fed);
    return r;
}
// The drawn view follows those angles with an ease per step (camera parameter 20: 0.45 in R3 look and the item aims,
// 0.6 boomerang, 0.75 telescope / Picto Box). While the mouse looks around the view takes the angles at once.
inline constexpr double kMouseViewEase = 1.0;
}  // namespace mods::camera_mouse
