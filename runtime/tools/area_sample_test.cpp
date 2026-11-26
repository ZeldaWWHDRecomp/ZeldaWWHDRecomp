// Tests for gfx/area_sample.h: the shadow mask's gather spacing on upscaled depth (issue #135) and
// the source rewrites both renderers apply.
#include "gfx/area_sample.h"
#include "gfx/shader_identity.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using namespace gfx::area_sample;

namespace {
struct V3 { double x, y, z; };
V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 norm(V3 a) { double l = std::sqrt(dot(a, a)); return {a.x / l, a.y / l, a.z / l}; }

// The shadow mask's model: a view ray per screen position (ndc u, v), scaled by the depth read for
// it. The scene is the plane dot(n, p) = c; `depth_at` is what the depth buffer holds at (u, v).
constexpr double kTanX = 1.0264, kTanY = 0.5774;  // the game's values (PS uniform 0.zw)
V3 ray(double u, double v) { return {u * kTanX, v * kTanY, -1.0}; }
double depth_at(V3 n, double c, double u, double v) { return c / dot(n, ray(u, v)); }
V3 at(double u, double v, double t) { V3 r = ray(u, v); return {r.x * t, r.y * t, r.z * t}; }

// The normal the shader rebuilds at (u, v) with the depth buffer `scale` times the guest's 1280x720:
// the gather returns texels 1/scale guest texels apart; the offset rays are one guest texel out
// (as the vertex shader passes them) or moved by gather_ray (the fix).
V3 rebuilt_normal(V3 n, double c, double u, double v, float scale, bool fixed) {
    const double du = 2.0 / 1280, dv = 2.0 / 720;
    const double tu = du / scale, tv = dv / scale;  // where the gathered texels are
    Ray centre{(float)u, (float)v, 0, 0}, rx{(float)(u + du), (float)v, 0, 0}, ry{(float)u, (float)(v + dv), 0, 0};
    if (fixed) {
        rx = gather_ray(centre, rx, scale, scale);
        ry = gather_ray(centre, ry, scale, scale);
    }
    V3 p0 = at(u, v, depth_at(n, c, u, v));
    V3 px = at(rx.x, rx.y, depth_at(n, c, u + tu, v));
    V3 py = at(ry.x, ry.y, depth_at(n, c, u, v + tv));
    V3 r = norm(cross(sub(px, p0), sub(py, p0)));
    return dot(r, n) < 0 ? V3{-r.x, -r.y, -r.z} : r;
}

double angle_deg(V3 a, V3 b) { return std::acos(std::fmin(1.0, std::fabs(dot(norm(a), norm(b))))) * 180.0 / M_PI; }

void test_gather_ray_math() {
    // scale 1: the console's rays, untouched
    Ray c{0.1f, 0.2f, 3, 4}, r{0.3f, 0.5f, 7, 8};
    Ray same = gather_ray(c, r, 1.0f, 1.0f);
    assert(same.x == r.x && same.y == r.y && same.z == r.z && same.w == r.w);
    // scaled: the offset shrinks by the factor per axis, z/w kept
    Ray k = gather_ray(c, r, 2.0f, 4.0f);
    assert(std::fabs(k.x - 0.2f) < 1e-6f && std::fabs(k.y - 0.275f) < 1e-6f && k.z == 7 && k.w == 8);

    // a pier floor seen from a camera above it, tilted and turned: the rebuilt normal must be the
    // floor's at every scale once the rays follow the gather; without the fix it leans towards the
    // camera by an amount that grows with the scale (the issue)
    const V3 floors[] = {norm({0.0, 0.8, 0.6}), norm({0.4, 0.7, 0.3}), norm({-0.5, 0.6, 0.2})};
    for (V3 n : floors) {
        const double c = -dot(n, V3{0, 0, -600});  // through a point 600 units in front of the camera
        for (float scale : {1.0f, 1.5f, 2.0f, 3.0f}) {
            for (double u : {-0.4, 0.0, 0.3}) {
                for (double v : {-0.3, -0.1}) {
                    double fixedErr = angle_deg(rebuilt_normal(n, c, u, v, scale, true), n);
                    double bugErr = angle_deg(rebuilt_normal(n, c, u, v, scale, false), n);
                    assert(fixedErr < 0.05);
                    if (scale == 1.0f) assert(bugErr < 0.05);
                    else assert(bugErr > 1.0);
                }
            }
        }
    }
}

// what the decompiler emits for the shadow mask's inputs, cut down
const char* kMslSrc =
    "struct SupportBuffer { int4 remapped[20]; float2 tex0Scale; };\n"
    "fragment FragmentOut main0(FragmentIn in [[stage_in]]) {\n"
    "int4 R0i = int4(0);\nint4 R1i = int4(0);\nint4 R2i = int4(0);\nint4 R3i = int4(0);\n"
    "R0i = as_type<int4>(in.passParameterSem0);\n"
    "R1i = as_type<int4>(in.passParameterSem1);\n"
    "R2i = as_type<int4>(in.passParameterSem2);\n"
    "R3i = as_type<int4>(in.passParameterSem3);\n"
    "R4i.xyzw = as_type<int4>(tex0.gather(samplr0, float2(0.0001) + float2(as_type<float>(R3i.x),as_type<float>(R3i.y))).xyzw);\n"
    "}\n";
const char* kGlslSrc =
    "layout(location = 0) in vec4 passParameterSem0;\nlayout(location = 1) in vec4 passParameterSem1;\n"
    "layout(location = 2) in vec4 passParameterSem2;\nlayout(location = 3) in vec4 passParameterSem3;\n"
    "uniform vec2 uf_tex0Scale;\n"
    "void main()\n{\nivec4 R0i = ivec4(0);\nivec4 R1i = ivec4(0);\nivec4 R2i = ivec4(0);\n"
    "R0i = floatBitsToInt(passParameterSem0);\n"
    "R1i = floatBitsToInt(passParameterSem1);\n"
    "R2i = floatBitsToInt(passParameterSem2);\n"
    "R3i = floatBitsToInt(passParameterSem3);\n"
    "}\n";

void test_rewrite_gather_rays() {
    const Program shadow{1u, Fix::GatherRays};
    std::string msl = kMslSrc;
    assert(rewrite(msl, shadow, true) == 2);
    assert(msl.find("R0i = as_type<int4>(in.passParameterSem0);") != std::string::npos);
    assert(msl.find("R1i = as_type<int4>(wwhd_gather_ray(in.passParameterSem0, in.passParameterSem1, supportBuffer.tex0Scale));") != std::string::npos);
    assert(msl.find("R2i = as_type<int4>(wwhd_gather_ray(in.passParameterSem0, in.passParameterSem2, supportBuffer.tex0Scale));") != std::string::npos);
    assert(msl.find("R3i = as_type<int4>(in.passParameterSem3);") != std::string::npos);
    assert(msl.find("static float4 wwhd_gather_ray(") < msl.find("fragment FragmentOut main0"));

    std::string glsl = kGlslSrc;
    assert(rewrite(glsl, shadow, false) == 2);
    assert(glsl.find("R1i = floatBitsToInt(wwhdGatherRay(passParameterSem0, passParameterSem1, uf_tex0Scale));") != std::string::npos);
    assert(glsl.find("R2i = floatBitsToInt(wwhdGatherRay(passParameterSem0, passParameterSem2, uf_tex0Scale));") != std::string::npos);
    assert(glsl.find("layout(location = 1) in vec4 passParameterSem1;") != std::string::npos);  // declarations untouched
    assert(glsl.find("vec4 wwhdGatherRay(") < glsl.find("void main("));

    // a shader that doesn't read its rays as expected is left alone
    std::string other = "fragment FragmentOut main0(FragmentIn in [[stage_in]]) {\nR0i = as_type<int4>(in.passParameterSem0);\n}\n";
    const std::string before = other;
    assert(rewrite(other, shadow, true) == 0 && other == before);
    // nothing to do for programs without a fix
    std::string none = kMslSrc;
    assert(rewrite(none, Program{}, true) == 0 && none == kMslSrc);
}

// The light buffer's blur (issues #66, #631): both passes take the console-grid taps reading level 0,
// through the overload with the sampler's LOD bias where the shader passes one (Metal always does,
// gfx/sampler_lod_bias.h), on every sample call.
void test_light_buffer_blur() {
    for (uint64_t hash : {0x51a7ccdf69184627ull, 0x16285301a96cbf8dull}) {
        const Program blur = pixel_program_for(448, hash);
        assert(blur.fix == Fix::GuestTapsLevel0 && blur.units == 1u);
        assert(pixel_program_for(656, hash).fix == Fix::None);  // the size is part of the program
    }
    const Program blur{1u, Fix::GuestTapsLevel0};
    std::string msl = "fragment FragmentOut main0(FragmentIn in [[stage_in]]) {\n"
                      "R1f = (tex0.sample(samplr0, float2(R0f.w,R0f.x)));\nR2f = (tex0.sample(samplr0, float2(R0f.y,R0f.x)));\n}\n";
    assert(rewrite(msl, blur, true) == 2);
    assert(msl.find("wwhd_guest_sample_level0(tex0, samplr0, supportBuffer.tex0Scale, float2(R0f.w,R0f.x))") != std::string::npos);
    assert(msl.find("static float4 wwhd_guest_sample_level0(") < msl.find("fragment FragmentOut main0"));
    assert(msl.find("#define WWHD_FETCH(p) t.sample(s, p, level(0.0))") != std::string::npos);
    std::string biased = "fragment FragmentOut main0(FragmentIn in [[stage_in]]) {\n"
                         "R1f = (tex0.sample(samplr0, float2(R0f.w,R0f.x), bias(supportBuffer.tex0LodBias)));\n}\n";
    assert(rewrite(biased, blur, true) == 1);
    // the bias as a number, as in GLSL: Metal's bias() can't be read back
    assert(biased.find("wwhd_guest_sample_level0(tex0, samplr0, supportBuffer.tex0Scale, float2(R0f.w,R0f.x), "
                       "supportBuffer.tex0LodBias))") != std::string::npos);
    assert(biased.find("bias(supportBuffer.tex0LodBias)") == std::string::npos);
    assert(biased.find("wwhd_guest_sample_level0(texture2d<float> t, sampler s, float2 scale, float2 uv, float lodBias)") <
           biased.find("fragment FragmentOut main0"));
    assert(biased.find("float2 uv, float lodBias)") < biased.find("static float4 wwhd_guest_sample_level0("));  // the area fallback's
    assert(biased.find("return t.sample(s, uv, bias(lodBias));") != std::string::npos);
    std::string glsl = "void main() {\nR1f = (texture(textureUnitPS0, vec2(R0f.w,R0f.x),uf_tex0LodBias));\n}\n";
    assert(rewrite(glsl, blur, false) == 1);
    assert(glsl.find("wwhdGuestSampleLevel0(textureUnitPS0, uf_tex0Scale, vec2(R0f.w,R0f.x),uf_tex0LodBias)") != std::string::npos);
    assert(glsl.find("vec4 wwhdGuestSampleLevel0(sampler2D t, vec2 scale, vec2 uv, float lodBias)") < glsl.find("void main("));
    assert(glsl.find("vec4 wwhdAreaSample(sampler2D t, vec2 scale, vec2 uv, float lodBias)") < glsl.find("vec4 wwhdGuestSampleLevel0("));
    assert(glsl.find("#define WWHD_FETCH(p) textureLod(t, p, 0.0)") != std::string::npos);
    // the console's grid only where every guest texel covers whole host texels
    assert(glsl.find("if (any(notEqual(round(size / scale) * r, size))) return wwhdAreaSample(t, scale, uv, lodBias);") !=
           std::string::npos);
    assert(biased.find("if (any(round(size / scale) * r != size)) return wwhd_area_sample(t, s, scale, uv, lodBias);") !=
           std::string::npos);
}

// The bloom extract's taps (issue #307) go through the console-grid helper, on both of its units;
// the area helper comes along as its fallback at fractional scales.
void test_guest_taps_rewritten() {
    const Program extract{3u, Fix::GuestTaps};
    std::string glsl = "void main() {\nR2f = (texture(textureUnitPS0, vec2(R0f.x,R0f.y)));\n"
                       "R4f.x = (texture(textureUnitPS1, vec2(R0f.x,R0f.y)).x);\n}\n";
    assert(rewrite(glsl, extract, false) == 2);
    assert(glsl.find("wwhdGuestSample(textureUnitPS0, uf_tex0Scale, vec2(R0f.x,R0f.y))") != std::string::npos);
    assert(glsl.find("wwhdGuestSample(textureUnitPS1, uf_tex1Scale, vec2(R0f.x,R0f.y))") != std::string::npos);
    assert(glsl.find("vec4 wwhdAreaSample(sampler2D t, vec2 scale, vec2 uv)") < glsl.find("vec4 wwhdGuestSample("));
    assert(glsl.find("vec4 wwhdGuestSample(sampler2D t, vec2 scale, vec2 uv, float lodBias)") < glsl.find("void main("));
    assert(glsl.find("textureGrad(t, p, dx, dy)") != std::string::npos);
    std::string msl = "fragment FragmentOut main0(FragmentIn in [[stage_in]]) {\n"
                      "R2f = (tex0.sample(samplr0, float2(R0f.x,R0f.y)));\n}\n";
    assert(rewrite(msl, extract, true) == 1);
    assert(msl.find("wwhd_guest_sample(tex0, samplr0, supportBuffer.tex0Scale, float2(R0f.x,R0f.y))") != std::string::npos);
    assert(msl.find("static float4 wwhd_area_sample(") < msl.find("static float4 wwhd_guest_sample("));
    assert(msl.find("static float4 wwhd_guest_sample(") < msl.find("fragment FragmentOut main0"));
    assert(msl.find("gradient2d(dx, dy)") != std::string::npos);
    // with the sampler's LOD bias both backends scale the gradients by 2^bias (gfx/sampler_lod_bias.h)
    std::string biased = "fragment FragmentOut main0(FragmentIn in [[stage_in]]) {\n"
                         "R2f = (tex1.sample(samplr1, float2(R0f.x,R0f.y), bias(supportBuffer.tex1LodBias)));\n}\n";
    assert(rewrite(biased, extract, true) == 1);
    assert(biased.find("wwhd_guest_sample(tex1, samplr1, supportBuffer.tex1Scale, float2(R0f.x,R0f.y), "
                       "supportBuffer.tex1LodBias))") != std::string::npos);
    assert(biased.find("float2 dx = dfdx(uv) * exp2(lodBias), dy = dfdy(uv) * exp2(lodBias);") != std::string::npos);
    assert(glsl.find("vec2 dx = dFdx(uv) * exp2(lodBias), dy = dFdy(uv) * exp2(lodBias);") != std::string::npos);
}

// Where the console-grid helpers take the console's grid: whole scales whose sizes are exact
// multiples of the guest's. Fractional scales and sizes rounded up from a scale just off a whole
// number take the area helper; scale 1 the game's own sample.
void test_console_grid_fallback() {
    assert(on_console_grid(1920, 1080, 2.0f, 2.0f));
    assert(on_console_grid(2880, 1620, 3.0f, 3.0f));
    assert(on_console_grid(16, 8, 2.0f, 2.0f));  // a small target
    assert(!on_console_grid(960, 540, 1.0f, 1.0f));
    assert(!on_console_grid(1440, 810, 1.5f, 1.5f));    // 1.5x
    assert(!on_console_grid(2400, 1350, 2.5f, 2.5f));   // rounds to k = 3
    assert(!on_console_grid(2160, 1215, 2.25f, 2.25f));  // dynamic resolution at 75% of 3x
    assert(!on_console_grid(1925, 1083, 2.005f, 2.005f));  // 960 x 540 rounded up at 2.005
    assert(!on_console_grid(1920, 1083, 2.0f, 2.005f));    // either axis
    // the shaders test the same, in every console-grid helper of both languages
    const Program both{1u, Fix::GuestTaps}, level0{1u, Fix::GuestTapsLevel0};
    for (bool msl : {false, true}) {
        const std::string guard = msl ? "    if (any(round(size / scale) * r != size)) return wwhd_area_sample(t, s, scale, uv"
                                      : "    if (any(notEqual(round(size / scale) * r, size))) return wwhdAreaSample(t, scale, uv";
        for (const Program& p : {both, level0}) {
            std::string src = msl ? "fragment FragmentOut main0(FragmentIn in [[stage_in]]) {\nR1f = (tex0.sample(samplr0, uv));\n}\n"
                                  : "void main() {\nR1f = (texture(textureUnitPS0, uv));\n}\n";
            assert(rewrite(src, p, msl) == 1);
            int guards = 0;
            for (size_t at = src.find(guard); at != std::string::npos; at = src.find(guard, at + 1)) guards++;
            assert(guards == 4);  // two helpers with and two without the bias
        }
    }
}

// The lit materials' depth-aware upsample of the half-size light buffer: the units they gather from,
// and their grid uniform scaled to the buffer's real size.
void test_gather_grid() {
    assert(gathered_units("R9f = (textureGather(textureUnitPS1, vec2(0.0001) + vec2(R11f.x,R11f.y)).xyzw);\n"
                          "R4f = texture(textureUnitPS2, vec2(R4f.x,R4f.w));\n", false) == 2u);
    assert(gathered_units("R9f = tex1.gather(samplr1, float2(R11f.x,R11f.y));\nR2 = tex12.gather(samplr12, uv);\n", true) ==
           ((1u << 1) | (1u << 12)));
    assert(gathered_units("R9f = (textureGather(textureUnitPS10, uv));\n", false) == 1u << 10);  // not unit 1
    assert(gathered_units("R1f = texture(textureUnitPS0, uv);\n", false) == 0);

    // remapped[0] something else, [1] the grid of a 640x360 buffer (the console's 1280x720 screen)
    float u[8] = {380.0f, 0.005f, 905.0f, 1.0f / 350.0f, 1280.0f, 720.0f, 1.0f / 1280.0f, 1.0f / 720.0f};
    assert(scale_gather_grid(u, 2, 640, 360, 1.0f, 1.0f) == 0 && u[4] == 1280.0f);  // 1x: unchanged
    assert(scale_gather_grid(u, 2, 960, 540, 3.0f, 3.0f) == 0 && u[4] == 1280.0f);  // another buffer's size
    assert(scale_gather_grid(u, 2, 640, 360, 3.0f, 3.0f) == 1);
    assert(u[0] == 380.0f && u[3] == 1.0f / 350.0f);
    assert(u[4] == 3840.0f && u[5] == 2160.0f);
    assert(std::fabs(u[6] * 3840.0f - 1.0f) < 1e-6f && std::fabs(u[7] * 2160.0f - 1.0f) < 1e-6f);
    assert(scale_gather_grid(u, 2, 640, 360, 3.0f, 3.0f) == 0);  // scaled once
    assert(scale_gather_grid(nullptr, 2, 640, 360, 3.0f, 3.0f) == 0);
}

void test_classification() {
    unsigned char zeros[2112] = {};
    assert(pixel_program(zeros, sizeof zeros).fix == Fix::None);
    assert(pixel_program(zeros, 448).fix == Fix::None);
    assert(pixel_program(zeros, 656).fix == Fix::None);  // the bloom extract's size, another program
    assert(pixel_program_for(656, 0x3a8d0f3808f1501bull).fix == Fix::GuestTaps);  // the bloom extract
    assert(pixel_program_for(2112, 0x62ae9ab4cb625546ull).fix == Fix::GatherRays);  // the sun shadow mask
    assert(pixel_program(nullptr, 2112).units == 0);
    // pixel_program hashes the sizes of the same table pixel_program_for reads
    for (const KnownProgram& known : kPrograms) {
        assert(pixel_program_for(known.size, known.hash).fix == known.program.fix);
        assert(pixel_program_for(known.size + 4, known.hash).fix == Fix::None);
    }
    assert(gfx::program_kind(zeros, 560, false) == gfx::ProgramKind::Other);  // the point-light passes' sizes
    assert(gfx::program_kind(zeros, 688, false) == gfx::ProgramKind::Other);
    assert(gfx::program_kind(zeros, 656, false) == gfx::ProgramKind::Other);  // the bloom chain's sizes
    assert(gfx::program_kind(zeros, 768, false) == gfx::ProgramKind::Other);
    assert(gfx::program_kind(zeros, 416, false) == gfx::ProgramKind::Other);  // the chain's helpers' size
    assert(gfx::program_kind(nullptr, 768, false) == gfx::ProgramKind::Other);
    assert(gfx::size_request(gfx::ProgramKind::BloomHelper).claim == gfx::TargetSize::Internal);  // never claims
    assert(gfx::size_request(gfx::ProgramKind::DepthDownsample).claim == gfx::TargetSize::Internal);
    assert(gfx::size_request(gfx::ProgramKind::DepthDownsample).renews == gfx::TargetSize::Light);
    assert(gfx::size_request(gfx::ProgramKind::PointLight).claim == gfx::TargetSize::Light);
}
}  // namespace

int main() {
    test_gather_ray_math();
    test_rewrite_gather_rays();
    test_light_buffer_blur();
    test_guest_taps_rewritten();
    test_console_grid_fallback();
    test_gather_grid();
    test_classification();
    std::puts("area_sample: ok");
    return 0;
}
