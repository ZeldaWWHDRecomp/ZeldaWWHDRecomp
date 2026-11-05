// 60 fps (generated): the game runs at its own 30 frames a second, and after each real frame an
// in-between one is made on the GPU and shown half a frame later.
// From Nycolazs/WindWakerHD-Android-Plus (vk/reproj.cpp 84b0ff2, fc5ddee) via GreenNaugahyde/ZeldaWWHDRecompAndroid
// v0.9.9 (db65d60), MPL-2.0: every shader below (kWarp, kGridVertex, kGridFragment, kCompose, kWarpCov,
// kFlow, kFilter, kHudMask, kSelect, kSpread, kDepthMerge, kQuarter) is theirs as published, and so are
// the generated camera (half the last rotation and movement), the cut rule, "follow", the objects'
// movement search, the depth merge, the warped mesh and the HUD's coverage (hud_coverage.h). Fitted to
// this renderer: their images, render passes and layouts are our Surfaces, dynamic-rendering passes
// (pass.h) and tracked transitions; their window picture is our TV scan. The capture (framegen.cpp,
// OP_FRAMEGEN_CAMERA / OP_FRAMEGEN_LAYOUT) and the present step (backend.cpp present_generated) are this
// port's own: their present side rode on a Lossless Scaling present thread, which is not part of this port.
//
// The in-between frame is the real one reprojected with the scene's depth to the camera moved on by
// half its last step (eye and turn: the camera orbits Link, so a turn alone would move Link
// sideways). The HUD stays where it is: at the first HUD layout drawn on the TV picture the scene
// (3D and its post-processing) is copied aside; the HUD draws from there on also write how much they
// cover (draw.cpp, hud_coverage.h), else pixels where the finished picture differs from the scene are
// HUD (kHudMask). Frame interpolation draws every frame twice on the game thread; this costs GPU passes
// instead, so it holds 60 where the game thread can't.
//
// In this port the in-between picture is made in the TV picture's own space: the published TV scan
// is warped into a scan-sized image, which swap() presents through the normal window composition
// (scaling, FXAA, the settings overlay), held until half a real frame after the real one
// (backend.cpp present_generated, the held-present path of even frame delivery).
//
// Switches (read once): WWHD_FRAMEGEN=1 on (framegen.h; the Settings window's "60 fps (generated)");
// WWHD_FRAMEGEN_TINT=1 tints the in-between frames red; WWHD_FRAMEGEN_FOLLOW=0 moves what the camera
// follows with the camera too; WWHD_FRAMEGEN_OBJECTS=0 no objects' own movement; WWHD_FRAMEGEN_SELECT=0
// the blocks' movement as found; WWHD_FRAMEGEN_GRID=0 the per-pixel warp instead of the mesh;
// WWHD_HUD_COVERAGE=0 the HUD mask instead of its coverage; WWHD_FRAMEGEN_HUD=x HUD difference
// threshold (default 3/255); WWHD_FRAMEGEN_DEBUG=1 shows the HUD mask (per pixel) / the movement's kind
// (mesh: red sure, green spread, blue undecided), =2 the scene's depth (log scale).
#include "reproj.h"
#include "reproj_camera.h"

#include "backend.h"
#include "deferred_dispatch.h"
#include "pass.h"
#include "shaders.h"
#include "stereo.h"
#include "framegen.h"
#include "runtime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace gfxvk::reproj {
namespace {

const char* kVertex = R"glsl(#version 450
layout(location=0) out vec2 uv;
void main() {
 vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
 uv = p;
 gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

// TV picture uv -> the generated TV picture. p.rect: where the TV picture lies in the target (uv; here
// always the whole of it); t: tanX, tanY, HUD difference threshold, tint; z: near, far, depth usable,
// objects' movement on (kFlow made it); r0..r2: rows of H (the generated camera's axes in the real
// camera's space), .w: its eye there; a: distance to the point the camera looks at, follows on, unused,
// debug view (1: HUD mask, 2: depth)
const char* kWarp = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D comp;      // the finished TV picture (the published scan)
layout(set=0,binding=1) uniform sampler2D depthImg;  // the scene's depth (TV picture space)
layout(set=0,binding=2) uniform sampler2D hudMask;   // where the HUD is, a quarter of the TV picture's size (kHudMask)
layout(set=0,binding=3) uniform sampler2D unused3;
layout(set=0,binding=4) uniform sampler2D flowImg;   // objects' own movement (kSelect/kSpread per quarter-size pixel, else kFlow per 16x16 block)
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform P { vec4 r0; vec4 r1; vec4 r2; vec4 t; vec4 z; vec4 rect; vec4 a; } p;
// the 2x2 depth texels around q, as textureGather would return them, read one by one: a gather on
// the game's depth-stencil image froze an Adreno 650 (Galaxy S20 FE) until it restarted and gave
// stray lines on others (LASRecomp-Android-Plus 08bbaef); plain fetches work on every driver
vec4 gatherDepth(vec2 q) {
 ivec2 sz = textureSize(depthImg, 0), b = ivec2(floor(q * vec2(sz) - 0.5));
 ivec2 lo = ivec2(0), hi = sz - 1;
 return vec4(texelFetch(depthImg, clamp(b + ivec2(0, 1), lo, hi), 0).r, texelFetch(depthImg, clamp(b + ivec2(1, 1), lo, hi), 0).r,
             texelFetch(depthImg, clamp(b + ivec2(1, 0), lo, hi), 0).r, texelFetch(depthImg, clamp(b, lo, hi), 0).r);
}
// the nearest depth of the 2x2 pixels around q: the game's outlines around characters lie on the
// background's depth and would otherwise move with the background (depth range [-1,1] as GX2)
float viewDepth(vec2 q) {
 vec4 m = gatherDepth(q);
 float d = min(min(m.x, m.y), min(m.z, m.w)), n = p.z.x, f = p.z.y;
 return 2.0 * n * f / max((f + n) - (d * 2.0 - 1.0) * (f - n), 1e-6);
}
// how much the pixel at q moves with the camera, 0..1: what lies about as far as the point the
// camera looks at (a.x; a.y: on). The camera follows Link (and the title's boat): they and what is
// near them keep their place on the screen while the camera moves, so moving them with the camera's
// movement shook them back and forth every frame; the background, further away, is reprojected.
// (z: the view depth at q, read once per step of the motion)
float follows(float z, vec2 q) {
 if (p.a.y < 0.5 || p.a.x <= 0.0) return 0.0;
 float r = abs(log(max(z, 1e-3) / p.a.x));
 // only around the point the camera looks at (the screen's centre): the world at Link's distance
 // elsewhere (a boat beside him, a wall) moves with the camera like the rest
 vec2 d = (q - 0.5) * vec2(p.t.x / p.t.y, 1.0);
 return (1.0 - smoothstep(0.35, 0.75, r)) * (1.0 - smoothstep(0.12, 0.24, length(d)));
}
// where the real picture's pixel q (TV uv), at view depth z, is in the generated picture
vec2 toGenCam(vec2 q, float z);
vec2 toGen(vec2 q, float z, float fw) { return mix(toGenCam(q, z), q, fw); }
vec2 toGenCam(vec2 q, float z) {
 vec3 P = vec3((q.x * 2.0 - 1.0) * p.t.x, (1.0 - q.y * 2.0) * p.t.y, -1.0) * z - vec3(p.r0.w, p.r1.w, p.r2.w);
 vec3 g = p.r0.xyz * P.x + p.r1.xyz * P.y + p.r2.xyz * P.z;
 if (g.z > -1e-4) return q;
 return vec2(g.x / (-g.z) / p.t.x * 0.5 + 0.5, 0.5 - g.y / (-g.z) / p.t.y * 0.5);
}
// half the movement of the object at q beyond the camera's (TV uv), where q's depth is its block's
// (characters and enemies: with the camera's movement alone they stood still in the in-between frame,
// then jumped: 30 fps motion)
vec2 objectMove(vec2 q, float z, float fw) {
 if (p.z.w < 0.5) return vec2(0.0);
 vec4 f = textureLod(flowImg, q, 0.0);
 if (f.w < 0.5) return vec2(0.0);
 float bz = p.z.x * exp(f.z * log(p.z.y / p.z.x));
 if (abs(z - bz) > 0.15 * bz) return vec2(0.0);
 return f.xy * 0.5 * (1.0 - fw);  // (what follows the camera already keeps its place)
}
// the HUD and a few pixels around it (glows, outlines and their soft edges): the mask's texels cover
// 4x4 TV pixels, its bilinear tap anything within one texel of them
bool hud(vec2 q) { return textureLod(hudMask, q, 0.0).r > 0.01; }
// the picture's pixel nearest to q, unfiltered: bilinear resampling softened fine detail (water,
// grass, distant edges) in every other frame, which shimmered; the movement is rounded to whole pixels
vec4 window(vec2 q) {
 ivec2 sz = textureSize(comp, 0);
 return texelFetch(comp, clamp(ivec2((p.rect.xy + q * p.rect.zw) * vec2(sz)), ivec2(0), sz - 1), 0);
}
void main() {
 vec2 tv = (uv - p.rect.xy) / p.rect.zw;
 if (p.a.w > 1.5) {  // debug (WWHD_FRAMEGEN_DEBUG=2): the scene's view depth, log scale from near to far
  float z = viewDepth(tv);
  result = vec4(vec3(clamp(log(max(z, p.z.x) / p.z.x) / log(p.z.y / p.z.x), 0.0, 1.0)), 1.0);
  return;
 }
 if (p.z.z < 0.5 || any(lessThan(tv, vec2(0.0))) || any(greaterThan(tv, vec2(1.0))) || hud(tv)) {
  result = textureLod(comp, uv, 0.0);  // outside the TV picture, without a depth buffer, or the HUD: unmoved
  if (p.a.w > 0.5 && hud(tv)) result.rgb = mix(result.rgb, vec3(1.0, 0.0, 1.0), 0.5);  // (debug 1: the HUD mask)
  return;
 }
 vec2 q = tv;  // the source pixel that lands here: a few fixed-point steps of the motion
 vec2 raw = tv;
 for (int i = 0; i < 3; i++) {
  float z = viewDepth(q), fw = follows(z, q);
  raw = tv - (toGen(q, z, fw) - q) - objectMove(q, z, fw);
  q = clamp(raw, vec2(0.0), vec2(1.0));
 }
 // it would come from outside the real picture (the edge while the camera turns): the edge of the
 // picture along the movement (q is clamped to it). The real picture's own pixel there, as before,
 // showed what lies next to the edge twice (a boat, a sign) in a band as wide as half the camera's
 // step: up to a tenth of the screen in a fast turn
 if (hud(q)) {  // the scene there is hidden under the HUD: the nearest visible scene along the motion
  vec2 px = 1.0 / vec2(textureSize(depthImg, 0));
  vec2 d = q - tv;
  float l = length(d / px);
  d = l > 0.5 ? d / l : vec2(px.x, 0.0);
  for (int k = 1; k <= 8; k++) {
   vec2 a = clamp(q + d * float(k * 12), vec2(0.0), vec2(1.0));
   if (!hud(a)) { q = a; break; }
   a = clamp(q - d * float(k * 12), vec2(0.0), vec2(1.0));
   if (!hud(a)) { q = a; break; }
  }
 }
 result = window(q);
 if (p.t.w > 0.5) result.rgb *= vec3(1.0, 0.6, 0.6);  // (debug: WWHD_FRAMEGEN_TINT=1) generated frames tinted
}
)glsl";
// ---- the in-between picture by a warped mesh (default; WWHD_FRAMEGEN_GRID=0: kWarp / kWarpCov) ----
// kWarp looks, for each pixel of the in-between picture, for the scene's pixel that lands there (a
// few fixed-point steps). Where something near stands in front of something far that search has two
// answers, and with a fast camera it jumped between them: thin parts (the King of Red Lions' fins,
// railings, leaves) came out torn into strips. Here the scene before the HUD is drawn as a mesh, one
// vertex every few TV pixels (a margin beyond the edges, held at their depth), each moved where the
// camera's (and the object's own) movement takes it, with a depth test: what is near always covers
// what is far. Triangles stretched over a gap (the background a near object leaves) are pushed
// behind everything, so they only fill what nothing else covers. kCompose then puts the HUD back
// with its coverage: picture + (moved scene - scene) * (1 - coverage).
// rect: vertices per row and rows, the margin (TV uv); z.w: object movement on
const char* kGridVertex = R"glsl(#version 450
layout(set=0,binding=1) uniform sampler2D depthImg;
layout(set=0,binding=4) uniform sampler2D flowImg;
layout(push_constant) uniform P { vec4 r0; vec4 r1; vec4 r2; vec4 t; vec4 z; vec4 rect; vec4 a; } p;
layout(location=0) out vec2 src;
layout(location=1) out vec3 farSrc;  // src weighted by depth^4: a gap's triangle takes the far side
layout(location=2) out vec4 dbg;     // WWHD_FRAMEGEN_DEBUG: the movement's kind
// the 2x2 depth texels around q, as textureGather would return them, read one by one: a gather on
// the game's depth-stencil image froze an Adreno 650 (Galaxy S20 FE) until it restarted and gave
// stray lines on others (LASRecomp-Android-Plus 08bbaef); plain fetches work on every driver
vec4 gatherDepth(vec2 q) {
 ivec2 sz = textureSize(depthImg, 0), b = ivec2(floor(q * vec2(sz) - 0.5));
 ivec2 lo = ivec2(0), hi = sz - 1;
 return vec4(texelFetch(depthImg, clamp(b + ivec2(0, 1), lo, hi), 0).r, texelFetch(depthImg, clamp(b + ivec2(1, 1), lo, hi), 0).r,
             texelFetch(depthImg, clamp(b + ivec2(1, 0), lo, hi), 0).r, texelFetch(depthImg, clamp(b, lo, hi), 0).r);
}
float viewDepth(vec2 q) {
 vec4 m = gatherDepth(q);
 float d = min(min(m.x, m.y), min(m.z, m.w)), n = p.z.x, f = p.z.y;
 return 2.0 * n * f / max((f + n) - (d * 2.0 - 1.0) * (f - n), 1e-6);
}
float follows(float z, vec2 q) {
 if (p.a.y < 0.5 || p.a.x <= 0.0) return 0.0;
 float r = abs(log(max(z, 1e-3) / p.a.x));
 vec2 d = (q - 0.5) * vec2(p.t.x / p.t.y, 1.0);
 return (1.0 - smoothstep(0.35, 0.75, r)) * (1.0 - smoothstep(0.12, 0.24, length(d)));
}
vec2 toGenCam(vec2 q, float z) {
 vec3 P = vec3((q.x * 2.0 - 1.0) * p.t.x, (1.0 - q.y * 2.0) * p.t.y, -1.0) * z - vec3(p.r0.w, p.r1.w, p.r2.w);
 vec3 g = p.r0.xyz * P.x + p.r1.xyz * P.y + p.r2.xyz * P.z;
 if (g.z > -1e-4) return q;
 return vec2(g.x / (-g.z) / p.t.x * 0.5 + 0.5, 0.5 - g.y / (-g.z) / p.t.y * 0.5);
}
vec2 objectMove(vec2 q, float z, float fw) {
 if (p.z.w < 0.5) return vec2(0.0);
 vec4 f = textureLod(flowImg, q, 0.0);
 if (f.w < 0.5) return vec2(0.0);
 float bz = p.z.x * exp(f.z * log(p.z.y / p.z.x));
 if (abs(z - bz) > 0.15 * bz) return vec2(0.0);
 return f.xy * 0.5 * (1.0 - fw);
}
void main() {
 // (unsigned: a signed division by a variable is what some mobile shader compilers get wrong)
 uint cols = uint(p.rect.x), rows = uint(p.rect.y), vi = uint(gl_VertexIndex);
 vec2 g = vec2(vi % cols, vi / cols) / vec2(cols - 1u, rows - 1u);
 float m = p.rect.z;
 vec2 q = g * (1.0 + 2.0 * m) - m;
 vec2 qc = clamp(q, vec2(0.0), vec2(1.0));
 float z = viewDepth(qc), fw = follows(z, qc);
 vec2 d = mix(toGenCam(q, z), q, fw) + objectMove(qc, z, fw);
 src = qc;
 vec4 fm = textureLod(flowImg, qc, 0.0);
 dbg = vec4(fm.w > 0.75 ? 1.0 : 0.0, fm.w > 0.25 && fm.w < 0.75 ? 1.0 : 0.0, fm.w < 0.25 ? 1.0 : 0.0, length(fm.xy) > 0.002 ? 1.0 : 0.0);
 float w = pow(clamp(z, 1.0, 1e5) / 100.0, 4.0);
 farSrc = vec3(qc * w, w);
 gl_Position = vec4(d * 2.0 - 1.0, clamp(log(max(z, p.z.x) / p.z.x) / log(p.z.y / p.z.x), 0.0, 1.0) * 0.99, 1.0);
}
)glsl";

