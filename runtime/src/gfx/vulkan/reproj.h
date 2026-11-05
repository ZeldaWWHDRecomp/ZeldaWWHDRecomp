// 60 fps (generated): in-between frames made on the GPU by reprojecting the real frame (reproj.cpp).
// The game side and the switch: framegen.h. Everything here is render-thread only, and nothing runs
// while WWHD_FRAMEGEN is unset.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace gfxvk {
struct Surface;
namespace reproj {

bool enabled();  // framegen::requested()

// why no in-between frame was made for a frame (counted; the [framegen] and [vulkan perf] lines)
enum Skip : int {
    kNone = -1,
    kOtherMode,   // frame interpolation, true 60 or fast forward is on
    kStereo,      // stereo 3D
    kPresentPath, // synchronous presentation, a present capture or the present observer
    kNotShown,    // the real frame was not presented (no TV window, GamePad only, no swapchain)
    kFast,        // the real frames come faster than 40 a second (or no interval known yet)
    kNoCamera,    // no 3D camera this frame (menus, loading, the title's 2D parts)
    kNoScene,     // no TV layout ended a scene this frame
    kNoDepth,     // no depth buffer of the scene's size
    kSize,        // the TV picture's size or format differs from the scene's
    kCut,         // a camera cut (turn >= 0.5 rad, eye step >= 400) or no camera of the frame before
    kStill,       // the camera did not move and the objects' movement is off: the in-between frame would be the real one
    kFailed,      // a pipeline or image could not be made
    kSkipCount
};

// OP_FRAMEGEN_CAMERA (framegen::kCameraWords words)
void note_camera(const uint32_t* words, uint32_t n);
// OP_FRAMEGEN_LAYOUT: a layout root about to be drawn into the colour target at colorBase (guest size),
// with the depth buffer at depthBase bound. At the first one on the TV picture this frame the scene is
// complete: it is copied aside, and its depth buffer noted.
void layout(uint32_t colorBase, uint32_t width, uint32_t height, uint32_t depthBase);

// surfaces.cpp clear_depth_stencil, around a depth clear: the main depth buffer cleared again in a frame
// after being drawn into (a second 3D pass) keeps the first pass's depth for the in-between frame
void depth_clearing(Surface* depth);
void depth_cleared(Surface* depth);
// HUD coverage (hud_coverage.h, draw.cpp): the coverage image a draw whose first colour target is
// `color0` also writes this frame (null: none, or not the TV picture); hud_coverage_failed(): a HUD draw
// could not write it (the frame's in-between one then finds the HUD by comparing pictures);
// hud_coverage_wanted(): frame generation and the coverage are on (else draw.cpp does nothing of it)
Surface* hud_coverage_target(Surface* color0);
void hud_coverage_failed();
bool hud_coverage_wanted();

// swap() after the real TV present: `shown` it was presented at `nowNs` (steady clock). Returns half the
// real frames' recent interval (ns), the in-between frame's delay; 0: none can be placed (kFast).
int64_t real_presented(bool shown, int64_t nowNs);
// swap(): the in-between picture of the frame being swapped, reprojected from the published TV scan
// `scan` and recorded into the current command buffer; null when none is made (`skip`, or the reason
// found here, is counted). The returned surface is the one generated_scan() holds.
Surface* generate(Surface* scan, Skip skip);
// the surface generate() made, as a scan: swap() exchanges it with the TV screen's scan around the
// in-between frame's present
std::unique_ptr<Surface>& generated_scan();
// the in-between frame's present call went out (`early`: before its time, forced by the next frame
// or a swapchain change), or its present() returned without presenting
void presented(bool early);
void not_presented();
// the counters since the last call: "generated N (early M), skipped K: reason n, ..."
std::string perf_line();

}  // namespace reproj
}  // namespace gfxvk
