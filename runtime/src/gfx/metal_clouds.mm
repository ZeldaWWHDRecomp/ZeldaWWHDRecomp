// Cloud research prototype, Metal side (cloud_proto.cpp has the game side; off by default).
//
// WWHD_CLOUDS_PROTO=sdf     each sky cloud (vrkumo) of the game becomes a toon-shaded SDF cumulus:
//                           a camera-facing impostor quad per cloud, raymarched against a few
//                           smooth-unioned spheres with a flat base, displaced by a baked tiling 3D
//                           noise; 3 hard light bands from the game's sun and cloud palette, a cool
//                           underside, a sun-side rim and a noise-frayed fringe.
// WWHD_CLOUDS_PROTO=volume  one full-screen pass: a raymarched cloud slab whose coverage comes from the
//                           same game clouds, with posterised (banded) light (option D, for comparison)
// WWHD_CLOUDS_PROTO=hide    only skip the game's cards (A/B reference)
// WWHD_CLOUDS_PROTO=mark    keep the game's cards (timing reference with WWHD_CLOUDS_TIME)
// WWHD_CLOUDS_TIME=1        draws the clouds in their own command buffer and logs its GPU time
// WWHD_CLOUDS_REPEAT=n      draws the pass n times (timing only)
// WWHD_CLOUDS_LOG=1         logs the snapshot now and then (calibration)
// WWHD_SKY_GRADE=x          (sdf/volume/hide) 0..1: deepens the sky behind the clouds (research only)
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <atomic>
#include <string>
#include <unordered_map>

#include "cloud_proto.h"
#include "cloud_scene.h"
#include "cloud_noise.h"
#include "renderer.h"
#include "metal.h"
#include "runtime.h"

