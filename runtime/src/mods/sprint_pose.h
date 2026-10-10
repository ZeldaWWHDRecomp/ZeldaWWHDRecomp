// Additive sprint and swim poses, composed with the game's current locomotion quaternion. No clip
// data.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mods::sprint {
constexpr uint32_t kStomach = 3, kChest = 4, kLeftArm = 6, kLeftElbow = 7,
                   kRightArm = 10, kRightElbow = 11, kHead = 15;

inline float weight(float ramp, float factor, float speed, float max_speed) {
    if (!std::isfinite(ramp) || !std::isfinite(factor) || !std::isfinite(speed) ||
        !std::isfinite(max_speed) || factor <= 1.f || speed <= 0.f || max_speed <= 0.f) return 0.f;
    const float boost = std::clamp((ramp - 1.f) / (factor - 1.f), 0.f, 1.f);
    return boost * boost * (3.f - 2.f * boost) * std::clamp(speed / max_speed, 0.f, 1.f);
}

// Local Z pitches the spine forward and bends both elbows towards the front of Link. The mirrored
// arms need opposite *phases*, not opposite bend signs. Phase comes from the actual run frame ctrl.
inline float angle_degrees(uint32_t joint, float phase) {
    const float swing = std::isfinite(phase) ? std::sin(phase * 6.28318530718f) : 0.f;
    switch (joint) {
    case kStomach: return 22.f;
    case kChest: return 8.f;
    case kHead: return -20.f;  // keep looking ahead rather than down at the ground
    case kLeftArm: return -10.f - 24.f * swing;
    case kRightArm: return -10.f + 24.f * swing;
    case kLeftElbow: return -40.f - 6.f * swing;
    case kRightElbow: return -40.f + 6.f * swing;
    default: return 0.f;
    }
}

// The crawl over the game's own swim cycle: a flatter torso and a much wider, phase-locked arm
// sweep. The left and right arms alternate over and under the water instead of pulling together,
// which is what reads as a crawl rather than the stock paddle.
inline float swim_angle_degrees(uint32_t joint, float phase) {
    const float swing = std::isfinite(phase) ? std::sin(phase * 6.28318530718f) : 0.f;
    switch (joint) {
    case kStomach: return 10.f;   // less lean: the swim body already lies flat
    case kChest: return 6.f;
    case kHead: return -16.f;
    case kLeftArm: return -12.f - 46.f * swing;
    case kRightArm: return -12.f + 46.f * swing;
    case kLeftElbow: return -22.f - 16.f * swing;
    case kRightElbow: return -22.f + 16.f * swing;
    default: return 0.f;
    }
}

struct Quaternion { float x, y, z, w; };
inline Quaternion rotate_local_z(Quaternion q, float degrees) {
    const float half = degrees * 0.00872664626f;
    const float s = std::sin(half), c = std::cos(half);
    return {q.x * c + q.y * s, q.y * c - q.x * s, q.z * c + q.w * s, q.w * c - q.z * s};
}
}  // namespace mods::sprint
