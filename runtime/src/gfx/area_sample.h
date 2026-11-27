#pragma once
// Pixel-shader fixes for upscaled render targets, shared by the Metal and the Vulkan renderer:
// blur taps on the console's grid (issues #66, #67, #307) and the shadow mask's gather spacing (issue #135).
//
// Blur taps on the console's grid.
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
// The taps go through the console-grid helper (as the bloom extract's, below), which reads what the
// console's bilinear tap reads: the two guest texels around it, each the average of the pixels under
// it. The first fix averaged one guest texel's worth of pixels around each tap instead (the area
// helper). That keeps the taps' footprint but not their grid: per axis a one-texel box at +-1.36
// texels, which passes about a third of the occlusion noise at a period near 2.7 guest texels (8
// pixels at 3x) where the console's filter passes about 3%, so a fainter grid stayed in the AO. The
// light buffer has one mip level, so its taps read level 0 directly (Fix::GuestTapsLevel0): the
// explicit gradients the bloom extract's taps need made the blur about 0.3 ms slower at 3x. On one
// level a sampler's LOD bias only picks the minification or the magnification filter, and the blur's
// sampler has no bias and filters linearly both ways, so the level-0 reads leave it out. At scale 1
// (or for buffers that are not scaled) the helper takes the one sample the game asked for, so the
// native picture is unchanged. The console's grid is only exact when every guest texel covers k x k
// whole host texels; otherwise (fractional scales, or a size rounded up from a scale just off a whole
// number, where the grid would drift by texels across the screen) it falls back to the area helper.
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
//
// Shadow blur, depth-aware or hybrid (render::shadow_edges, off by default; Vulkan). The game softens
// the sun shadow mask (G of the 960x540 light buffer) with the Gaussian above, in place, then shrinks it to 240x135 (the
// copy 44B43800), blurs it again and enlarges it to 480x270 as the buffer's second mip level; the lit
// materials take the shadow from that level. None of it looks at depth, so a sunlit object in front of
// a shadowed wall or floor spread its "lit" a few console pixels onto it: the shadow stopped short of
// the object, with a lit border, at 1x as on the console. The mask itself also placed each pixel at
// the farthest of the four depths it gathers, so the edge pixels of every object took the shadow of the
// background behind them (a lit line along a wall's top against the sky). With the option on, the
// renderers follow which targets hold the mask and its chain (gfx::ProgramKind::SunChain) and draw every
// step a second time into a private twin of its target, the full-size steps at kShadowTwinScale of the
// scene's resolution (the smaller ones at most 4 pixels per console texel), so each pixel has its own depth
// and the shrink does not mix an object into its background. Those draws use a translation of the mask of
// their own, which uses each pixel's own depth and writes a depth key into A (detail::shadow_key; every twin
// draw writes A; the game's draw keeps the game's mask), and get a negative tex0 scale, with which the
// Gaussian weighs every sample by how close its key is to the pixel's own surface (rewrite_edge_blur). Each
// of the game's targets then takes its twin's shadow (G, B) once, from its last step; their size, ambient
// occlusion (R) and A stay as the game drew them. Hybrid (render::shadow_rims) makes the blur one-sided: an
// object's edge still takes the light or shadow of the background behind it, close to the game's blur, but
// the background takes nothing from the object. Off, or wherever else these programs run, the scale stays
// positive and the original sums are used.
#include <bit>
#include <cmath>
#include <utility>
#include <cstdint>
#include <cstring>
#include <string>