// the scene's pixel the mesh brings here (display encoded, as the window picture); a.z: sRGB scan buffer
const char* kGridFragment = R"glsl(#version 450
layout(set=0,binding=3) uniform sampler2D sceneImg;
layout(push_constant) uniform P { vec4 r0; vec4 r1; vec4 r2; vec4 t; vec4 z; vec4 rect; vec4 a; } p;
layout(location=0) in vec2 src;
layout(location=1) in vec3 farSrc;
layout(location=2) in vec4 dbg;
layout(location=0) out vec4 result;
vec3 enc(vec3 c) {
 return p.a.z > 0.5 ? mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)) : c;
}
void main() {
 vec2 ds = vec2(textureSize(sceneImg, 0));
 vec2 dx = dFdx(src) * ds, dy = dFdy(src) * ds;
 // scene texels per pixel: about 1 where the mesh only moved; far less where a triangle spans a gap
 float area = abs(dx.x * dy.y - dx.y * dy.x);
 // a gap (stretched between something near and what is behind it) shows what is behind
 bool gap = area < 0.15;
 gl_FragDepth = gap ? 0.995 : gl_FragCoord.z;
 result = vec4(enc(textureLod(sceneImg, gap ? farSrc.xy / max(farSrc.z, 1e-30) : src, 0.0).rgb), 1.0);
 // debug: red sure, green spread, blue undecided; bright where it moves on its own
 if (p.rect.w > 1.5) result.rgb = vec3(gl_FragCoord.z / 0.99);  // debug 2: the depth (log scale)
 else if (p.rect.w > 0.5) result.rgb = mix(result.rgb, dbg.rgb * (0.4 + 0.6 * dbg.w), 0.6);
}
)glsl";

// the in-between window picture from the warped scene (TV uv: binding 1) and the HUD's coverage
// (binding 2: coverage, or the HUD mask where this frame has none); a.z: sRGB scan buffer
const char* kCompose = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D comp;
layout(set=0,binding=1) uniform sampler2D warpImg;
layout(set=0,binding=2) uniform sampler2D covImg;
layout(set=0,binding=3) uniform sampler2D sceneImg;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform P { vec4 r0; vec4 r1; vec4 r2; vec4 t; vec4 z; vec4 rect; vec4 a; } p;
vec3 enc(vec3 c) {
 return p.a.z > 0.5 ? mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)) : c;
}
void main() {
 vec4 c0 = textureLod(comp, uv, 0.0);
 vec2 tv = (uv - p.rect.xy) / p.rect.zw;
 if (any(lessThan(tv, vec2(0.0))) || any(greaterThan(tv, vec2(1.0)))) { result = c0; return; }
 float c = textureLod(covImg, tv, 0.0).r;
 if (c > 0.996) { result = c0; return; }
 vec3 moved = textureLod(warpImg, tv, 0.0).rgb;
 if (c < 0.004) result = vec4(moved, c0.a);  // no HUD here: the moved scene
 else result = vec4(clamp(c0.rgb + (moved - enc(textureLod(sceneImg, tv, 0.0).rgb)) * (1.0 - c), 0.0, 1.0), c0.a);
 if (p.t.w > 0.5) result.rgb *= vec3(1.0, 0.6, 0.6);
}
)glsl";

