#pragma once
// Sharper text (issue #621), shared by the Metal and the Vulkan renderer.
//
// The game draws all of its layout text (dialogue, the HUD's counters and button words, menus) with the
// nw::font text draw (028F8250): glyph quads that sample a font sheet, a 1024x1024 BC4 2D array whose one
// channel the texture's component selection turns into (1, 1, 1, R). Two pixel shaders of the layout
// shader archive do it, PS 17F00200 and 17F00F00 (USA load addresses; 528 bytes each, identified by their
// program hash below, wherever they are loaded). Both read only the alpha `a` of one bilinear sample and
// decode it as a bordered glyph:
//   a < 0.54:  alpha 2a, the border colour   (0 outside .. 0.5 on the border: the glyph's outer edge)
//   a >= 0.54: alpha 1, mix(border, fill, 2a - 1)   (0.5 border .. 1 inside: the edge of the fill)
// so a sheet value is two coverage ramps in one, the outer edge around a = 0.25 and the fill's edge around
// a = 0.75. The sheets are made for the console's 1280x720: magnified at a higher internal resolution, the
// bilinear ramps get as many pixels wide as the resolution scale, and the text looks soft.
//
// The fix splits the sample into its two ramps, o = clamp(2a) and i = clamp(2a - 1) (a = (o + i) / 2
// exactly), and makes each one steeper around its middle by k = clamp(1 / |grad|, 1, gain), its screen-space
// gradient giving how many pixels the ramp spans; `gain` is the render target's resolution scale. An edge
// then spans as many output pixels as it spanned console pixels (at least one), so the text is as crisp at
// 4x as at 1x, with no stair steps (the ramp is still a smooth one, just narrower). Colours, the border and
// the drop shadow (the same glyphs drawn again) keep their own values and widths: each ramp keeps its
// middle, only its slope changes. With gain 1 (resolution scale 1, the option off, any other texture) the
// sample is returned as it was.
//
// Which draws: the renderers rewrite the glyph sample of those two shaders (the decompiler declares a float
// uf_texNSharp for the unit, LatteDecompilerOptions::sharpTextTextures) and fill the uniform per draw:
// the target's scale when the option is on and the bound texture is a coverage sheet (glyph_sheet), else 0.
// Other textures in those shaders (none seen; the icon font's RGBA sheets fail glyph_sheet) and every
// other shader are untouched.
//
// Settings > Graphics > Sharper text (render::sharp_text), on by default; WWHD_SHARP_TEXT=0/1 sets it at
// start. WWHD_SHARP_TEXT_LOG=1 logs each text draw's texture once with the gain it gets.
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

#include "area_sample.h"  // program_hash

