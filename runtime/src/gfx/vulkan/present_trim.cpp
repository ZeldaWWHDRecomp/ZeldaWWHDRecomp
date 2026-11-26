// Cheaper window composition (gpusave A2): see present_trim.h.
#include "present_trim.h"
#include "backend.h"
#include "runtime.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace gfxvk::present_trim {
namespace {
// The general shader's encoding conversion (present.cpp fragmentSource) without its HDR branch: plain() requires
// hdrWhite == 0, where conv 1 is toLinear(clamp(c)) and conv 2 toSrgb(clamp(c)), exactly as there.
#define TRIM_ENCODING R"glsl(
vec3 toLinear(vec3 c) {
 return mix(pow((c+0.055)/1.055,vec3(2.4)),c/12.92,lessThanEqual(c,vec3(0.04045)));
}
vec3 toSrgb(vec3 c) {
 return mix(1.055*pow(c,vec3(1.0/2.4))-0.055,c*12.92,lessThanEqual(c,vec3(0.0031308)));
}
)glsl"
#define TRIM_PARAMS R"glsl(
layout(push_constant) uniform Params { int aa; int conv; float sharp; float foot; float alpha; float hdrWhite; float hdrPeak; float hdrExposure; float hdrHighlights; int pq; int hdrSdrCurve; float backdrop; } params;
)glsl"
// Same value as the general shader for aa 0, sharp <= 1, foot <= 1.25, backdrop 0, hdrWhite 0, pq 0: there
// c = texture(image, uv).rgb, coverage = 1, result = vec4(c * 1 * 1, 1 * alpha) (multiplications by 1 are exact).
const char* kPlain = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D image;
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
)glsl" TRIM_PARAMS TRIM_ENCODING R"glsl(
void main() {
 vec3 c=texture(image,uv).rgb;
 if(params.conv==1)c=toLinear(clamp(c,0.0,1.0));
 else if(params.conv==2)c=toSrgb(clamp(c,0.0,1.0));
 result=vec4(c,params.alpha);
}
)glsl";
// The general shader's edge colour (backdrop 2..5): four texel-centred edge taps of the single-level image, each
// weighted 0.25 (a power of two: the products and their sum are the same in any stage, fused or not), once per vertex.
const char* kBarsVertex = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D image;
layout(location=0) out vec2 uv;
layout(location=1) flat out vec3 edgeColour;
)glsl" TRIM_PARAMS R"glsl(
void main() {
 vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
 uv=p;
 gl_Position=vec4(p*2.0-1.0,0.0,1.0);
 vec2 size=vec2(textureSize(image,0));
 vec3 c=vec3(0.0);
 for(int i=0;i<4;++i) {
  float along=(float(i)*2.0+1.0)/8.0;
  vec2 edge=params.backdrop==2.0?vec2(along,0.5/size.y):
   params.backdrop==3.0?vec2(along,1.0-0.5/size.y):
   params.backdrop==4.0?vec2(0.5/size.x,along):vec2(1.0-0.5/size.x,along);
  c+=textureLod(image,edge,0.0).rgb*0.25;
 }
 edgeColour=c;
}
)glsl";
// the rest of the general shader for backdrop >= 2 (coverage 1): conversion, HDR mapping, PQ
const char* kBarsFragment = R"glsl(#version 450
layout(location=0) in vec2 uv;
layout(location=1) flat in vec3 edgeColour;
layout(location=0) out vec4 result;
)glsl" TRIM_PARAMS TRIM_ENCODING R"glsl(
void main() {
 vec3 c=edgeColour;
 if(params.conv==1) {
  if(params.hdrWhite>0.0 && params.hdrSdrCurve!=2)
   c=pow(max(c,vec3(0.0)),vec3(params.hdrSdrCurve==1?2.4:2.2));
  else c=toLinear(clamp(c,0.0,1.0));
 }
 else if(params.conv==2)c=toSrgb(clamp(c,0.0,1.0));
 else if(params.hdrWhite>0.0 && params.hdrSdrCurve!=2) {
  c=pow(max(toSrgb(max(c,vec3(0.0))),vec3(0.0)),vec3(params.hdrSdrCurve==1?2.4:2.2));
 }
 if(params.hdrWhite>0.0) {
  c=max(c,vec3(0.0))*exp2(params.hdrExposure);
  float m=max(c.r,max(c.g,c.b));
  if(m>0.0) {
   float t=clamp((m-0.5)*2.0,0.0,1.0);
   float n=m*params.hdrWhite+(params.hdrPeak-params.hdrWhite)*params.hdrHighlights*t*t*(3.0-2.0*t);
   float knee=min(params.hdrWhite,params.hdrPeak*0.75);
   if(n>knee)n=knee+(params.hdrPeak-knee)*(1.0-exp(-(n-knee)/(params.hdrPeak-knee)));
   c*=n/(m*80.0);
  }
 }
 if(params.pq!=0) {
  const mat3 bt2020=mat3(0.6274040,0.0690970,0.0163916,0.3292820,0.9195400,0.0880132,0.0433136,0.0113612,0.8955950);
  vec3 y=pow(clamp(bt2020*c*(80.0/10000.0),0.0,1.0),vec3(0.1593017578125));
  c=pow((0.8359375+18.8515625*y)/(1.0+18.6875*y),vec3(78.84375));
 }
 result=vec4(c,params.alpha);
}
)glsl";
#undef TRIM_ENCODING
#undef TRIM_PARAMS
int env_int(const char* name) {
    const char* e = std::getenv(name);
    return e && *e ? std::atoi(e) : 0;
}
}  // namespace

