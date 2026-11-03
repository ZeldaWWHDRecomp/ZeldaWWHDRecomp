// Button art drawn by the port itself. Shoulder lettering is sampled at run time from the game's
// HUD font (button_glyph_font.cpp); the frame and the PlayStation shapes below are plain geometry
// (a rounded box, a disc, two strokes, a ring, a square, a triangle): no image files, fonts or
// third-party artwork are shipped or copied.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace button_glyphs {

// Original button frame only. Lettering is sampled at run time from the game's HUD font.
constexpr uint32_t shoulder_background(int x, int y) {
    if (x < 2 || x > 61 || y < 10 || y > 53) return 0;
    const int cx = x < 8 ? 8 : x > 55 ? 55 : x;
    const int cy = y < 16 ? 16 : y > 47 ? 47 : y;
    const int distance = (x-cx)*(x-cx) + (y-cy)*(y-cy);
    if (distance > 36) return 0;
    const uint32_t shade = distance > 16 || x < 4 || x > 59 || y < 12 || y > 51 ? 112 : 218 + (53-y)/3;
    return (shade << 24) | (shade << 16) | (shade << 8) | 255;
}

// PlayStation-style face shapes for the Wii U A, B, X, Y (by label: Cross is A, Circle B,
// Square X, Triangle Y). Colours follow the common PlayStation convention. Without colours the
// dialog icon's shape is dark grey on its disc, and the HUD's bare shape is white coverage: the
// game draws it with its own letter colours (button_glyphs.cpp, coverage_sheet).
constexpr uint32_t kShapeColours[4] = {0x3C7DD9FF, 0xD9413CFF, 0xD05FB0FF, 0x2AA67DFF};
constexpr uint32_t kShapeInk = 0x3A3A3AFF;
// GameCube shoulder plates: purple Z, light grey L/R.
constexpr uint32_t kGameCubeZ = 0x6754C8FF, kGameCubeShoulder = 0xE5E5E5FF;

namespace art {
inline float segment(float px, float py, float ax, float ay, float bx, float by) {
    const float dx = bx-ax, dy = by-ay;
    const float t = std::clamp(((px-ax)*dx + (py-ay)*dy) / (dx*dx + dy*dy), 0.0f, 1.0f);
    return std::hypot(px - (ax + t*dx), py - (ay + t*dy));
}
// Distance from (x, y) (relative to the shape's centre, y down) to the outline of shape `face`
// with half-size s.
inline float shape_distance(int face, float x, float y, float s) {
    switch (face) {
    case 0: return std::min(segment(x, y, -s, -s, s, s), segment(x, y, -s, s, s, -s));  // cross
    case 1: return std::fabs(std::hypot(x, y) - s);                                   // circle
    case 2: return std::fabs(std::max(std::fabs(x), std::fabs(y)) - s*0.9f);          // square
    default: {                                                                        // triangle
        const float top = -s*1.0f, base = s*0.68f, half = s*1.02f;
        return std::min({segment(x, y, 0, top, -half, base), segment(x, y, -half, base, half, base),
                         segment(x, y, half, base, 0, top)});
    }
    }
}
}  // namespace art

// One 64x64 cell, RGBA (0xRRGGBBAA), top-down. `disc`: the dialog icon, a light disc with a rim
// sized like the game's icon font glyphs (centre 32,29, radius 21); otherwise the bare shape for
// the HUD letter panes, which sit on the game's own button pictures.
constexpr float kDiscStroke = 4.2f, kBareStroke = 8.0f;  // stroke widths in cell pixels
inline uint32_t face_shape(int face, bool colour, bool disc, int x, int y) {
    const float cx = 32, cy = disc ? 29 : 32, s = disc ? 10.5f : 21.0f, width = disc ? kDiscStroke : kBareStroke;
    const uint32_t ink = colour ? kShapeColours[face & 3] : disc ? kShapeInk : 0xFFFFFFFFu;
    float r = 0, g = 0, b = 0, a = 0;
    constexpr int kSamples = 4;  // 4x4 supersampling for smooth edges
    for (int sy = 0; sy < kSamples; sy++) for (int sx = 0; sx < kSamples; sx++) {
        const float px = x + (sx + 0.5f)/kSamples - cx, py = y + (sy + 0.5f)/kSamples - cy;
        uint32_t c = 0;
        if (art::shape_distance(face, px, py, s) <= width*0.5f) c = ink;
        else if (disc) {
            const float d = std::hypot(px, py);
            if (d <= 19.2f) c = 0xF2F2F2FF;
            else if (d <= 21.0f) c = 0xA8A8A8FF;
        }
        if (!c) continue;
        r += (c >> 24) & 255; g += (c >> 16) & 255; b += (c >> 8) & 255; a += 1;
    }
    if (a == 0) return 0;
    const auto channel = [&](float v) { return (uint32_t)std::lround(v / a); };  // straight colour
    const uint32_t alpha = (uint32_t)std::lround(255.0f * a / (kSamples*kSamples));
    return channel(r) << 24 | channel(g) << 16 | channel(b) << 8 | alpha;
}

