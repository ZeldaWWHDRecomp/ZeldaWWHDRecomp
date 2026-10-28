#pragma once
// Pixel-shader fixes for upscaled render targets, shared by the Metal and the Vulkan renderer:
// area-sampled blur taps (issues #66, #67, #307) and the shadow mask's gather spacing (issue #135).
//
// Area-sampled blur taps.
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
// The bloom extract (PS 44F91200, issue #307) reads the scene with four bilinear taps on guest texel
// corners, each the average of a 2x2 block, so together they cover the 4x4 block under an output
// texel. On the upscaled scene those taps read single pixels: small bright things (rupees, eyes)
// glowed only where a tap landed. Its taps go through the console-grid helper instead, which reads
// exactly what the console's bilinear tap reads, from the pixels under those guest texels. The rest
// of the chain keeps the console's size (gfx::ProgramKind::BloomChain, target_scale in the
// renderers): its blur's taps are sparse lumps that the final bilinear upscale blends into a soft
// glow, while on an upscaled chain every lump got its own pixels and lights glowed as blocks.
//
// The decompiler declares uf_texNScale (texture pixels per guest pixel, filled from the surface's
// sx/sy by both renderers) for the units named in LatteDecompilerOptions::areaSampledTextures; the
// sample calls of those units are then rewritten here to go through the helper.
//
// Gather spacing of the sun shadow mask (issue #135). PS 68048F00 / 44D3BF00 (the address depends on
// where the game loads it) writes the shadow mask into the light buffer. For each pixel it gathers
// the 2x2 quad of the 1280x720 scene depth around it (tex0) and rebuilds view-space positions from
// those depths and three view rays its vertex shader passes in: the pixel's own (R0), one a guest
// texel further in y (R1) and one a guest texel further in x (R2). Their cross product is the
// surface normal, which sets the normal-offset bias of the shadow lookup and the softness term the
// later passes read (B, from 1 - |N.L|). On a depth buffer scaled by the internal resolution the
// gather returns texels 1/scale guest texels apart, but the rays stay one guest texel apart: the
// depth steps come out `scale` times too small for the lateral step, so every rebuilt normal leans
// towards the camera. How far |N.L| is then off depends on the angle between the view and the sun,
// so turning the camera switched the shadows between sharp and very soft at 2x/3x, with a halo
// around Link where the quad straddles his silhouette; at 1x everything matched. The fix moves the
// two offset rays to host-texel spacing (R0 + (Rn - R0) / scale), so the depths and the rays agree
// again and the normals come out as on the console, at the higher resolution.
//
// Grid of the half-size light buffer. Scenes with point lights (torches) are lit into a 640x360
// buffer next to a 640x360 copy of the depth, and every lit material reads the light back with a
// depth-aware upsample: from a uniform (2w, 2h, 1/2w, 1/2h), the screen's size for a w x h buffer,
// it puts the pixel on the buffer's grid, gathers the four depths around it and nudges one bilinear
// read of the light towards the texels whose depth matches its own. On upscaled buffers the gather
// returns texels a fraction of a grid cell apart while the grid stays the console's, so the light
// along silhouettes (a torch against the wall it lights) came out in console-sized blocks. For the
// units a shader gathers from, the renderers scale that uniform to the buffer's real size, so the
// grid is the upscaled buffer's texels (scale_gather_grid). With Full-size torchlight (on by default,
// off on Android, render::full_light) the buffer and its depth copy also get as many pixels as the screen
// (a gfx::TargetSize::Light claim by gfx::ProgramKind::PointLight, target_scale in the renderers): at half size a pixel on an object's
// edge often has no depth texel of its own and takes the light meant for the wall behind it, so with
// the flame's flicker lit objects had a blocky, flickering outline, at 1x too. Off keeps
// the console's half size.
#include <bit>
#include <utility>
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