uint32_t mask() {
    static const uint32_t m = [] {
        // on by default (all bits); WWHD_VK_PRESENT_TRIM=0 runs the old composition code, another value picks bits
        const char* e = std::getenv("WWHD_VK_PRESENT_TRIM");
        const uint32_t all = kShader | kBars | kClear | kHidden;
        const uint32_t v = e && *e ? uint32_t(std::max(0, env_int("WWHD_VK_PRESENT_TRIM"))) & all : all;
        if (v == 0) LOG("[present trim] off (WWHD_VK_PRESENT_TRIM=0)");
        else if (v != all) LOG("[present trim] on: mask %u (plain shader %s, bars per vertex %s, no clear when covered %s, hidden quads "
                   "skipped %s)", v, v & kShader ? "on" : "off", v & kBars ? "on" : "off", v & kClear ? "on" : "off",
                   v & kHidden ? "on" : "off");
        return v;
    }();
    return m;
}
const char* plain_fragment_source() { return kPlain; }
const char* bars_vertex_source() { return kBarsVertex; }
const char* bars_fragment_source() { return kBarsFragment; }

bool plain(const PresentImageParams& p, int backdrop) {
    return !backdrop && !p.aa && p.sharp <= 1.0f && p.foot <= 1.25f && !(p.hdrWhite > 0.0f) && !p.pq;
}

Rect pixels(float x, float y, float w, float h, const Rect& scissor) {
    // conservative: a pixel counts when its centre is at least 1/1000 pixel inside the box (no tie-rule cases)
    constexpr float e = 1e-3f;
    if (!(w > 0) || !(h > 0) || !std::isfinite(x + y + w + h)) return {0, 0, 0, 0};
    Rect r{int(std::floor(x - 0.5f + e)) + 1, int(std::floor(y - 0.5f + e)) + 1, int(std::ceil(x + w - 0.5f - e)),
           int(std::ceil(y + h - 0.5f - e))};
    r.x0 = std::max(r.x0, scissor.x0), r.y0 = std::max(r.y0, scissor.y0);
    r.x1 = std::min(r.x1, scissor.x1), r.y1 = std::min(r.y1, scissor.y1);
    if (r.x1 <= r.x0 || r.y1 <= r.y0) return {0, 0, 0, 0};
    return r;
}

Rect outer_pixels(float x, float y, float w, float h, const Rect& scissor) {
    if (!(w > 0) || !(h > 0) || !std::isfinite(x + y + w + h)) return {0, 0, 0, 0};
    Rect r{int(std::floor(x)) - 1, int(std::floor(y)) - 1, int(std::ceil(x + w)) + 1, int(std::ceil(y + h)) + 1};
    r.x0 = std::max(r.x0, scissor.x0), r.y0 = std::max(r.y0, scissor.y0);
    r.x1 = std::min(r.x1, scissor.x1), r.y1 = std::min(r.y1, scissor.y1);
    if (r.x1 <= r.x0 || r.y1 <= r.y0) return {0, 0, 0, 0};
    return r;
}

