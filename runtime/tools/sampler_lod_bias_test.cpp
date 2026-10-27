// The GX2 sampler LOD bias as both renderers and the shader translation decode it
// (runtime/src/gfx/sampler_lod_bias.h). Synthetic sampler words only; no game files or GPU needed.
#include "gfx/sampler_lod_bias.h"
#include "gx2_texture_regs.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace lb = gfx::sampler_lod_bias;

int main() {
    // the glow under the Triforce chests (issue #297): word 1 FC0D0000 = LOD 0 to 13, bias -1
    const uint32_t chest = 0xFC0D0000;
    assert(lb::raw(chest) == -64);
    assert(lb::levels(chest) == -1.0f);
    assert(lb::host(chest) == -1.0f);
    Latte::LATTE_SQ_TEX_SAMPLER_WORD1_0 w1;
    w1 = Latte::LATTE_SQ_TEX_SAMPLER_WORD1_0{};
    static_assert(sizeof w1 == 4);
    std::memcpy(&w1, &chest, 4);
    assert(w1.get_MIN_LOD() == 0 && w1.get_MAX_LOD() == 13 * 64);

    // every 12-bit value matches Cemu's register definition (signed, bits 20-31), whatever MIN/MAX_LOD hold
    for (int32_t v = -2048; v < 2048; v++) {
        const uint32_t word = (uint32_t(v) << 20) | ((uint32_t(v) * 2654435761u) & 0xFFFFF);
        std::memcpy(&w1, &word, 4);
        assert(lb::raw(word) == v);
        assert(lb::raw(word) == int32_t(w1.get_LOD_BIAS()));
        assert(lb::levels(word) == float(v) / 64.0f);
        const float h = lb::host(word);
        assert(h >= lb::kHostMin && h <= lb::kHostMax);
        if (v >= -1024 && v < 1024) assert(h == float(v) / 64.0f);  // within Metal's S4.6 range: unchanged
    }
    assert(lb::levels(0x80000000u) == -32.0f && lb::host(0x80000000u) == -16.0f);
    assert(lb::levels(0x7FF00000u) == 2047.0f / 64.0f && lb::host(0x7FF00000u) == lb::kHostMax);
    assert(lb::levels(0x00100000u) == 1.0f / 64.0f);
    assert(lb::levels(0xFFF00000u) == -1.0f / 64.0f);
    assert(lb::levels(0x000FFFFFu) == 0.0f);  // MIN/MAX_LOD bits never leak into the bias

    // what the game sets through GX2InitSamplerLOD (floor to 1/64, clamped to -32..31.98) decodes back
    const float biases[] = {-1.0f, -0.5f, 0.25f, 1.0f, -0.3f, 0.7f, -32.0f, 31.99f, 40.0f, -40.0f, 0.0f};
    for (float b : biases) {
        GX2::GX2Sampler s{};
        GX2::GX2InitSamplerLOD(&s, 0.0f, 13.0f, b);
        const uint32_t word = s.word1.value().getRawValue();
        const float want = std::clamp(std::floor(b * 64.0f), -2048.0f, 2047.0f) / 64.0f;
        assert(lb::levels(word) == want);
        std::memcpy(&w1, &word, 4);
        assert(w1.get_MAX_LOD() == 13 * 64);
    }

    // WWHD_LOD_BIAS
    assert(lb::parse_mode(nullptr) == lb::Mode::Shader);
    assert(lb::parse_mode("shader") == lb::Mode::Shader);
    assert(lb::parse_mode("off") == lb::Mode::Off && lb::parse_mode("0") == lb::Mode::Off);
    assert(lb::parse_mode("bogus") == lb::Mode::Shader);
    std::puts("sampler_lod_bias: ok");
    return 0;
}
