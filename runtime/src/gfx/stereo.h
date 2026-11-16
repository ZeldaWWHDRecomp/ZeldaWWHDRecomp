#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <array>
#include <cstdint>
#include <cstring>
#include "stereo_projection.h"

// Output packing is independent of the aspect ratio of one eye.
namespace stereo {
enum Mode { Off, HalfSBS, FullSBS, TAB, Count };
inline constexpr float eyeDistanceAtUnitDepth = 3.2f;
inline std::atomic<int> requestedMode{Off};
inline std::atomic<float> requestedDepth{1.0f}, requestedConvergence{600.0f};
inline std::atomic<bool> requestedSwap{false};
// how far in front of the viewer the 2D layouts (HUD, prompts, menus), the GamePad picture-in-picture and
// the settings overlay appear: the distance (camera units) of a scene point with the same disparity. Automatic
// (requestedHudAuto): in front of the nearest scene depth the renderer measured (hud::follow), at most this far.
inline std::atomic<float> requestedHudDistance{150.0f};
inline std::atomic<bool> requestedHudAuto{true};
// "behind the glass" (a display with a frame, e.g. a window in the room): the HUD distance is the
// screen plane (zero disparity), so the scene lies behind the frame, not in front of its edges; automatic,
// the plane is at the nearest scene depth
inline std::atomic<bool> requestedBehindGlass{false};
// the scene camera: the projection of the last draw into the main (screen-sized) depth buffer, recorded by the
// renderers (note_main_camera after a draw whose binding gave a camera projection an eye). Its horizontal scale
// sets the 2D layouts' disparity (layout_shift), its depth rows read the depth samples (hud::distance).
inline float mainCamera[3] = {0.974f, 0, 0};  // (60 degrees vertical at 16:9)
// after a draw's vertex stage was bound (cameraSeen cleared before it); mainDepth: eye 0's draw into the main depth
inline void note_main_camera(bool mainDepth) {
    if (cameraSeen && mainDepth) std::memcpy(mainCamera, lastCamera, sizeof mainCamera);
}
// the main depth buffer's own linearization, from the sun-shadow pass that reconstructs positions from it (its
// pixel stage's remapped[0].xy: near, 1 - near/far). Draws into that buffer can use other projections (other
// near planes); this is the one its depth values mean. 0: not seen (the scene camera's rows instead).
inline float mainDepthLinear[2] = {0, 0};
inline void note_shadow_pass(const unsigned char* remapped, size_t size) {
    if (size >= 8) std::memcpy(mainDepthLinear, remapped, 8);
}
namespace hud {
// the renderers' depth samples: a grid over the main depth buffer, read back a frame or two later
inline constexpr uint32_t kGridX = 32, kGridY = 18, kSamples = kGridX * kGridY;
inline void grid_point(uint32_t i, uint32_t width, uint32_t height, uint32_t& x, uint32_t& y) {
    x = std::min(width - 1, uint32_t((i % kGridX + 0.5f) * width / kGridX));
    y = std::min(height - 1, uint32_t((i / kGridX + 0.5f) * height / kGridY));
}
// camera distance of a depth buffer value (0..1, near to far) with near and 1 - near/far; 0: none (the far
// plane, sky, or nearer than kNearest: the near plane's clipped fragments)
inline constexpr float kNearest = 10.0f;
inline float distance(float depth, float nearPlane, float k) {
    const float d = nearPlane / (1.0f - depth * k);
    return depth < 1.0f && std::isfinite(d) && d >= kNearest ? d : 0.0f;
}
// the main depth buffer's near and k (mainDepthLinear, else the scene camera's rows); false: none yet
inline bool depth_linearization(float& nearPlane, float& k) {
    if (mainDepthLinear[0] > 0) { nearPlane = mainDepthLinear[0]; k = mainDepthLinear[1]; return true; }
    if (mainCamera[1] == 0) return false;
    nearPlane = mainCamera[2] / mainCamera[1];
    k = -1.0f / mainCamera[1];
    return true;
}
inline std::atomic<float> nearest{-1};     // the last readback's nearest distance (0: nothing near, -1: none yet)
inline std::atomic<float> followed{150};   // the automatic HUD distance (render thread writes)
// a readback (any thread): the nearest of the samples
inline void observe(const float* depths, uint32_t n, float nearPlane, float k) {
    float best = 0;
    for (uint32_t i = 0; i < n; ++i)
        if (const float d = distance(depths[i], nearPlane, k); d > 0 && (best == 0 || d < best)) best = d;
    nearest = best;
}
// once a frame (latch): a margin in front of the nearest depth (the plane at it, behind the glass), at most
// farthest and not nearer than kNearestHud (a camera pressed against a wall would put the HUD at an
// uncomfortable disparity); nearer values are taken within a few frames, farther ones over about 20 (no pumping)
inline constexpr float kNearestHud = 50.0f;
inline float follow(float farthest, bool glass) {
    const float measured = nearest.load();
    const float target = measured > 0    ? std::clamp(measured * (glass ? 1.0f : 0.8f), kNearestHud, glass ? 10000.0f : std::max(farthest, kNearestHud))
                         : measured == 0 ? (glass ? 10000.0f : farthest)
                                         : farthest;
    float f = followed.load();
    f += (target - f) * (target < f ? 0.5f : 0.05f);
    followed = f;
    return f;
}
}  // namespace hud
inline float requested_hud_distance() { return requestedHudAuto ? hud::followed.load() : requestedHudDistance.load(); }
inline float requested_convergence() { return requestedBehindGlass ? requested_hud_distance() : requestedConvergence.load(); }
inline void set_mode(int v) { requestedMode = v >= Off && v < Count ? v : Off; }
inline void set_depth(float v) { requestedDepth = std::isfinite(v) ? std::clamp(v, 0.0f, 5.0f) : 1.0f; }
inline void set_convergence(float v) { requestedConvergence = std::isfinite(v) ? std::clamp(v, 10.0f, 10000.0f) : 600.0f; }
inline void set_hud_distance(float v) { requestedHudDistance = std::isfinite(v) ? std::clamp(v, 10.0f, 10000.0f) : 150.0f; }
struct Frame {
    int mode = Off;
    float depth = 1, convergence = 600;
    bool swap = false;
    float hudDistance = 150;
};
inline Frame frame; // renderer-thread only; settings take effect at a frame boundary
// The command stream brackets nw::lyt Pane::DrawSelf. Projection values alone
// cannot distinguish a layout from a 3D camera with the same lens parameters.
inline unsigned layoutDrawDepth = 0;
// The game's captured-photo pane samples a single canonical image in both eyes.
inline bool layoutPhotograph = false;
inline void layout_draw(bool begin) {
    if (begin) ++layoutDrawDepth;
    else if (layoutDrawDepth) --layoutDrawDepth;
}
inline uint32_t layoutProjectionStart = 0;
inline std::array<uint32_t,16> layoutProjection{};
inline void tag_layout(uint32_t start, const uint32_t* words) {
    layoutProjectionStart=start;
    std::memcpy(layoutProjection.data(),words,64);
}
inline bool is_layout(const uint32_t* regs) {
    return layoutProjectionStart && !std::memcmp(regs+layoutProjectionStart,layoutProjection.data(),64);
}
inline void latch() {
    if (requestedHudAuto) hud::follow(requestedHudDistance, requestedBehindGlass);
    frame = {requestedMode.load(), requestedDepth.load(), requested_convergence(), requestedSwap.load(), requested_hud_distance()};
}
// the camera shift of an eye's picture (0 left, 1 right): the offset project_eye gets
inline float eye_offset(int eye) { return (eye ? 1.0f : -1.0f) * frame.depth * eyeDistanceAtUnitDepth; }
// a 2D layout's horizontal shift in normalized device coordinates for the eye with camera shift eyeOffset:
// that of a scene point at hudDistance (project_eye's convention, with the scene camera's scale)
inline float layout_shift(float eyeOffset) {
    return eyeOffset * mainCamera[0] * (1.0f / frame.convergence - 1.0f / frame.hudDistance);
}
struct Rect { float x, y, w, h; };
inline Rect pack(Rect r, float width, float height, int mode, int eye) {
    if (mode == TAB) return {r.x, (r.y + eye * height) * .5f, r.w, r.h * .5f};
    if (mode == FullSBS) return {r.x + eye * std::floor(width * .5f), r.y, r.w, r.h};  // (r: in the eye's half-width canvas)
    return {(r.x + eye * width) * .5f, r.y, r.w * .5f, r.h};
}
// the width of the canvas one eye's layout is made for: Full SBS gives each eye half the target at its own
// pixels; Half SBS and top-and-bottom squeeze a whole-target layout into their half (pack)
inline float eye_canvas_width(float width) { return frame.mode == FullSBS ? width * .5f : width; }
// the GamePad screen's targets (854x480, shown in its own window, and its halvings)
inline bool gamepad_shaped(uint32_t w, uint32_t h) {
    for (uint32_t gw = 854, gh = 480; gw >= 32; gw >>= 1, gh >>= 1)
        if ((w == gw || w == gw + 1) && h == gh) return true;
    return false;
}
// the surfaces eye 0 alone draws and both eyes sample: WWHD's three 1024-square sun cascades (light space,
// the same for both eyes) and the GamePad screen's pictures (one GamePad)
inline bool sun_atlas(uint32_t width, uint32_t height, uint32_t layers, bool depth) {
    return depth && width == 1024 && height == 1024 && layers == 3;
}
inline bool eye_shared_target(uint32_t width, uint32_t height, uint32_t layers, bool depth) {
    return sun_atlas(width, height, layers, depth) || (layers <= 1 && gamepad_shaped(width, height));
}
// the camera shift a draw of an eye gets: on the screen-shaped targets outside the 2D layouts
inline float camera_eye_offset(int eye, bool screenTarget) {
    return frame.mode != Off && !layoutDrawDepth && screenTarget ? eye_offset(eye) : 0.0f;
}
// a 2D layout draw's viewport shift (pixels of a viewport viewportWidth wide; 0 for other draws)
inline float layout_shift_px(int eye, float viewportWidth, bool screenTarget) {
    return frame.mode != Off && layoutDrawDepth && screenTarget ? layout_shift(eye_offset(eye)) * viewportWidth * .5f : 0.0f;
}
// the HUD-distance shift of the composition's GamePad picture-in-picture and settings overlay in the picture of
// a displayed eye (0 left, 1 right; the swap option applied), for a TV picture tvWidth wide
inline float hud_shift_px(int displayedEye, float tvWidth) {
    if (frame.mode == Off) return 0;
    return layout_shift(eye_offset((displayedEye != 0) != frame.swap)) * tvWidth * .5f;
}
// Parallel cameras with an asymmetric frustum. clipW is camera-space distance;
// no depth buffer is read and newly exposed geometry is rasterized for each eye.
inline float clip_shift(float clipW, float eyeOffset, float convergence) {
    return eyeOffset * (clipW / convergence - 1.0f);
}
}