// Original controller inspired button silhouettes. These are fresh geometry: a large A,
// smaller B and the two indented, kidney-like item buttons. No controller artwork is used.
inline uint32_t gamecube_icon(int face, bool framed, int x, int y, bool letter = true) {
    float r = 0, g = 0, b = 0, a = 0;
    for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) {
        const float px = x + (sx + .5f) / 4 - 32.f;
        const float py = y + (sy + .5f) / 4 - 32.f;
        bool inside, outer;
        if (face == 2) {
            // X is a vertical bean. Its long, shallow inner arc faces left toward A.
            auto bean = [](float x, float y, float margin) {
                const float rx = 18.f + margin, ry = 27.f + margin;
                const float q = y / ry;
                if (std::fabs(q) >= 1.f) return false;
                const float half = rx * std::sqrt(1.f - q*q);
                const float q4 = y / 14.f, profile = std::exp(-q4*q4*q4*q4);
                const float left = 3.f - half + 8.f * profile;
                return x > left && x < 3.f + half;
            };
            inside = bean(px, py, 0.f);
            const float margin = framed ? 3.f : 0.f;
            outer = bean(px, py, margin);
        } else if (face == 3) {
            // Y is a broad horizontal bean above A. Its lower edge bends gently toward A,
            // then closes into rounded lobes at both ends.
            auto bean = [](float x, float y, float margin) {
                const float rx = 27.f + margin, ry = 18.f + margin;
                const float q = x / rx;
                if (std::fabs(q) >= 1.f) return false;
                const float half = ry * std::sqrt(1.f - q*q);
                const float q4 = x / 13.f, profile = std::exp(-q4*q4*q4*q4);
                const float top = -1.f - half;
                const float bottom = -1.f + half - 8.f * profile;
                return y > top && y < bottom;
            };
            inside = bean(px, py, 0.f);
            const float margin = framed ? 3.f : 0.f;
            outer = bean(px, py, margin);
        } else {
            const float scale = face == 0 ? 27.f : 20.f;
            inside = std::hypot(px, py) < scale;
            outer = std::hypot(px, py) < (framed ? scale + 3.f : scale);
        }
        uint32_t c = 0;
        if (outer && !inside) c = framed ? 0x34383CFF : 0;
        else if (inside) c = face == 0 ? 0x58BE36FF : face == 1 ? 0xE34B49FF : 0xA9ADB1FF;
        // Letterforms from line segments and a compact elliptical bowl for B.
        auto line = [](float px, float py, float ax, float ay, float bx, float by) { return art::segment(px, py, ax, ay, bx, by) < 2.0f; };
        const float lpx = px - (face == 2 ? 8.f : 0.f);
        const float lpy = py + (face == 3 ? 4.f : 0.f);
        bool ink=false;
        if (face == 0) ink=line(px,py, -8,9,0,-10)||line(px,py,0,-10,8,9)||line(px,py,-5,2,5,2);
        else if (face == 1) ink=line(px,py,-5,-10,-5,10)||line(px,py,-5,-10,3,-10)||line(px,py,3,-10,7,-6)||line(px,py,7,-6,7,-2)||line(px,py,7,-2,2,0)||line(px,py,2,0,7,3)||line(px,py,7,3,7,7)||line(px,py,7,7,3,10)||line(px,py,3,10,-5,10);
        else if (face == 2) ink=line(lpx,lpy,-5,-8,5,8)||line(lpx,lpy,5,-8,-5,8);
        else ink=line(lpx,lpy,-6,-8,0,0)||line(lpx,lpy,6,-8,0,0)||line(lpx,lpy,0,0,0,8);
        if (letter && inside && ink) c=face >= 2 ? 0x24282CFF : 0xF4F4F4FF;
        if (!c) continue;
        r += (c>>24)&255; g += (c>>16)&255; b += (c>>8)&255; a += 1;
    }
    if (!a) return 0;
    const auto ch=[&](float v){return (uint32_t)std::lround(v/a);};
    return ch(r)<<24|ch(g)<<16|ch(b)<<8|(uint32_t)std::lround(255*a/16);
}