namespace gfx::area_sample {

// The shadow twins of the chain's full-size steps (the mask and its Gaussian) at this fraction of the scene's
// resolution: 3 twin pixels to 4 scene pixels, 0.75 being exact in binary (shadow_key's mapping relies on it).
inline constexpr float kShadowTwinScale = 0.75f;

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
enum class Fix : uint8_t { None, GuestTaps, GuestTapsLevel0, GatherRays };
struct Program {
    uint32_t units = 0;
    Fix fix = Fix::None;
};
struct KnownProgram {
    uint32_t size;
    uint64_t hash;
    Program program;
};
inline constexpr KnownProgram kPrograms[] = {
    {448, 0x51a7ccdf69184627ull, {1u, Fix::GuestTapsLevel0}},  // PS 44B3AB00, horizontal Gaussian
    {448, 0x16285301a96cbf8dull, {1u, Fix::GuestTapsLevel0}},  // PS 44B39A00, vertical Gaussian
    {656, 0x3a8d0f3808f1501bull, {3u, Fix::GuestTaps}},        // PS 44F91200, bloom extract
    {2112, 0x62ae9ab4cb625546ull, {1u, Fix::GatherRays}},      // sun shadow mask
};
inline Program pixel_program_for(uint32_t size, uint64_t hash) {
    for (const KnownProgram& known : kPrograms)
        if (known.size == size && known.hash == hash) return known.program;
    return {};
}
// Hashes only programs of a size in the table.
inline Program pixel_program(const void* bytes, uint32_t size) {
    if (!bytes) return {};
    for (const KnownProgram& known : kPrograms)
        if (known.size == size) return pixel_program_for(size, program_hash(bytes, size));
    return {};
}

// Whether an upscaled w x h texture, sx x sy host texels per guest texel, takes the console's grid:
// k x k whole host texels under every guest texel, exactly. The same test is in the shader helpers
// below; other sizes take the area helper, and scale 1 the game's own sample.
inline bool on_console_grid(uint32_t w, uint32_t h, float sx, float sy) {
    const float kx = std::floor(sx + 0.5f), ky = std::floor(sy + 0.5f);
    if (kx <= 1.0f && ky <= 1.0f) return false;
    return std::round(float(w) / sx) * kx == float(w) && std::round(float(h) / sy) * ky == float(h);
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
// console-grid helper (Fix::GuestTaps) or, with level0, its variant that reads level 0 directly
// (Fix::GuestTapsLevel0), and inserts the helpers, the area helper among them as their fallback.
// Returns the number of calls rewritten (0: source left unchanged).
inline int rewrite_area_taps(std::string& src, uint32_t units, bool msl, bool level0 = false) {
    const size_t entry = src.find(msl ? "fragment FragmentOut main0" : "void main(");
    if (!units || entry == std::string::npos) return 0;
    int count = 0;
    for (uint32_t unit = 0; unit < 32; unit++) {
        if (!(units >> unit & 1)) continue;
        const std::string n = std::to_string(unit);
        const std::string from = msl ? "tex" + n + ".sample(samplr" + n + ", " : "texture(textureUnitPS" + n + ", ";
        const std::string fn = msl ? (level0 ? "wwhd_guest_sample_level0(" : "wwhd_guest_sample(")
                                   : (level0 ? "wwhdGuestSampleLevel0(" : "wwhdGuestSample(");
        const std::string to = msl ? fn + "tex" + n + ", samplr" + n + ", supportBuffer.tex" + n + "Scale, "
                                   : fn + "textureUnitPS" + n + ", uf_tex" + n + "Scale, ";
        int calls = 0;
        for (size_t at = src.find(from); at != std::string::npos; at = src.find(from, at + to.size())) {
            src.replace(at, from.size(), to);
            calls++;
        }
        // Metal's bias() can't be read back as a number, so the helpers take the support buffer's
        // float (as in GLSL), which the sample calls of this unit wrap in bias()
        const std::string lodBias = "supportBuffer.tex" + n + "LodBias", wrapped = "bias(" + lodBias + ")";
        if (msl && calls)
            for (size_t at = src.find(wrapped); at != std::string::npos; at = src.find(wrapped, at))
                src.replace(at, wrapped.size(), lodBias);
        count += calls;
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
        "static float4 wwhd_area_sample(texture2d<float> t, sampler s, float2 scale, float2 uv, float lodBias) {\n"
        "    float2 k = ceil(scale - 0.001);\n"
        "    if (k.x <= 1.0 && k.y <= 1.0) return t.sample(s, uv, bias(lodBias));\n"
        "    float2 step = scale / (float2(t.get_width(), t.get_height()) * k);\n"
        "    float4 sum = float4(0.0);\n"
        "    for (float j = 0.5; j < k.y; j += 1.0)\n"
        "        for (float i = 0.5; i < k.x; i += 1.0) sum += t.sample(s, uv + (float2(i, j) - 0.5 * k) * step, bias(lodBias));\n"
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
        "    float2 size = float2(t.get_width(), t.get_height());  // k x k whole host texels per guest texel, exactly\n"
        "    if (any(round(size / scale) * r != size)) return wwhd_area_sample(t, s, scale, uv);\n"
        "    float2 dx = dfdx(uv), dy = dfdy(uv);  // the tap's own footprint: the fetch positions jump between console texels\n"
        "#define WWHD_FETCH(p) t.sample(s, p, gradient2d(dx, dy))\n"
        "    WWHD_GUEST_SAMPLE(WWHD_FETCH)\n"
        "#undef WWHD_FETCH\n"
        "}\n"
        "static float4 wwhd_guest_sample(texture2d<float> t, sampler s, float2 scale, float2 uv, float lodBias) {\n"
        "    float2 r = floor(scale + 0.5);\n"
        "    if (r.x <= 1.0 && r.y <= 1.0) return t.sample(s, uv, bias(lodBias));\n"
        "    float2 size = float2(t.get_width(), t.get_height());  // k x k whole host texels per guest texel, exactly\n"
        "    if (any(round(size / scale) * r != size)) return wwhd_area_sample(t, s, scale, uv, lodBias);\n"
        "    float2 dx = dfdx(uv) * exp2(lodBias), dy = dfdy(uv) * exp2(lodBias);\n"
        "#define WWHD_FETCH(p) t.sample(s, p, gradient2d(dx, dy))\n"
        "    WWHD_GUEST_SAMPLE(WWHD_FETCH)\n"
        "#undef WWHD_FETCH\n"
        "}\n"
        "// the console-grid tap reading level 0 directly, for one-level textures with no LOD bias to apply\n"
        "// (runtime/src/gfx/area_sample.h)\n"
        "static float4 wwhd_guest_sample_level0(texture2d<float> t, sampler s, float2 scale, float2 uv) {\n"
        "    float2 r = floor(scale + 0.5);\n"
        "    if (r.x <= 1.0 && r.y <= 1.0) return t.sample(s, uv);\n"
        "    float2 size = float2(t.get_width(), t.get_height());  // k x k whole host texels per guest texel, exactly\n"
        "    if (any(round(size / scale) * r != size)) return wwhd_area_sample(t, s, scale, uv);\n"
        "#define WWHD_FETCH(p) t.sample(s, p, level(0.0))\n"
        "    WWHD_GUEST_SAMPLE(WWHD_FETCH)\n"
        "#undef WWHD_FETCH\n"
        "}\n"
        "static float4 wwhd_guest_sample_level0(texture2d<float> t, sampler s, float2 scale, float2 uv, float lodBias) {\n"
        "    float2 r = floor(scale + 0.5);\n"
        "    if (r.x <= 1.0 && r.y <= 1.0) return t.sample(s, uv, bias(lodBias));\n"
        "    float2 size = float2(t.get_width(), t.get_height());  // k x k whole host texels per guest texel, exactly\n"
        "    if (any(round(size / scale) * r != size)) return wwhd_area_sample(t, s, scale, uv, lodBias);\n"
        "#define WWHD_FETCH(p) t.sample(s, p, level(0.0))\n"
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
        "    vec2 size = vec2(textureSize(t, 0));  // k x k whole host texels per guest texel, exactly\n"
        "    if (any(notEqual(round(size / scale) * r, size))) return wwhdAreaSample(t, scale, uv);\n"
        "    vec2 dx = dFdx(uv), dy = dFdy(uv);  // the tap's own footprint: the fetch positions jump between console texels\n"
        "#define WWHD_FETCH(p) textureGrad(t, p, dx, dy)\n"
        "    WWHD_GUEST_SAMPLE(WWHD_FETCH)\n"
        "#undef WWHD_FETCH\n"
        "}\n"
        "vec4 wwhdGuestSample(sampler2D t, vec2 scale, vec2 uv, float lodBias) {\n"
        "    vec2 r = floor(scale + 0.5);\n"
        "    if (r.x <= 1.0 && r.y <= 1.0) return texture(t, uv, lodBias);\n"
        "    vec2 size = vec2(textureSize(t, 0));  // k x k whole host texels per guest texel, exactly\n"
        "    if (any(notEqual(round(size / scale) * r, size))) return wwhdAreaSample(t, scale, uv, lodBias);\n"
        "    vec2 dx = dFdx(uv) * exp2(lodBias), dy = dFdy(uv) * exp2(lodBias);\n"
        "#define WWHD_FETCH(p) textureGrad(t, p, dx, dy)\n"
        "    WWHD_GUEST_SAMPLE(WWHD_FETCH)\n"
        "#undef WWHD_FETCH\n"
        "}\n"
        "// the console-grid tap reading level 0 directly, for one-level textures with no LOD bias to apply\n"
        "// (runtime/src/gfx/area_sample.h)\n"
        "vec4 wwhdGuestSampleLevel0(sampler2D t, vec2 scale, vec2 uv) {\n"
        "    vec2 r = floor(scale + 0.5);\n"
        "    if (r.x <= 1.0 && r.y <= 1.0) return texture(t, uv);\n"
        "    vec2 size = vec2(textureSize(t, 0));  // k x k whole host texels per guest texel, exactly\n"
        "    if (any(notEqual(round(size / scale) * r, size))) return wwhdAreaSample(t, scale, uv);\n"
        "#define WWHD_FETCH(p) textureLod(t, p, 0.0)\n"
        "    WWHD_GUEST_SAMPLE(WWHD_FETCH)\n"
        "#undef WWHD_FETCH\n"
        "}\n"
        "vec4 wwhdGuestSampleLevel0(sampler2D t, vec2 scale, vec2 uv, float lodBias) {\n"
        "    vec2 r = floor(scale + 0.5);\n"
        "    if (r.x <= 1.0 && r.y <= 1.0) return texture(t, uv, lodBias);\n"
        "    vec2 size = vec2(textureSize(t, 0));  // k x k whole host texels per guest texel, exactly\n"
        "    if (any(notEqual(round(size / scale) * r, size))) return wwhdAreaSample(t, scale, uv, lodBias);\n"
        "#define WWHD_FETCH(p) textureLod(t, p, 0.0)\n"
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

// "passPixelColor0 = <value>;" as decompiled, plain or in the NaN guard's wwhd_finite() (LatteDecompiler.h
// finiteColorOutputs): its position and length, and whether the guard wraps it
struct ColourOutput {
    size_t at = std::string::npos, size = 0;
    bool finite = false;
};
inline ColourOutput find_colour_output(const std::string& src, const std::string& value) {
    const std::string plain = "passPixelColor0 = " + value + ";", guarded = "passPixelColor0 = wwhd_finite(" + value + ");";
    if (size_t at = src.find(plain); at != std::string::npos) return {at, plain.size(), false};
    if (size_t at = src.find(guarded); at != std::string::npos) return {at, guarded.size(), true};
    return {};
}
// the replacement export, guarded as the one it replaces
inline std::string colour_output(const std::string& value, bool finite) {
    return "passPixelColor0 = " + (finite ? "wwhd_finite(" + value + ")" : value) + ";";
}

// Depth-aware shadow softening (render::shadow_edges): in the twins' own translation of the mask
// (rewrite_gather_rays with twin; the game's draw keeps the game's mask, unchanged), the shadow mask
// places each pixel at its own depth (the depth texel under it: the renderers give the mask the scene's
// size) instead of the farthest of the four depths it gathers, which put the edge pixels of every
// object on the background behind them (a lit line along a wall's top against the sky). Its normal,
// N = cross(P2 - P0, P1 - P0) over three of the gathered depths (P1, P2 along the rays R1, R2), mixed an
// object's depth into the background's beside it. N sets both the normal offset of the shadow lookup and
// B, (1 - |N.L|)^p, a grazing-light term the materials read, so the background pixels along a silhouette
// could come out lit (a thin lit ring on the ground around Link seen from above). With the option on,
// the normal is rebuilt from the pixel's own depth and its neighbours along the same two directions,
// each edge from the side whose depth matches (the forward one, as the game, unless it is the
// discontinuity). The mask also writes a depth key into its twin: log2 of that view distance,
// (log2(d) + 4) / 24, at 16 bits: its 8-bit steps (1/10 of an octave each, 7% of the distance) in A, the
// fraction of a step in R (the twins' R is never merged into the game's). At 8 bits an object standing on
// the ground and the ground just behind its outline often fell in neighbouring steps, a whole step apart
// however close they were (Link's boots, which then lit a strip of the shadowed sand behind them).
// The view distance is rebuilt as the mask itself does: -c0.x / (1 - z c0.y).
// The twin of the mask has kShadowTwinScale of a scene pixel per scene pixel (the renderers' shadow_twin), so each
// of its pixels takes its depth from one scene pixel. Positions divide by the scale (0.75 is exact), as the
// renderers' mapping does: a product with its rounded inverse put some pixels a scene pixel off, across an outline
// from the depth the full-resolution read matches them by.
inline const std::string& twin_scale() {
    static const std::string scale = std::to_string(kShadowTwinScale);
    return scale;
}
inline bool shadow_key(std::string& src) {
    const size_t entry = src.find("void main(");
    const std::string farthest = "R2i.z = floatBitsToInt(mul_nonIEEE1(intBitsToFloat(backupReg1i), -(intBitsToFloat(uf_remappedPS[0].x))));";
    const std::string normal = "R126i.z = floatBitsToInt(mul_nonIEEE(intBitsToFloat(R126i.z), intBitsToFloat(PS0i)));";
    const ColourOutput out = find_colour_output(src, "vec4(intBitsToFloat(R4i.x), intBitsToFloat(R4i.y), intBitsToFloat(R4i.z), "
                                                     "intBitsToFloat(R4i.x))");
    const size_t f = src.find(farthest), n = src.find(normal), ex = out.at;
    if (entry == std::string::npos || f == std::string::npos || ex == std::string::npos || ex < f) return false;
    const bool ownNormal = n != std::string::npos && n > f && n < ex;
    // the later places first, so the earlier positions stay valid
    src.replace(ex, out.size,
                "vec2 wwhdKey = wwhdShadowKey(intBitsToFloat(R2i.z));\n" +
                    colour_output("vec4(wwhdKey.x, intBitsToFloat(R4i.y), intBitsToFloat(R4i.z), wwhdKey.y)", out.finite));
    if (ownNormal)  // the normalized N is (R127i.x, R127i.y, R126i.z) after this line
        src.insert(n + normal.size(),
                   "\n{\n"
                   "vec3 wwhdN = wwhdOwnNormal(textureUnitPS0, intBitsToFloat(uf_remappedPS[0].x), intBitsToFloat(uf_remappedPS[0].y), "
                   "passParameterSem0.xy, intBitsToFloat(R1i.xy), intBitsToFloat(R2i.xy), "
                   "vec3(intBitsToFloat(R127i.x), intBitsToFloat(R127i.y), intBitsToFloat(R126i.z)));\n"
                   "R127i.x = floatBitsToInt(wwhdN.x); R127i.y = floatBitsToInt(wwhdN.y); R126i.z = floatBitsToInt(wwhdN.z);\n"
                   "}");
    src.insert(f + farthest.size(),
               "\nR2i.z = floatBitsToInt(wwhdOwnDepth(textureUnitPS0, intBitsToFloat(uf_remappedPS[0].x),"
               " intBitsToFloat(uf_remappedPS[0].y)));");
    src.insert(entry,
               "// own depth, normal and depth key for the edge-aware shadow softening (runtime/src/gfx/area_sample.h)\n"
               "float wwhdViewZ(sampler2D depth, ivec2 p, float c0x, float c0y) {\n"
               "    p = clamp(p, ivec2(0), textureSize(depth, 0) - 1);\n"
               "    return (1.0 / (1.0 - texelFetch(depth, p, 0).x * c0y)) * -c0x;\n"
               "}\n"
               "float wwhdOwnDepth(sampler2D depth, float c0x, float c0y) {\n"
               "    return wwhdViewZ(depth, ivec2(gl_FragCoord.xy / " + twin_scale() + " + 0.001), c0x, c0y);\n"
               "}\n"
               "// the edge from P0 along one ray direction: forward, or backward where the forward neighbour is the\n"
               "// discontinuity (its depth twice as far from P0's as the backward one's)\n"
               "vec3 wwhdOwnEdge(sampler2D depth, ivec2 p, ivec2 o, vec2 r0, vec2 dr, float z0, float c0x, float c0y) {\n"
               "    float zf = wwhdViewZ(depth, p + o, c0x, c0y), zb = wwhdViewZ(depth, p - o, c0x, c0y);\n"
               "    vec3 p0 = vec3(r0 * z0, z0);\n"
               "    if (abs(zf - z0) > 2.0 * abs(zb - z0)) return p0 - vec3((r0 - dr) * zb, zb);\n"
               "    return vec3((r0 + dr) * zf, zf) - p0;\n"
               "}\n"
               "// N = cross(P2 - P0, P1 - P0) as the game builds it, from the pixel's own side of any silhouette\n"
               "vec3 wwhdOwnNormal(sampler2D depth, float c0x, float c0y, vec2 r0, vec2 r1, vec2 r2, vec3 game) {\n"
               // the change of the unmoved ray (the moved one, wwhdSem0, steps by a different amount per pixel)
               "    mat2 j = mat2(dFdx(passParameterSem0.xy), dFdy(passParameterSem0.xy)) * " + twin_scale() +
               ";  // the view ray's change per depth pixel\n"
               "    if (abs(determinant(j)) < 1e-20) return game;\n"
               "    mat2 ji = inverse(j);\n"
               "    ivec2 o1 = ivec2(round(ji * (r1 - r0))), o2 = ivec2(round(ji * (r2 - r0)));\n"
               "    if (o1 == ivec2(0) || o2 == ivec2(0) || o1 == o2) return game;\n"
               "    ivec2 p = ivec2(gl_FragCoord.xy / " + twin_scale() + " + 0.001);\n"
               "    float z0 = wwhdViewZ(depth, p, c0x, c0y);\n"
               "    vec3 e1 = wwhdOwnEdge(depth, p, o1, r0, j * vec2(o1), z0, c0x, c0y);\n"
               "    vec3 e2 = wwhdOwnEdge(depth, p, o2, r0, j * vec2(o2), z0, c0x, c0y);\n"
               "    vec3 n = cross(e2, e1);\n"
               "    float l = length(n);\n"
               "    return l > 1e-12 ? n / l : game;\n"
               "}\n"
               "// the depth key at 16 bits: (the fraction of a step for R, the 8-bit steps for A)\n"
               "vec2 wwhdShadowKey(float z) {\n"
               "    float k = clamp((log2(max(abs(z), 1e-6)) + 4.0) / 24.0, 0.0, 1.0) * 255.0;\n"
               "    float steps = floor(k);\n"
               "    return vec2(k - steps, steps / 255.0);\n"
               "}\n");
    // the pixel takes its depth from one scene pixel (wwhdOwnDepth): its rays (every varying) move to that pixel's
    // centre, so the position they rebuild lies on the surface that depth came from (off it, at an outline, the
    // pixel took the shadow of a point beside the object: a lit speck along a wall's top against the sky)
    const size_t open = src.find('{', src.find("void main("));
    if (open != std::string::npos) {
        std::string body = src.substr(open + 1), header =
            "\nvec2 wwhdShift = (floor(gl_FragCoord.xy / " + twin_scale() + " + 0.001) + 0.5) * " + twin_scale() +
            " - gl_FragCoord.xy;\n";
        for (int i = 0; i < 8; i++) {
            const std::string name = "passParameterSem" + std::to_string(i), moved = "wwhdSem" + std::to_string(i);
            bool used = false;
            for (size_t at = body.find(name); at != std::string::npos; at = body.find(name, at + moved.size())) {
                body.replace(at, name.size(), moved);
                used = true;
            }
            if (used)
                header += "vec4 " + moved + " = " + name + " + dFdx(" + name + ") * wwhdShift.x + dFdy(" + name +
                          ") * wwhdShift.y;\n";
        }
        src = src.substr(0, open + 1) + header + body;
    }
    return true;
}
}  // namespace detail

// The softening chain's Gaussian (44B3AB00, 44B39A00) with the tex0 scale negated: every sample of its
// three taps (one at the console's size, a footprint of up to 8x8 texels on upscaled buffers) keeps its
// weight only while its depth key (A and R, see detail::shadow_key) is within an 8-bit step of the pixel's
// own or of the pixel's surface continued to it (the key's slope from the neighbours on the side that
// is not a silhouette), falling to nothing at three steps (about 0.3 octave), so the mask of a lit
// foreground object does not spread onto the shadowed ground and walls behind it (and the reverse),
// even a wall only half as far again, while slanted ground still blurs along itself. The samples are
// whole texels: a filtered read straddling a silhouette mixes both keys into one that matches neither.
// The pixel's key passes on unchanged; the renderers let these draws write A and R (the game masks A off
// on some steps, which left the vertical passes without a key, and R off the mask, which left the key at
// 8 bits). This is the twins' translation only (rewrite with twin); the game's own is the console-grid
// taps alone (Fix::GuestTapsLevel0), as before the option, so Original runs none of it.
//
// Hybrid shadow blur (render::shadow_rims): a sample behind the pixel's surface keeps part of its weight, the
// value the renderers write into the 16 bytes of padding the decompiler puts before uf_tex0Scale
// (uf_fragCoordScaleCompatPadding, which nothing else uses; rims_offset). An object's edge then takes the
// light or shadow of the background behind it much as the game's blur gave it (the inner halo): a shaded
// rim the character materials turn into their rounded shading (a white hood stays lit where it faces the
// sun and turns blue where it curves away), or a lit edge against the sky. The background still takes
// nothing from the object in front of it: no lit border on it (the outer halo).
// Only part of the weight, because the game's background beside an object had already taken some of the
// object's light in the passes before; at full weight the rims come out wider than the game's. With no
// weight (depth-aware, or no padding) the blur keeps to the pixel's own surface both ways and objects are
// lit right up to their outline.
inline int rewrite_edge_blur(std::string& src, bool msl) {
    int count = rewrite_area_taps(src, 1u, msl, true);
    if (!count || msl) return count;
    const std::string call = "wwhdGuestSampleLevel0(textureUnitPS0, uf_tex0Scale, ";
    const detail::ColourOutput out = detail::find_colour_output(src, "vec4(R0f.x, R0f.y, R0f.z, R0f.w)");
    const size_t ex = out.at;
    int taps = 0;
    for (size_t at = src.find(call); at != std::string::npos; at = src.find(call, at + 1)) taps++;
    if (ex == std::string::npos || taps != 3) return count;  // another body: the console-grid taps only
    // the hybrid weight comes in the decompiler's padding before uf_tex0Scale (rims_offset)
    const bool padding = src.find("uniform vec4 uf_fragCoordScaleCompatPadding;") != std::string::npos;
    src.replace(ex, out.size,
                detail::colour_output(std::string("uf_tex0Scale.x < 0.0 ? wwhdEdgeBlur(textureUnitPS0, abs(uf_tex0Scale), ") +
                                          (padding ? "clamp(uf_fragCoordScaleCompatPadding.x, 0.0, 1.0)" : "0.0") +
                                          ") : vec4(R0f.x, R0f.y, R0f.z, R0f.w)",
                                      out.finite));
    int tap = 0;  // the taps in program order: the centre (weight 0.29), then the two outer ones (0.35)
    for (size_t at = src.find(call); at != std::string::npos; at = src.find(call, at + 1), tap++)
        src.replace(at, call.size(), "wwhdEdgeTap(textureUnitPS0, uf_tex0Scale, " + std::to_string(tap) + ", ");
    src.insert(src.find("void main("),
               "// edge-aware Gaussian for the shadow softening (runtime/src/gfx/area_sample.h)\n"
               "vec2 wwhdTapUv[3];\n"
               "// the edge blur reads its own texels: with the scale negated the game's tap is never used\n"
               "vec4 wwhdEdgeTap(sampler2D t, vec2 scale, int i, vec2 uv) {\n"
               "    wwhdTapUv[i] = uv;\n"
               "    if (scale.x < 0.0) return vec4(0.0);\n"
               "    return wwhdGuestSampleLevel0(t, abs(scale), uv);\n"
               "}\n"
               "// with the sampler's LOD bias (runtime/src/gfx/sampler_lod_bias.h)\n"
               "vec4 wwhdEdgeTap(sampler2D t, vec2 scale, int i, vec2 uv, float lodBias) {\n"
               "    wwhdTapUv[i] = uv;\n"
               "    if (scale.x < 0.0) return vec4(0.0);\n"
               "    return wwhdGuestSampleLevel0(t, abs(scale), uv, lodBias);\n"
               "}\n"
               "// the depth key at 16 bits (detail::shadow_key): its 8-bit steps in A, the fraction of one in R\n"
               "float wwhdKeyOf(vec4 v) {\n"
               "    return v.w + v.x / 255.0;\n"
               "}\n"
               "// the depth key's change per texel along o, from the side that continues the pixel's surface\n"
               "// (none at a ridge or a groove, where both neighbours are nearer or both farther)\n"
               "float wwhdKeySlope(sampler2D t, ivec2 p, ivec2 o, float own) {\n"
               "    ivec2 last = textureSize(t, 0) - 1;\n"
               "    float f = wwhdKeyOf(texelFetch(t, clamp(p + o, ivec2(0), last), 0)) - own;\n"
               "    float b = own - wwhdKeyOf(texelFetch(t, clamp(p - o, ivec2(0), last), 0));\n"
               "    return f * b <= 0.0 ? 0.0 : (abs(f) < abs(b) ? f : b) / float(o.x + o.y);\n"
               "}\n"
               "// one texel of the blur, weighted by how well its key continues the pixel's own surface (within a\n"
               "// 8-bit step of it, nothing from 3 steps); a texel behind that surface keeps at least the rims weight\n"
               "void wwhdEdgeAdd(sampler2D t, ivec2 q, float w, float own, vec2 slope, float rims, inout vec4 sum, inout float total) {\n"
               "    vec4 v = texelFetch(t, clamp(q, ivec2(0), textureSize(t, 0) - 1), 0);\n"
               "    float plain = wwhdKeyOf(v) - own, cont = plain - dot(slope, vec2(q - ivec2(gl_FragCoord.xy)));\n"
               "    float keep = clamp(1.5 - 127.5 * min(abs(plain), abs(cont)), 0.0, 1.0);\n"
               "    w *= plain > 0.0 && cont > 0.0 ? max(keep, rims) : keep;\n"
               "    sum += v * w;\n"
               "    total += w;\n"
               "}\n"
               "vec4 wwhdEdgeBlur(sampler2D t, vec2 scale, float rims) {\n"
               "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
               "    vec4 own = texelFetch(t, p, 0);\n"
               "    float key = wwhdKeyOf(own);\n"
               "    vec2 size = vec2(textureSize(t, 0));\n"
               "    vec2 k = min(ceil(scale - 0.001), vec2(8.0));\n"
               "    vec2 step = scale / (size * k);\n"
               "    int reach = int(clamp(k.x, 2.0, 8.0));\n"
               "    vec2 slope = vec2(wwhdKeySlope(t, p, ivec2(reach, 0), key), wwhdKeySlope(t, p, ivec2(0, reach), key));\n"
               "    const float tapWeight[3] = float[3](0.29411766, 0.3529412, 0.3529412);\n"
               "    vec4 sum = vec4(0.0);\n"
               "    float total = 0.0;\n"
               "    // whole texels: a filtered read between an object and the background behind it would mix both\n"
               "    // keys into one that matches neither, and bring the object's shadow along\n"
               "    for (int i = 0; i < 3; i++)\n"
               "        for (float y = 0.5; y < k.y; y += 1.0)\n"
               "            for (float x = 0.5; x < k.x; x += 1.0) {\n"
               "                vec2 f = (wwhdTapUv[i] + (vec2(x, y) - 0.5 * k) * step) * size - 0.5;\n"
               "                if (k.x * k.y > 1.0) {  // at least a sample per texel\n"
               "                    wwhdEdgeAdd(t, ivec2(floor(f + 0.5)), tapWeight[i], key, slope, rims, sum, total);\n"
               "                } else {  // the console's spacing: the bilinear tap, texel by texel\n"
               "                    ivec2 q = ivec2(floor(f));\n"
               "                    vec2 a = f - floor(f);\n"
               "                    wwhdEdgeAdd(t, q, tapWeight[i] * (1.0 - a.x) * (1.0 - a.y), key, slope, rims, sum, total);\n"
               "                    wwhdEdgeAdd(t, q + ivec2(1, 0), tapWeight[i] * a.x * (1.0 - a.y), key, slope, rims, sum, total);\n"
               "                    wwhdEdgeAdd(t, q + ivec2(0, 1), tapWeight[i] * (1.0 - a.x) * a.y, key, slope, rims, sum, total);\n"
               "                    wwhdEdgeAdd(t, q + ivec2(1, 1), tapWeight[i] * a.x * a.y, key, slope, rims, sum, total);\n"
               "                }\n"
               "            }\n"
               "    vec4 r = total > 1e-4 ? sum / total : own;\n"
               "    r.xw = own.xw;  // the key passes on unchanged\n"
               "    return r;\n"
               "}\n");
    return count;
}

// Rewrites the shadow mask's reads of the offset rays R1 and R2 to go through the helper (unit 0 is
// the gathered depth). twin: the translation for the twins' draws, with the depth key (detail::shadow_key).
// Returns the number of reads rewritten (0: source left unchanged).
inline int rewrite_gather_rays(std::string& src, bool msl, bool twin = false) {
    const size_t entry = src.find(msl ? "fragment FragmentOut main0" : "void main(");
    if (entry == std::string::npos) return 0;
    std::string centre, ray1, ray2;
    size_t at, at1, at2;
    if (!detail::find_varying(src, entry, 0, msl, at, centre) || !detail::find_varying(src, entry, 1, msl, at1, ray1) ||
        !detail::find_varying(src, entry, 2, msl, at2, ray2) || centre == ray1 || centre == ray2 || ray1 == ray2)
        return 0;
    // abs: the renderers give the twins' draws a negative scale (rewrite_edge_blur)
    const std::string scale = msl ? "abs(supportBuffer.tex0Scale)" : "abs(uf_tex0Scale)";
    const std::string fn = msl ? "wwhd_gather_ray(" : "wwhdGatherRay(";
    // the later read first, so the earlier position stays valid
    std::pair<size_t, std::string> reads[2] = {{at1, ray1}, {at2, ray2}};
    if (reads[0].first < reads[1].first) std::swap(reads[0], reads[1]);
    for (auto& [pos, name] : reads) src.replace(pos, name.size(), fn + centre + ", " + name + ", " + scale + ")");
    if (!msl && twin) detail::shadow_key(src);
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

// Where the renderer writes the hybrid shadow blur weight for a rewritten pixel shader (rewrite_edge_blur):
// the padding the decompiler declares just before uf_tex0Scale. -1: the shader has none.
inline int rims_offset(const std::string& glsl, int tex0ScaleOffset) {
    if (tex0ScaleOffset < 16 || glsl.find("uf_fragCoordScaleCompatPadding.x") == std::string::npos) return -1;
    if (glsl.find("uniform vec4 uf_fragCoordScaleCompatPadding;\nuniform vec2 uf_tex0Scale;") == std::string::npos &&
        glsl.find("uniform vec4 uf_fragCoordScaleCompatPadding;\r\nuniform vec2 uf_tex0Scale;") == std::string::npos)
        return -1;
    return tex0ScaleOffset - 16;
}

// Whether the twins' translation of this rewritten mask (Vulkan GLSL) gets the depth key.
inline bool shadow_keyable(const std::string& glsl) {
    std::string probe = glsl;
    return detail::shadow_key(probe);
}

// Applies the program's fix to its decompiled source (MSL or Vulkan GLSL). twin: the translation for the
// shadow twins' draws (rewrite_gather_rays). Returns the number of places rewritten (0: source left unchanged).
inline int rewrite(std::string& src, const Program& program, bool msl, bool twin = false) {
    switch (program.fix) {
    case Fix::GuestTaps: return rewrite_area_taps(src, program.units, msl);
    // the game's own translation of the chain's Gaussian is the console-grid taps alone; the twins' adds the
    // edge blur
    case Fix::GuestTapsLevel0: return twin ? rewrite_edge_blur(src, msl) : rewrite_area_taps(src, program.units, msl, true);
    case Fix::GatherRays: return program.units & 1u ? rewrite_gather_rays(src, msl, twin) : 0;
    default: return 0;
    }
}

}  // namespace gfx::area_sample
