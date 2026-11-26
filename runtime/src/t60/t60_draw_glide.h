// The Deku Leaf glide between the frame ends (2026-11-23). Included by t60_draw.cpp only
// (its model()); draw only, no logic.
//
// The Deku Leaf glide (decomp daPy_lk_c::procFanGlide) runs its visual part (the body lean m34F2 / m34F4 on the root
// joint, the waist sway mWaistAngleZ / Y, the leaf's flutter m3600 / m355E on the parachute's arm / root joints) as one
// 30 Hz update on the frame's tick30 step (two integer chases with momentum and three RNG draws a frame);
// on the frame's earlier steps those words hold while Link moves per step. At N 2 the lean / sway / flutter showed the
// frame's whole change on one pass and none on the other (a staircase, like the cap before its frame-end blend). Link's
// models are drawn one step behind his newest step: on an earlier step's pass the frame's next end is kept already.
// There his models are drawn with those joint rotations blended between the two frame ends' words (as frame
// interpolation draws them at N 1): the root lean as a rotation of everything about the drawn root (all of Link's
// body-attached models: the parachute, sword, shield ...; not the ground shadow or the gust), the waist sway on the waist's
// subtree, the flutter on the parachute's joints. Only while the glide runs (the proc on the frame's earlier steps and its
// end) and each word group holds on the earlier steps (else drawn as it is).
//
// The Deku Leaf gust (setCollision: mFanWindTimer counts on the frame's step, the gust model yuchw00 is placed and its bck
// played on the tick30 step, held on the others) is drawn between its frame ends likewise while its matrices hold on the
// frame's earlier steps. Its btk / brk (material) frames stay on the 30 Hz grid as at N 1.
//
// NC: WWHD_T60_GLIDE_FRAMEEND_NEGCTRL=1 (drawn as before).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <utility>
#include <vector>

#include "guest_addr.h"
#include "interp_joints.h"
#include "t60/t60_draw_hist.h"

namespace interp { void blend_world_matrix(const float* a, const float* b, float* out, float t); }

