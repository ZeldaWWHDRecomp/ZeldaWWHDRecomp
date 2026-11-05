// 60 fps (generated) (reproj.cpp): the generated camera, without Vulkan (tools/reproj_camera_test.cpp tests it).
// From Nycolazs/WindWakerHD-Android-Plus (vk/reproj.cpp 84b0ff2) via GreenNaugahyde/ZeldaWWHDRecompAndroid
// v0.9.9 (db65d60), MPL-2.0: basis() and the half step of their record().
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>

namespace gfxvk::reproj {

// camera-to-world of a camera at eye looking at center: columns right, up, back (it looks along -z),
// m[row * 3 + column]
inline void basis(const float eye[3], const float center[3], const float upv[3], float m[9]) {
    float f[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
    float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl < 1e-6f) fl = 1;
    for (float& v : f) v /= fl;
    float r[3] = {f[1] * upv[2] - f[2] * upv[1], f[2] * upv[0] - f[0] * upv[2], f[0] * upv[1] - f[1] * upv[0]};
    float rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    if (rl < 1e-6f) rl = 1;
    for (float& v : r) v /= rl;
    float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    for (int i = 0; i < 3; i++) {
        m[i * 3 + 0] = r[i];
        m[i * 3 + 1] = u[i];
        m[i * 3 + 2] = -f[i];
    }
}

enum class Step { Moved, Cut, Still };
// The generated camera, half a step on from cur (prevM/prevEye: the frame before): half the last
// rotation (D = Cprev^T Ccur, H = D^(1/2): the generated camera's axes in the real camera's space,
// half[row * 3 + column]) and half the last movement in the real camera's space (move). A cut (a turn
// of 0.5 rad or more, an eye step of 400 or more) or no movement at all leaves them at identity / 0.
inline Step half_step(const float prevM[9], const float prevEye[3], const float curM[9], const float curEye[3], float half[9],
                      float move[3]) {
    const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(half, id, sizeof id);
    move[0] = move[1] = move[2] = 0;
    float step[3], dist = 0;
    for (int k = 0; k < 3; k++) {
        step[k] = curEye[k] - prevEye[k];
        dist += step[k] * step[k];
    }
    float d[9];
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) {
            float s = 0;
            for (int k = 0; k < 3; k++) s += prevM[k * 3 + a] * curM[k * 3 + b];
            d[a * 3 + b] = s;
        }
    const float tr = d[0] + d[4] + d[8];
    const float ang = std::acos(std::clamp((tr - 1) * 0.5f, -1.0f, 1.0f));
    if (std::sqrt(dist) >= 400.0f || ang >= 0.5f) return Step::Cut;
    if (dist == 0.0f && ang <= 1e-6f) return Step::Still;
    for (int j = 0; j < 3; j++) {
        float v = 0;
        for (int k = 0; k < 3; k++) v += curM[k * 3 + j] * step[k];
        move[j] = 0.5f * v;
    }
    if (ang > 1e-5f) {
        float ax[3] = {d[7] - d[5], d[2] - d[6], d[3] - d[1]};
        const float l = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
        if (l > 1e-8f) {
            for (float& v : ax) v /= l;
            const float a = ang * 0.5f, co = std::cos(a), si = std::sin(a), t = 1 - co;
            const float x = ax[0], y = ax[1], z = ax[2];
            const float rm[9] = {t * x * x + co,     t * x * y - si * z, t * x * z + si * y, t * x * y + si * z, t * y * y + co,
                                 t * y * z - si * x, t * x * z - si * y, t * y * z + si * x, t * z * z + co};
            std::memcpy(half, rm, sizeof rm);
        }
    }
    return Step::Moved;
}

// The objects' movement search (kFlow / kSelect): a point in the current camera's space, in the
// previous camera's = D p + Cprev^T step (D = Cprev^T Ccur, full[row * 3 + column]; fullMove: Cprev^T
// step). From the fork's record() (the flow pass's camera).
inline void full_step(const float prevM[9], const float prevEye[3], const float curM[9], const float curEye[3], float full[9],
                      float fullMove[3]) {
    float step[3];
    for (int k = 0; k < 3; k++) step[k] = curEye[k] - prevEye[k];
    for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) {
            float s = 0;
            for (int k = 0; k < 3; k++) s += prevM[k * 3 + a] * curM[k * 3 + b];
            full[a * 3 + b] = s;
        }
    for (int j = 0; j < 3; j++) {
        float v = 0;
        for (int k = 0; k < 3; k++) v += prevM[k * 3 + j] * step[k];
        fullMove[j] = v;
    }
}

}  // namespace gfxvk::reproj
