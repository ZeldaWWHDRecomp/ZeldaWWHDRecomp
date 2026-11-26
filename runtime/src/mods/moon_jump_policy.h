#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mods::moon_jump_policy {
// The moon jump leaves Link alone (no new flight, a flight ends) while the game runs an event (dComIfGp_event_runCheck:
// talks, cutscenes, the lava / void-out restart) or while Link is in one of his damage or demo procedures
// (daPyProc_*, the same ids in the HD): issue #616, holding it as he fell into lava put him into a fall out of
// DEMO_LAVA_DAMAGE, whose step starts the restart, so the event it had ordered never ended (no control, no save).
constexpr uint32_t kProcDamage = 0x66, kProcElecDamage = 0x6C;  // DAMAGE .. POLY, LARGE, LAVA (Gohma) .. ELEC_DAMAGE
constexpr uint32_t kProcDemoFirst = 0xA9;                       // DEMO_TOOL: the demo procedures from here on
constexpr uint32_t kProcDemoLavaDamage = 0xB0;
inline bool leaves_alone(uint32_t proc, uint8_t event_mode) {
    return event_mode != 0 || (proc >= kProcDamage && proc <= kProcElecDamage) || proc >= kProcDemoFirst;
}
// Speeds use the game's 30 Hz units; dt (a fraction of one 30 Hz step, the game passes 1) scales
// only the turning rate.
struct Steering { int16_t facing; float x = 0, z = 0; };
inline Steering steer(int16_t facing, float stick_x, float stick_y,
                      float forward_x, float forward_z, float dt) {
    Steering result{facing};
    if (!std::isfinite(stick_x) || !std::isfinite(stick_y) ||
        !std::isfinite(forward_x) || !std::isfinite(forward_z) ||
        !std::isfinite(dt) || dt <= 0 || dt > 1) return result;
    stick_x = std::clamp(stick_x, -1.f, 1.f);
    stick_y = std::clamp(stick_y, -1.f, 1.f);
    const float magnitude = std::min(1.f, std::hypot(stick_x, stick_y));
    const float length = std::hypot(forward_x, forward_z);
    if (magnitude < .15f || !std::isfinite(length) || length < .001f) return result;
    forward_x /= length;
    forward_z /= length;
    const float x = forward_x * stick_y - forward_z * stick_x;
    const float z = forward_z * stick_y + forward_x * stick_x;
    constexpr float pi = 3.14159265358979323846f;
    const int target = int(std::lround(std::atan2(x, z) * (32768.f / pi)));
    // Explicit modular arithmetic avoids implementation-defined signed narrowing
    // at the -180/+180 degree boundary.
    int difference = (target - int(facing) + 32768) % 65536;
    if (difference < 0) difference += 65536;
    difference -= 32768;
    const int limit = std::max(1, int(std::lround(3000.f * dt)));
    int next = int(facing) + std::clamp(difference, -limit, limit);
    if (next > 32767) next -= 65536;
    if (next < -32768) next += 65536;
    result.facing = int16_t(next);
    const float angle = next * (pi / 32768.f);
    result.x = std::sin(angle) * 40.f * magnitude;
    result.z = std::cos(angle) * 40.f * magnitude;
    return result;
}
} // namespace mods::moon_jump_policy