// The in-between picture with the HUD's coverage (vk_draw.cpp, hud coverage): covImg holds how much
// of the scene the HUD covers at each TV pixel, sceneImg the scene before the HUD. Where nothing
// covers it the scene is reprojected as in kWarp; where the HUD covers it fully the pixel stays;
// in between (translucent HUD parts, soft edges) the scene's change shows through by the share the
// HUD leaves of it: picture + (scene moved - scene) * (1 - coverage). The scene under an opaque
// HUD part is known (sceneImg): no search for a visible pixel. a.z: the scan buffer is sRGB (the
// window picture is encoded, the scene not).
const char* kWarpCov = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D comp;
layout(set=0,binding=1) uniform sampler2D depthImg;
layout(set=0,binding=2) uniform sampler2D covImg;
layout(set=0,binding=3) uniform sampler2D sceneImg;
layout(set=0,binding=4) uniform sampler2D flowImg;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform P { vec4 r0; vec4 r1; vec4 r2; vec4 t; vec4 z; vec4 rect; vec4 a; } p;
// the 2x2 depth texels around q, as textureGather would return them, read one by one: a gather on
// the game's depth-stencil image froze an Adreno 650 (Galaxy S20 FE) until it restarted and gave
// stray lines on others (LASRecomp-Android-Plus 08bbaef); plain fetches work on every driver
vec4 gatherDepth(vec2 q) {
 ivec2 sz = textureSize(depthImg, 0), b = ivec2(floor(q * vec2(sz) - 0.5));
 ivec2 lo = ivec2(0), hi = sz - 1;
 return vec4(texelFetch(depthImg, clamp(b + ivec2(0, 1), lo, hi), 0).r, texelFetch(depthImg, clamp(b + ivec2(1, 1), lo, hi), 0).r,
             texelFetch(depthImg, clamp(b + ivec2(1, 0), lo, hi), 0).r, texelFetch(depthImg, clamp(b, lo, hi), 0).r);
}
float viewDepth(vec2 q) {
 vec4 m = gatherDepth(q);
 float d = min(min(m.x, m.y), min(m.z, m.w)), n = p.z.x, f = p.z.y;
 return 2.0 * n * f / max((f + n) - (d * 2.0 - 1.0) * (f - n), 1e-6);
}
float follows(float z, vec2 q) {
 if (p.a.y < 0.5 || p.a.x <= 0.0) return 0.0;
 float r = abs(log(max(z, 1e-3) / p.a.x));
 // only around the point the camera looks at (the screen's centre): the world at Link's distance
 // elsewhere (a boat beside him, a wall) moves with the camera like the rest
 vec2 d = (q - 0.5) * vec2(p.t.x / p.t.y, 1.0);
 return (1.0 - smoothstep(0.35, 0.75, r)) * (1.0 - smoothstep(0.12, 0.24, length(d)));
}
vec2 toGenCam(vec2 q, float z) {
 vec3 P = vec3((q.x * 2.0 - 1.0) * p.t.x, (1.0 - q.y * 2.0) * p.t.y, -1.0) * z - vec3(p.r0.w, p.r1.w, p.r2.w);
 vec3 g = p.r0.xyz * P.x + p.r1.xyz * P.y + p.r2.xyz * P.z;
 if (g.z > -1e-4) return q;
 return vec2(g.x / (-g.z) / p.t.x * 0.5 + 0.5, 0.5 - g.y / (-g.z) / p.t.y * 0.5);
}
vec2 toGen(vec2 q, float z, float fw) { return mix(toGenCam(q, z), q, fw); }
vec2 objectMove(vec2 q, float z, float fw) {
 if (p.z.w < 0.5) return vec2(0.0);
 vec4 f = textureLod(flowImg, q, 0.0);
 if (f.w < 0.5) return vec2(0.0);
 float bz = p.z.x * exp(f.z * log(p.z.y / p.z.x));
 if (abs(z - bz) > 0.15 * bz) return vec2(0.0);
 return f.xy * 0.5 * (1.0 - fw);
}
vec4 window(vec2 q) {
 ivec2 sz = textureSize(comp, 0);
 return texelFetch(comp, clamp(ivec2((p.rect.xy + q * p.rect.zw) * vec2(sz)), ivec2(0), sz - 1), 0);
}
vec3 enc(vec3 c) {
 return p.a.z > 0.5 ? mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)) : c;
}
vec3 scene(vec2 q) {
 ivec2 sz = textureSize(sceneImg, 0);
 return enc(texelFetch(sceneImg, clamp(ivec2(q * vec2(sz)), ivec2(0), sz - 1), 0).rgb);
}
float cover(vec2 q) { return textureLod(covImg, q, 0.0).r; }
void main() {
 vec4 c0 = textureLod(comp, uv, 0.0);
 vec2 tv = (uv - p.rect.xy) / p.rect.zw;
 if (p.z.z < 0.5 || any(lessThan(tv, vec2(0.0))) || any(greaterThan(tv, vec2(1.0)))) { result = c0; return; }
 float c = cover(tv);
 if (c > 0.996) { result = c0; return; }  // under opaque HUD: as drawn
 vec2 q = tv, raw = tv;
 for (int i = 0; i < 3; i++) {
  float z = viewDepth(q), fw = follows(z, q);
  raw = tv - (toGen(q, z, fw) - q) - objectMove(q, z, fw);
  q = clamp(raw, vec2(0.0), vec2(1.0));
 }
 if (c < 0.004)  // no HUD here: the picture moved (the scene itself where the HUD covers the source)
  result = cover(q) < 0.004 ? window(q) : vec4(scene(q), 1.0);
 else
  result = vec4(clamp(c0.rgb + (scene(q) - scene(tv)) * (1.0 - c), 0.0, 1.0), c0.a);
 if (p.t.w > 0.5) result.rgb *= vec3(1.0, 0.6, 0.6);
}
)glsl";

// Objects' own movement, one value per 16x16 block of the TV picture: the block of this frame's scene
// (quarter size, no HUD) is looked for in the previous one around where the camera's movement alone
// puts it (depth), and around the same place on the screen (what the camera follows). Output
// (RGBA16F): the movement beyond the camera's, previous -> this frame (TV uv), the block's nearest
// depth (log scale), valid. Blocks with little detail or no clearly better match: 0.
// r0..r2: rows of D (this camera's axes in the previous camera's space), .w: this eye there
const char* kFlow = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D curImg;
layout(set=0,binding=1) uniform sampler2D prevImg;
layout(set=0,binding=2) uniform sampler2D depthImg;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform P { vec4 r0; vec4 r1; vec4 r2; vec4 t; vec4 z; vec4 rect; } p;
// the 2x2 depth texels around q, as textureGather would return them, read one by one: a gather on
// the game's depth-stencil image froze an Adreno 650 (Galaxy S20 FE) until it restarted and gave
// stray lines on others (LASRecomp-Android-Plus 08bbaef); plain fetches work on every driver
vec4 gatherDepth(vec2 q) {
 ivec2 sz = textureSize(depthImg, 0), b = ivec2(floor(q * vec2(sz) - 0.5));
 ivec2 lo = ivec2(0), hi = sz - 1;
 return vec4(texelFetch(depthImg, clamp(b + ivec2(0, 1), lo, hi), 0).r, texelFetch(depthImg, clamp(b + ivec2(1, 1), lo, hi), 0).r,
             texelFetch(depthImg, clamp(b + ivec2(1, 0), lo, hi), 0).r, texelFetch(depthImg, clamp(b, lo, hi), 0).r);
}
float lin(float d) {
 float n = p.z.x, f = p.z.y;
 return 2.0 * n * f / max((f + n) - (d * 2.0 - 1.0) * (f - n), 1e-6);
}
// 4x4 quarter-size pixels as four gathers of the green channel (brightness is enough to match blocks)
void block(sampler2D img, vec2 at, vec2 ts, out vec4 b[4]) {
 b[0] = textureGather(img, at + vec2(-1.0, -1.0) * ts, 1); b[1] = textureGather(img, at + vec2(1.0, -1.0) * ts, 1);
 b[2] = textureGather(img, at + vec2(-1.0, 1.0) * ts, 1);  b[3] = textureGather(img, at + vec2(1.0, 1.0) * ts, 1);
}
float sad(vec4 a[4], vec4 b[4]) {
 vec4 d = abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]) + abs(a[3] - b[3]);
 return d.x + d.y + d.z + d.w;
}
void main() {
 result = vec4(0.0);
 vec2 ts = 1.0 / vec2(textureSize(curImg, 0));
 vec4 cur[4]; block(curImg, uv, ts, cur);
 vec4 m4 = (cur[0] + cur[1] + cur[2] + cur[3]) * 0.25; float mean = (m4.x + m4.y + m4.z + m4.w) * 0.25;
 vec4 dv = abs(cur[0] - mean) + abs(cur[1] - mean) + abs(cur[2] - mean) + abs(cur[3] - mean);
 vec2 dt = 1.0 / vec2(textureSize(depthImg, 0));
 vec4 g = min(min(gatherDepth(uv + vec2(-4.0, -4.0) * dt), gatherDepth(uv + vec2(4.0, -4.0) * dt)),
              min(gatherDepth(uv + vec2(-4.0, 4.0) * dt), gatherDepth(uv + vec2(4.0, 4.0) * dt)));
 float z = lin(min(min(g.x, g.y), min(g.z, g.w)));
 float zl = clamp(log(z / p.z.x) / log(p.z.y / p.z.x), 0.0, 1.0);
 result.z = zl;
 if (dv.x + dv.y + dv.z + dv.w < 0.12) return;  // little detail: no reliable match
 // where the camera's movement alone puts this block in the previous picture (the world held still)
 vec3 P = vec3((uv.x * 2.0 - 1.0) * p.t.x, (1.0 - uv.y * 2.0) * p.t.y, -1.0) * z;
 vec3 q = vec3(dot(p.r0.xyz, P), dot(p.r1.xyz, P), dot(p.r2.xyz, P)) + vec3(p.r0.w, p.r1.w, p.r2.w);
 bool camOk = q.z < -1e-4;
 vec2 pp = camOk ? vec2(q.x / (-q.z) / p.t.x * 0.5 + 0.5, 0.5 - q.y / (-q.z) / p.t.y * 0.5) : uv;
 camOk = camOk && all(greaterThan(pp, vec2(0.01))) && all(lessThan(pp, vec2(0.99)));
 // two places to look: around pp, and around the same place on the screen (something the camera
 // follows: Link, the boat he sails, the title's boat), each +-6 quarter-size pixels, then refined
 float best = 1e9, atCam = 1e9; vec2 bp = uv; vec4 b[4];
 for (int h = 0; h < 2; h++) {
  vec2 c = h == 0 ? pp : uv;
  if (h == 0 && !camOk) continue;
  for (int oy = -6; oy <= 6; oy += 2) for (int ox = -6; ox <= 6; ox += 2) {
   vec2 at = c + vec2(ox, oy) * ts;
   if (any(lessThan(at, vec2(0.01))) || any(greaterThan(at, vec2(0.99)))) continue;
   block(prevImg, at, ts, b); float sd = sad(cur, b);
   if (h == 0 && ox == 0 && oy == 0) atCam = sd;
   if (sd < best) { best = sd; bp = at; }
  }
 }
 vec2 c0 = bp;
 for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) {
  if (ox == 0 && oy == 0) continue;
  vec2 at = c0 + vec2(ox, oy) * ts;
  block(prevImg, at, ts, b); float sd = sad(cur, b);
  if (sd < best) { best = sd; bp = at; }
 }
 if (best > 1.2 || atCam <= best * 1.5 + 0.05) return;  // no clear match, or the camera's movement explains it
 // the movement beyond the camera's, previous -> this frame (uv)
 result = vec4(pp - bp, zl, 1.0);
}
)glsl";
// a block keeps its movement only when at least 2 of its 8 neighbours moved the same way, and takes
// their average; lone blocks (water, flicker) are dropped. t.x: the agreement distance in the encoding
const char* kFilter = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D flowIn;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform P { vec4 r0; vec4 r1; vec4 r2; vec4 t; vec4 z; vec4 rect; } p;
void main() {
 vec4 c = textureLod(flowIn, uv, 0.0);
 result = vec4(0.0, 0.0, c.z, 0.0);
 if (c.w < 0.5) return;
 vec2 ts = 1.0 / vec2(textureSize(flowIn, 0));
 vec2 sum = c.xy; float n = 0.0;
 for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
  if (i == 0 && j == 0) continue;
  vec4 o = textureLod(flowIn, uv + vec2(i, j) * ts, 0.0);
  if (o.w > 0.5 && all(lessThan(abs(o.xy - c.xy), vec2(p.t.x)))) { sum += o.xy; n += 1.0; }
 }
 if (n >= 2.0) result = vec4(sum / (n + 1.0), c.z, 1.0);
}
)glsl";
// where the HUD is (WWHD: the finished TV picture differs from the scene there), one texel per 4x4 TV
// pixels: 1 if any of them differs by more than t.z
const char* kHudMask = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D scanImg;
layout(set=0,binding=1) uniform sampler2D sceneImg;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform P { vec4 r0; vec4 r1; vec4 r2; vec4 t; vec4 z; vec4 rect; vec4 a; } p;
void main() {
 ivec2 sz = textureSize(scanImg, 0), b = ivec2(uv * vec2(sz) / 4.0) * 4;
 float m = 0.0;
 for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
  ivec2 c = min(b + ivec2(x, y), sz - 1);
  vec3 d = abs(texelFetch(scanImg, c, 0).rgb - texelFetch(sceneImg, c, 0).rgb);
  m = max(m, max(max(d.r, d.g), d.b));
 }
 result = vec4(m > p.t.z ? 1.0 : 0.0, 0.0, 0.0, 1.0);
}
)glsl";