inline uint32_t gamecube_shoulder_icon(int shoulder, bool framed, int x, int y) {
    const float px=x+0.5f-32.f, py=y+0.5f-32.f, d=std::hypot(px,py);
    const auto rounded=[](float x,float y,float rx,float ry,float radius) {
        return std::hypot(std::max(std::fabs(x)-(rx-radius),0.f),
                          std::max(std::fabs(y)-(ry-radius),0.f))<radius;
    };
    const bool outer=framed ? rounded(px,py,29.f,shoulder==1?16.f:20.f,8.f) : d<22.f;
    const bool inner=framed ? rounded(px,py,26.f,shoulder==1?13.f:17.f,6.f) : d<20.f;
    uint32_t c=0;
    if (framed && outer && !inner) c=0x676B70FF;
    else if (inner) c=shoulder==1?kGameCubeZ:framed?kGameCubeShoulder:0xFFFFFFFF;
    const float width=2.5f;
    auto line=[](float px,float py,float ax,float ay,float bx,float by){return art::segment(px,py,ax,ay,bx,by)<2.5f;};
    const int code[4]={0,1,0,2}; // L, Z, L, R for the Wii U's L/R/ZL/ZR prompt panes.
    const int glyph=code[shoulder&3];
    bool ink=false;
    if(glyph==0) ink=line(px,py,-6,-10,-6,10)||line(px,py,-6,10,7,10);
    else if(glyph==1) ink=line(px,py,-7,-9,7,-9)||line(px,py,7,-9,-7,9)||line(px,py,-7,9,7,9);
    else ink=line(px,py,-6,-10,3,-10)||line(px,py,-6,-10,-6,10)||line(px,py,3,-10,6,-7)||line(px,py,6,-7,6,-3)||line(px,py,6,-3,3,0)||line(px,py,3,0,-6,0)||line(px,py,0,0,6,10);
    if (inner && ink) c=shoulder==1?0xFFFFFFFF:0x292D31FF;
    return c;
}

inline uint32_t gamecube_cstick_icon(bool framed, int x, int y) {
    const float px=x+0.5f-32.f, py=y+0.5f-32.f, d=std::hypot(px,py);
    const bool outer=d<(framed?25.f:22.f), inner=d<20.f;
    uint32_t c=framed&&outer&&!inner?0x5C6064FF:0;
    auto line=[](float px,float py,float ax,float ay,float bx,float by){return art::segment(px,py,ax,ay,bx,by)<2.7f;};
    const bool ink=line(px,py,0,-12,0,12)||line(px,py,-12,0,12,0)||line(px,py,0,-12,-4,-7)||
        line(px,py,0,-12,4,-7)||line(px,py,0,12,-4,7)||line(px,py,0,12,4,7)||
        line(px,py,-12,0,-7,-4)||line(px,py,-12,0,-7,4)||line(px,py,12,0,7,-4)||line(px,py,12,0,7,4);
    if(inner&&ink)c=0xE8C72AFF;
    return c;
}

inline uint32_t gamecube_start_icon(bool framed, int x, int y) {
    const float px=x+0.5f-32.f, py=y+0.5f-32.f;
    auto rounded=[](float x,float y,float halfw,float halfh,float radius) {
        const float qx=std::fabs(x)-(halfw-radius), qy=std::fabs(y)-(halfh-radius);
        return std::hypot(std::max(qx,0.f),std::max(qy,0.f))+std::min(std::max(qx,qy),0.f)-radius;
    };
    const float outer=rounded(px,py,framed?24.f:21.f,framed?13.f:10.f,framed?7.f:5.f);
    const float inner=rounded(px,py,framed?21.f:18.f,framed?10.f:8.f,framed?5.f:4.f);
    uint32_t c=outer<0&&inner>=0?0x4A4F54FF:inner<0?0x737A80FF:0;
    auto line=[](float px,float py,float ax,float ay,float bx,float by){return art::segment(px,py,ax,ay,bx,by)<1.15f;};
    bool ink=false;
    // Five compact vector letters spell START across the button face.
    for(int i=0;i<5;i++) {
        const float ox=-14.f+i*7.f;
        if(i==0) ink|=line(px,py,ox+2,-4,ox-2,-4)||line(px,py,ox-2,-4,ox-2,0)||line(px,py,ox-2,0,ox+2,0)||line(px,py,ox+2,0,ox+2,4)||line(px,py,ox+2,4,ox-2,4);
        else if(i==2) ink|=line(px,py,ox-3,4,ox, -4)||line(px,py,ox,-4,ox+3,4)||line(px,py,ox-2,1,ox+2,1);
        else if(i==3) ink|=line(px,py,ox-2,4,ox-2,-4)||line(px,py,ox-2,-4,ox+2,-4)||line(px,py,ox+2,-4,ox+2,-1)||line(px,py,ox+2,-1,ox-2,0)||line(px,py,ox,0,ox+2,4);
        else ink|=line(px,py,ox-2,4,ox-2,-4)||line(px,py,ox-2,-4,ox+2,-4)||line(px,py,ox, -4,ox,4);
    }
    if(inner<0&&ink)c=0xF2F2F2FF;
    return c;
}

}  // namespace button_glyphs
