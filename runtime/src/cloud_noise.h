// The clouds' tiling 64^3 RG8 noise (R: value-noise fBm, G: inverted Worley with cells of 8), generated
// here (no assets), shared by Metal and Vulkan so both renderers draw the same clouds.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cloud_noise {
constexpr int kSize = 64;
inline std::vector<uint8_t> rg8() {
    const int N = kSize;
    std::vector<uint8_t> data(size_t(N) * N * N * 2);
    auto hash = [](uint32_t x) {
        x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
        return x;
    };
    auto lattice = [&](int x, int y, int z, int period, uint32_t salt) {
        x = (x % period + period) % period, y = (y % period + period) % period, z = (z % period + period) % period;
        return (hash((uint32_t)(x + period * (y + period * z)) * 2654435761u + salt) & 0xFFFF) / 65535.0f;
    };
    auto vnoise = [&](float x, float y, float z, int period, uint32_t salt) {
        int ix = (int)std::floor(x), iy = (int)std::floor(y), iz = (int)std::floor(z);
        float fx = x - ix, fy = y - iy, fz = z - iz;
        auto sm = [](float t) { return t * t * (3 - 2 * t); };
        fx = sm(fx), fy = sm(fy), fz = sm(fz);
        float v = 0;
        for (int c = 0; c < 8; c++) {
            int dx = c & 1, dy = (c >> 1) & 1, dz = c >> 2;
            float w = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) * (dz ? fz : 1 - fz);
            v += w * lattice(ix + dx, iy + dy, iz + dz, period, salt);
        }
        return v;
    };
    const int cells = 8;
    std::vector<float> fp(cells * cells * cells * 3);
    for (int i = 0; i < cells * cells * cells; i++)
        for (int k = 0; k < 3; k++) fp[i * 3 + k] = (hash(i * 3 + k + 977) & 0xFFFF) / 65535.0f;
    for (int z = 0; z < N; z++)
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                float f = 0, amp = 0.5f, norm = 0;
                for (int o = 0, per = 4; o < 4; o++, per *= 2, amp *= 0.5f) {
                    float s = (float)per / N;
                    f += amp * vnoise(x * s, y * s, z * s, per, 31u * o);
                    norm += amp;
                }
                f /= norm;
                // Worley: distance to the nearest feature point (tiling)
                float cs = (float)N / cells, best = 9;
                int cx = (int)(x / cs), cy = (int)(y / cs), cz = (int)(z / cs);
                for (int dz = -1; dz <= 1; dz++)
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            int ox = cx + dx, oy = cy + dy, oz = cz + dz;
                            int wx = (ox + cells) % cells, wy = (oy + cells) % cells, wz = (oz + cells) % cells;
                            const float* q = &fp[(wx + cells * (wy + cells * wz)) * 3];
                            float px = (ox + q[0]) * cs, py = (oy + q[1]) * cs, pz = (oz + q[2]) * cs;
                            float ddx = px - x, ddy = py - y, ddz = pz - z;
                            best = std::min(best, (ddx * ddx + ddy * ddy + ddz * ddz) / (cs * cs));
                        }
                float wv = 1.0f - std::min(1.0f, std::sqrt(best));
                size_t i = (size_t(z) * N * N + size_t(y) * N + x) * 2;
                data[i] = (uint8_t)std::lround(std::clamp(f, 0.0f, 1.0f) * 255);
                data[i + 1] = (uint8_t)std::lround(wv * 255);
            }
    return data;
}
}  // namespace cloud_noise
