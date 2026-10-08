#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace mods {
inline float clamp_move_speed(float factor) {
    return std::isfinite(factor) ? std::clamp(factor, 1.25f, 4.f) : 1.5f;
}
inline float move_factor(bool enabled, uint32_t proc, uint32_t held, uint32_t button, float factor) {
    return enabled && (proc == 0x06 || proc == 0x37) && (held & button) ? clamp_move_speed(factor) : 1.f;
}
}
