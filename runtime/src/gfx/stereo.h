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
inline void set_mode(int v) { requestedMode = v >= Off && v < Count ? v : Off; }
inline void set_depth(float v) { requestedDepth = std::isfinite(v) ? std::clamp(v, 0.0f, 5.0f) : 1.0f; }
inline void set_convergence(float v) { requestedConvergence = std::isfinite(v) ? std::clamp(v, 10.0f, 10000.0f) : 600.0f; }
struct Frame {
    int mode = Off;
    float depth = 1, convergence = 600;
    bool swap = false;
};
inline Frame frame; // renderer-thread only; settings take effect at a frame boundary
inline uint32_t layoutProjectionStart = 0;
inline std::array<uint32_t,16> layoutProjection{};
inline void tag_layout(uint32_t start, const uint32_t* words) {
    layoutProjectionStart=start;
    std::memcpy(layoutProjection.data(),words,64);
    float p[16];std::memcpy(p,words,64);
    add_layout_signature(p);
}
inline bool is_layout(const uint32_t* regs) {
    return layoutProjectionStart && !std::memcmp(regs+layoutProjectionStart,layoutProjection.data(),64);
}
inline void latch() { frame = {requestedMode.load(), requestedDepth.load(), requestedConvergence.load(), requestedSwap.load()}; }
struct Rect { float x, y, w, h; };
inline Rect pack(Rect r, float width, float height, int mode, int eye) {
    if (mode == TAB) return {r.x, (r.y + eye * height) * .5f, r.w, r.h * .5f};
    return {(r.x + eye * width) * .5f, r.y, r.w * .5f, r.h};
}
// Parallel cameras with an asymmetric frustum. clipW is camera-space distance;
// no depth buffer is read and newly exposed geometry is rasterized for each eye.
inline float clip_shift(float clipW, float eyeOffset, float convergence) {
    return eyeOffset * (clipW / convergence - 1.0f);
}
}
