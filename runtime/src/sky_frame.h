// Per-frame values of the sky passes (horizon-haze grade, toon clouds), shared by Metal and Vulkan:
// camera basis, projection, light direction and daylight, warmth of a low sun, sky colour, haze
// reduction. Header-only so it can be tested without the game (runtime/tools/sky_frame_test.cpp).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sky {

// std140 / MSL layout: 8 vec4 = 128 bytes, the minimum push-constant size
struct SkyFrame {
    float view[12];   // world (camera-relative) -> camera, 3 columns of vec4
    float proj[4];    // f/aspect, f, aspect, -
    float sun[4];     // light direction, daylight 0..1
    float lit[4];     // -, -, -, warmth (low sun) 0..1
    float sky[4];     // palette sky/fog colour, -
    float params[4];  // -, -, haze reduction 0..1, -
};
static_assert(sizeof(SkyFrame) == 128, "push-constant sized");

struct Inputs {
    float eye[3], sunPos[3], fog[3];
    float viewRot[12];  // the game view's camera matrix, 3x4 rows (world -> camera)
    float fovy;         // degrees
    float curTime;      // the game's clock: 0..360 over a day (180 = noon)
    bool sunVisible;
};

// how warm the light is: around dusk (268) and dawn (92) by the clock, or whenever the sun is low
inline float warmth(float curTime, float sunElevation, float daylight) {
    auto bump = [](float t, float c, float w) { return std::max(0.0f, 1.0f - std::fabs(t - c) / w); };
    float clock = std::min(bump(curTime, 268.0f, 28.0f) + bump(curTime, 92.0f, 22.0f), 1.0f);
    float low = std::clamp(1.0f - (sunElevation - 0.05f) * 3.0f, 0.0f, 1.0f) * daylight;
    return std::max(clock, low);
}

inline void frame(const Inputs& in, float aspect, float hazeReduction, SkyFrame& g) {
    g = {};
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) g.view[c * 4 + r] = in.viewRot[r * 4 + c];
    float f = 1.0f / std::tan(in.fovy * 0.5f * 3.14159265f / 180.0f);
    g.proj[0] = f / aspect, g.proj[1] = f, g.proj[2] = aspect;
    float L[3] = {0, 1, 0};
    if (in.sunVisible) {
        float d[3] = {in.sunPos[0] - in.eye[0], in.sunPos[1] - in.eye[1], in.sunPos[2] - in.eye[2]};
        float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 1e-3f) for (int i = 0; i < 3; i++) L[i] = d[i] / len;
    }
    float day = std::clamp(L[1] * 5.0f + 0.3f, 0.0f, 1.0f);
    for (int i = 0; i < 3; i++) g.sun[i] = L[i];
    g.sun[3] = day;
    g.lit[3] = warmth(in.curTime, L[1], day);
    for (int i = 0; i < 3; i++) g.sky[i] = in.fog[i];
    g.params[2] = std::clamp(hazeReduction, 0.0f, 1.0f);
}

// the Horizon haze setting from the environment: WWHD_HORIZON_HAZE (1 = original, 0 = removed), else
// the research name WWHD_SKY_HAZE (the amount removed); null/garbage: the original
inline float haze_from_env(const char* haze, const char* removed) {
    float v = 1.0f;
    if (haze) v = std::strtof(haze, nullptr);
    else if (removed) v = 1.0f - std::strtof(removed, nullptr);
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 1.0f;
}

}  // namespace sky