bool covers(const std::vector<Rect>& opaque, uint32_t width, uint32_t height) {
    if (!width || !height) return false;
    return covers(opaque, Rect{0, 0, int(width), int(height)});
}
bool covers(const std::vector<Rect>& opaque, const Rect& region) {
    if (region.x1 <= region.x0 || region.y1 <= region.y0) return true;  // nothing to cover
    if (opaque.empty()) return false;
    std::vector<int> xs{region.x0, region.x1}, ys{region.y0, region.y1};
    for (const auto& r : opaque) {
        if (r.x1 <= r.x0 || r.y1 <= r.y0) continue;
        xs.push_back(std::clamp(r.x0, region.x0, region.x1)), xs.push_back(std::clamp(r.x1, region.x0, region.x1));
        ys.push_back(std::clamp(r.y0, region.y0, region.y1)), ys.push_back(std::clamp(r.y1, region.y0, region.y1));
    }
    std::sort(xs.begin(), xs.end()), xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    std::sort(ys.begin(), ys.end()), ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    for (size_t j = 0; j + 1 < ys.size(); ++j)
        for (size_t i = 0; i + 1 < xs.size(); ++i) {
            const int x = xs[i], y = ys[j];  // the cell [x, xs[i+1]) x [y, ys[j+1]) lies inside or outside each rect
            bool in = false;
            for (const auto& r : opaque)
                if (x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1) { in = true; break; }
            if (!in) return false;
        }
    return true;
}

int log_every() {
    static const int n = std::max(0, env_int("WWHD_VK_PRESENT_LOG"));
    return n;
}
bool no_overlay() {
    static const bool v = env_int("WWHD_VK_PRESENT_NO_OVERLAY") != 0;
    return v;
}

static void log_overlay(const char* name, ImDrawData* d, VkExtent2D extent) {
    if (!d) { LOG("[present trim]   %s: none", name); return; }
    int cmds = 0, callbacks = 0, tris = 0;
    double area = 0, scissorArea = 0;
    const float sx = d->DisplaySize.x > 0 ? extent.width / d->DisplaySize.x : 1;
    const float sy = d->DisplaySize.y > 0 ? extent.height / d->DisplaySize.y : 1;
    for (const ImDrawList* l : d->CmdLists)
        for (const ImDrawCmd& c : l->CmdBuffer) {
            if (c.UserCallback) { ++callbacks; continue; }
            ++cmds;
            tris += int(c.ElemCount / 3);
            scissorArea += double(std::max(0.f, c.ClipRect.z - c.ClipRect.x) * sx) * (std::max(0.f, c.ClipRect.w - c.ClipRect.y) * sy);
            for (unsigned k = 0; k + 2 < c.ElemCount; k += 3) {
                const ImDrawVert* v = l->VtxBuffer.Data + c.VtxOffset;
                const ImDrawIdx* ix = l->IdxBuffer.Data + c.IdxOffset + k;
                const ImVec2 a = v[ix[0]].pos, b = v[ix[1]].pos, e = v[ix[2]].pos;
                area += std::fabs((b.x - a.x) * (e.y - a.y) - (e.x - a.x) * (b.y - a.y)) * 0.5 * sx * sy;
            }
        }
    LOG("[present trim]   %s: %d lists, %d draws, %d callbacks, %d vertices, %d triangles, triangle area %.0f px (%.2f of "
        "the target), scissor area %.0f px",
        name, int(d->CmdLists.Size), cmds, callbacks, d->TotalVtxCount, tris, area,
        area / (double(extent.width) * extent.height), scissorArea);
}

void log_composition(const std::vector<ComposeQuad>& quads, VkExtent2D extent, ImDrawData* overlay, ImDrawData* gamepadOverlay,
                     bool cleared, const std::vector<char>& hidden) {
    LOG("[present trim] composition %ux%u: %zu quads, %s", extent.width, extent.height, quads.size(),
        cleared ? "cleared" : "not cleared (covered)");
    for (size_t i = 0; i < quads.size(); ++i) {
        const auto& q = quads[i];
        if (q.solid)
            LOG("[present trim]   quad %zu: solid box %.1f,%.1f %.1fx%.1f colour %.2f %.2f %.2f %.2f", i, q.box.x, q.box.y,
                q.box.w, q.box.h, q.color[0], q.color[1], q.color[2], q.color[3]);
        else
            LOG("[present trim]   quad %zu: image %ux%u fmt %d%s box %.1f,%.1f %.1fx%.1f alpha %.3f backdrop %d%s", i,
                q.image ? q.image->extent.width : 0, q.image ? q.image->extent.height : 0,
                q.image ? int(q.image->fmt.pixel) : 0, q.sourceLinear ? " linear" : "", q.box.x, q.box.y, q.box.w, q.box.h,
                q.alpha, q.backdrop, q.clipped ? " clipped" : "");
        if (i < hidden.size() && hidden[i]) LOG("[present trim]   quad %zu hidden by later opaque quads: skipped", i);
    }
    log_overlay("gamepad overlay", gamepadOverlay, extent);
    log_overlay("overlay", overlay, extent);
}
}  // namespace gfxvk::present_trim