namespace gfx::sharp_text {

// the texture units of a pixel shader whose glyph samples are sharpened (bit n = unit n)
inline uint32_t pixel_units(const void* bytes, uint32_t size) {
    if (!bytes || size != 528) return 0;
    switch (area_sample::program_hash(bytes, size)) {
    case 0xb84517cef37ac92dull:  // PS 17F00200, nw::font glyphs
    case 0xd9953dbd73143099ull:  // PS 17F00F00, nw::font glyphs (the variant with its own colour blend)
        return 1u;
    default:
        return 0;
    }
}

// A coverage glyph sheet, from the texture's resource words (SQ_TEX_RESOURCE_WORD0..6): a 2D texture or 2D
// array in a one- or two-channel format (8, 8_8, BC4, BC5) whose component selection gives constant 1 for
// R, G and B and a channel for alpha. The font sheets are BC4 2D arrays selected (1, 1, 1, R); the icon
// font's RGBA sheets and any colour picture are not.
inline bool glyph_sheet(const uint32_t* words) {
    if (!words) return false;
    const uint32_t dim = words[0] & 7, format = words[1] >> 26;
    if (dim != 1 && dim != 5) return false;  // DIM_2D, DIM_2D_ARRAY
    if (format != 0x01 && format != 0x07 && format != 0x34 && format != 0x35) return false;
    const uint32_t sel = words[4] >> 16;  // DST_SEL_X/Y/Z/W, 3 bits each: 0-3 a channel, 4 zero, 5 one
    const uint32_t x = sel & 7, y = (sel >> 3) & 7, z = (sel >> 6) & 7, w = (sel >> 9) & 7;
    return x == 5 && y == 5 && z == 5 && w < 4;
}

// The uniform's value: the target's resolution scale (texture-independent; 0 when nothing changes)
inline float gain(bool enabled, bool sheet, float scaleX, float scaleY) {
    if (!enabled || !sheet) return 0.0f;
    const float scale = std::min(scaleX, scaleY);
    return scale > 1.001f ? scale : 0.0f;
}

// The same arithmetic as the shader helpers below, for tests: one sample's alpha at its gradient.
inline float sharpen_ramp(float h, float slope, float gain) {
    const float k = std::clamp(1.0f / std::max(slope, 1e-6f), 1.0f, gain);
    return std::clamp((h - 0.5f) * k + 0.5f, 0.0f, 1.0f);
}
// alpha a with the screen-space gradient lengths of its two ramps (|grad o|, |grad i|)
inline float sharpen_alpha(float a, float slopeOuter, float slopeInner, float gain) {
    if (gain <= 1.0f) return a;
    const float o = std::clamp(2.0f * a, 0.0f, 1.0f), i = std::clamp(2.0f * a - 1.0f, 0.0f, 1.0f);
    return 0.5f * (sharpen_ramp(o, slopeOuter, gain) + sharpen_ramp(i, slopeInner, gain));
}

// Rewrites the samples of `units` in a decompiled pixel shader (MSL or Vulkan GLSL) to the helper and
// inserts the helper. Returns the number of samples rewritten (0: source left unchanged).
inline int rewrite(std::string& src, uint32_t units, bool msl) {
    const size_t entry = src.find(msl ? "fragment FragmentOut main0" : "void main(");
    if (!units || entry == std::string::npos) return 0;
    int count = 0;
    for (uint32_t unit = 0; unit < 32; unit++) {
        if (!(units >> unit & 1)) continue;
        const std::string n = std::to_string(unit);
        const std::string from = msl ? "tex" + n + ".sample(samplr" + n + ", " : "texture(textureUnitPS" + n + ", ";
        const std::string to = msl ? "wwhd_sharp_text(tex" + n + ", samplr" + n + ", supportBuffer.tex" + n + "Sharp, "
                                   : "wwhdSharpText(textureUnitPS" + n + ", uf_tex" + n + "Sharp, ";
        for (size_t at = src.find(from, entry); at != std::string::npos; at = src.find(from, at + to.size())) {
            src.replace(at, from.size(), to);
            count++;
        }
    }
    if (!count) return 0;
    static const char* const kMsl =
        "// sharper text: the two coverage ramps of a glyph sample, each narrowed to at most `gain` times its\n"
        "// slope (runtime/src/gfx/sharp_text.h)\n"
        "static float wwhd_sharp_ramp(float h, float gain) {\n"
        "    float slope = length(float2(dfdx(h), dfdy(h)));\n"
        "    float k = clamp(1.0 / max(slope, 1e-6), 1.0, gain);\n"
        "    return saturate((h - 0.5) * k + 0.5);\n"
        "}\n"
        "static float4 wwhd_sharp_glyph(float4 c, float gain) {\n"
        "    if (gain <= 1.0) return c;\n"
        "    float o = saturate(2.0 * c.w), i = saturate(2.0 * c.w - 1.0);\n"
        "    c.w = 0.5 * (wwhd_sharp_ramp(o, gain) + wwhd_sharp_ramp(i, gain));\n"
        "    return c;\n"
        "}\n"
        "static float4 wwhd_sharp_text(texture2d_array<float> t, sampler s, float gain, float2 uv, uint layer) {\n"
        "    return wwhd_sharp_glyph(t.sample(s, uv, layer), gain);\n"
        "}\n"
        "static float4 wwhd_sharp_text(texture2d_array<float> t, sampler s, float gain, float2 uv, uint layer, bias lodBias) {\n"
        "    return wwhd_sharp_glyph(t.sample(s, uv, layer, lodBias), gain);\n"
        "}\n"
        "static float4 wwhd_sharp_text(texture2d<float> t, sampler s, float gain, float2 uv) {\n"
        "    return wwhd_sharp_glyph(t.sample(s, uv), gain);\n"
        "}\n"
        "static float4 wwhd_sharp_text(texture2d<float> t, sampler s, float gain, float2 uv, bias lodBias) {\n"
        "    return wwhd_sharp_glyph(t.sample(s, uv, lodBias), gain);\n"
        "}\n";
    static const char* const kGlsl =
        "// sharper text: the two coverage ramps of a glyph sample, each narrowed to at most `gain` times its\n"
        "// slope (runtime/src/gfx/sharp_text.h)\n"
        "float wwhdSharpRamp(float h, float gain) {\n"
        "    float slope = length(vec2(dFdx(h), dFdy(h)));\n"
        "    float k = clamp(1.0 / max(slope, 1e-6), 1.0, gain);\n"
        "    return clamp((h - 0.5) * k + 0.5, 0.0, 1.0);\n"
        "}\n"
        "vec4 wwhdSharpGlyph(vec4 c, float gain) {\n"
        "    if (gain <= 1.0) return c;\n"
        "    float o = clamp(2.0 * c.w, 0.0, 1.0), i = clamp(2.0 * c.w - 1.0, 0.0, 1.0);\n"
        "    c.w = 0.5 * (wwhdSharpRamp(o, gain) + wwhdSharpRamp(i, gain));\n"
        "    return c;\n"
        "}\n"
        "vec4 wwhdSharpText(sampler2DArray t, float gain, vec3 uv) { return wwhdSharpGlyph(texture(t, uv), gain); }\n"
        "vec4 wwhdSharpText(sampler2DArray t, float gain, vec3 uv, float lodBias) { return wwhdSharpGlyph(texture(t, uv, lodBias), gain); }\n"
        "vec4 wwhdSharpText(sampler2D t, float gain, vec2 uv) { return wwhdSharpGlyph(texture(t, uv), gain); }\n"
        "vec4 wwhdSharpText(sampler2D t, float gain, vec2 uv, float lodBias) { return wwhdSharpGlyph(texture(t, uv, lodBias), gain); }\n";
    src.insert(entry, msl ? kMsl : kGlsl);
    return count;
}

}  // namespace gfx::sharp_text
