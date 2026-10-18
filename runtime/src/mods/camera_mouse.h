// Mouse movement is measured in points per camera logic step (30 Hz in every display mode).
#pragma once
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
inline float yaw_speed(float points, float sensitivity, double yaw_step) {
    if (!(yaw_step > 0.01)) yaw_step = 8.0;
    return static_cast<float>(-points * sensitivity / (0.92f * yaw_step));
}
inline float fallback_stick(float points) {
    return std::fmax(-1.0f, std::fmin(1.0f, points * 0.08f));
}
}  // namespace mods::camera_mouse
