// SPDX-License-Identifier: MPL-2.0
// Mod demos, joint attach (story_demo_attach.cpp): the carried actor's pose from the carrier's joint
// matrix (a J3D anm matrix: 3x4 row-major, world space, may hold scale) times the attachment's offset
// and rotation. Actor angles are s16 in the game's ZXY order (mDoMtx_ZXYrotS builds RY * RX * RZ).
#pragma once
#include "demo_scene.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace demo_attach_math {
using M34 = float[12];
inline void zxy(const demo_scene::Vec& degrees, float r[9]) {  // mDoMtx_ZXYrotS: RY * RX * RZ
    const float k = 3.14159265358979f / 180.f, x = degrees.x * k, y = degrees.y * k, z = degrees.z * k;
    const float cx = std::cos(x), sx = std::sin(x), cy = std::cos(y), sy = std::sin(y), cz = std::cos(z), sz = std::sin(z);
    const float m[9] = {cy * cz + sy * sx * sz, -cy * sz + sy * sx * cz, sy * cx, cx * sz, cx * cz, -sx, -sy * cz + cy * sx * sz, sy * sz + cy * sx * cz, cy * cx};
    std::memcpy(r, m, sizeof m);
}
inline int16_t angle(float radians) { return int16_t(std::lround(radians * 32768.f / 3.14159265358979f)); }
// the actor angles (s16, ZXY order) of a rotation matrix
inline void zxy_angles(const float r[9], int16_t out[3]) {
    const float s = std::fmax(-1.f, std::fmin(1.f, -r[5]));
    const float x = std::asin(s);
    float y, z;
    if (std::fabs(s) < 0.9999f) { y = std::atan2(r[2], r[8]); z = std::atan2(r[3], r[4]); }
    else { y = std::atan2(-r[6], r[0]); z = 0; }
    out[0] = angle(x); out[1] = angle(y); out[2] = angle(z);
}
// world pose of the carried actor: joint matrix J (3x4, may hold scale) times offset/rotation
// upright: only the heading (the yaw of the carried actor's forward axis), as Demo07 gives Link
inline bool pose(const M34 j, const demo_scene::Vec& offset, const demo_scene::Vec& rotation, float position[3], int16_t angles[3], bool upright = false) {
    for (int i = 0; i < 12; ++i) if (!std::isfinite(j[i])) return false;
    float n[9];
    for (int col = 0; col < 3; ++col) {
        const float len = std::sqrt(j[col] * j[col] + j[4 + col] * j[4 + col] + j[8 + col] * j[8 + col]);
        if (!(len > 1e-6f)) return false;
        for (int row = 0; row < 3; ++row) n[row * 3 + col] = j[row * 4 + col] / len;
    }
    for (int row = 0; row < 3; ++row) position[row] = j[row * 4] * offset.x + j[row * 4 + 1] * offset.y + j[row * 4 + 2] * offset.z + j[row * 4 + 3];
    float o[9], r[9];
    zxy(rotation, o);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) r[row * 3 + col] = n[row * 3] * o[col] + n[row * 3 + 1] * o[3 + col] + n[row * 3 + 2] * o[6 + col];
    zxy_angles(r, angles);
    if (upright) { angles[0] = 0; angles[2] = 0; angles[1] = angle(std::atan2(r[2], r[8])); }
    return true;
}

// An attach's blend: the carried actor's place and angles `t` (0..1, eased) of the way from where it
// was when the attach began to its place on the joint; angles turn the short way round.
inline float ease(float t) { t = std::fmax(0.f, std::fmin(1.f, t)); return t * t * (3.f - 2.f * t); }
inline void blend(const float from[3], const int16_t from_angles[3], float t, float position[3], int16_t angles[3]) {
    const float s = ease(t);
    for (int i = 0; i < 3; ++i) {
        position[i] = from[i] + (position[i] - from[i]) * s;
        const int16_t d = int16_t(uint16_t(angles[i]) - uint16_t(from_angles[i]));
        angles[i] = int16_t(uint16_t(from_angles[i]) + uint16_t(int16_t(std::lround(float(d) * s))));
    }
}

}  // namespace demo_attach_math