namespace gfx {

static const char* kCloudShader = R"(
#include <metal_stdlib>
using namespace metal;

struct Globals {
    float3x3 view;      // world (camera-relative) -> camera
    float4 proj;        // f/aspect, f, aspect, w: 1 = combined mode (D draws only the far banks)
    float4 sun;         // xyz: light direction (sun, or the moon at night), w: daylight 0..1
    float4 lit;         // rgb: palette lit colour (normalised), w: warmth (low sun) 0..1
    float4 sky;         // rgb: palette sky/fog colour, w: storm strength 0..1
    float4 params;      // x: band softness, y: noise strength, z: haze reduction, w: self-shadow steps
    float4 tune;        // x: fringe width, y: palette saturation, z: shadow level, w: mid level
    float4 extra;       // x: max march steps, y: 1 = cheap normals, z: clip-space depth, w: instance count
};
struct Inst {
    float4 center;      // xyz: cloud base centre (camera-relative world), w: width radius
    float4 shape;       // x: height/width, y: seed, z: alpha, w: distance blend (1 - falloff)
    float4 base;        // rgb: lit colour of this cloud, w: 1 = far (horizon bank)
};
struct VOut {
    float4 pos [[position]];
    float3 world;
    uint inst [[flat]];
};
struct FOut { float4 c [[color(0)]]; };

static float hash1(float n) { return fract(sin(n) * 43758.5453); }
static float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

// ---- shared toon shading: 3 hard bands, warm lit / cool shadow with a height-gradient ambient
// (sky above, sea bounce below: Cumulus' ambient idea), sun-side rim and a forward-scatter silver lining
struct Shade { float3 lit, mid, shadow, rim; };
static Shade palette(constant Globals& g, float3 base, float far) {
    Shade s;
    float storm = g.sky.w, warm = g.lit.w;
    float luma = dot(base, float3(0.3, 0.59, 0.11));
    // whiter at noon, the palette's own colour (gold, pink) when the sun is low
    float sat = mix(g.tune.y, 1.0, warm);
    float3 lit = mix(float3(luma), base, sat);
    // full brightness by day; at night the palette's own (dark) level
    float mx = max(max(base.r, max(base.g, base.b)), 1e-3);
    lit = lit / max(max(lit.r, max(lit.g, lit.b)), 1e-3) * mix(min(mx * 1.15, 1.0), 1.0, g.sun.w);
    lit *= 1.0 - 0.45 * storm;
    float3 skyAmb = mix(g.sky.rgb, float3(1.0), 0.42 * g.sun.w);  // no white ambient at night
    s.lit = lit;
    s.shadow = mix(lit * g.tune.z, skyAmb * mix(0.95, 0.55, storm), 0.48);
    s.mid = mix(lit * g.tune.w, skyAmb, 0.10);
    // low sun: golden-pink lit side, lavender shadows (warm/cool contrast)
    s.lit = mix(s.lit, s.lit * float3(1.0, 0.82, 0.62) + float3(0.0, 0.02, 0.04), warm * 0.8);
    s.shadow = mix(s.shadow, s.shadow * float3(0.80, 0.66, 0.92), warm);
    s.mid = mix(s.mid, s.mid * float3(0.95, 0.80, 0.82), warm * 0.5);
    s.mid = mix(s.mid, s.mid * float3(1.0, 0.86, 0.80), warm * 0.8);
    s.rim = min(mix(lit * 1.08 + 0.04, float3(1.0, 0.86, 0.55), warm), float3(1.0));
    float3 haze = mix(g.sky.rgb, float3(1.0), 0.55 * g.sun.w);
    float f = far * 0.32;
    s.lit = mix(s.lit, haze, f), s.mid = mix(s.mid, haze, f), s.shadow = mix(s.shadow, haze, f), s.rim = mix(s.rim, haze, f * 0.5);
    return s;
}
static float3 toon(constant Globals& g, Shade c, float3 n, float3 nDetail, float3 rd, float height, float shadowTerm) {
    float3 L = g.sun.xyz;
    float ndl = dot(n, L) * 0.5 + 0.5;
    float hw = 0.4 * (1.0 - 0.75 * g.lit.w);  // a low sun lights the sides it faces, not every top
    float lightv = (ndl * (1.0 - hw) + height * hw) * mix(0.45, 1.0, shadowTerm);
    float sw = g.params.x;
    float3 col = mix(c.shadow, c.mid, smoothstep(0.26 - sw, 0.26 + sw, lightv));
    col = mix(col, c.lit, smoothstep(0.40 - sw, 0.40 + sw, lightv));
    // sea bounce under the base: cooler, a little lighter than the side shadow
    float down = smoothstep(-0.3, -0.8, n.y);
    col = mix(col, c.shadow * float3(0.94, 1.0, 1.06), down * 0.7);
    // rim and silver lining: silhouette x light side; much stronger looking toward a low sun
    // silhouette from the smooth normal (outer edges only), nudged by the detail normal for a frayed line
    float facing = saturate(dot(normalize(mix(n, nDetail, 0.25)), -rd));
    float toward = saturate(dot(rd, L));
    float fwd = 0.5 + 3.0 * pow(toward, 2.0) * (0.3 + 0.7 * g.lit.w);
    float rim = pow(1.0 - facing, 2.5) * saturate(dot(n, L) + 0.55) * fwd * g.sun.w;
    col = mix(col, c.rim, smoothstep(0.32 - sw, 0.32 + sw, rim));
    return col;
}

// ---- C: one impostor per cloud, raymarched against smooth-unioned spheres
vertex VOut cl_vs(uint vid [[vertex_id]], uint iid [[instance_id]], constant Globals& g [[buffer(0)]],
                  const device Inst* inst [[buffer(1)]]) {
    Inst c = inst[iid];
    float2 corner = float2((vid & 1) ? 1.0 : -1.0, (vid & 2) ? 1.0 : -1.0);
    float r = c.center.w * max(1.0, c.shape.x) * 1.6;
    float3 mid = c.center.xyz + float3(0, c.center.w * c.shape.x * 0.6, 0);
    float3 right = float3(g.view[0][0], g.view[1][0], g.view[2][0]);
    float3 up = float3(g.view[0][1], g.view[1][1], g.view[2][1]);
    float3 p = mid + (corner.x * right + corner.y * up) * r;
    float3 q = g.view * p;
    VOut o;
    float w = -q.z;
    o.pos = float4(q.x * g.proj.x, q.y * g.proj.y, g.extra.z * w, w);
    o.world = p;
    o.inst = iid;
    return o;
}
static float blobs(float3 p, float hs, float seed) {
    float d = 1e9;
    for (int i = 0; i < 4; i++) {  // base row
        float fi = float(i);
        float x = -0.85 + fi * 0.57 + (hash1(seed + fi) - 0.5) * 0.25;
        float r = 0.42 + hash1(seed + fi + 7.0) * 0.16;
        float z = (hash1(seed + fi + 13.0) - 0.5) * 0.35;
        d = smin(d, length(p - float3(x, r * 0.75 * hs, z)) - r, 0.12);
    }
    for (int i = 0; i < 3; i++) {  // middle
        float fi = float(i);
        float x = -0.5 + fi * 0.5 + (hash1(seed + fi + 21.0) - 0.5) * 0.3;
        float r = 0.36 + hash1(seed + fi + 29.0) * 0.18;
        float y = (0.55 + hash1(seed + fi + 37.0) * 0.25) * hs;
        d = smin(d, length(p - float3(x, y, (hash1(seed + fi + 41.0) - 0.5) * 0.3)) - r, 0.1);
    }
    for (int i = 0; i < 2; i++) {  // crown
        float fi = float(i);
        float cx = (hash1(seed + 53.0 + fi) - 0.5) * 0.7;
        d = smin(d, length(p - float3(cx, (0.95 + hash1(seed + 59.0 + fi) * 0.25) * hs, 0.0)) -
                    (0.26 + hash1(seed + 61.0 + fi) * 0.12), 0.1);
    }
    return max(d, -(p.y - 0.06));  // flat base
}
static float sdf(float3 p, float hs, float seed, texture3d<float> noise, sampler s, float amp, float ero) {
    float d = blobs(p, hs, seed) + ero;
    if (d > 0.35) return d;
    float3 np = p * 0.8 + float3(seed * 0.37, 0.0, seed * 0.11);
    float n = noise.sample(s, np).g * 0.6 + noise.sample(s, np * 2.7).r * 0.4;
    return d + (0.5 - n) * amp;
}
fragment FOut cl_fs(VOut in [[stage_in]], constant Globals& g [[buffer(0)]], const device Inst* inst [[buffer(1)]],
                    texture3d<float> noise [[texture(0)]], sampler s [[sampler(0)]]) {
    Inst c = inst[in.inst];
    float hs = c.shape.x, seed = c.shape.y, amp = g.params.y;
    float ero = (1.0 - saturate(c.shape.z * 1.25)) * 0.55;  // the game's fade erodes the shape
    float3 rd = normalize(in.world);
    float3 ro = -c.center.xyz / c.center.w;
    float3 bc = float3(0, 0.6 * hs, 0);
    float br = 1.5 * max(1.0, hs);
    float3 oc = ro - bc;
    float b = dot(oc, rd), cc = dot(oc, oc) - br * br, h = b * b - cc;
    if (h < 0.0) discard_fragment();
    h = sqrt(h);
    float t = max(-b - h, 0.0), t1 = -b + h;
    float dmin = 1e9, tmin = t;
    bool hit = false;
    float3 p;
    float eps = 0.0015 * t;
    for (int i = 0; i < int(g.extra.x) && t < t1; i++) {
        p = ro + rd * t;
        float d = sdf(p, hs, seed, noise, s, amp, ero);
        if (d < dmin) { dmin = d; tmin = t; }
        if (d < eps) { hit = true; break; }
        t += max(d * 0.8, 0.008);
    }
    Shade pal = palette(g, c.base.rgb, c.shape.w);
    FOut o;
    if (!hit) {  // frayed fringe just outside the surface
        float fw = g.tune.x;
        if (dmin > fw) discard_fragment();
        float3 q = ro + rd * tmin;
        float n = noise.sample(s, q * 1.9 + seed).g;
        float a = (1.0 - dmin / fw) * smoothstep(0.4, 0.7, n);
        if (a < 0.05) discard_fragment();
        a = saturate(a * 2.0) * saturate(c.shape.z * 4.0);
        float3 col = mix(pal.mid, pal.lit, saturate(q.y / (1.2 * hs)));
        o.c = float4(col * a, a);
        return o;
    }
    float3 nb = normalize(float3( 1, -1, -1) * blobs(p + float3( 1, -1, -1) * 0.06, hs, seed) +
                          float3(-1, -1,  1) * blobs(p + float3(-1, -1,  1) * 0.06, hs, seed) +
                          float3(-1,  1, -1) * blobs(p + float3(-1,  1, -1) * 0.06, hs, seed) +
                          float3( 1,  1,  1) * blobs(p + float3( 1,  1,  1) * 0.06, hs, seed));
    float3 nd = nb;
    if (g.extra.y < 0.5) {
        const float e = 0.01;
        nd = normalize(float3( 1, -1, -1) * sdf(p + float3( 1, -1, -1) * e, hs, seed, noise, s, amp, ero) +
                       float3(-1, -1,  1) * sdf(p + float3(-1, -1,  1) * e, hs, seed, noise, s, amp, ero) +
                       float3(-1,  1, -1) * sdf(p + float3(-1,  1, -1) * e, hs, seed, noise, s, amp, ero) +
                       float3( 1,  1,  1) * sdf(p + float3( 1,  1,  1) * e, hs, seed, noise, s, amp, ero));
    }
    float3 n = normalize(mix(nb, nd, 0.15));
    float sh = 1.0;
    if (g.params.w > 0.0) {  // self-shadow: soft-shadow march toward the light through the blob field
        float ts = 0.06;
        for (int i = 0; i < int(g.params.w); i++) {
            float dd = blobs(p + g.sun.xyz * ts, hs, seed);
            sh = min(sh, saturate(6.0 * dd / ts + 0.5));
            ts += max(dd, 0.08);
        }
    }
    float3 col = toon(g, pal, n, nd, rd, saturate(p.y / (1.1 * hs)), sh);
    float a = saturate(c.shape.z * 6.0);
    o.c = float4(col * a, a);
    return o;
}

// ---- D: one full-screen pass over a merged cloud field. Each game cloud is a flat-based, dome-topped
// coverage blob; neighbouring blobs merge into banks; noise erodes billows; the first crossing of a
// density threshold is the toon surface (hard edge), shaded like C, with a density march toward the sun
// for self-shadowing.
struct FVOut { float4 pos [[position]]; float2 ndc; };
vertex FVOut vol_vs(uint vid [[vertex_id]], constant Globals& g [[buffer(0)]]) {
    float2 p = float2((vid << 1) & 2, vid & 2) * 2.0 - 1.0;
    FVOut o; o.pos = float4(p, g.extra.z, 1); o.ndc = p; return o;
}
struct Cand { int k; float t0, t1; };
constant float g_dfreq = 0.00042;
static float field(float3 p, const device Inst* inst, thread const Cand* cand, int nc, texture3d<float> noise, sampler s,
                   float amp, thread float& hgt, thread int& who, bool detail = true) {
    float cov = 0.0, rr = 1000.0;
    hgt = 0.0;
    for (int j = 0; j < nc; j++) {
        Inst c = inst[cand[j].k];
        float R = c.center.w * 1.7, H = c.center.w * c.shape.x * 1.9;
        float2 dxz = (p.xz - c.center.xz) / R;
        float fh = 1.0 - dot(dxz, dxz);
        if (fh <= 0.0) continue;
        float v = (p.y - c.center.y) / H;
        float prof = saturate(v * 6.0) * saturate(1.0 - v * v / max(fh, 0.05));
        float a = saturate(c.shape.z * 1.3) * prof * sqrt(fh);
        if (a > cov) { cov = a; hgt = saturate(v); who = cand[j].k; rr = R; }
    }
    if (cov <= 0.0) return -1.0;
    if (!detail) return cov - 0.30;
    float3 np = p / (rr * 0.85);
    float n = noise.sample(s, np).g * 0.6 + noise.sample(s, np * 2.9).r * 0.4;
    return cov - 0.30 - (0.55 - n) * amp * 1.25;
}
fragment FOut vol_fs(FVOut in [[stage_in]], constant Globals& g [[buffer(0)]], const device Inst* inst [[buffer(1)]],
                     texture3d<float> noise [[texture(0)]], sampler s [[sampler(0)]]) {
    float3 dc = normalize(float3(in.ndc.x / g.proj.x, in.ndc.y / g.proj.y, -1.0));
    float3 rd = transpose(g.view) * dc;
    // candidate clouds: ray vs each blob's vertical cylinder (up to 8, nearest first)
    Cand cand[8];
    int nc = 0;
    int count = int(g.extra.w);
    for (int k = 0; k < count; k++) {
        Inst c = inst[k];
        if (c.base.w < 0.5 && g.proj.w > 0.5) continue;  // combined mode: D draws only the far banks
        float R = c.center.w * 1.7, H = c.center.w * c.shape.x * 1.9;
        float2 o2 = -c.center.xz, d2 = rd.xz;
        float A = dot(d2, d2), B = dot(o2, d2), C = dot(o2, o2) - R * R;
        float disc = B * B - A * C;
        if (disc <= 0.0 || A < 1e-8) continue;
        disc = sqrt(disc);
        float t0 = (-B - disc) / A, t1 = (-B + disc) / A;
        // clip to the blob's height range
        if (abs(rd.y) > 1e-5) {
            float ya = (c.center.y - rd.y * 0.0) / rd.y, yb = (c.center.y + H) / rd.y;
            float lo = min(ya, yb), hi = max(ya, yb);
            t0 = max(t0, lo), t1 = min(t1, hi);
        } else if (c.center.y > 0.0 || c.center.y + H < 0.0) continue;
        t0 = max(t0, 0.0);
        if (t1 <= t0) continue;
        Cand nw = {k, t0, t1};
        int at = nc < 8 ? nc : 8;
        for (int j = 0; j < min(nc, 8); j++) if (t0 < cand[j].t0) { at = j; break; }
        if (at >= 8) continue;
        for (int j = min(nc, 7); j > at; j--) cand[j] = cand[j - 1];
        cand[at] = nw;
        nc = min(nc + 1, 8);
    }
    if (nc == 0) discard_fragment();
    float amp = g.params.y;
    float hgt; int who = cand[0].k;
    bool hit = false;
    float3 p;
    int steps = int(g.extra.x);
    for (int j = 0; j < nc && !hit; j++) {
        float dt = (cand[j].t1 - cand[j].t0) / float(steps);
        for (int i = 0; i <= steps; i++) {
            float t = cand[j].t0 + dt * float(i);
            float3 q = rd * t;
            float f = field(q, inst, cand, nc, noise, s, amp, hgt, who);
            if (f > 0.0) {
                // refine the crossing
                float a = i > 0 ? t - dt : t, b2 = t;
                for (int r = 0; r < 5; r++) {
                    float m = 0.5 * (a + b2);
                    if (field(rd * m, inst, cand, nc, noise, s, amp, hgt, who) > 0.0) b2 = m; else a = m;
                }
                p = rd * b2;
                hit = true;
                break;
            }
        }
    }
    if (!hit) discard_fragment();
    Inst c = inst[who];
    float hh; int ww;
    float e = c.center.w * 0.04;
    float f0 = field(p, inst, cand, nc, noise, s, amp, hh, ww);
    float3 grad = float3(field(p + float3(e, 0, 0), inst, cand, nc, noise, s, amp, hh, ww) - f0,
                         field(p + float3(0, e, 0), inst, cand, nc, noise, s, amp, hh, ww) - f0,
                         field(p + float3(0, 0, e), inst, cand, nc, noise, s, amp, hh, ww) - f0);
    float3 nd = normalize(-grad + float3(0, 1e-6, 0));
    // proxy normal from the smooth coverage field (clean toon bands), detail normal for the rim
    float e2 = c.center.w * 0.15;
    float g0 = field(p, inst, cand, nc, noise, s, amp, hh, ww, false);
    float3 gradC = float3(field(p + float3(e2, 0, 0), inst, cand, nc, noise, s, amp, hh, ww, false) - g0,
                          field(p + float3(0, e2, 0), inst, cand, nc, noise, s, amp, hh, ww, false) - g0,
                          field(p + float3(0, 0, e2), inst, cand, nc, noise, s, amp, hh, ww, false) - g0);
    float3 nc2 = normalize(-gradC + float3(0, 1e-6, 0));
    float3 n = normalize(mix(nc2, nd, 0.3));
    float sh = 1.0;
    if (g.params.w > 0.0) {
        float acc = 0.0;
        for (int i = 1; i <= int(g.params.w); i++) {
            float fs = field(p + g.sun.xyz * c.center.w * 0.12 * float(i), inst, cand, nc, noise, s, amp, hh, ww);
            acc += saturate(fs * 4.0);
        }
        sh = 1.0 - saturate(acc / max(g.params.w, 1.0) * 1.6);
    }
    Shade pal = palette(g, c.base.rgb, c.shape.w);
    float3 col = toon(g, pal, n, nd, rd, hgt, sh);
    float a = saturate(c.shape.z * 6.0);
    FOut o;
    o.c = float4(col * a, a);
    return o;
}

// ---- vivid sky: pull the horizon haze band (kasumi) back toward the sky colour (sky buffer, before clouds)
fragment FOut grade_fs(FVOut in [[stage_in]], constant Globals& g [[buffer(0)]]) {
    float3 dc = normalize(float3(in.ndc.x / g.proj.x, in.ndc.y / g.proj.y, -1.0));
    float3 rd = transpose(g.view) * dc;
    float band = (1.0 - smoothstep(0.0, 0.32, rd.y)) * step(-0.02, rd.y);
    float3 sky = g.sky.rgb * mix(0.95, 1.12, saturate(rd.y * 4.0));
    // keep a warm glow around a low sun
    float aroundSun = pow(saturate(dot(rd, g.sun.xyz)), 6.0) * g.lit.w;
    float a = saturate(g.params.z) * band * (1.0 - aroundSun);
    FOut o;
    o.c = float4(sky * a, a);
    return o;
}

vertex FVOut sh_vs(uint vid [[vertex_id]]) {
    float2 p = float2((vid << 1) & 2, vid & 2) * 2.0 - 1.0;
    FVOut o; o.pos = float4(p, 0.5, 1); o.ndc = p; return o;
}
// ---- cloud shadows. The shadow map is the cloud layer seen from above (camera-centred square, R8
// coverage); the apply pass reconstructs each opaque pixel's world position from depth, follows the
// light up to the cloud layer and darkens by the coverage there, in two toon steps.
struct ShadowGlobals {
    float4 view0, view1, view2;  // world (camera-relative) -> camera, columns
    float4 proj;                 // f/aspect, f, near, far
    float4 sun;                  // light direction, daylight
    float4 layer;                // x: cloud layer height above the sea, y: map half size, z: eye height, w: count
    float4 look;                 // x: darkness, y: softness, z: debug grid, w: depth mode (0 GL, 1 DX)
};
fragment FOut shmap_fs(FVOut in [[stage_in]], constant ShadowGlobals& g [[buffer(0)]],
                       constant float4* clouds [[buffer(1)]], texture3d<float> noise [[texture(0)]], sampler s [[sampler(0)]]) {
    float2 xz = float2(in.ndc.x, -in.ndc.y) * g.layer.y;  // camera-relative position in the cloud layer
    float cov = 0.0;
    for (int k = 0; k < int(g.layer.w); k++) {
        float4 c = clouds[k];
        float2 d = (xz - c.xy) / c.z;
        float r2 = dot(d, d);
        if (r2 >= 1.0) continue;
        cov = max(cov, saturate(c.w * 1.3) * (1.0 - r2));
    }
    // billowy, eroded edges (the same noise as the clouds)
    float n = noise.sample(s, float3(xz * 0.00035, 0.37)).g * 0.6 + noise.sample(s, float3(xz * 0.0011, 0.71)).r * 0.4;
    cov = saturate((cov - 0.18 - (0.55 - n) * 0.45) * 3.0);
    FOut o;
    o.c = float4(cov, cov, cov, 1);
    return o;
}
static float sh_hash(float2 p) { return fract(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
static float sh_vnoise(float2 p) {
    float2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(sh_hash(i), sh_hash(i + float2(1, 0)), f.x), mix(sh_hash(i + float2(0, 1)), sh_hash(i + float2(1, 1)), f.x), f.y);
}
fragment FOut shadow_apply_fs(FVOut in [[stage_in]], constant ShadowGlobals& g [[buffer(0)]],
                              depth2d<float> depth [[texture(0)]], texture2d<float> map [[texture(1)]],
                              sampler s [[sampler(0)]], device float* probe [[buffer(2)]],
                              constant float4* clouds [[buffer(1)]]) {
    uint2 px = uint2(in.pos.xy);
    const int dbg = int(g.look.z + 0.5);
    if (dbg == 9) {  // debug 9: depth at probe pixels (fractions of the screen) to the CPU
        const float2 at[6] = {float2(0.5, 0.75), float2(0.5, 0.6), float2(0.7, 0.45), float2(0.3, 0.45), float2(0.5, 0.92), float2(0.85, 0.35)};
        float2 sz = float2(depth.get_width(), depth.get_height());
        for (int i = 0; i < 6; i++)
            if (all(px == uint2(at[i] * sz))) probe[i] = depth.read(px);
    }
    if (dbg == 5) {  // debug 5: the shadow map itself, over the whole screen
        float c = map.sample(s, in.ndc * float2(0.5, -0.5) + 0.5).r;
        FOut o0;
        o0.c = float4(1.0 - 0.8 * c, 1.0 - 0.8 * c, 1.0, 1);
        return o0;
    }
    float d = depth.read(min(px, uint2(depth.get_width() - 1, depth.get_height() - 1)));
    if (dbg == 10) {  // debug 10: magenta wherever there is geometry
        FOut o0;
        o0.c = d < 1.0 ? float4(1, 0, 1, 1) : float4(1, 1, 1, 1);
        return o0;
    }
    if (dbg == 8) {  // debug 8: raw depth: red = exactly 1 (cleared), else grey by log distance to 1
        FOut o0;
        if (d >= 1.0) { o0.c = float4(1, 0.3, 0.3, 1); return o0; }
        float v = saturate(-log2(max(1.0 - d, 1e-7)) / 24.0);
        o0.c = float4(v, v, v, 1);
        return o0;
    }
    if (d >= 0.99999) discard_fragment();  // sky
    if (dbg == 7) {  // debug 7: which depth formula puts the sea at height 0
        float n0 = g.proj.z, f0 = g.proj.w;
        float zgl = 2.0 * f0 * n0 / ((f0 + n0) - (2.0 * d - 1.0) * (f0 - n0));
        float zdx = n0 * f0 / (f0 - d * (f0 - n0));
        float3 dc0 = float3(in.ndc.x / g.proj.x, in.ndc.y / g.proj.y, -1.0);
        float3x3 view0 = float3x3(g.view0.xyz, g.view1.xyz, g.view2.xyz);
        float3 rdw = transpose(view0) * dc0;  // world direction per unit of view depth
        float zsea = rdw.y < -1e-4 ? g.layer.z / -rdw.y : 1e9;  // view depth where the ray meets y = 0
        float rg = zgl / zsea, rx = zdx / zsea;
        FOut o0;
        o0.c = float4(abs(rg - 1.0) < 0.05 ? 1.0 : 0.5, abs(rx - 1.0) < 0.05 ? 1.0 : 0.5, 0.5, 1);
        return o0;
    }
    float n = g.proj.z, f = g.proj.w;
    float zlin = g.look.w > 0.5 ? n * f / (f - d * (f - n)) : 2.0 * f * n / ((f + n) - (2.0 * d - 1.0) * (f - n));
    float3 dc = float3(in.ndc.x / g.proj.x, in.ndc.y / g.proj.y, -1.0);
    float3 pc = dc * zlin;
    float3x3 view = float3x3(g.view0.xyz, g.view1.xyz, g.view2.xyz);
    float3 p = transpose(view) * pc;  // camera-relative world position
    float wy = p.y + g.layer.z;       // world height
    FOut o;
    if (dbg == 3) {  // debug 3: world height bands every 50 units, sea level (|y| < 25) tinted red
        float band = fract(wy / 100.0) < 0.5 ? 0.85 : 1.0;
        o.c = abs(wy) < 25.0 ? float4(1.0, 0.4, 0.4, 1) : float4(band, band, band, 1);
        return o;
    }
    if (dbg == 2) {  // debug 2: the reconstructed distance, as grey (dark = near)
        float v = saturate(zlin / 20000.0);
        o.c = float4(v, v, v, 1);
        return o;
    }
    if (dbg == 1) {  // debug: a world-space grid (must stay put on the sea when the camera moves)
        float2 w = p.xz + float2(0.0);
        float2 gl = abs(fract(w / 1000.0) - 0.5);
        float line = min(gl.x, gl.y) < 0.02 ? 0.5 : 1.0;
        o.c = float4(line, line, line, 1);
        return o;
    }
    float3 L = g.sun.xyz;
    float t = (g.layer.x - wy) / max(L.y, 0.05);
    float2 q = p.xz + L.xz * t;
    float2 uv = q / (2.0 * g.layer.y) + 0.5;
    if (dbg == 11) {  // debug 11: probes of uv, coverage, height
        const float2 at[6] = {float2(0.5, 0.75), float2(0.5, 0.6), float2(0.7, 0.45), float2(0.3, 0.45), float2(0.5, 0.92), float2(0.85, 0.35)};
        float2 sz = float2(depth.get_width(), depth.get_height());
        for (int i = 0; i < 6; i++)
            if (all(px == uint2(at[i] * sz))) { probe[i * 4] = uv.x; probe[i * 4 + 1] = uv.y; probe[i * 4 + 2] = t; probe[i * 4 + 3] = wy; }
    }
    // cloud coverage at q, as the Vulkan pass computes it (the game's clouds anchored in the world, eroded
    // by value noise)
    float cov = 0.0;
    for (int k = 0; k < int(g.layer.w); k++) {
        float4 c = clouds[k];
        float2 dd = (q - c.xy) / c.z;
        float r2 = dot(dd, dd);
        if (r2 < 1.0) cov = max(cov, saturate(c.w * 1.3) * (1.0 - r2));
    }
    float nz = sh_vnoise(q * 0.0011) * 0.6 + sh_vnoise(q * 0.0034) * 0.4;
    cov = saturate((cov - 0.18 - (0.55 - nz) * 0.45) * 3.0);
    if (dbg == 6) {  // debug 6: the map coordinates (bands every 1/20)
        float2 b = step(0.5, fract(uv * 200.0));
        o.c = float4(0.6 + 0.4 * b.x, 0.6 + 0.4 * b.y, 1.0, 1);
        return o;
    }
    if (dbg == 4) {  // debug 4: the raw coverage, strongly
        float kk = 1.0 - 0.8 * cov;
        o.c = float4(1.0, kk, kk, 1);
        return o;
    }
    // two toon steps: a light outer shade and the full shadow
    float sw = g.look.y;
    float shade = 0.45 * smoothstep(0.25 - sw, 0.25 + sw, cov) + 0.55 * smoothstep(0.6 - sw, 0.6 + sw, cov);
    // fade with distance so the far sea stays even
    float fade = 1.0 - smoothstep(12000.0, 22000.0, length(p.xz));
    float k = 1.0 - g.look.x * shade * fade * g.sun.w;
    if (k > 0.999) discard_fragment();
    o.c = float4(k, k, k, 1);
    return o;
}
)";

// a tiling 64^3 RG8 noise: R value-noise fbm, G inverted Worley (cells of 8) - generated here
static id<MTLTexture> make_noise() {
    const int N = cloud_noise::kSize;
    std::vector<uint8_t> data = cloud_noise::rg8();
    MTLTextureDescriptor* d = [MTLTextureDescriptor new];
    d.textureType = MTLTextureType3D;
    d.pixelFormat = MTLPixelFormatRG8Unorm;
    d.width = d.height = d.depth = N;
    d.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> t = [R.device newTextureWithDescriptor:d];
    [t replaceRegion:MTLRegionMake3D(0, 0, 0, N, N, N) mipmapLevel:0 slice:0 withBytes:data.data()
         bytesPerRow:N * 2 bytesPerImage:N * N * 2];
    return t;
}

using cloud_scene::Globals;
using cloud_scene::Inst;

static float envf(const char* n, float d) { const char* e = getenv(n); return e && *e ? (float)atof(e) : d; }
static bool env_on(const char* n) { const char* e = getenv(n); return e && *e && strcmp(e, "0"); }

// draws the clouds of snapshot `seq` into the open encoder's pass; vp: the game's viewport
enum { kGrade = 1, kClouds = 2 };
static void encode(id<MTLRenderCommandEncoder> enc, const cloud_proto::Snapshot& s, const MTLViewport& vp, int what,
                   id<MTLDepthStencilState> depthState = nil) {
    static id<MTLLibrary> lib;
    static id<MTLTexture> noise;
    static id<MTLSamplerState> samp;
    static id<MTLDepthStencilState> noDepth;
    static std::unordered_map<std::string, id<MTLRenderPipelineState>> pipes;
    if (!lib) {
        NSError* err = nil;
        lib = [R.device newLibraryWithSource:[NSString stringWithUTF8String:kCloudShader] options:nil error:&err];
        if (!lib) { LOG("[clouds] shader: %s", err.localizedDescription.UTF8String); return; }
        noise = make_noise();
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
        sd.sAddressMode = sd.tAddressMode = sd.rAddressMode = MTLSamplerAddressModeRepeat;
        samp = [R.device newSamplerStateWithDescriptor:sd];
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionAlways;
        dd.depthWriteEnabled = NO;
        noDepth = [R.device newDepthStencilStateWithDescriptor:dd];
        LOG("[clouds] prototype shaders ready (mode %d)", cloud_proto::mode());
    }
    if (!(what & kClouds) && render::horizon_haze() >= 1.0f) return;  // nothing to draw: leave the encoder alone
    const int mode = cloud_proto::mode();
    // pipeline for the open pass's attachment formats
    auto pipe = [&](NSString* vsName, NSString* fsName) -> id<MTLRenderPipelineState> {
        std::string key = std::string(vsName.UTF8String) + fsName.UTF8String;
        MTLRenderPipelineDescriptor* d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = [lib newFunctionWithName:vsName];
        d.fragmentFunction = [lib newFunctionWithName:fsName];
        for (int i = 0; i < 8; i++) {
            if (!R.passColor[i]) continue;
            MTLPixelFormat f = R.passColor[i]->tex.pixelFormat;
            d.colorAttachments[i].pixelFormat = f;
            d.rasterSampleCount = R.passColor[i]->tex.sampleCount;
            key += "/" + std::to_string((int)f);
            if (i == 0) {
                auto* a = d.colorAttachments[0];
                a.blendingEnabled = YES;
                a.sourceRGBBlendFactor = MTLBlendFactorOne;
                a.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                a.sourceAlphaBlendFactor = MTLBlendFactorZero;
                a.destinationAlphaBlendFactor = MTLBlendFactorOne;
            } else {
                d.colorAttachments[i].writeMask = MTLColorWriteMaskNone;
            }
        }
        if (R.passDepth) {
            d.depthAttachmentPixelFormat = R.passDepth->tex.pixelFormat;
            if (R.passDepth->fmt.stencil) d.stencilAttachmentPixelFormat = R.passDepth->tex.pixelFormat;
            key += "/d" + std::to_string((int)R.passDepth->tex.pixelFormat);
        }
        auto it = pipes.find(key);
        if (it != pipes.end()) return it->second;
        NSError* err = nil;
        id<MTLRenderPipelineState> p = [R.device newRenderPipelineStateWithDescriptor:d error:&err];
        if (!p) LOG("[clouds] pipeline %s: %s", key.c_str(), err.localizedDescription.UTF8String);
        else LOG("[clouds] pipeline %s", key.c_str());
        pipes[key] = p;
        return p;
    };

    Globals g;
    std::vector<Inst> sorted;
    cloud_scene::build(s, (float)(vp.width / std::fabs(vp.height)), mode, depthState != nil, R.frame, g, sorted);
    [enc setDepthStencilState:depthState ? depthState : noDepth];
    [enc setCullMode:MTLCullModeNone];
    [enc setDepthBias:0 slopeScale:0 clamp:0];
    [enc setViewport:vp];
    [enc setVertexBytes:&g length:sizeof g atIndex:0];
    [enc setFragmentBytes:&g length:sizeof g atIndex:0];
    if ((what & kGrade) && g.params[2] > 0) {
        if (id<MTLRenderPipelineState> p = pipe(@"vol_vs", @"grade_fs")) {
            [enc setRenderPipelineState:p];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
    }
    if (!(what & kClouds) || mode == cloud_proto::kHide || mode == cloud_proto::kMark || sorted.empty()) return;
    id<MTLBuffer> ib = [R.device newBufferWithBytes:sorted.data() length:sorted.size() * sizeof(Inst)
                                            options:MTLResourceStorageModeShared];
    [enc setVertexBuffer:ib offset:0 atIndex:1];
    [enc setFragmentBuffer:ib offset:0 atIndex:1];
    [enc setFragmentTexture:noise atIndex:0];
    [enc setFragmentSamplerState:samp atIndex:0];
    static const int repeat = getenv("WWHD_CLOUDS_REPEAT") ? std::max(1, atoi(getenv("WWHD_CLOUDS_REPEAT"))) : 1;
    if (mode == cloud_proto::kVolume || mode == cloud_proto::kCombo) {
        if (id<MTLRenderPipelineState> p = pipe(@"vol_vs", @"vol_fs")) {
            [enc setRenderPipelineState:p];
            for (int r = 0; r < repeat; r++) [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
    }
    if (mode == cloud_proto::kSdf || mode == cloud_proto::kCombo) {
        std::vector<Inst> near;
        const std::vector<Inst>* list = &sorted;
        if (mode == cloud_proto::kCombo) {
            for (auto& it : sorted) if (it.base[3] < 0.5f) near.push_back(it);
            list = &near;
            if (near.empty()) return;
            ib = [R.device newBufferWithBytes:near.data() length:near.size() * sizeof(Inst) options:MTLResourceStorageModeShared];
            [enc setVertexBuffer:ib offset:0 atIndex:1];
            [enc setFragmentBuffer:ib offset:0 atIndex:1];
            Globals g2 = g;
            g2.extra[0] = envf("WWHD_CLOUDS_STEPS_C", 48.0f);
            [enc setVertexBytes:&g2 length:sizeof g2 atIndex:0];
            [enc setFragmentBytes:&g2 length:sizeof g2 atIndex:0];
        }
        if (id<MTLRenderPipelineState> p = pipe(@"cl_vs", @"cl_fs")) {
            [enc setRenderPipelineState:p];
            for (int r = 0; r < repeat; r++)
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:list->size()];
        }
    }
}

// ---- GPU timing (WWHD_CLOUDS_TIME): the cloud draws get their own command buffer
static std::atomic<uint64_t> g_time_ns{0}, g_time_n{0};
static void timed_commit(const char* what) {
    end_encoder();
    if (!R.cmd) return;
    id<MTLCommandBuffer> cb = R.cmd;
    void pool_retire(id<MTLCommandBuffer> cmd);
    pool_retire(cb);
    [cb addCompletedHandler:^(id<MTLCommandBuffer> b) {
        double t = b.GPUEndTime - b.GPUStartTime;
        if (t > 0) { g_time_ns += (uint64_t)(t * 1e9); g_time_n++; }
    }];
    [cb commit];
    R.cmd = nil;
    uint64_t n = g_time_n.load();
    static uint64_t lastLog = 0;
    if (n >= lastLog + 300) {
        lastLog = n;
        LOG("[clouds] GPU time (%s): %.3f ms per cloud pass over %llu passes", what, g_time_ns.load() / 1e6 / (double)n,
            (unsigned long long)n);
        g_time_ns = 0, g_time_n = 0;
        lastLog = 0;
    }
}
void flush();

static void timed_commit(const char* what);
void flush();

// ---- full resolution (WWHD_CLOUDS_FULLRES=1): the clouds are drawn right after the draw that composites
// the half-res sky buffer into the full-resolution target, with that draw's depth test
static Surface* g_sky = nullptr;
static uint64_t g_skyFrame = ~0ull;
static uint32_t g_pendingSeq = 0, g_skySeq = 0;
static uint64_t g_composites = 0;
static Surface *g_shadowColor = nullptr, *g_shadowDepth = nullptr;
static uint32_t g_shadowSlice = 0, g_shadowSeq = 0;
static uint64_t g_shadowFrame = ~0ull;
bool clouds_sky_surface(Surface* s) { return (g_pendingSeq || g_skySeq) && s == g_sky && g_skyFrame == R.frame; }
static void draw_cloud_shadows(const cloud_proto::Snapshot& s, Surface* color, uint32_t colorSlice, Surface* depth);
void clouds_after_sky_composite(id<MTLRenderCommandEncoder> enc, const uint32_t* regs, const MTLViewport& vp, bool hasDepth,
                                id<MTLDepthStencilState> depthState, bool (*ensure)(Surface* const*, const uint32_t*, Surface*, uint32_t)) {
    const uint32_t cloudsSeq = g_pendingSeq, seq = g_skySeq;
    g_composites++;
    g_pendingSeq = 0;
    g_skySeq = 0;
    static const bool trace = env_on("WWHD_CLOUDS_LOG");
    if (trace && R.frame % 150 == 0)
        LOG("[clouds] sky composite: target %lux%lu depth %d DB_DEPTH_CONTROL %08X vp z %.3f..%.3f",
            R.passColor[0] ? (unsigned long)R.passColor[0]->tex.width : 0, R.passColor[0] ? (unsigned long)R.passColor[0]->tex.height : 0,
            hasDepth, regs[0xA200], vp.znear, vp.zfar);
    (void)depthState;
    if (cloudsSeq) {
        // our own test, not the composite's EQUAL: the impostors' interpolated depth is not bit-exact 1.0,
        // so test "in front of or at the far plane" with a depth just short of it (sky only, no write)
        static id<MTLDepthStencilState> skyOnly = [] {
            MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
            dd.depthCompareFunction = MTLCompareFunctionLessEqual;
            dd.depthWriteEnabled = NO;
            return [R.device newDepthStencilStateWithDescriptor:dd];
        }();
        static const bool timing = env_on("WWHD_CLOUDS_TIME");
        if (timing && ensure) {  // the full-resolution cloud pass in a command buffer of its own
            Surface* colors[8];
            uint32_t slices[8];
            for (int i = 0; i < 8; i++) colors[i] = R.passColor[i], slices[i] = R.passColorSlice[i];
            Surface* depth = R.passDepth;
            uint32_t depthSlice = R.passDepthSlice;
            flush();
            if (!ensure(colors, slices, depth, depthSlice)) return;
            enc = R.enc;
        }
        encode(enc, *cloud_proto::snapshot(cloudsSeq), vp, kClouds, hasDepth ? skyOnly : nil);
        if (timing && ensure) {
            const int mode = cloud_proto::mode();
            timed_commit(mode == cloud_proto::kVolume ? "volume, full res" : mode == cloud_proto::kCombo ? "combo, full res" : "sdf, full res");
        }
    }
    if (seq && render::cloud_shadows() && R.passColor[0] && R.passDepth) {
        // the sea is drawn by now, but islands, Link, the boat and the pier come later: the shadows go in
        // when the picture is first read (the post effects), or at the end of the frame
        g_shadowColor = R.passColor[0], g_shadowDepth = R.passDepth, g_shadowSlice = R.passColorSlice[0];
        g_shadowSeq = seq, g_shadowFrame = R.frame;
    }
}
// draw(), while it looks up its textures (before its pass opens): the picture is about to be read
bool clouds_shadow_target(Surface* s) { return g_shadowSeq && s == g_shadowColor && g_shadowFrame == R.frame; }
bool clouds_shadow_depth(Surface* s) { return g_shadowSeq && g_shadowDepth && s->addr == g_shadowDepth->addr && g_shadowFrame == R.frame; }
void clouds_shadows_now() {
    if (!g_shadowSeq) return;
    static const uint64_t traceFrame = getenv("WWHD_CLOUDS_TRACE_FRAME") ? strtoull(getenv("WWHD_CLOUDS_TRACE_FRAME"), nullptr, 10) : 0;
    if (traceFrame && R.frame == traceFrame) LOG("[ctrace] shadows applied before draw %llu", (unsigned long long)R.drawCount);
    uint32_t seq = g_shadowSeq;
    g_shadowSeq = 0;
    static const bool timing = env_on("WWHD_CLOUD_SHADOWS_TIME");
    // several surfaces can share an address (other shapes or formats): take the ones the last pass on
    // this picture used, where the later scene draws (islands, Link, the boat) went
    Surface* color = g_shadowColor;
    Surface* depth = g_shadowDepth;
    if (R.passColor[0] && R.passColor[0]->addr == color->addr) color = R.passColor[0];
    if (R.passDepth && R.passDepth->addr == depth->addr) depth = R.passDepth;
    static const bool logOn = env_on("WWHD_CLOUDS_LOG");
    if (logOn && R.frame % 150 == 0)
        LOG("[shadows] applied: colour %s, depth %s", color == g_shadowColor ? "as composited" : "of the last pass",
            depth == g_shadowDepth ? "as composited" : "of the last pass");
    if (timing) flush(); else end_encoder();  // the depth buffer is read: no pass may hold it
    draw_cloud_shadows(*cloud_proto::snapshot(seq), color, g_shadowSlice, depth);
    if (timing) timed_commit("cloud shadows");
}

// ---- cloud shadows (Graphics > Cloud shadows; WWHD_CLOUD_SHADOWS=1): after the sky composite, the
// opaque picture and its depth are complete; the shadow map is drawn, then the apply pass multiplies
// the colour target where clouds cover the light
struct ShadowGlobals { float view[12], proj[4], sun[4], layer[4], look[4]; };
static id<MTLLibrary> shadow_library() {
    static id<MTLLibrary> lib;
    if (!lib) {
        NSError* err = nil;
        lib = [R.device newLibraryWithSource:[NSString stringWithUTF8String:kCloudShader] options:nil error:&err];
        if (!lib) LOG("[shadows] shader: %s", err.localizedDescription.UTF8String);
    }
    return lib;
}
static void draw_cloud_shadows(const cloud_proto::Snapshot& s, Surface* color, uint32_t colorSlice, Surface* depth) {
    if (!color || !depth) return;
    id<MTLLibrary> lib = shadow_library();
    if (!lib) return;
    static id<MTLTexture> map, noise3d;
    static id<MTLSamplerState> linearClamp, linearRepeat;
    static id<MTLRenderPipelineState> mapPipe;
    static std::unordered_map<int, id<MTLRenderPipelineState>> applyPipes;
    const int N = 256;
    if (!map) {
        MTLTextureDescriptor* d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:N height:N mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModePrivate;
        map = [R.device newTextureWithDescriptor:d];
        noise3d = make_noise();
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
        linearClamp = [R.device newSamplerStateWithDescriptor:sd];
        sd.sAddressMode = sd.tAddressMode = sd.rAddressMode = MTLSamplerAddressModeRepeat;
        linearRepeat = [R.device newSamplerStateWithDescriptor:sd];
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [lib newFunctionWithName:@"sh_vs"];
        pd.fragmentFunction = [lib newFunctionWithName:@"shmap_fs"];
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatR8Unorm;
        NSError* err = nil;
        mapPipe = [R.device newRenderPipelineStateWithDescriptor:pd error:&err];
        if (!mapPipe) LOG("[shadows] map pipeline: %s", err.localizedDescription.UTF8String);
    }
    int fmt = (int)color->tex.pixelFormat;
    auto it = applyPipes.find(fmt);
    if (it == applyPipes.end()) {
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [lib newFunctionWithName:@"sh_vs"];
        pd.fragmentFunction = [lib newFunctionWithName:@"shadow_apply_fs"];
        pd.colorAttachments[0].pixelFormat = color->tex.pixelFormat;
        auto* a = pd.colorAttachments[0];
        a.blendingEnabled = YES;  // multiply: dst * k
        a.sourceRGBBlendFactor = MTLBlendFactorZero;
        a.destinationRGBBlendFactor = MTLBlendFactorSourceColor;
        a.sourceAlphaBlendFactor = MTLBlendFactorZero;
        a.destinationAlphaBlendFactor = MTLBlendFactorOne;
        NSError* err = nil;
        id<MTLRenderPipelineState> p = [R.device newRenderPipelineStateWithDescriptor:pd error:&err];
        if (!p) LOG("[shadows] apply pipeline: %s", err.localizedDescription.UTF8String);
        it = applyPipes.emplace(fmt, p).first;
    }
    if (!it->second) return;
    // values
    ShadowGlobals g{};
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) g.view[c * 4 + r] = s.viewRot[r * 4 + c];
    float aspect = (float)color->tex.width / (float)color->tex.height;
    float f = 1.0f / std::tan(s.fovy * 0.5f * 3.14159265f / 180.0f);
    g.proj[0] = f / aspect, g.proj[1] = f, g.proj[2] = s.viewNear, g.proj[3] = s.viewFar;
    sky::SkyFrame sf;
    cloud_proto::sky_frame(s, aspect, 0, sf);
    for (int i = 0; i < 4; i++) g.sun[i] = sf.sun[i];
    static const float layerH = envf("WWHD_CLOUD_SHADOW_HEIGHT", 6000.0f), half = envf("WWHD_CLOUD_SHADOW_RANGE", 24000.0f);
    g.layer[0] = layerH, g.layer[1] = half, g.layer[2] = s.eye[1], g.layer[3] = (float)s.nShadow;
    // subtle: darker at noon, weaker as the sun gets low (warmth) and in storms, none at night
    static const float dark = envf("WWHD_CLOUD_SHADOW_DARKNESS", 0.35f);
    float strengthK = std::clamp(1.0f - 0.6f * s.strength, 0.3f, 1.0f);
    g.look[0] = dark * (1.0f - 0.7f * sf.lit[3]) * strengthK;
    g.look[1] = envf("WWHD_CLOUD_SHADOW_SOFTNESS", 0.04f);
    g.look[2] = envf("WWHD_CLOUD_SHADOWS_DEBUG", 0.0f);
    g.look[3] = envf("WWHD_CLOUD_SHADOW_ZMODE", 1.0f);  // GX2 depth is 0..1 (D3D-style)
    static const bool logOn = env_on("WWHD_CLOUDS_LOG");
    if (logOn && R.frame % 150 == 0)
        LOG("[shadows] near %g far %g aspect %g fovy %g eye y %.0f, %d clouds, darkness %.2f (warm %.2f strength %.2f)", s.viewNear,
            s.viewFar, s.viewAspect, s.fovy, s.eye[1], s.nShadow, g.look[0], sf.lit[3], s.strength);
    // apply onto the picture
    {
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = color->tex;
        if (color->tex.textureType == MTLTextureType3D) rp.colorAttachments[0].depthPlane = colorSlice;
        else rp.colorAttachments[0].slice = colorSlice;
        rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> e = [command_buffer() renderCommandEncoderWithDescriptor:rp];
        [e setRenderPipelineState:it->second];
        [e setFragmentBytes:&g length:sizeof g atIndex:0];
        [e setFragmentTexture:depth->tex atIndex:0];
        [e setFragmentTexture:map atIndex:1];
        [e setFragmentSamplerState:linearClamp atIndex:0];
        float clouds[100][4] = {};
        for (int i = 0; i < s.nShadow; i++)
            for (int k = 0; k < 4; k++) clouds[i][k] = s.shadow[i][k];
        [e setFragmentBytes:clouds length:sizeof clouds atIndex:1];
        static id<MTLBuffer> probe = [R.device newBufferWithLength:256 options:MTLResourceStorageModeShared];
        [e setFragmentBuffer:probe offset:0 atIndex:2];
        if (g.look[2] == 11.0f && R.frame % 60 == 0) {
            id<MTLBuffer> pb = probe;
            [command_buffer() addCompletedHandler:^(id<MTLCommandBuffer>) {
                const float* v = (const float*)pb.contents;
                for (int i = 0; i < 6; i++)
                    LOG("[shadows] probe %d: uv %.4f %.4f cov %.3f height %.0f", i, v[i * 4], v[i * 4 + 1], v[i * 4 + 2], v[i * 4 + 3]);
            }];
        }
        if (g.look[2] == 9.0f && R.frame % 60 == 0) {
            id<MTLBuffer> pb = probe;
            unsigned long w = depth->tex.width, h = depth->tex.height, sl = depth->tex.arrayLength;
            [command_buffer() addCompletedHandler:^(id<MTLCommandBuffer>) {
                const float* v = (const float*)pb.contents;
                LOG("[shadows] depth probes (%lux%lu x%lu): %.7f %.7f %.7f %.7f %.7f %.7f", w, h, sl, v[0], v[1], v[2], v[3], v[4], v[5]);
            }];
        }
        [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [e endEncoding];
    }
    mark_gpu_written(color);
}

// draw(), right after the pass is open, for every game draw between OP_CLOUDS_BEGIN and _END: with
// WWHD_CLOUDS_PROTO=mark and timing, the game's own cards start a command buffer of their own
static void clouds_end();
void clouds_mark_start(Surface* const* colors, const uint32_t* colorSlices, Surface* depth, uint32_t depthSlice,
                       bool (*ensure)(Surface* const*, const uint32_t*, Surface*, uint32_t)) {
    static const bool timing = env_on("WWHD_CLOUDS_TIME");
    if (cloud_proto::mode() != cloud_proto::kMark || cloud_proto::g_drawn) return;
    cloud_proto::g_drawn = true;
    if (timing) {
        cloud_proto::g_on_end = clouds_end;
        flush();
        ensure(colors, colorSlices, depth, depthSlice);
    }
}

// draw(), with the draw's state set up to the scissor: true = skip the game's draw
bool clouds_draw(id<MTLRenderCommandEncoder> enc, const MTLViewport& vp, Surface* const* colors, const uint32_t* colorSlices,
                 Surface* depth, uint32_t depthSlice, bool (*ensure)(Surface* const*, const uint32_t*, Surface*, uint32_t)) {
    const int mode = cloud_proto::mode();
    static const bool timing = env_on("WWHD_CLOUDS_TIME");
    if (mode == cloud_proto::kMark || mode == cloud_proto::kOff) return false;  // the game's own cards
    if (cloud_proto::g_drawn) return true;
    cloud_proto::g_drawn = true;
    if (timing) {
        flush();
        if (!ensure(colors, colorSlices, depth, depthSlice)) return true;
        enc = R.enc;
    }
    // High: the full-resolution picture; Lite: the half-resolution sky buffer (a quarter of the pixels).
    // WWHD_CLOUDS_FULLRES=0|1 overrides.
    static const char* fullresEnv = getenv("WWHD_CLOUDS_FULLRES");
    const bool fullres = fullresEnv ? strcmp(fullresEnv, "0") != 0 : cloud_proto::tier() != 1;
    encode(enc, *cloud_proto::snapshot(cloud_proto::g_active_seq), vp, fullres ? 0 : kClouds);
    if (fullres) {  // the clouds go into the full-resolution picture where the sky buffer is composited
        g_sky = R.passColor[0], g_skyFrame = R.frame, g_pendingSeq = cloud_proto::g_active_seq;
    }
    if (timing) timed_commit(mode == cloud_proto::kVolume ? "volume" : mode == cloud_proto::kHide ? "hide" : "sdf");
    return true;
}
// OP_CLOUDS_BEGIN: the sky buffer's pass is open (the sky dome and its haze band are drawn); the
// horizon-haze grade goes in before the clouds. Also builds the shaders at the same moment in every
// mode, so the game's cloud set does not depend on the mode (comparable runs).
bool open_target_pass(const uint32_t* regs);  // metal_draw.mm
void sky_clouds_begin(const uint32_t* regs, uint32_t seq) {
    static uint64_t begins = 0, opened = 0;
    begins++;
    if (render::cloud_shadows() && R.enc && R.passColor[0]) {  // the sky buffer: its composite triggers the shadows
        g_sky = R.passColor[0], g_skyFrame = R.frame, g_skySeq = seq;
        opened++;
    }
    static const bool logOn = env_on("WWHD_CLOUDS_LOG");
    if (logOn && begins % 300 == 0) LOG("[shadows] sky begins %llu, with an open sky pass %llu, composites %llu", (unsigned long long)begins,
                                        (unsigned long long)opened, (unsigned long long)g_composites);
    if (render::horizon_haze() >= 1.0f && cloud_proto::mode() == cloud_proto::kOff) return;
    if ((!R.enc || !R.passColor[0]) && !open_target_pass(regs)) return;  // no open pass: the target registers' one
    Surface* t = R.passColor[0];
    if (!t || !R.enc) return;
    MTLViewport vp{0, 0, (double)t->tex.width, (double)t->tex.height, 0, 1};
    encode(R.enc, *cloud_proto::snapshot(seq), vp, kGrade);
}
// OP_CLOUDS_END (mark + timing): closes the command buffer of the game's own cards
static void clouds_end() {
    if (cloud_proto::g_drawn) timed_commit("game cards");
}

}  // namespace gfx
