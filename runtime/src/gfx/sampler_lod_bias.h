#pragma once
// The LOD bias of a GX2 sampler, shared by both renderers and the shader translation.
//
// SQ_TEX_SAMPLER_WORD1 holds MIN_LOD (bits 0-9) and MAX_LOD (bits 10-19), unsigned with 6 fraction
// bits, and LOD_BIAS (bits 20-31): signed two's complement with 6 fraction bits, so -32.0 to
// 31.984375 in steps of 1/64. GX2InitSamplerLOD stores floor(bias * 64) clamped to -2048..2047 there
// (Cemu's gx2_texture_regs.cpp, LatteReg.h LATTE_BITFIELD_SIGNED(LOD_BIAS, 20, 12)). The glow under
// the Triforce chests (issue #297) uses word 1 FC0D0000: LOD 0 to 13, bias -1.
//
// Native Vulkan applies it through VkSamplerCreateInfo::mipLodBias, which the spec adds to the level
// of detail of every sample before the MIN_LOD..MAX_LOD clamp: implicit, explicit and gradient LODs
// alike. Metal samplers have no working bias (MTLSamplerDescriptor.lodBias, new in macOS 26, made no
// difference on an M-series Mac under 26.6) and MoltenVK cannot map mipLodBias, so on Apple the
// translated shaders add it themselves: bias() on implicit samples (level() at the bias in vertex
// shaders, which have no derivatives), the explicit level plus the bias, gradients scaled by 2^bias
// (log2 of the footprint grows by exactly the bias). Depth-compare samples, gathers and texel reads
// take none. The value reaches the shader in the support buffer (one float per texture unit that
// samples with a level of detail), so no shader or pipeline depends on it.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace gfx::sampler_lod_bias {

// LOD_BIAS in 1/64 levels, sign-extended (-2048..2047)
inline int32_t raw(uint32_t word1) { return static_cast<int32_t>(word1) >> 20; }

// LOD_BIAS in levels (-32.0..31.984375)
inline float levels(uint32_t word1) { return static_cast<float>(raw(word1)) / 64.0f; }

// What a sampler or shader on the host adds: Metal takes -16..15.999 (S4.6), Vulkan devices clamp to
// maxSamplerLodBias (at least 2, 15-16 on current GPUs). Biases beyond 16 levels select the first or
// the last level either way.
constexpr float kHostMin = -16.0f, kHostMax = 15.984375f;
inline float host(uint32_t word1) { return std::clamp(levels(word1), kHostMin, kHostMax); }

// WWHD_LOD_BIAS=off drops it on Apple, as before (for comparisons); the shaders still declare it.
// Native Vulkan always uses mipLodBias.
enum class Mode { Shader, Off };
inline Mode parse_mode(const char* value) {
    if (!value) return Mode::Shader;
    const std::string_view v(value);
    if (v == "off" || v == "0") return Mode::Off;
    return Mode::Shader;
}
inline Mode mode() {
    static const Mode m = parse_mode(std::getenv("WWHD_LOD_BIAS"));
    return m;
}

}  // namespace gfx::sampler_lod_bias