// The movement of each quarter-size pixel (4x4 TV pixels), chosen among three explanations of where
// it was in the previous picture: the world held still (the camera's movement alone), held on the
// screen (something the camera follows: Link, his boat, the title's boat), or its block's own
// movement (kFlow). The block movement alone tore objects apart: a block's textured part went one
// way and the rest of the same object the other (the title's dragon came out twice, trembling).
// Each is tried with the 3x3 neighbourhood in colour; the one that fits clearly better wins. Output
// (RGBA16F): the movement beyond the camera's, previous -> this frame (TV uv), log depth, 1 where
// sure, 0 where nothing tells them apart (flat colour: kSpread fills it in).
const char* kSelect = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D curImg;
layout(set=0,binding=1) uniform sampler2D prevImg;
layout(set=0,binding=2) uniform sampler2D depthImg;
layout(set=0,binding=3) uniform sampler2D flowImg;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform P { vec4 r0; vec4 r1; vec4 r2; vec4 t; vec4 z; vec4 rect; vec4 a; } p;
// the 2x2 depth texels around q, as textureGather would return them, read one by one: a gather on
// the game's depth-stencil image froze an Adreno 650 (Galaxy S20 FE) until it restarted and gave
// stray lines on others (LASRecomp-Android-Plus 08bbaef); plain fetches work on every driver
vec4 gatherDepth(vec2 q) {
 ivec2 sz = textureSize(depthImg, 0), b = ivec2(floor(q * vec2(sz) - 0.5));
 ivec2 lo = ivec2(0), hi = sz - 1;
 return vec4(texelFetch(depthImg, clamp(b + ivec2(0, 1), lo, hi), 0).r, texelFetch(depthImg, clamp(b + ivec2(1, 1), lo, hi), 0).r,
             texelFetch(depthImg, clamp(b + ivec2(1, 0), lo, hi), 0).r, texelFetch(depthImg, clamp(b, lo, hi), 0).r);
}
float lin(float d) {
 float n = p.z.x, f = p.z.y;
 return 2.0 * n * f / max((f + n) - (d * 2.0 - 1.0) * (f - n), 1e-6);
}
float err(vec2 at, vec2 from, vec2 ts) {
 float e = 0.0;
 for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
  vec2 o = vec2(i, j) * ts;
  vec3 d = abs(textureLod(curImg, at + o, 0.0).rgb - textureLod(prevImg, from + o, 0.0).rgb);
  e += d.r + d.g + d.b;
 }
 return e;
}
void main() {
 vec2 ts = 1.0 / vec2(textureSize(curImg, 0));
 vec4 g = gatherDepth(uv);
 float z = lin(min(min(g.x, g.y), min(g.z, g.w)));
 float zl = clamp(log(z / p.z.x) / log(p.z.y / p.z.x), 0.0, 1.0);
 result = vec4(0.0, 0.0, zl, 0.0);
 vec3 P = vec3((uv.x * 2.0 - 1.0) * p.t.x, (1.0 - uv.y * 2.0) * p.t.y, -1.0) * z;
 vec3 q = vec3(dot(p.r0.xyz, P), dot(p.r1.xyz, P), dot(p.r2.xyz, P)) + vec3(p.r0.w, p.r1.w, p.r2.w);
 if (q.z > -1e-4) return;
 vec2 pp = vec2(q.x / (-q.z) / p.t.x * 0.5 + 0.5, 0.5 - q.y / (-q.z) / p.t.y * 0.5);
 bool inside = all(greaterThan(pp, ts)) && all(lessThan(pp, 1.0 - ts));
 // detail: flat colour can't tell the explanations apart
 vec3 m = vec3(0.0);
 for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) m += textureLod(curImg, uv + vec2(i, j) * ts, 0.0).rgb;
 m /= 9.0;
 float detail = 0.0;
 for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
  vec3 d = abs(textureLod(curImg, uv + vec2(i, j) * ts, 0.0).rgb - m);
  detail += d.r + d.g + d.b;
 }
 float eCam = inside ? err(uv, pp, ts) : 1e9;
 float eHold = err(uv, uv, ts);
 vec4 f = textureLod(flowImg, uv, 0.0);
 vec2 fromFlow = pp - f.xy;
 float eFlow = f.w > 0.5 && all(greaterThan(fromFlow, ts)) && all(lessThan(fromFlow, 1.0 - ts)) ? err(uv, fromFlow, ts) : 1e9;
 // the camera's explanation is the default: the others must fit clearly better
 float best = eCam; vec2 res = vec2(0.0); float second = 1e9;
 if (eHold < best * 0.8 - 0.02) { second = best; best = eHold; res = pp - uv; } else second = min(second, eHold);
 if (eFlow < best * 0.8 - 0.02) { second = min(second, best); best = eFlow; res = f.xy; } else second = min(second, eFlow);
 bool sure = detail > 0.15 && (second > best * 1.25 + 0.03);
 result = vec4(sure ? res : vec2(0.0), zl, sure ? 1.0 : 0.0);
}
)glsl";

// pixels without a sure movement take the one of sure neighbours at the same depth (5x5, every
// t.x texels; t.y: the depth tolerance in the log encoding); found: 0.5
const char* kSpread = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D selIn;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(push_constant) uniform P { vec4 r0; vec4 r1; vec4 r2; vec4 t; vec4 z; vec4 rect; vec4 a; } p;
void main() {
 vec4 c = textureLod(selIn, uv, 0.0);
 result = c;
 if (c.w > 0.25) return;
 vec2 ts = p.t.x / vec2(textureSize(selIn, 0));
 vec2 sum = vec2(0.0); float n = 0.0;
 for (int j = -2; j <= 2; j++) for (int i = -2; i <= 2; i++) {
  vec4 o = textureLod(selIn, uv + vec2(i, j) * ts, 0.0);
  if (o.w > 0.25 && abs(o.z - c.z) < p.t.y) { sum += o.xy; n += 1.0; }
 }
 if (n > 0.0) result = vec4(sum / n, c.z, 0.5);
}
)glsl";

// the depth of the frame's two 3D passes as one (the nearer of both)
const char* kDepthMerge = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D curDepth;
layout(set=0,binding=1) uniform sampler2D keptDepth;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
void main() {
 ivec2 c = ivec2(gl_FragCoord.xy);
 result = vec4(min(texelFetch(curDepth, c, 0).r, texelFetch(keptDepth, c, 0).r), 0.0, 0.0, 1.0);
}
)glsl";

// the scene at quarter size (a 4x4 average from 4 bilinear taps)
const char* kQuarter = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D src;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
void main() {
 vec2 t = 1.0 / vec2(textureSize(src, 0));
 result = 0.25 * (textureLod(src, uv + vec2(-t.x, -t.y), 0.0) + textureLod(src, uv + vec2(t.x, -t.y), 0.0) +
                  textureLod(src, uv + vec2(-t.x, t.y), 0.0) + textureLod(src, uv + vec2(t.x, t.y), 0.0));
}
)glsl";

struct Push { float r0[4], r1[4], r2[4], t[4], z[4], rect[4], a[4]; };
static_assert(sizeof(Push) == 112, "push constants: 7 vec4");
// every pass's push constants and samplers are visible to both stages (the mesh's vertex shader
// reads the depth and the objects' movement)
constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// GX2 formats of this file's own images (formats.cpp format_info)
constexpr uint32_t kRGBA8 = 0x1A, kRGBA16F = 0x20, kR32F = 0x0E, kR8 = 0x01, kD16 = 0x05;

struct Cam {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};  // camera-to-world: columns right, up, back (the camera looks along -z)
    float eye[3] = {0, 0, 0};
    float target = 0;  // distance from the eye to the point the camera looks at (Link, the title's boat)
    float tanY = 0, znear = 1, zfar = 100000;
    uint32_t flags = 0;
    uint64_t frame = ~0ull;  // note_camera: the renderer frame it came in; sceneCam: the frame of its scene
    uint64_t seq = 0;        // note_camera's count
    bool valid = false;
};

