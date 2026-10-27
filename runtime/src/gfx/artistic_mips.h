// SPDX-License-Identifier: MPL-2.0
// Textures whose mip levels are drawn by hand rather than downscaled ("artistic" mips): the game picks a level by
// distance on purpose (the Tower of the Gods effect sprite at 2EC6C000, a BC5 sampled with bias -6, whose levels
// 6-8 are no downscales). At a higher internal resolution the GPU picks a finer level than the Wii U did, and
// forced anisotropy blends other levels in; both change what such a texture shows. The renderers add
// log2(resolution scale) to its LOD bias and leave anisotropy off for it (metal_draw.mm, vulkan/draw.cpp).
// WWHD_ARTISTIC_MIPS=1 turns the detection on; WWHD_ARTISTIC_MIPS_LOG=1 logs each texture checked with a verdict.
//
// Detection after aurora's arb_mip_check (MIT, Copyright (c) 2022 Luke Street; lib/gfx/texture_convert.cpp:36-110):
// box-downscale each level into the next one's size and compare (RMS difference in percent); the downscale and the
// difference measure and the verdict (mean difference over the compared levels above kThreshold) follow it. Changed
// here: the chain starts at the first level of at most kMaxPixels pixels (the cost stays small on area loads) and
// stops below kMinSide (BC blocks make 1-2 pixel levels noisy). In WWHD the Tower of the Gods effect sprite
// (2EC6C000, BC5 384x384, sampled with bias -6) turns out an ordinary downscale chain (differences 0.5-0.6 once
// level 1 is read at the mip pointer, #297); a handful of textures pass the check (WWHD_ARTISTIC_MIPS_LOG=2 lists
// every verdict).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

#include "gfx/vulkan/bc_reference.h"

namespace gfx::artistic_mips {

constexpr float kThreshold = 15.0f;     // aurora's kArbMipThreshold
constexpr uint32_t kMaxPixels = 64 * 64; // first level compared
constexpr uint32_t kMinSide = 4;         // levels below 4x4 (BC: one block) are not compared

inline bool enabled() {
    static const bool on = [] { const char* e = std::getenv("WWHD_ARTISTIC_MIPS"); return e && !std::strcmp(e, "1"); }();
    return on;
}
inline bool log_enabled() {
    static const bool on = std::getenv("WWHD_ARTISTIC_MIPS_LOG") != nullptr;
    return on;
}

// BC type 1-5 of a GX2 format (0: not block-compressed), and whether it is signed
inline unsigned bc_type(uint32_t gx2Format) {
    const uint32_t f = gx2Format & 0x3F;
    return f >= 0x31 && f <= 0x35 ? f - 0x30 : 0;
}
// Formats the check reads: BC1-5 and 8-bit RGBA (host bytes are the texels' own)
inline bool supported(uint32_t gx2Format) { return bc_type(gx2Format) || (gx2Format & 0x3F) == 0x1A; }

// RGBA8 texels of one level (the first slice) from its host bytes
inline bool to_rgba8(uint32_t gx2Format, const uint8_t* data, size_t size, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
    out.assign(size_t(w) * h * 4, 0);
    if (const unsigned type = bc_type(gx2Format)) {
        const bool sign = (gx2Format & 0x200) != 0;
        const unsigned bytes = gfxvk::bc::block_bytes(type);
        const uint32_t bw = (w + 3) / 4, bh = (h + 3) / 4;
        if (size < size_t(bw) * bh * bytes) return false;
        for (uint32_t by = 0; by < bh; ++by)
            for (uint32_t bx = 0; bx < bw; ++bx) {
                const uint8_t* block = data + (size_t(by) * bw + bx) * bytes;
                for (unsigned p = 0; p < 16; ++p) {
                    const uint32_t x = bx * 4 + p % 4, y = by * 4 + p / 4;
                    if (x >= w || y >= h) continue;
                    const auto px = gfxvk::bc::pixel(block, type, sign, p);
                    memcpy(&out[(size_t(y) * w + x) * 4], px.data(), 4);
                }
            }
        return true;
    }
    if ((gx2Format & 0x3F) == 0x1A && size >= out.size()) {
        memcpy(out.data(), data, out.size());
        return true;
    }
    return false;
}

// box filter into (dw, dh) (aurora's downscale)
inline std::vector<uint8_t> downscale(const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh) {
    std::vector<uint8_t> dst(size_t(dw) * dh * 4);
    for (uint32_t y = 0; y < dh; ++y) {
        const uint32_t y0 = std::min(y * 2, sh - 1), y1 = std::min(y0 + 1, sh - 1);
        for (uint32_t x = 0; x < dw; ++x) {
            const uint32_t x0 = std::min(x * 2, sw - 1), x1 = std::min(x0 + 1, sw - 1);
            const size_t at[4] = {(size_t(y0) * sw + x0) * 4, (size_t(y0) * sw + x1) * 4, (size_t(y1) * sw + x0) * 4,
                                  (size_t(y1) * sw + x1) * 4};
            for (size_t c = 0; c < 4; ++c) {
                uint32_t sum = 0;
                for (size_t o : at) sum += src[o + c];
                dst[(size_t(y) * dw + x) * 4 + c] = uint8_t((sum + 2) / 4);
            }
        }
    }
    return dst;
}

// RMS difference in percent of the full range (aurora's avg_diff)
inline float difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    double sum = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const int d = int(a[i]) - int(b[i]);
        sum += double(d * d);
    }
    return a.empty() ? 0.0f : float(std::sqrt(sum / double(a.size())) / 2.56);
}

