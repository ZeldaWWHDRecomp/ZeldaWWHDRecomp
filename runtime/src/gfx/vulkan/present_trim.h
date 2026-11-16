#pragma once
// Cheaper window composition (gpusave A2). WWHD_VK_PRESENT_TRIM=<mask> (on by default = 15, all bits;
// 0 = off), pixel-identical:
//
//   1 plain picture shader: a picture quad without FXAA, sharp filtering, the smooth filter's 4-tap footprint, a
//     backdrop or HDR (the phone's default: smooth filter, FXAA off) is drawn by a shader that samples once and only
//     converts the encoding; the general shader computes the same value through uniform branches it skips
//   2 edge-colour bars: the GamePad bars' edge colour (4 taps, the same for every pixel of a bar) is sampled per
//     vertex instead of per pixel (the sum c += tap * 0.25 is exact in either stage)
//   4 no clear when covered: the swap image starts DONT_CARE instead of CLEAR when opaque quads cover every pixel
//   8 hidden quads skipped: a quad whose pixels later opaque quads all overwrite is not drawn (the phone's GamePad-only
//     layout draws the PiP frame, 2155x1222 blended, under the bars and the picture that cover all of it)
//
// Diagnostics (not pixel-identical, off by default): WWHD_VK_PRESENT_LOG=N logs every N-th composition's quads and
// overlay draw data; WWHD_VK_PRESENT_NO_OVERLAY=1 leaves the window overlay out (its GPU cost by difference).
// WWHD_VK_PRESENT_TRIM=0: present.cpp runs its old code (each hook is behind present_trim::mask()).
#include "present.h"
#include <cstdint>
#include <vector>

struct ImDrawData;
namespace gfxvk::present_trim {
enum : uint32_t { kShader = 1, kBars = 2, kClear = 4, kHidden = 8 };
uint32_t mask();
inline bool shader() { return mask() & kShader; }
inline bool bars() { return mask() & kBars; }
inline bool clear() { return mask() & kClear; }
inline bool hidden() { return mask() & kHidden; }
// the plain picture shader's fragment source, and the bars' vertex + fragment sources
const char* plain_fragment_source();
const char* bars_vertex_source();
const char* bars_fragment_source();
// the picture quad's parameters need nothing but one tap and the encoding conversion
bool plain(const PresentImageParams& params, int backdrop);
// opaque quads (pixel rectangles after their scissor) cover the whole extent
struct Rect { int x0, y0, x1, y1; };
// the pixels a viewport box rasterises (pixel centres inside it), clipped to the scissor
Rect pixels(float x, float y, float w, float h, const Rect& scissor);
bool covers(const std::vector<Rect>& opaque, uint32_t width, uint32_t height);
// every pixel of region lies in one of the opaque rects
bool covers(const std::vector<Rect>& opaque, const Rect& region);
// all pixels a viewport box can touch (outer bound), clipped to the scissor
Rect outer_pixels(float x, float y, float w, float h, const Rect& scissor);
// diagnostics
int log_every();
bool no_overlay();
void log_composition(const std::vector<ComposeQuad>& quads, VkExtent2D extent, ImDrawData* overlay, ImDrawData* gamepadOverlay,
                     bool cleared, const std::vector<char>& hidden);
}