struct State {
    Cam cur;  // the newest camera (note_camera; a frame's last one counts: others are 2D, menus)
    // the camera this frame's scene was drawn with (the newest one at the scene copy, layout()) and the
    // previous scene's. (The game hands over the next frame's camera before the frame's swap: the camera
    // at the swap is one ahead of the picture, and the in-between frame came out 1.5 steps on.)
    Cam sceneCam, sceneCamPrev;
    uint64_t cameras = 0;
    // the TV picture before the HUD, and the scene's depth buffer, of this frame
    std::unique_ptr<Surface> scene;
    uint64_t sceneFrame = ~0ull;
    uint32_t depthAddr = 0;
    // the depth of an earlier 3D pass of the frame (keep_depth), and both merged (R32F, kDepthMerge)
    std::unique_ptr<Surface> depthKeep, depthMerged;
    uint64_t keepFrame = ~0ull;
    uint32_t keepAddr = 0;
    // the in-between picture (a scan of the TV picture's size and format) and the HUD mask
    std::unique_ptr<Surface> gen, mask;
    // objects' movement: the scene at quarter size (this frame's and the one before), per-block movement,
    // and the movement per quarter-size pixel (kSelect, then kSpread twice)
    std::unique_ptr<Surface> small[2], flowRaw, flow, sel[2];
    uint64_t smallFrame[2] = {~0ull, ~0ull};
    int smallCur = 0;
    // the warped mesh: the moved scene (the TV picture's format) and its depth, and the index buffer
    std::unique_ptr<Surface> gridColor, gridDepth;
    Buffer gridIndex;
    uint32_t gridCols = 0, gridRows = 0, gridIndices = 0;
    bool gridFailed = false;
    // HUD coverage (hud_coverage.h; draw.cpp writes it): an R8 image of the TV picture's size, cleared
    // at the first layout; valid while every HUD draw of the frame wrote it
    std::unique_ptr<Surface> cov;
    Surface* covTarget = nullptr;
    uint64_t covFrame = ~0ull;
    bool covValid = false;
    // pipelines: the generation passes' layout (5 sampled images, push constants), by fragment shader and format
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler nearest = VK_NULL_HANDLE, linear = VK_NULL_HANDLE;
    std::unordered_map<uint64_t, VkPipeline> pipelines;
    bool failed = false;  // a pipeline failed to build: no more attempts (logged once)
    // the real frames' interval (ns): exponential average of the time between presented real frames
    int64_t lastReal = 0;
    double interval = 0;
    // counters: since the last perf line (window) and in all
    uint64_t generated = 0, early = 0, skipped[kSkipCount] = {}, mesh = 0, withCoverage = 0, withObjects = 0;
    uint64_t totalGenerated = 0, totalEarly = 0, totalSkipped[kSkipCount] = {}, swaps = 0, totalMesh = 0, totalCoverage = 0,
             totalObjects = 0;
    int lastKinds = 0;  // the last generated frame's ways (1 mesh, 2 coverage, 4 objects), for not_presented
} S;

const char* const kSkipNames[kSkipCount] = {"other 60 fps mode", "stereo", "present path", "not shown", "fast", "no camera",
                                             "no scene", "no depth", "size", "cut", "still", "failed"};

void count_skip(Skip why) {
    if (why < 0 || why >= kSkipCount) return;
    S.skipped[why]++;
    S.totalSkipped[why]++;
}

bool env_on(const char* name, bool def) {
    const char* e = getenv(name);
    return e ? atoi(e) != 0 : def;
}

// a game surface (no lookup side effects: find_or_create_surface would claim sizes and adopt aliases):
// 2D, one level and layer, of guest size w x h (0: any); the newest written
Surface* find_surface(uint32_t addr, uint32_t w, uint32_t h, bool depth) {
    if (!addr) return nullptr;
    Surface* best = nullptr;
    auto range = R.surfaces.equal_range(addr);
    for (auto it = range.first; it != range.second; ++it) {
        Surface* s = it->second.get();
        if (s->isDepth != depth || !s->image || !s->view || s->evicted || s->imageType != VK_IMAGE_TYPE_2D || s->mips != 1 || s->arrayLayers != 1)
            continue;
        if (w && (s->width != w || s->height != h)) continue;
        if (!best || s->writeSeq > best->writeSeq) best = s;
    }
    return best;
}

bool same_extent(const Surface* a, const Surface* b) {
    return a && b && a->extent.width == b->extent.width && a->extent.height == b->extent.height;
}