// What a pixel shader needs on upscaled render targets: the texture units whose uf_texNScale the
// decompiler must declare (bit n = unit n) and how its source is rewritten.
enum class Fix : uint8_t { None, AreaTaps, GuestTaps, GatherRays };
struct Program {
    uint32_t units = 0;
    Fix fix = Fix::None;
};
inline Program pixel_program(const void* bytes, uint32_t size) {
    if (!bytes) return {};
    if (size == 448) {
        switch (program_hash(bytes, size)) {
        case 0x51a7ccdf69184627ull:  // PS 44B3AB00, horizontal Gaussian
        case 0x16285301a96cbf8dull:  // PS 44B39A00, vertical Gaussian
            return {1u, Fix::AreaTaps};
        default:
            return {};
        }
    }
    if (size == 656 && program_hash(bytes, size) == 0x3a8d0f3808f1501bull)  // PS 44F91200, bloom extract
        return {3u, Fix::GuestTaps};
    if (size == 2112 && program_hash(bytes, size) == 0x62ae9ab4cb625546ull)  // sun shadow mask
        return {1u, Fix::GatherRays};
    return {};
}
inline uint32_t units_for_pixel_shader(const void* bytes, uint32_t size) { return pixel_program(bytes, size).units; }

// The texture units a decompiled pixel shader (MSL or Vulkan GLSL) gathers from (bit n = unit n).
inline uint32_t gathered_units(const std::string& src, bool msl) {
    uint32_t units = 0;
    for (uint32_t unit = 0; unit < 32; unit++) {
        const std::string n = std::to_string(unit);
        if (src.find(msl ? "tex" + n + ".gather(" : "textureGather(textureUnitPS" + n + ",") != std::string::npos)
            units |= 1u << unit;
    }
    return units;
}

// The half-size light buffer's grid (see above): scales each vec4 of `uniforms` (count vec4s) that is
// (2w, 2h, 1/2w, 1/2h) for a gathered texture of guest size w x h to the texture's sx/sy texels per
// guest texel. Returns the number of vec4s scaled.
inline int scale_gather_grid(float* uniforms, size_t count, uint32_t w, uint32_t h, float sx, float sy) {
    if (!uniforms || !w || !h || (sx == 1.0f && sy == 1.0f)) return 0;
    const float gw = 2.0f * float(w), gh = 2.0f * float(h);
    int scaled = 0;
    for (size_t i = 0; i < count; i++) {
        float* v = uniforms + i * 4;
        if (v[0] != gw || v[1] != gh || v[2] * gw < 0.9999f || v[2] * gw > 1.0001f || v[3] * gh < 0.9999f ||
            v[3] * gh > 1.0001f)
            continue;
        v[0] *= sx; v[1] *= sy; v[2] /= sx; v[3] /= sy;
        scaled++;
    }
    return scaled;
}

// The shadow mask's offset view rays at host-texel spacing (see above): `scale` is the depth
// texture's pixels per guest pixel. The same formula is in the shader helpers below.
struct Ray { float x, y, z, w; };
inline Ray gather_ray(Ray centre, Ray ray, float scale_x, float scale_y) {
    if (scale_x == 1.0f && scale_y == 1.0f) return ray;
    return {centre.x + (ray.x - centre.x) / scale_x, centre.y + (ray.y - centre.y) / scale_y, ray.z, ray.w};
}

