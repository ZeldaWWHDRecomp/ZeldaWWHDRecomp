// Sharper text (gfx/sharp_text.h): which shaders and textures it touches, the gain, the ramp arithmetic and
// the shader rewrite (MSL and Vulkan GLSL).
#include "gfx/sharp_text.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace st = gfx::sharp_text;

static uint32_t words4(uint32_t x, uint32_t y, uint32_t z, uint32_t w) { return (x | y << 3 | z << 6 | w << 9) << 16; }

int main() {
    // shaders: only the two 528-byte text programs (other sizes or bytes: none)
    std::vector<uint8_t> program(528, 0);
    assert(st::pixel_units(program.data(), 528) == 0);
    assert(st::pixel_units(nullptr, 528) == 0 && st::pixel_units(program.data(), 400) == 0);

    // textures: the font sheet (BC4 2D array, (1,1,1,R)) yes; RGBA, colour selections, 3D, depth no
    uint32_t sheet[7] = {5, 0x34u << 26, 0, 0, words4(5, 5, 5, 0), 0, 0};
    assert(st::glyph_sheet(sheet));
    uint32_t r8[7] = {1, 0x01u << 26, 0, 0, words4(5, 5, 5, 0), 0, 0};
    assert(st::glyph_sheet(r8));
    uint32_t rgba[7] = {5, 0x1Au << 26, 0, 0, words4(5, 5, 5, 3), 0, 0};
    assert(!st::glyph_sheet(rgba));
    uint32_t grey[7] = {1, 0x34u << 26, 0, 0, words4(0, 0, 0, 5), 0, 0};  // (R,R,R,1): a grey picture
    assert(!st::glyph_sheet(grey));
    uint32_t la[7] = {1, 0x35u << 26, 0, 0, words4(0, 0, 0, 1), 0, 0};  // (R,R,R,G): an LA icon
    assert(!st::glyph_sheet(la));
    uint32_t vol[7] = {2, 0x34u << 26, 0, 0, words4(5, 5, 5, 0), 0, 0};
    assert(!st::glyph_sheet(vol) && !st::glyph_sheet(nullptr));

    // gain: the target's scale when on and a sheet; nothing at 1x, off, or another texture
    assert(st::gain(true, true, 3.0f, 3.0f) == 3.0f && st::gain(true, true, 2.0f, 3.0f) == 2.0f);
    assert(st::gain(true, true, 1.0f, 1.0f) == 0.0f && st::gain(false, true, 4.0f, 4.0f) == 0.0f &&
           st::gain(true, false, 4.0f, 4.0f) == 0.0f);

    // arithmetic: identity at gain <= 1; a ramp keeps its middle; the decomposition is exact
    for (float a = 0.0f; a <= 1.0f; a += 1.0f / 64)
        assert(st::sharpen_alpha(a, 0.1f, 0.1f, 1.0f) == a && st::sharpen_alpha(a, 0.1f, 0.1f, 0.0f) == a);
    for (float a = 0.0f; a <= 1.0f; a += 1.0f / 64)  // steep ramps (1 px or less): unchanged
        assert(std::fabs(st::sharpen_alpha(a, 2.0f, 2.0f, 4.0f) - a) < 1e-6f);
    assert(std::fabs(st::sharpen_alpha(0.25f, 0.1f, 0.1f, 4.0f) - 0.25f) < 1e-6f);  // the outer edge's middle
    assert(std::fabs(st::sharpen_alpha(0.75f, 0.1f, 0.1f, 4.0f) - 0.75f) < 1e-6f);  // the fill edge's middle
    assert(st::sharpen_alpha(0.0f, 0.1f, 0.1f, 4.0f) == 0.0f && st::sharpen_alpha(1.0f, 0.1f, 0.1f, 4.0f) == 1.0f);
    assert(st::sharpen_alpha(0.5f, 0.1f, 0.1f, 4.0f) == 0.5f);  // the border's own value
    // a ramp 10 px wide at gain 4 gets 4x steeper (2.5 px), never steeper than 1 px
    assert(std::fabs(st::sharpen_ramp(0.6f, 0.1f, 4.0f) - 0.9f) < 1e-5f);
    assert(std::fabs(st::sharpen_ramp(0.6f, 0.5f, 4.0f) - 0.7f) < 1e-5f);
    for (float h = 0.0f; h <= 1.0f; h += 1.0f / 32)  // monotonic
        assert(st::sharpen_ramp(h, 0.05f, 3.0f) <= st::sharpen_ramp(h + 1.0f / 32, 0.05f, 3.0f) + 1e-6f);

    // rewrite: MSL
    std::string msl = "struct S{};\nfragment FragmentOut main0(texture2d_array<float> tex0 [[texture(0)]], sampler samplr0) {\n"
                      "R1i.w = as_type<int>(tex0.sample(samplr0, float2(a, b), uint(c), bias(supportBuffer.tex0LodBias)).w);\n}\n";
    assert(st::rewrite(msl, 1u, true) == 1);
    assert(msl.find("wwhd_sharp_text(tex0, samplr0, supportBuffer.tex0Sharp, float2(a, b), uint(c), bias(") != std::string::npos);
    assert(msl.find("tex0.sample(samplr0, float2(a") == std::string::npos);
    assert(msl.find("static float4 wwhd_sharp_glyph") < msl.find("fragment FragmentOut main0"));
    // GLSL
    std::string glsl = "uniform float uf_tex0Sharp;\nvoid main() {\nR1f.w = (texture(textureUnitPS0, vec3(a, b, c)).w);\n}\n";
    assert(st::rewrite(glsl, 1u, false) == 1);
    assert(glsl.find("wwhdSharpText(textureUnitPS0, uf_tex0Sharp, vec3(a, b, c))") != std::string::npos);
    assert(glsl.find("vec4 wwhdSharpGlyph") < glsl.find("void main("));
    // nothing to rewrite: unchanged
    std::string other = "void main() {\nR0f = texture(textureUnitPS1, vec2(a, b));\n}\n";
    const std::string before = other;
    assert(st::rewrite(other, 1u, false) == 0 && other == before);
    assert(st::rewrite(other, 0u, false) == 0 && other == before);
    std::puts("sharp_text_test: ok");
}