// one of this file's own images (not a game surface: never in R.surfaces), made again when the size or
// format changes (the old one is retired once the GPU is done with it)
bool ensure_image(std::unique_ptr<Surface>& s, uint32_t format, const FormatInfo& fmt, uint32_t w, uint32_t h, VkImageUsageFlags need) {
    if (s && s->image && s->fmt.pixel == fmt.pixel && s->extent.width == w && s->extent.height == h) return (s->usage & need) == need;
    if (s) destroy_surface_image(s.get());
    s = std::make_unique<Surface>();
    s->width = w;
    s->height = h;
    s->format = format;
    s->fmt = fmt;
    s->isDepth = fmt.depth;
    s->scale = 1;
    s->gpuWritten = true;  // (never uploaded from guest memory)
    s->dirty = false;
    try {
        create_surface_image(s.get(), false, {w, h, 1});
    } catch (const std::exception& e) {
        LOG("[framegen] image %ux%u format %d failed: %s", w, h, int(fmt.pixel), e.what());
        s.reset();
        return false;
    }
    return (s->usage & need) == need;
}
// a render target of the generation passes of GX2 format `format` (kRGBA8, kRGBA16F, kR32F, kD16 with depth)
bool ensure_target(std::unique_ptr<Surface>& s, uint32_t format, uint32_t w, uint32_t h, bool depth = false) {
    return ensure_image(s, format, format_info(format, depth), std::max(1u, w), std::max(1u, h),
                        VK_IMAGE_USAGE_SAMPLED_BIT |
                            (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                   : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
}

VkShaderModule shader_module(const char* source, bool vertex) {
    std::string error;
    auto words = vk::compile_glsl(source, vertex, &error);
    if (words.empty() || !error.empty()) {
        LOG("[framegen] shader failed: %s", error.c_str());
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = words.size() * 4;
    ci.pCode = words.data();
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(R.device, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

bool make_layout() {
    if (S.device && S.device != R.device) {  // (a new device: the old objects went with it)
        S.dsl = VK_NULL_HANDLE;
        S.layout = VK_NULL_HANDLE;
        S.nearest = S.linear = VK_NULL_HANDLE;
        S.pipelines.clear();
        S.gridIndex = Buffer{};
        S.gridCols = S.gridRows = S.gridIndices = 0;
    }
    S.device = R.device;
    if (S.layout) return true;
    VkDescriptorSetLayoutBinding b[5];
    for (uint32_t i = 0; i < 5; i++) b[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, kStages, nullptr};
    VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dci.bindingCount = 5;
    dci.pBindings = b;
    if (vkCreateDescriptorSetLayout(R.device, &dci, nullptr, &S.dsl) != VK_SUCCESS) return false;
    VkPushConstantRange push{kStages, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.setLayoutCount = 1;
    lci.pSetLayouts = &S.dsl;
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(R.device, &lci, nullptr, &S.layout) != VK_SUCCESS) return false;
    for (VkFilter filter : {VK_FILTER_NEAREST, VK_FILTER_LINEAR}) {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = filter;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 0;
        if (vkCreateSampler(R.device, &si, nullptr, filter == VK_FILTER_LINEAR ? &S.linear : &S.nearest) != VK_SUCCESS) return false;
    }
    return true;
}

const char* shader_name(const char* src) {
    return src == kWarp ? "the warp" : src == kHudMask ? "the HUD mask" : src == kGridFragment ? "the warped mesh" : src == kCompose ? "the composition"
         : src == kWarpCov ? "the warp with the HUD's coverage" : src == kFlow ? "the block movement" : src == kFilter ? "the movement filter"
         : src == kSelect ? "the movement choice" : src == kSpread ? "the movement spread" : src == kDepthMerge ? "the depth merge"
         : src == kQuarter ? "the quarter-size scene" : "?";
}

// the pipeline of fragment shader `src` drawing into `format`: a full-screen triangle (kVertex), or with
// `depth` the warped mesh (kGridVertex: strips with restarts, depth test, gl_FragDepth)
VkPipeline pipeline_for(const char* src, VkFormat format, VkFormat depth = VK_FORMAT_UNDEFINED) {
    const uint64_t key = (uint64_t(uintptr_t(src)) * 1000003u ^ uint64_t(format)) * 1000003u ^ uint64_t(depth);
    if (auto it = S.pipelines.find(key); it != S.pipelines.end()) return it->second;
    const bool mesh = depth != VK_FORMAT_UNDEFINED;
    VkShaderModule vs = shader_module(mesh ? kGridVertex : kVertex, true), fs = shader_module(src, false);
    VkPipeline pipe = VK_NULL_HANDLE;
    if (vs && fs) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (int i = 0; i < 2; i++) {
            stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
            stages[i].module = i ? fs : vs;
            stages[i].pName = "main";
        }
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = mesh ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ia.primitiveRestartEnable = mesh ? VK_TRUE : VK_FALSE;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        if (mesh) {  // what is near always covers what is far
            dss.depthTestEnable = VK_TRUE;
            dss.depthWriteEnable = VK_TRUE;
            dss.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        }
        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        bs.attachmentCount = 1;
        bs.pAttachments = &blend;
        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        ds.dynamicStateCount = 2;
        ds.pDynamicStates = dyn;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &format;
        rendering.depthAttachmentFormat = depth;
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipeline_targets(ci, rendering);  // (a render pass on devices without dynamic rendering)
        ci.stageCount = 2;
        ci.pStages = stages;
        ci.pVertexInputState = &vi;
        ci.pInputAssemblyState = &ia;
        ci.pViewportState = &vp;
        ci.pRasterizationState = &rs;
        ci.pMultisampleState = &ms;
        ci.pDepthStencilState = &dss;
        ci.pColorBlendState = &bs;
        ci.pDynamicState = &ds;
        ci.layout = S.layout;
        if (vkCreateGraphicsPipelines(R.device, R.pipelineCache, 1, &ci, nullptr, &pipe) != VK_SUCCESS) pipe = VK_NULL_HANDLE;
    }
    if (vs) vkDestroyShaderModule(R.device, vs, nullptr);
    if (fs) vkDestroyShaderModule(R.device, fs, nullptr);
    if (!pipe) {
        LOG("[framegen] pipeline for %s (format %d) failed", shader_name(src), int(format));
        // (the warped mesh and the movement passes have a fallback; the warp and its HUD mask do not)
        if (src == kWarp || src == kHudMask) S.failed = true;
        S.pipelines.emplace(key, VK_NULL_HANDLE);  // (not tried again)
        return VK_NULL_HANDLE;
    }
    LOG("[framegen] pipeline for %s (format %d)", shader_name(src), int(format));
    S.pipelines.emplace(key, pipe);
    return pipe;
}

struct Input {
    Surface* image;
    VkSampler sampler;
};
VkDescriptorSet descriptor_set(const Input* in, int n) {
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = R.descriptorPool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &S.dsl;
    vk_check(vkAllocateDescriptorSets(R.device, &allocation, &set), "framegen descriptor set");
    VkDescriptorImageInfo ii[5];
    VkWriteDescriptorSet wr[5]{};
    for (int k = 0; k < 5; k++) {
        const Input& x = in[k < n ? k : 0];  // unused bindings get the first input
        ii[k] = {x.sampler, x.image->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        wr[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[k].dstSet = set;
        wr[k].dstBinding = uint32_t(k);
        wr[k].descriptorCount = 1;
        wr[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr[k].pImageInfo = &ii[k];
    }
    vkUpdateDescriptorSets(R.device, 5, wr, 0, nullptr);
    return set;
}
// every input of a pass is sampled by both stages (the mesh's vertex shader too)
void sampled(Surface* s) {
    transition_image(s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                     VK_ACCESS_SHADER_READ_BIT);
}
// one pass of `pipe` into `target`: a full-screen triangle, or (`depthTarget`) the warped mesh's strips
// with the index buffer; up to 5 sampled inputs (unused bindings get the first), each made ready here.
// The target is left sampled (the next pass reads it).
void run_pass(Surface& target, VkPipeline pipe, const Input* in, int n, const Push& pc, Surface* depthTarget = nullptr) {
    for (int k = 0; k < n; k++) sampled(in[k].image);
    transition_image(&target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    VkCommandBuffer cmd = command_buffer();
    if (depthTarget) {  // (cleared as a transfer: the render-pass path loads depth attachments)
        transition_image(depthTarget, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        const VkClearDepthStencilValue one{1.0f, 0};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        record::vkCmdClearDepthStencilImage(cmd, depthTarget->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &one, 1, &range);
        transition_image(depthTarget, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }
    const VkDescriptorSet set = descriptor_set(in, n);
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = target.view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // (a full-screen pass writes every pixel; the mesh leaves what nothing covers at 0)
    attachment.loadOp = depthTarget ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (depthTarget) {
        depthAttachment.imageView = depthTarget->view;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    }
    const VkExtent2D extent{target.extent.width, target.extent.height};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    if (depthTarget) rendering.pDepthAttachment = &depthAttachment;
    begin_pass(cmd, rendering, [&] {
        PassBegin pass = color_pass(target.fmt.pixel, target.layout);
        if (depthTarget) {
            pass.formats.depth = depthTarget->fmt.pixel;
            pass.layouts[8] = depthTarget->layout;
        }
        return pass;
    });
    R.rendering = true;
    R.passTracked = false;
    VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    record::vkCmdSetViewport(cmd, 0, 1, &viewport);
    record::vkCmdSetScissor(cmd, 0, 1, &scissor);
    record::vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    record::vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, S.layout, 0, 1, &set, 0, nullptr);
    record::vkCmdPushConstants(cmd, S.layout, kStages, 0, sizeof pc, &pc);
    if (depthTarget) {
        record::vkCmdBindIndexBuffer(cmd, S.gridIndex.buffer, 0, VK_INDEX_TYPE_UINT32);
        record::vkCmdDrawIndexed(cmd, S.gridIndices, 1, 0, 0, 0);
    } else {
        record::vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    end_pass(cmd);
    R.rendering = false;
    R.passTracked = false;
    mark_gpu_written(&target);
    sampled(&target);
}

// the mesh's index buffer (strips, one per row of cells, joined by primitive restarts) for `cols` x
// `rows` vertices (the fork's ensure_grid)
bool ensure_grid_index(uint32_t cols, uint32_t rows) {
    if (S.gridIndex.buffer && S.gridCols == cols && S.gridRows == rows) return true;
    if (S.gridIndex.buffer) defer_buffer(S.gridIndex);
    S.gridIndex = Buffer{};
    std::vector<uint32_t> idx;
    idx.reserve(size_t(2) * cols * (rows - 1) + rows);
    for (uint32_t y = 0; y + 1 < rows; y++) {
        for (uint32_t x = 0; x < cols; x++) {
            idx.push_back(y * cols + x);
            idx.push_back((y + 1) * cols + x);
        }
        idx.push_back(0xFFFFFFFFu);
    }
    try {
        S.gridIndex = create_buffer(idx.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    } catch (const std::exception& e) {
        LOG("[framegen] the warped mesh's index buffer failed: %s", e.what());
        S.gridIndex = Buffer{};
        return false;
    }
    std::memcpy(S.gridIndex.mapped, idx.data(), idx.size() * 4);
    S.gridCols = cols;
    S.gridRows = rows;
    S.gridIndices = uint32_t(idx.size());
    return true;
}

int64_t steady_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// every 600 swaps: the counters so far
void log_totals() {
    if (++S.swaps % 600) return;
    std::string why;
    for (int k = 0; k < kSkipCount; k++)
        if (S.totalSkipped[k]) why += std::string(why.empty() ? "" : ", ") + kSkipNames[k] + " " + std::to_string(S.totalSkipped[k]);
    LOG("[framegen] %llu in-between frames generated (%llu by the warped mesh, %llu with the HUD's coverage, %llu with objects' "
        "movement; %llu presented early), %llu real frames shown alone (%s); real frames every %.2f ms",
        (unsigned long long)S.totalGenerated, (unsigned long long)S.totalMesh, (unsigned long long)S.totalCoverage,
        (unsigned long long)S.totalObjects, (unsigned long long)S.totalEarly, (unsigned long long)S.swaps - S.totalGenerated,
        why.empty() ? "-" : why.c_str(), S.interval / 1e6);
}

// HUD coverage (hud_coverage.h; WWHD_HUD_COVERAGE=0: off): needs independent blending (the coverage
// attachment blends otherwise than the picture)
bool hud_coverage_on() {
    static const bool on = env_on("WWHD_HUD_COVERAGE", true);
    return on && R.enabledFeatures.independentBlend && false;  // (draw.cpp writes it from the next change on)
}
// at the first layout on the TV picture: from here on the HUD draws into `tv` also write their coverage
void hud_coverage_begin(Surface* tv) {
    S.covFrame = ~0ull;
    S.covTarget = nullptr;
    if (!tv || !hud_coverage_on()) return;
    if (!ensure_image(S.cov, kR8, format_info(kR8, false), tv->extent.width, tv->extent.height,
                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return;
    // guest size and scales as the picture's, so draws scale their viewports alike
    S.cov->width = tv->width;
    S.cov->height = tv->height;
    S.cov->sx = tv->sx;
    S.cov->sy = tv->sy;
    S.cov->scale = tv->scale;
    S.cov->ax = tv->ax;
    S.cov->ay = tv->ay;
    S.cov->dyn = tv->dyn;
    transition_image(S.cov.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    const VkClearColorValue zero{};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    record::vkCmdClearColorImage(command_buffer(), S.cov->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    mark_gpu_written(S.cov.get());
    S.covTarget = tv;
    S.covFrame = R.frame;
    S.covValid = true;
}

}  // namespace

bool enabled() { return framegen::requested(); }

Surface* hud_coverage_target(Surface* color0) {
    return S.covFrame == R.frame && S.covValid && color0 && color0 == S.covTarget && S.cov && S.cov->image ? S.cov.get() : nullptr;
}
void hud_coverage_failed() { S.covValid = false; }
bool hud_coverage_wanted() { return hud_coverage_on() && framegen::requested(); }

// the main depth buffer cleared a second time in a frame after being drawn into (the title draws
// Outset, then the boat over a cleared depth): the in-between frame keeps the first pass's depth
// (surfaces.cpp clear_depth_stencil: depth_clearing before the clear, depth_cleared after it). "Drawn
// into" since the last clear: its writeSeq moved on from the one the clear left.
static uint64_t g_clearFrame = ~0ull, g_clearedSeq = 0;
static uint32_t g_clearAddr = 0;
void depth_clearing(Surface* s) {
    if (!framegen::requested() || !s || !s->image || s->addr != R.mainDepthAddr || !s->fmt.depth) return;
    const bool drawnSince = g_clearFrame == R.frame && g_clearAddr == s->addr && s->writeSeq != g_clearedSeq;
    if (!drawnSince || S.sceneFrame == R.frame || stereo::frame.mode != stereo::Off) return;
    if (!ensure_image(S.depthKeep, s->format, s->fmt, s->extent.width, s->extent.height, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return;
    transition_image(s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    transition_image(S.depthKeep.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageCopy r{};
    r.srcSubresource = r.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    r.extent = {s->extent.width, s->extent.height, 1};
    record::vkCmdCopyImage(command_buffer(), s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, S.depthKeep->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    mark_gpu_written(S.depthKeep.get());
    S.keepFrame = R.frame;
    S.keepAddr = s->addr;
    static int logged = 0;
    if (logged++ < 3)
        LOG("[framegen] a second 3D pass in the frame: the first one's depth kept (%08X %ux%u)", s->addr, s->extent.width, s->extent.height);
}
void depth_cleared(Surface* s) {
    if (!framegen::requested() || !s || s->addr != R.mainDepthAddr) return;
    g_clearFrame = R.frame;
    g_clearAddr = s->addr;
    g_clearedSeq = s->writeSeq;
}

// render thread (OP_FRAMEGEN_CAMERA): the 3D camera of the frame being drawn
void note_camera(const uint32_t* words, uint32_t n) {
    if (n < framegen::kCameraWords) return;
    float v[framegen::kCameraWords - 1];
    memcpy(v, words, sizeof v);
    const float *eye = v, *center = v + 3, *up = v + 6;
    static const bool camLog = getenv("WWHD_CAM_LOG") != nullptr;
    if (camLog)
        LOG("[framegen] cam frame %llu eye %.1f %.1f %.1f center %.1f %.1f %.1f fovy %.2f tan %.4f near %.2f far %.0f flags %u",
            (unsigned long long)R.frame, eye[0], eye[1], eye[2], center[0], center[1], center[2], v[9], v[12], v[10], v[11],
            words[framegen::kCameraWords - 1]);
    basis(eye, center, up, S.cur.m);
    memcpy(S.cur.eye, eye, sizeof S.cur.eye);
    S.cur.target = std::sqrt((center[0] - eye[0]) * (center[0] - eye[0]) + (center[1] - eye[1]) * (center[1] - eye[1]) +
                             (center[2] - eye[2]) * (center[2] - eye[2]));
    S.cur.tanY = v[12];
    S.cur.znear = v[10];
    S.cur.zfar = v[11];
    S.cur.flags = words[framegen::kCameraWords - 1];
    S.cur.frame = R.frame;
    S.cur.seq = ++S.cameras;
    S.cur.valid = true;
    static int logged = 0;
    if (logged < 2) {
        logged++;
        LOG("[framegen] camera (frame %llu): fovy %.1f (tan %.4f as drawn) near %.2f far %.1f", (unsigned long long)R.frame, v[9], v[12],
            v[10], v[11]);
    }
}

// render thread (OP_FRAMEGEN_LAYOUT): a layout is about to be drawn into the target bound now. At the
// first one on the TV picture this frame, the scene is complete: copy it aside, and note its depth buffer.
void layout(uint32_t colorBase, uint32_t width, uint32_t height, uint32_t depthBase) {
    if (S.sceneFrame == R.frame || stereo::frame.mode != stereo::Off) return;
    // (no copy while another 60 fps mode or fast forward runs: no in-between frame follows)
    if (S.cur.valid && S.cur.frame == R.frame && (S.cur.flags & (framegen::kInterp | framegen::kTrue60 | framegen::kFastForward))) return;
    // the TV picture: 16:9 at the console's size (the GamePad's 854x480 family is not it)
    if (height < 360 || height == 480 || std::abs((float)width * 9.0f / ((float)height * 16.0f) - 1.0f) > 0.03f) return;
    Surface* color = find_surface(colorBase, width, height, false);
    if (!color || color->fmt.depth || color->fmt.kind != FormatInfo::FLOAT) return;
    S.sceneFrame = R.frame;
    if (!ensure_image(S.scene, color->format, color->fmt, color->extent.width, color->extent.height,
                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        S.sceneFrame = ~0ull;
        return;
    }
    transition_image(color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    transition_image(S.scene.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageCopy r{};
    r.srcSubresource = r.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    r.extent = {color->extent.width, color->extent.height, 1};
    record::vkCmdCopyImage(command_buffer(), color->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, S.scene->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    mark_gpu_written(S.scene.get());
    // the scene's camera: the newest since the previous scene (none: a frame without a 3D camera)
    if (S.cur.valid && S.cur.seq != S.sceneCam.seq) {
        S.sceneCamPrev = S.sceneCam;
        S.sceneCam = S.cur;
    } else {
        S.sceneCamPrev = Cam{};
        S.sceneCam.valid = false;
    }
    S.sceneCam.frame = R.frame;
    if (getenv("WWHD_CAM_LOG"))
        LOG("[framegen] scene frame %llu: camera %llu (came in frame %llu), eye %.1f %.1f %.1f", (unsigned long long)R.frame,
            (unsigned long long)S.sceneCam.seq, (unsigned long long)S.cur.frame, S.sceneCam.eye[0], S.sceneCam.eye[1], S.sceneCam.eye[2]);
    hud_coverage_begin(color);  // the HUD draws from here on also write how much they cover
    // the scene's depth: the TV-sized depth buffer bound for the HUD, else the main 3D pass's
    S.depthAddr = 0;
    Surface* d = find_surface(depthBase, 0, 0, true);
    if (!same_extent(d, color)) d = find_surface(R.mainDepthAddr, 0, 0, true);
    if (same_extent(d, color)) S.depthAddr = d->addr;
    static int logged = 0;
    if (logged < 2) {
        logged++;
        LOG("[framegen] scene %08X %ux%u (%ux%u) format %d, depth %08X format %d", color->addr, width, height, color->extent.width,
            color->extent.height, int(color->fmt.pixel), S.depthAddr, d && S.depthAddr ? int(d->fmt.pixel) : 0);
    }
}

int64_t real_presented(bool shown, int64_t now) {
    if (!shown) {
        S.lastReal = 0;
        return 0;
    }
    if (S.lastReal) {
        const double d = double(now - S.lastReal);
        if (d > 5e6 && d < 1e8) S.interval = S.interval > 0 ? S.interval * 0.9 + d * 0.1 : d;
    }
    S.lastReal = now;
    // (the real frames come faster than 40 a second: no room for an in-between frame)
    if (S.interval < 25e6) return 0;
    return int64_t(std::min(S.interval, 50e6) * 0.5);
}

// The in-between picture (the fork's record(), fitted to this renderer: TV picture space, our images
// and passes): the objects' movement (kQuarter, kFlow, kFilter, kSelect, kSpread), the depth of an
// earlier 3D pass merged in (kDepthMerge), then the warped mesh with kCompose (default), else the
// per-pixel warp with the HUD's coverage (kWarpCov) or with the HUD mask (kHudMask, kWarp).
Surface* generate(Surface* scan, Skip skip) {
    log_totals();
    const Cam& cur = S.sceneCam;
    const Cam& prev = S.sceneCamPrev;
    // WWHD_FRAMEGEN_OBJECTS=0: objects move with the camera only; WWHD_FRAMEGEN_SELECT=0: the blocks'
    // movement as it is; WWHD_FRAMEGEN_GRID=0: the per-pixel warp
    static const bool objects = env_on("WWHD_FRAMEGEN_OBJECTS", true), selectOn = env_on("WWHD_FRAMEGEN_SELECT", true),
                      gridOn = env_on("WWHD_FRAMEGEN_GRID", true);
    if (skip == kNone) {
        if (!cur.valid || cur.frame != R.frame) skip = kNoCamera;
        else if (cur.flags & (framegen::kInterp | framegen::kTrue60 | framegen::kFastForward)) skip = kOtherMode;
        else if (S.sceneFrame != R.frame || !S.scene || !S.scene->image) skip = kNoScene;
        else if (!scan || !scan->image || !scan->view || !same_extent(scan, S.scene.get()) || scan->fmt.pixel != S.scene->fmt.pixel)
            skip = kSize;
        else if (!(cur.znear > 0) || !(cur.zfar > cur.znear) || !(cur.tanY > 0)) skip = kNoCamera;
    }
    Surface* depth = nullptr;
    if (skip == kNone) {
        depth = find_surface(S.depthAddr, 0, 0, true);
        if (!same_extent(depth, S.scene.get())) skip = kNoDepth;
    }
    // the generated camera: half the last rotation (D = Cprev^T Ccur, D^(1/2)) and half the last
    // movement, in the real camera's space; none across a cut or without a camera of the frame before.
    // The objects' search needs the whole step (full_step). A still camera with the objects' movement
    // on still makes the frame (what moves on its own moves on).
    float half[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, move[3] = {0, 0, 0};
    float full[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}, fullMove[3] = {0, 0, 0};
    if (skip == kNone) {
        if (!prev.valid || prev.frame + 1 != cur.frame) skip = kCut;
    }
    if (skip == kNone) {
        const Step step = half_step(prev.m, prev.eye, cur.m, cur.eye, half, move);
        if (step == Step::Cut) skip = kCut;
        else if (step == Step::Still && !objects) skip = kStill;
        else full_step(prev.m, prev.eye, cur.m, cur.eye, full, fullMove);
    }
    // the images and pipelines every way needs
    if (skip == kNone) {
        const VkImageUsageFlags target = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (S.failed || !make_layout() ||
            !ensure_image(S.gen, scan->format, scan->fmt, scan->extent.width, scan->extent.height, target | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
            skip = kFailed;
    }
    if (skip != kNone) {
        count_skip(skip);
        static int logged = 0;
        if (logged < 4 && skip != kStill && skip != kNotShown) {
            logged++;
            LOG("[framegen] frame %llu not generated (%s): camera frame %lld, scene frame %lld, depth %08X", (unsigned long long)R.frame,
                kSkipNames[skip], cur.valid ? (long long)cur.frame : -1ll, S.scene ? (long long)S.sceneFrame : -1ll, S.depthAddr);
        }
        return nullptr;
    }
    // the TV picture's size relative to its own height: its projection's aspect (the game widens it)
    const float znear = cur.znear, zfar = cur.zfar;
    const float tanY = cur.tanY, tanX = tanY * float(S.scene->extent.width) / float(S.scene->extent.height);
    static const float hudThreshold = getenv("WWHD_FRAMEGEN_HUD") ? (float)atof(getenv("WWHD_FRAMEGEN_HUD")) : 3.0f / 255.0f;
    static const bool tint = env_on("WWHD_FRAMEGEN_TINT", false);
    // WWHD_FRAMEGEN_FOLLOW=0: everything moves with the camera's movement (what it follows too)
    static const bool followOn = env_on("WWHD_FRAMEGEN_FOLLOW", true);
    static const float debugView = getenv("WWHD_FRAMEGEN_DEBUG") ? (float)atof(getenv("WWHD_FRAMEGEN_DEBUG")) : 0.0f;
    Push pc{{half[0], half[1], half[2], move[0]}, {half[3], half[4], half[5], move[1]}, {half[6], half[7], half[8], move[2]},
            {tanX, tanY, hudThreshold, tint ? 1.0f : 0.0f}, {znear, zfar, 1, 0}, {0, 0, 1, 1},
            {cur.target, followOn ? 1.0f : 0.0f, 0, debugView}};
    // (in this port every pass works in the TV picture's own space: the published scan, the scene copy
    // and the targets share its format, so nothing is display-encoded by hand: a.z, the fork's sRGB
    // switch, stays 0)
    end_encoder();
    const uint32_t tw = S.scene->extent.width, th = S.scene->extent.height;
    // a second 3D pass cleared the depth (the title's boat over Outset): both passes' depth merged
    Surface* depthIn = depth;
    if (S.keepFrame == S.sceneFrame && S.keepAddr == depth->addr && same_extent(S.depthKeep.get(), depth)) {
        const VkPipeline mergePipe = pipeline_for(kDepthMerge, format_info(kR32F, false).pixel);
        if (mergePipe && ensure_target(S.depthMerged, kR32F, tw, th)) {
            const Input in[2] = {{depth, S.nearest}, {S.depthKeep.get(), S.nearest}};
            run_pass(*S.depthMerged, mergePipe, in, 2, Push{});
            depthIn = S.depthMerged.get();
        }
    }
    // objects' own movement: this frame's scene at quarter size, searched in the previous frame's per
    // 16x16 block, then filtered; then the movement per quarter-size pixel (kSelect, kSpread)
    bool flow = false;
    Surface* motion = nullptr;  // the movement the generation passes read (binding 4)
    const VkFormat rgba8 = format_info(kRGBA8, false).pixel, rgba16f = format_info(kRGBA16F, false).pixel;
    const VkPipeline quarterPipe = objects ? pipeline_for(kQuarter, rgba8) : VK_NULL_HANDLE;
    const VkPipeline flowPipe = objects ? pipeline_for(kFlow, rgba16f) : VK_NULL_HANDLE;
    const VkPipeline filterPipe = objects ? pipeline_for(kFilter, rgba16f) : VK_NULL_HANDLE;
    if (quarterPipe && flowPipe && filterPipe) {
        const uint32_t sw = std::max(1u, tw / 4), sh = std::max(1u, th / 4);
        S.smallCur ^= 1;
        auto& small = S.small[S.smallCur];
        auto& before = S.small[S.smallCur ^ 1];
        if (ensure_target(small, kRGBA8, sw, sh) && ensure_target(S.flowRaw, kRGBA16F, (tw + 15) / 16, (th + 15) / 16) &&
            ensure_target(S.flow, kRGBA16F, (tw + 15) / 16, (th + 15) / 16)) {
            const Input qin[1] = {{S.scene.get(), S.linear}};
            run_pass(*small, quarterPipe, qin, 1, Push{});
            S.smallFrame[S.smallCur] = S.sceneFrame;
            if (before && before->image && before->extent.width == sw && before->extent.height == sh &&
                S.smallFrame[S.smallCur ^ 1] + 1 == S.sceneFrame) {
                const Push fp{{full[0], full[1], full[2], fullMove[0]}, {full[3], full[4], full[5], fullMove[1]},
                              {full[6], full[7], full[8], fullMove[2]}, {tanX, tanY, 0, 0}, {znear, zfar, 1, 0}, {}, {}};
                const Input fin[3] = {{small.get(), S.nearest}, {before.get(), S.nearest}, {depthIn, S.nearest}};
                run_pass(*S.flowRaw, flowPipe, fin, 3, fp);
                const Push ff{{}, {}, {}, {1.2f / float(sw), 0, 0, 0}, {}, {}, {}};  // ~1 quarter-size pixel
                const Input gin[1] = {{S.flowRaw.get(), S.nearest}};
                run_pass(*S.flow, filterPipe, gin, 1, ff);
                flow = true;
                const VkPipeline selPipe = selectOn ? pipeline_for(kSelect, rgba16f) : VK_NULL_HANDLE;
                const VkPipeline spreadPipe = selectOn ? pipeline_for(kSpread, rgba16f) : VK_NULL_HANDLE;
                if (selPipe && spreadPipe && ensure_target(S.sel[0], kRGBA16F, sw, sh) && ensure_target(S.sel[1], kRGBA16F, sw, sh)) {
                    const Input sin_[4] = {{small.get(), S.nearest}, {before.get(), S.linear}, {depthIn, S.nearest}, {S.flow.get(), S.nearest}};
                    run_pass(*S.sel[0], selPipe, sin_, 4, fp);
                    // spread twice (every 2, then every 4 quarter-size pixels); depth within ~8%
                    const float tol = float(0.08 / std::log(zfar / znear));
                    for (int k = 0; k < 2; k++) {
                        const Push sp{{}, {}, {}, {k ? 4.0f : 2.0f, tol, 0, 0}, {}, {}, {}};
                        const Input spin[1] = {{S.sel[k].get(), S.nearest}};
                        run_pass(*S.sel[k ^ 1], spreadPipe, spin, 1, sp);
                    }
                    motion = S.sel[0].get();
                }
            }
        }
    }
    pc.z[3] = flow ? 1.0f : 0.0f;
    if (flow && !motion) motion = S.flow.get();
    Surface* motionIn = flow ? motion : S.scene.get();
    Surface* cov = hud_coverage_target(S.covTarget);
    bool made = false, byMesh = false;
    // the warped mesh (WWHD_FRAMEGEN_GRID=0: the per-pixel warp below), with the HUD's coverage of this
    // frame where every HUD draw wrote it, else its mask
    if (gridOn && !S.gridFailed) {
        const VkFormat d16 = format_info(kD16, true).pixel;
        const VkPipeline gridPipe = pipeline_for(kGridFragment, scan->fmt.pixel, d16);
        const VkPipeline composePipe = pipeline_for(kCompose, scan->fmt.pixel);
        const VkPipeline maskPipe = cov ? VK_NULL_HANDLE : pipeline_for(kHudMask, rgba8);
        // a vertex every 2 TV pixels at 720 lines, every 3 at 1080 (about the same on the screen)
        const uint32_t cell = std::max(2u, (th + 359) / 360);
        const float margin = 0.08f;
        const uint32_t cols = uint32_t((tw * (1 + 2 * margin)) / cell) + 2, rows = uint32_t((th * (1 + 2 * margin)) / cell) + 2;
        const uint32_t mw = (tw + 3) / 4, mh = (th + 3) / 4;
        if (!gridPipe || !composePipe) {
            S.gridFailed = true;
            LOG("[framegen] the warped mesh is not available: the in-between frames are made per pixel");
        } else if (ensure_target(S.gridColor, scan->format, tw, th) && ensure_target(S.gridDepth, kD16, tw, th, true) &&
                   S.gridColor->fmt.pixel == scan->fmt.pixel && ensure_grid_index(cols, rows) &&
                   (cov || (maskPipe && ensure_target(S.mask, kRGBA8, mw, mh)))) {
            if (!cov) {
                const Input min_[2] = {{scan, S.nearest}, {S.scene.get(), S.nearest}};
                run_pass(*S.mask, maskPipe, min_, 2, pc);
            }
            Push gp = pc;
            gp.rect[0] = float(cols);
            gp.rect[1] = float(rows);
            gp.rect[2] = margin;
            gp.rect[3] = debugView;
            const Input gin[5] = {{S.scene.get(), S.nearest}, {depthIn, S.nearest}, {S.scene.get(), S.nearest}, {S.scene.get(), S.linear},
                                  {motionIn, S.nearest}};
            run_pass(*S.gridColor, gridPipe, gin, 5, gp, S.gridDepth.get());
            const Input cin[4] = {{scan, S.linear}, {S.gridColor.get(), S.linear}, {cov ? cov : S.mask.get(), S.linear}, {S.scene.get(), S.linear}};
            run_pass(*S.gen, composePipe, cin, 4, pc);
            made = byMesh = true;
        }
    }
    // the HUD's coverage of this frame (every HUD draw wrote it): the scene moves behind translucent
    // HUD parts too, and no mask pass is needed
    if (!made && cov) {
        if (const VkPipeline covPipe = pipeline_for(kWarpCov, scan->fmt.pixel)) {
            const Input cin[5] = {{scan, S.nearest}, {depthIn, S.nearest}, {cov, S.linear}, {S.scene.get(), S.nearest}, {motionIn, S.nearest}};
            run_pass(*S.gen, covPipe, cin, 5, pc);
            made = true;
        }
    }
    // the HUD mask (one pass at a quarter of the TV picture's size) and the per-pixel warp
    if (!made) {
        const VkPipeline warpPipe = pipeline_for(kWarp, scan->fmt.pixel), maskPipe = pipeline_for(kHudMask, rgba8);
        if (warpPipe && maskPipe && ensure_target(S.mask, kRGBA8, (tw + 3) / 4, (th + 3) / 4)) {
            const Input min_[2] = {{scan, S.nearest}, {S.scene.get(), S.nearest}};
            run_pass(*S.mask, maskPipe, min_, 2, pc);
            const Input win[5] = {{scan, S.nearest}, {depthIn, S.nearest}, {S.mask.get(), S.linear}, {S.mask.get(), S.linear}, {motionIn, S.nearest}};
            run_pass(*S.gen, warpPipe, win, 5, pc);
            made = true;
        }
    }
    if (!made) {
        count_skip(kFailed);
        return nullptr;
    }
    S.gen->dyn = scan->dyn;  // (the size the picture stands for: as the real scan's)
    S.generated++;
    S.totalGenerated++;
    if (byMesh) S.mesh++, S.totalMesh++;
    if (cov) S.withCoverage++, S.totalCoverage++;
    if (flow) S.withObjects++, S.totalObjects++;
    S.lastKinds = (byMesh ? 1 : 0) | (cov ? 2 : 0) | (flow ? 4 : 0);
    static bool first = true;
    if (first) {
        first = false;
        LOG("[framegen] first in-between frame (frame %llu): %ux%u format %d, tan %.4f x %.4f, step %.2f %.2f %.2f, %s%s%s%s%s",
            (unsigned long long)R.frame, S.gen->extent.width, S.gen->extent.height, int(S.gen->fmt.pixel), tanX, tanY, move[0] * 2, move[1] * 2,
            move[2] * 2, byMesh ? "warped mesh" : "per pixel", cov ? ", HUD coverage" : ", HUD mask", flow ? ", objects' movement" : "",
            tint ? ", tinted" : "", followOn ? "" : ", follow off");
    }
    return S.gen.get();
}

std::unique_ptr<Surface>& generated_scan() { return S.gen; }

void presented(bool early) {
    if (early) {
        S.early++;
        S.totalEarly++;
    }
}
void not_presented() {
    S.generated--;
    S.totalGenerated--;
    if (S.lastKinds & 1) S.mesh--, S.totalMesh--;
    if (S.lastKinds & 2) S.withCoverage--, S.totalCoverage--;
    if (S.lastKinds & 4) S.withObjects--, S.totalObjects--;
    S.lastKinds = 0;
    count_skip(kNotShown);
}

std::string perf_line() {
    char buf[160];
    snprintf(buf, sizeof buf, "generated %llu (mesh %llu, HUD coverage %llu, objects %llu; presented early %llu), skipped",
             (unsigned long long)S.generated, (unsigned long long)S.mesh, (unsigned long long)S.withCoverage,
             (unsigned long long)S.withObjects, (unsigned long long)S.early);
    std::string out = buf;
    uint64_t skipped = 0;
    for (int k = 0; k < kSkipCount; k++) skipped += S.skipped[k];
    out += " " + std::to_string(skipped);
    for (int k = 0; k < kSkipCount; k++)
        if (S.skipped[k]) out += std::string(", ") + kSkipNames[k] + " " + std::to_string(S.skipped[k]);
    S.generated = S.early = S.mesh = S.withCoverage = S.withObjects = 0;
    for (auto& k : S.skipped) k = 0;
    return out;
}

}  // namespace gfxvk::reproj