// Rewrites the sample calls of `units` in a decompiled pixel shader (MSL or Vulkan GLSL) to the
// helper and inserts the helper. Returns the number of calls rewritten (0: source left unchanged).
// guest: the taps go through the console-grid helper (Fix::GuestTaps) instead of the area helper.
inline int rewrite_area_taps(std::string& src, uint32_t units, bool msl, bool guest = false) {
    const size_t entry = src.find(msl ? "fragment FragmentOut main0" : "void main(");
    if (!units || entry == std::string::npos) return 0;
    int count = 0;
    for (uint32_t unit = 0; unit < 32; unit++) {
        if (!(units >> unit & 1)) continue;
        const std::string n = std::to_string(unit);
        const std::string from = msl ? "tex" + n + ".sample(samplr" + n + ", " : "texture(textureUnitPS" + n + ", ";
        const std::string fn = msl ? (guest ? "wwhd_guest_sample(" : "wwhd_area_sample(")
                                   : (guest ? "wwhdGuestSample(" : "wwhdAreaSample(");
        const std::string to = msl ? fn + "tex" + n + ", samplr" + n + ", supportBuffer.tex" + n + "Scale, "
                                   : fn + "textureUnitPS" + n + ", uf_tex" + n + "Scale, ";
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
        "}\n"
        "// with the sampler's LOD bias (runtime/src/gfx/sampler_lod_bias.h)\n"
        "static float4 wwhd_area_sample(texture2d<float> t, sampler s, float2 scale, float2 uv, bias lodBias) {\n"
        "    float2 k = ceil(scale - 0.001);\n"
        "    if (k.x <= 1.0 && k.y <= 1.0) return t.sample(s, uv, lodBias);\n"
        "    float2 step = scale / (float2(t.get_width(), t.get_height()) * k);\n"
        "    float4 sum = float4(0.0);\n"
        "    for (float j = 0.5; j < k.y; j += 1.0)\n"
        "        for (float i = 0.5; i < k.x; i += 1.0) sum += t.sample(s, uv + (float2(i, j) - 0.5 * k) * step, lodBias);\n"
        "    return sum / (k.x * k.y);\n"
        "}\n"
        "// a bilinear tap on the console's texel grid, read from a texture k (whole) times larger per axis:\n"
        "// per axis the two console texels around uv cover 2k host texels weighted (1-f)/k and f/k, fetched\n"
        "// in pairs with one bilinear sample each (runtime/src/gfx/area_sample.h)\n"
        "#define WWHD_GUEST_SAMPLE(FETCH)                                                                        \\\n"
        "    float2 k = floor(scale + 0.5);                                                                     \\\n"
        "    float2 host = float2(t.get_width(), t.get_height());                                               \\\n"
        "    float2 g = uv * host / k - 0.5;                                                                    \\\n"
        "    float2 i0 = floor(g);                                                                              \\\n"
        "    float2 f = g - i0;                                                                                 \\\n"
        "    float4 sum = float4(0.0);                                                                          \\\n"
        "    for (float m = 0.0; m < k.y; m += 1.0) {                                                           \\\n"
        "        float2 wy = float2(2.0 * m < k.y ? 1.0 - f.y : f.y, 2.0 * m + 1.0 < k.y ? 1.0 - f.y : f.y);    \\\n"
        "        float y = i0.y * k.y + 2.0 * m + 0.5 + wy.y / max(wy.x + wy.y, 1e-6);                         \\\n"
        "        for (float n = 0.0; n < k.x; n += 1.0) {                                                       \\\n"
        "            float2 wx = float2(2.0 * n < k.x ? 1.0 - f.x : f.x, 2.0 * n + 1.0 < k.x ? 1.0 - f.x : f.x);\\\n"
        "            float x = i0.x * k.x + 2.0 * n + 0.5 + wx.y / max(wx.x + wx.y, 1e-6);                     \\\n"
        "            sum += (wx.x + wx.y) * (wy.x + wy.y) * FETCH(float2(x, y) / host);                        \\\n"
        "        }                                                                                              \\\n"
        "    }                                                                                                  \\\n"
        "    return sum / (k.x * k.y);\n"
        "static float4 wwhd_guest_sample(texture2d<float> t, sampler s, float2 scale, float2 uv) {\n"
        "    float2 r = floor(scale + 0.5);\n"
        "    if (r.x <= 1.0 && r.y <= 1.0) return t.sample(s, uv);\n"
        "    if (any(abs(scale - r) > float2(0.01))) return wwhd_area_sample(t, s, scale, uv);\n"
        "    float2 dx = dfdx(uv), dy = dfdy(uv);  // the tap's own footprint: the fetch positions jump between console texels\n"
        "#define WWHD_FETCH(p) t.sample(s, p, gradient2d(dx, dy))\n"
        "    WWHD_GUEST_SAMPLE(WWHD_FETCH)\n"
        "#undef WWHD_FETCH\n"
        "}\n"
        "static float4 wwhd_guest_sample(texture2d<float> t, sampler s, float2 scale, float2 uv, bias lodBias) {\n"
        "    float2 r = floor(scale + 0.5);\n"
        "    if (r.x <= 1.0 && r.y <= 1.0) return t.sample(s, uv, lodBias);\n"
        "    if (any(abs(scale - r) > float2(0.01))) return wwhd_area_sample(t, s, scale, uv, lodBias);\n"
        "    float2 dx = dfdx(uv), dy = dfdy(uv);  // (the bloom samplers carry no LOD bias)\n"
        "#define WWHD_FETCH(p) t.sample(s, p, gradient2d(dx, dy))\n"
        "    WWHD_GUEST_SAMPLE(WWHD_FETCH)\n"
        "#undef WWHD_FETCH\n"
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
        "}\n"
        "// with the sampler's LOD bias (runtime/src/gfx/sampler_lod_bias.h)\n"
        "vec4 wwhdAreaSample(sampler2D t, vec2 scale, vec2 uv, float lodBias) {\n"
        "    vec2 k = ceil(scale - 0.001);\n"
        "    if (k.x <= 1.0 && k.y <= 1.0) return texture(t, uv, lodBias);\n"
        "    vec2 step = scale / (vec2(textureSize(t, 0)) * k);\n"
        "    vec4 sum = vec4(0.0);\n"
        "    for (float j = 0.5; j < k.y; j += 1.0)\n"
        "        for (float i = 0.5; i < k.x; i += 1.0) sum += texture(t, uv + (vec2(i, j) - 0.5 * k) * step, lodBias);\n"
        "    return sum / (k.x * k.y);\n"
        "}\n"
        "// a bilinear tap on the console's texel grid, read from a texture k (whole) times larger per axis:\n"
        "// per axis the two console texels around uv cover 2k host texels weighted (1-f)/k and f/k, fetched\n"
        "// in pairs with one bilinear sample each (runtime/src/gfx/area_sample.h)\n"
        "#define WWHD_GUEST_SAMPLE(FETCH)                                                                        \\\n"
        "    vec2 k = floor(scale + 0.5);                                                                       \\\n"
        "    vec2 host = vec2(textureSize(t, 0));                                                               \\\n"
        "    vec2 g = uv * host / k - 0.5;                                                                      \\\n"
        "    vec2 i0 = floor(g);                                                                                \\\n"
        "    vec2 f = g - i0;                                                                                   \\\n"
        "    vec4 sum = vec4(0.0);                                                                              \\\n"
        "    for (float m = 0.0; m < k.y; m += 1.0) {                                                           \\\n"
        "        vec2 wy = vec2(2.0 * m < k.y ? 1.0 - f.y : f.y, 2.0 * m + 1.0 < k.y ? 1.0 - f.y : f.y);        \\\n"
        "        float y = i0.y * k.y + 2.0 * m + 0.5 + wy.y / max(wy.x + wy.y, 1e-6);                         \\\n"
        "        for (float n = 0.0; n < k.x; n += 1.0) {                                                       \\\n"
        "            vec2 wx = vec2(2.0 * n < k.x ? 1.0 - f.x : f.x, 2.0 * n + 1.0 < k.x ? 1.0 - f.x : f.x);    \\\n"
        "            float x = i0.x * k.x + 2.0 * n + 0.5 + wx.y / max(wx.x + wx.y, 1e-6);                     \\\n"
        "            sum += (wx.x + wx.y) * (wy.x + wy.y) * FETCH(vec2(x, y) / host);                          \\\n"
        "        }                                                                                              \\\n"
        "    }                                                                                                  \\\n"
        "    return sum / (k.x * k.y);\n"
        "vec4 wwhdGuestSample(sampler2D t, vec2 scale, vec2 uv) {\n"
        "    vec2 r = floor(scale + 0.5);\n"
        "    if (r.x <= 1.0 && r.y <= 1.0) return texture(t, uv);\n"
        "    if (any(greaterThan(abs(scale - r), vec2(0.01)))) return wwhdAreaSample(t, scale, uv);\n"
        "    vec2 dx = dFdx(uv), dy = dFdy(uv);  // the tap's own footprint: the fetch positions jump between console texels\n"
        "#define WWHD_FETCH(p) textureGrad(t, p, dx, dy)\n"
        "    WWHD_GUEST_SAMPLE(WWHD_FETCH)\n"
        "#undef WWHD_FETCH\n"
        "}\n"
        "vec4 wwhdGuestSample(sampler2D t, vec2 scale, vec2 uv, float lodBias) {\n"
        "    vec2 r = floor(scale + 0.5);\n"
        "    if (r.x <= 1.0 && r.y <= 1.0) return texture(t, uv, lodBias);\n"
        "    if (any(greaterThan(abs(scale - r), vec2(0.01)))) return wwhdAreaSample(t, scale, uv, lodBias);\n"
        "    vec2 dx = dFdx(uv) * exp2(lodBias), dy = dFdy(uv) * exp2(lodBias);\n"
        "#define WWHD_FETCH(p) textureGrad(t, p, dx, dy)\n"
        "    WWHD_GUEST_SAMPLE(WWHD_FETCH)\n"
        "#undef WWHD_FETCH\n"
        "}\n";
    src.insert(src.find(msl ? "fragment FragmentOut main0" : "void main("), msl ? kMsl : kGlsl);
    return count;
}

namespace detail {
// the varying that the line "R<reg>i = ..." (or R<reg>f) in main reads: its position and name
inline bool find_varying(const std::string& src, size_t entry, int reg, bool msl, size_t& at, std::string& name) {
    const std::string r = "\nR" + std::to_string(reg);
    for (const char* type : {"i = ", "f = "}) {
        size_t line = src.find(r + type, entry);
        if (line == std::string::npos) continue;
        size_t end = src.find(';', line);
        size_t v = src.find(msl ? "in.passParameterSem" : "passParameterSem", line);
        if (end == std::string::npos || v == std::string::npos || v > end) return false;
        size_t e = src.find_first_not_of("0123456789", v + (msl ? 19 : 16));
        if (e == std::string::npos || e > end || e == v + (msl ? 19 : 16)) return false;
        at = v;
        name = src.substr(v, e - v);
        return true;
    }
    return false;
}
}  // namespace detail

// Rewrites the shadow mask's reads of the offset rays R1 and R2 to go through the helper (unit 0 is
// the gathered depth). Returns the number of reads rewritten (0: source left unchanged).
inline int rewrite_gather_rays(std::string& src, bool msl) {
    const size_t entry = src.find(msl ? "fragment FragmentOut main0" : "void main(");
    if (entry == std::string::npos) return 0;
    std::string centre, ray1, ray2;
    size_t at, at1, at2;
    if (!detail::find_varying(src, entry, 0, msl, at, centre) || !detail::find_varying(src, entry, 1, msl, at1, ray1) ||
        !detail::find_varying(src, entry, 2, msl, at2, ray2) || centre == ray1 || centre == ray2 || ray1 == ray2)
        return 0;
    const std::string scale = msl ? "supportBuffer.tex0Scale" : "uf_tex0Scale";
    const std::string fn = msl ? "wwhd_gather_ray(" : "wwhdGatherRay(";
    // the later read first, so the earlier position stays valid
    std::pair<size_t, std::string> reads[2] = {{at1, ray1}, {at2, ray2}};
    if (reads[0].first < reads[1].first) std::swap(reads[0], reads[1]);
    for (auto& [pos, name] : reads) src.replace(pos, name.size(), fn + centre + ", " + name + ", " + scale + ")");
    static const char* const kMsl =
        "// shadow-mask view ray at host-texel spacing (runtime/src/gfx/area_sample.h, issue #135)\n"
        "static float4 wwhd_gather_ray(float4 centre, float4 ray, float2 scale) {\n"
        "    if (all(scale == float2(1.0))) return ray;\n"
        "    return float4(centre.xy + (ray.xy - centre.xy) / scale, ray.zw);\n"
        "}\n";
    static const char* const kGlsl =
        "// shadow-mask view ray at host-texel spacing (runtime/src/gfx/area_sample.h, issue #135)\n"
        "vec4 wwhdGatherRay(vec4 centre, vec4 ray, vec2 scale) {\n"
        "    if (all(equal(scale, vec2(1.0)))) return ray;\n"
        "    return vec4(centre.xy + (ray.xy - centre.xy) / scale, ray.zw);\n"
        "}\n";
    src.insert(entry, msl ? kMsl : kGlsl);
    return 2;
}

// Applies the program's fix to its decompiled source (MSL or Vulkan GLSL). Returns the number of
// places rewritten (0: source left unchanged).
inline int rewrite(std::string& src, const Program& program, bool msl) {
    switch (program.fix) {
    case Fix::AreaTaps: return rewrite_area_taps(src, program.units, msl);
    case Fix::GuestTaps: return rewrite_area_taps(src, program.units, msl, true);
    case Fix::GatherRays: return program.units & 1u ? rewrite_gather_rays(src, msl) : 0;
    default: return 0;
    }
}

}  // namespace gfx::area_sample