namespace t60::draw::glide {
inline const bool g_nc = [] {
    const char* e = getenv("WWHD_T60_GLIDE_FRAMEEND_NEGCTRL");
    return e && atoi(e) != 0;
}();
// daPy_lk_c words (decomp d_a_player_main.h) and model pointers (PlayerModelState at 0xD90, d_a_player_main_01.h)
constexpr uint32_t kCurProc = 0x65F0, kLeanX = 0x6942, kLeanZ = 0x6944, kWaistZ = 0x6928, kWaistY = 0x692C,
                   kLeafY = 0x69AE, kLeafX = 0x6A58;
constexpr uint32_t kCLModel = 0x448, kFanMorf = 0x44D0, kMorfModel = 0x90, kGust = 0xD90 + 0x4660,
                   kBafo = 0xD90 + 0x47DC, kShadow = 0x438;
constexpr uint32_t kMdlJoints = 0x2C, kJntMtx = 0x10, kJntNum = 0x2C;
constexpr int kGlideProc = 0x93;  // daPyProc_FAN_GLIDE_e
constexpr uint32_t kWaist = 0x1E;  // CL_JNT_WAIST_JNT_e
// the CL joints under the waist (the legs; CL_JNT_LCLOTCH .. RFOOT and their ends)
constexpr uint32_t kWaistFirst = 0x1E, kWaistLast = 0x28;
// the parachute (FANB) joints the flutter turns / moves, and the last joint of each one's subtree
constexpr uint32_t kLeafJ[4] = {1, 3, 7, 9}, kLeafEnd[4] = {6, 6, 12, 12};

struct Words {
    float g[6];   // lean x, lean z, waist z, waist y, leaf y (s16 units), leaf x (m3600)
    int proc;
    float root[12];  // the CL root joint's matrix at the step (the lean rotates about it)
};
struct Hist { uint32_t proc = 0, id = 0; std::deque<std::pair<double, Words>> steps; };
inline Hist g_hist;

inline void mtx_of(const uint32_t* w, float* m) {
    for (int k = 0; k < 12; k++) m[k] = interp::joints::word_f(w[k]);
}
inline void put(uint32_t* w, const float* m) {
    for (int k = 0; k < 12; k++) w[k] = interp::joints::f_word(m[k]);
}
// A * B (3x4 affine)
inline void mul(const float* A, const float* B, float* O) {
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++)
            O[4 * r + c] = A[4 * r] * B[c] + A[4 * r + 1] * B[4 + c] + A[4 * r + 2] * B[8 + c];
        O[4 * r + 3] += A[4 * r + 3];
    }
}
inline bool inv(const float* A, float* O) {
    const float a = A[0], b = A[1], c = A[2], d = A[4], e = A[5], f = A[6], g = A[8], h = A[9], i = A[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-12f) return false;
    const float r = 1.0f / det;
    const float I[9] = {(e * i - f * h) * r, (c * h - b * i) * r, (b * f - c * e) * r,
                        (f * g - d * i) * r, (a * i - c * g) * r, (c * d - a * f) * r,
                        (d * h - e * g) * r, (b * g - a * h) * r, (a * e - b * d) * r};
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) O[4 * row + col] = I[3 * row + col];
        O[4 * row + 3] = -(I[3 * row] * A[3] + I[3 * row + 1] * A[7] + I[3 * row + 2] * A[11]);
    }
    return true;
}
// the rotations of J3D's Euler angles (s16 units): about X, Y, Z (row major 3x3 inside a 3x4 with no translation)
inline void rot(int axis, float s, float* M) {
    const float k = (float)(M_PI / 32768.0), cs = std::cos(s * k), sn = std::sin(s * k);
    for (int i = 0; i < 12; i++) M[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    const int a = (axis + 1) % 3, b = (axis + 2) % 3;  // (X: y z; Y: z x; Z: x y)
    M[4 * a + a] = cs, M[4 * a + b] = -sn, M[4 * b + a] = sn, M[4 * b + b] = cs;
}
// D = A * X * A^-1: the world change of joint A when its local frame turns / moves by X (applied to its subtree)
inline bool delta(const float* A, const float* X, float* D) {
    float Ai[12], T[12];
    if (!inv(A, Ai)) return false;
    mul(A, X, T);
    mul(T, Ai, D);
    return true;
}
inline void apply(const float* D, uint32_t* out, uint32_t j0, uint32_t j1) {
    float J[12], O[12];
    for (uint32_t j = j0; j <= j1; j++) {
        mtx_of(out + 12 * j, J);
        mul(D, J, O);
        put(out + 12 * j, O);
    }
}

// the step's words (called on each of Link's model draws; the newest step's state is in place)
inline void record(uint32_t proc, uint32_t id, double time) {
    if (g_hist.proc != proc || g_hist.id != id) g_hist = Hist{}, g_hist.proc = proc, g_hist.id = id;
    Words w{};
    w.g[0] = (int16_t)ld16(proc + kLeanX), w.g[1] = (int16_t)ld16(proc + kLeanZ);
    w.g[2] = (int16_t)ld16(proc + kWaistZ), w.g[3] = (int16_t)ld16(proc + kWaistY);
    w.g[4] = (int16_t)ld16(proc + kLeafY);
    const uint32_t lx = ld32(proc + kLeafX);
    std::memcpy(&w.g[5], &lx, 4);
    w.proc = (int)ld32(proc + kCurProc);
    const uint32_t cl = ld32(proc + kCLModel), jnt = cl ? ld32(cl + kMdlJoints) : 0, mtx = jnt ? ld32(jnt + kJntMtx) : 0;
    if (!mtx || !ld16(jnt + kJntNum)) return;
    mtx_of((const uint32_t*)ppc_ptr(mtx), w.root);
    hist::stamp(g_hist.steps, time, w);
}
inline const Words* at(double t) {
    for (const auto& s : g_hist.steps)
        if (std::fabs(s.first - t) < hist::kEps) return &s.second;
    return nullptr;
}

// Link's model `model` drawn between his kept steps ta / tb at f (out: as drawn so far)
inline void frameend(uint32_t proc, uint32_t model, double ta, double tb, float f, uint32_t* out, uint32_t n) {
    if (g_nc || g_hist.proc != proc) return;
    const uint32_t morf = ld32(proc + kFanMorf);
    const bool cl = model == ld32(proc + kCLModel), leaf = morf && model == ld32(morf + kMorfModel);
    // (the gust, the crash gust and the ground shadow are not on his body: drawn as they are; other world-placed models
    // are told by their distance from the root below)
    if (!cl && !leaf && (model == ld32(proc + kGust) || model == ld32(proc + kBafo) || model == ld32(proc + kShadow))) return;
    const double td = ta + f * (tb - ta);
    const double F1 = std::floor(td + hist::kEps), F2 = F1 + 1.0, frac = td - F1;
    if (frac < hist::kEps || frac > 1.0 - hist::kEps) return;  // (a frame end: drawn as it is)
    int i1 = -1, i2 = -1;
    const auto& st = g_hist.steps;
    for (int i = 0; i < (int)st.size(); i++) {
        if (std::fabs(st[i].first - F1) < hist::kEps) i1 = i;
        if (std::fabs(st[i].first - F2) < hist::kEps) i2 = i;
    }
    if (i1 < 0 || i2 < i1 + 2) return;  // (a frame end or an earlier step not kept: drawn as it is)
    const Words* wa = at(ta);
    const Words* wb = at(tb);
    if (!wa || !wb) return;
    for (int i = i1 + 1; i <= i2; i++)
        if (st[i].second.proc != kGlideProc) return;  // (the glide only)
    // a group of words: held on the frame's earlier steps, changed at its end
    auto active = [&](int g0, int g1) {
        bool diff = false;
        for (int g = g0; g <= g1; g++) {
            for (int i = i1 + 1; i < i2; i++)
                if (st[i].second.g[g] != st[i1].second.g[g]) return false;
            diff = diff || st[i2].second.g[g] != st[i1].second.g[g];
        }
        return diff;
    };
    // the words drawn (the kept steps' blend) and the words to draw (the frame ends' blend)
    float d[6], t[6];
    for (int g = 0; g < 6; g++) {
        d[g] = wa->g[g] + f * (wb->g[g] - wa->g[g]);
        t[g] = st[i1].second.g[g] + (float)frac * (st[i2].second.g[g] - st[i1].second.g[g]);
    }
    float X[12], A[12], B[12], C[12], D[12];
    if (active(0, 1)) {  // the root lean: q = anim x Euler(x, 0, 0) x Euler(0, 0, z) (jointBeforeCB joint 0)
        float R[12];
        if (cl) mtx_of(out, R);
        else if (f == 0.0f) std::memcpy(R, wa->root, sizeof R);
        else interp::blend_world_matrix(wa->root, wb->root, R, f);
        if (!cl && !leaf) {  // (a model not near his root is not on his body)
            const float dx = interp::joints::word_f(out[3]) - R[3], dy = interp::joints::word_f(out[7]) - R[7],
                        dz = interp::joints::word_f(out[11]) - R[11];
            if (dx * dx + dy * dy + dz * dz > 150.0f * 150.0f) return;
        }
        rot(0, d[0], A), rot(2, d[1], B), mul(A, B, C);  // (drawn)
        float Ci[12];
        inv(C, Ci);
        rot(0, t[0], A), rot(2, t[1], B), mul(A, B, X);  // (to draw)
        mul(Ci, X, A);                                   // drawn^-1 x to draw
        if (delta(R, A, D)) apply(D, out, 0, n - 1);
    }
    if (cl && n > kWaistLast && active(2, 3)) {  // the waist: q = anim x Euler(0, y, 0) x Euler(0, 0, z)
        rot(1, d[3], A), rot(2, d[2], B), mul(A, B, C);
        float Ci[12], W[12];
        inv(C, Ci);
        rot(1, t[3], A), rot(2, t[2], B), mul(A, B, X);
        mul(Ci, X, A);
        mtx_of(out + 12 * kWaist, W);
        if (delta(W, A, D)) apply(D, out, kWaistFirst, kWaistLast);
    }
    if (leaf && n > kLeafEnd[3] && active(4, 5)) {  // the flutter (parachuteJointCB): roots Y-turned, arms moved on x
        for (int k = 0; k < 4; k++) {
            const uint32_t j = kLeafJ[k];
            float J[12];
            mtx_of(out + 12 * j, J);
            if (k == 0 || k == 2) rot(1, t[4] - d[4], X);
            else rot(1, 0.0f, X), X[3] = t[5] - d[5];
            if (delta(J, X, D)) apply(D, out, j, kLeafEnd[k]);
        }
    }
}

// the gust model: drawn between its frame ends while its matrices hold on the frame's earlier steps
template <class Steps>
void gust(uint32_t proc, uint32_t model, const Steps& steps, double td, uint32_t* out, uint32_t n) {
    if (g_nc || model != ld32(proc + kGust)) return;
    const double F1 = std::floor(td + hist::kEps), F2 = F1 + 1.0, frac = td - F1;
    if (frac < hist::kEps || frac > 1.0 - hist::kEps) return;
    int i1 = -1, i2 = -1;
    for (int i = 0; i < (int)steps.size(); i++) {
        if (std::fabs(steps[i].first - F1) < hist::kEps) i1 = i;
        if (std::fabs(steps[i].first - F2) < hist::kEps) i2 = i;
    }
    if (i1 < 0 || i2 < i1 + 2) return;
    const size_t words = 12 * (size_t)n;
    const auto& a = steps[i1].second;
    const auto& b = steps[i2].second;
    if (a.size() < words || b.size() < words) return;
    for (int i = i1 + 1; i < i2; i++)
        if (steps[i].second.size() < words || std::memcmp(steps[i].second.data(), a.data(), 4 * words) != 0) return;
    if (std::memcmp(a.data(), b.data(), 4 * words) == 0) return;
    const auto r = interp::joints::blend(a.data(), b.data(), nullptr, n, 400.0f, (float)frac, out, interp::blend_world_matrix);
    if (r.cut) std::memcpy(out, steps[i1 + 1].second.data(), 4 * words);  // (a jump: drawn as it is)
}
}  // namespace t60::draw::glide
