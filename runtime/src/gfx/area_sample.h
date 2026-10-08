#pragma once
// Area-sampled blur taps on upscaled render targets (issues #66, #67), shared by the Metal and the
// Vulkan renderer.
//
// The game blurs its light buffer (ambient occlusion in R, the sun shadow mask in G; 960x540) and
// its bloom chain with a separable Gaussian: PS 44B3AB00 (horizontal) and 44B39A00 (vertical),
// three bilinear taps at 0 and +-1.36 texels, weights 0.29 / 0.35 / 0.35. At the console's size a
// tap 1.36 texels out blends two neighbours, so the taps cover five texels and the 4x4 noise of the
// occlusion pass (and the random offsets of the shadow lookups) average out. On a target scaled by
// the internal resolution the taps stay 1.36 *guest* texels apart, which is 2.7 pixels at 2x: the
// pixels in between get no weight, and the filter passes detail with a period near the tap spacing
// unchanged. The third harmonic of the occlusion noise sits right there, so the blurred buffer kept
// a fine grid that the scene lighting showed around everything standing on the ground (issue #66).
//
// The fix gives each tap the footprint a tap has at the console's size: the average of k x k
// bilinear samples spread evenly over one guest texel (k = ceil(scale) per axis), which is what
// sampling a guest-sized copy of the buffer would read, while silhouettes keep the higher
// resolution. At scale 1 (or for buffers that are not scaled) the helper takes the one sample the
// game asked for, so the native picture is unchanged.
//
// The decompiler declares uf_texNScale (texture pixels per guest pixel, filled from the surface's
// sx/sy by both renderers) for the units named in LatteDecompilerOptions::areaSampledTextures; the
// sample calls of those units are then rewritten here to go through the helper.
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

namespace gfx::area_sample {

// Cemu's program hash (graphic_pack_hash.h): the program's words, wherever it was loaded.
inline uint64_t program_hash(const void* bytes, uint32_t size) {
    uint64_t a = 0, b = 0;
    for (uint32_t i = 0; i < size / 4; i++) {
        uint32_t word;
        std::memcpy(&word, static_cast<const uint8_t*>(bytes) + i * 4, 4);
        a = std::rotl(a + word, 3);
        b = std::rotr(b ^ word, 7);
    }
    return a + b;
}

// texture units of this pixel shader that are read area-sampled (bit n = unit n); 0 for all others
inline uint32_t units_for_pixel_shader(const void* bytes, uint32_t size) {
    if (!bytes || size != 448) return 0;
    switch (program_hash(bytes, size)) {
    case 0x51a7ccdf69184627ull:  // PS 44B3AB00, horizontal Gaussian
    case 0x16285301a96cbf8dull:  // PS 44B39A00, vertical Gaussian
        return 1u;
    default:
        return 0;
    }
}

// Rewrites the sample calls of `units` in a decompiled pixel shader (MSL or Vulkan GLSL) to the
// helper and inserts the helper. Returns the number of calls rewritten (0: source left unchanged).
inline int rewrite(std::string& src, uint32_t units, bool msl) {
    const size_t entry = src.find(msl ? "fragment FragmentOut main0" : "void main(");
    if (!units || entry == std::string::npos) return 0;
    int count = 0;
    for (uint32_t unit = 0; unit < 32; unit++) {
        if (!(units >> unit & 1)) continue;
        const std::string n = std::to_string(unit);
        const std::string from = msl ? "tex" + n + ".sample(samplr" + n + ", " : "texture(textureUnitPS" + n + ", ";
        const std::string to = msl ? "wwhd_area_sample(tex" + n + ", samplr" + n + ", supportBuffer.tex" + n + "Scale, "
                                   : "wwhdAreaSample(textureUnitPS" + n + ", uf_tex" + n + "Scale, ";
        for (size_t at = src.find(from); at != std::string::npos; at = src.find(from, at + to.size())) {
            src.replace(at, from.size(), to);
            count++;
        }
    }
    if (!count) return 0;
    static const char* const kMsl =
        "// area-sampled tap for upscaled render targets (runtime/src/gfx/area_sample.h)\n"
        "static float4 wwhd_area_sample(texture2d<float> t, sampler s, float2 scale, float2 uv) {\n"
        "    float2 k = ceil(scale - 0.001);\n"
        "    if (k.x <= 1.0 && k.y <= 1.0) return t.sample(s, uv);\n"
        "    float2 step = scale / (float2(t.get_width(), t.get_height()) * k);\n"
        "    float4 sum = float4(0.0);\n"
        "    for (float j = 0.5; j < k.y; j += 1.0)\n"
        "        for (float i = 0.5; i < k.x; i += 1.0) sum += t.sample(s, uv + (float2(i, j) - 0.5 * k) * step);\n"
        "    return sum / (k.x * k.y);\n"
        "}\n";
    static const char* const kGlsl =
        "// area-sampled tap for upscaled render targets (runtime/src/gfx/area_sample.h)\n"
        "vec4 wwhdAreaSample(sampler2D t, vec2 scale, vec2 uv) {\n"
        "    vec2 k = ceil(scale - 0.001);\n"
        "    if (k.x <= 1.0 && k.y <= 1.0) return texture(t, uv);\n"
        "    vec2 step = scale / (vec2(textureSize(t, 0)) * k);\n"
        "    vec4 sum = vec4(0.0);\n"
        "    for (float j = 0.5; j < k.y; j += 1.0)\n"
        "        for (float i = 0.5; i < k.x; i += 1.0) sum += texture(t, uv + (vec2(i, j) - 0.5 * k) * step);\n"
        "    return sum / (k.x * k.y);\n"
        "}\n";
    src.insert(src.find(msl ? "fragment FragmentOut main0" : "void main("), msl ? kMsl : kGlsl);
    return count;
}

}  // namespace gfx::area_sample