struct Verdict {
    bool artistic = false;
    uint32_t firstLevel = 0;            // the first level compared
    std::array<float, 16> diff{};       // per compared level n: level n vs the downscale of level n-1 (from firstLevel)
    uint32_t compared = 0;
};

// level(n, data, size) gives level n's host bytes (first slice first); widths and heights halve per level
inline Verdict check(uint32_t gx2Format, uint32_t width, uint32_t height, uint32_t mips,
                     const std::function<bool(uint32_t, const uint8_t*&, size_t&)>& level) {
    Verdict v;
    if (mips < 2 || !supported(gx2Format)) return v;
    auto side = [](uint32_t s, uint32_t n) { return std::max(s >> n, 1u); };
    uint32_t first = 0;
    while (first + 1 < mips && uint64_t(side(width, first)) * side(height, first) > kMaxPixels) ++first;
    v.firstLevel = first;
    std::vector<uint8_t> chain, next;
    const uint8_t* data; size_t size;
    if (!level(first, data, size) || !to_rgba8(gx2Format, data, size, side(width, first), side(height, first), chain)) return v;
    for (uint32_t n = first + 1; n < mips && n < 16; ++n) {
        const uint32_t w = side(width, n), h = side(height, n);
        static const uint32_t minSide = [] { const char* e = std::getenv("WWHD_ARTISTIC_MIPS_MIN_SIDE"); return e && *e ? uint32_t(std::max(1, std::atoi(e))) : kMinSide; }();
        if (w < minSide || h < minSide) break;
        if (!level(n, data, size) || !to_rgba8(gx2Format, data, size, w, h, next)) break;
        chain = downscale(chain, side(width, n - 1), side(height, n - 1), w, h);
        v.diff[n] = difference(next, chain);
        ++v.compared;
    }
    float sum = 0;
    for (uint32_t n = first + 1; n < first + 1 + v.compared; ++n) sum += v.diff[n];
    v.artistic = v.compared && sum / float(v.compared) > kThreshold;
    return v;
}

// what the renderers add to the LOD bias of an artistic-mip texture drawn into a target at scale (sx, sy)
inline float resolution_bias(float sx, float sy) {
    const float s = std::min(sx, sy);
    return s > 0.0f ? std::log2(s) : 0.0f;
}

}  // namespace gfx::artistic_mips
