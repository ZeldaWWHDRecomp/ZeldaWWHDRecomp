#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gfx::depth_peek {
inline uint32_t from_float(float z) {
    if (!std::isfinite(z)) return 0;
    return z >= 1.0f ? 0xFFFFFFu : uint32_t(std::clamp(z, 0.0f, 1.0f) * 16777215.0f);
}
inline int pixel(uint32_t coordinate, uint32_t extent, uint32_t guestExtent) {
    return int(std::clamp(double(int32_t(coordinate)) * extent / guestExtent, 0.0, double(extent - 1)));
}
uint64_t next_ticket();
void publish(uint64_t ticket, const std::vector<uint32_t>& destinations, const std::vector<uint32_t>& depths);
} // namespace gfx::depth_peek
