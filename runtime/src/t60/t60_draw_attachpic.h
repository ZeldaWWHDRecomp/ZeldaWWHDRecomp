// (2026-11-25; from the the sweep: mt / bdk, Link-attached 6-joint
// model on his CL joint 13, fracT 0.73, excess 0.141). Included by t60_draw.cpp only (its model()); draw only, no logic.
//
// Link's execute places his attached models from the CL joints after the calc (setItemModel: the shield on his back /
// in his hand, the scabbard and the sheathed sword on CL joint 13 cl_podA, the equipped item on his hands; katsura and
// the mask on the head joint 15) and calcs them there, each step. Their draw history is their own steps; his CL model
// is drawn from its own history, and on a frame whose earlier steps' pose is not the straight in-between (a dash start:
// the T pass's catch-up, RATIOPIC / MORFPIC frames between the frame ends) the two part: mt frame 3577 the shield on his
// back 5.1 units off its cl_podA offset on the T pass, 3.45 / 1.73 / 0 after (N 4); RATIOPIC's attached() moves them with
// the nearest CL joint, which for the shield (16 units off its joint) is another joint. Form: on a frame's earlier pass,
// each of his attached models (not his CL model, the shadow, the gust, the leaf, the heavy boots)
// whose root keeps a rigid offset from one CL joint at both frame ends (the joint whose frame-end offsets differ least,
// under kRigid units, within kNear) is drawn at that joint as his CL is drawn at this pass (RATIOPIC / MORFPIC: the
// frame ends' blend; else as model() picks it) times the in-between of its frame-end offsets from it. Not rigid, a frame
// end missing, or a move over kJump: drawn as before. NC: WWHD_T60_LINK_ATTACHPIC_NEGCTRL=1.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "interp_joints.h"
#include "t60/t60_draw_glide.h"
#include "t60/t60_draw_hist.h"
#include "t60/t60_draw_ratiopic.h"

namespace interp { void blend_world_matrix(const float* a, const float* b, float* out, float t); }

namespace t60::draw::attachpic {
inline const bool g_nc = [] {
    const char* e = getenv("WWHD_T60_LINK_ATTACHPIC_NEGCTRL");
    return e && atoi(e) != 0;
}();
constexpr float kNear = 40.0f, kRigid = 0.5f, kJump = 60.0f;
inline uint32_t g_cl_jnt = 0;  // his CL model's joint block (its draw history key)

// Link's attached model `out` (n joints, its own history `own`) at its CL joint as drawn at td; `cl` / `ncl`: his CL
// history; `rp`: RATIOPIC / MORFPIC draws his CL between the frame ends at this td. Returns whether it placed it.
template <class Steps, class CL>
bool apply(const Steps& own, const CL& cl, uint32_t ncl, double td, bool rp, uint32_t* out, uint32_t n) {
    if (g_nc || n == 0 || ncl == 0) return false;
    const double F1 = std::floor(td + hist::kEps), F2 = F1 + 1.0, frac = td - F1;
    if (frac < hist::kEps || frac > 1.0 - hist::kEps) return false;  // (a frame end: drawn as it is)
    const int ma = ratiopic::find(own, F1), mb = ratiopic::find(own, F2);
    const int ha = ratiopic::find(cl, F1), hb = ratiopic::find(cl, F2);
    if (ma < 0 || mb < 0 || ha < 0 || hb < 0) return false;
    const size_t mw = 12 * (size_t)n, cw = 12 * (size_t)ncl;
    if (own[ma].second.size() < mw || own[mb].second.size() < mw || cl[ha].second.size() < cw || cl[hb].second.size() < cw)
        return false;
    float Ma[12], Mb[12];
    glide::mtx_of(own[ma].second.data(), Ma);
    glide::mtx_of(own[mb].second.data(), Mb);
    // (its joint: the rigid one; the offsets in the joint's frame at both frame ends)
    int best = -1;
    float bd = kRigid, Ra[12], Rb[12];
    for (uint32_t j = 0; j < ncl; j++) {
        float Ja[12], Jb[12], Ia[12], Ib[12], A[12], B[12];
        glide::mtx_of(cl[ha].second.data() + 12 * j, Ja);
        glide::mtx_of(cl[hb].second.data() + 12 * j, Jb);
        const float dx = Ma[3] - Ja[3], dy = Ma[7] - Ja[7], dz = Ma[11] - Ja[11];
        if (dx * dx + dy * dy + dz * dz > kNear * kNear) continue;
        if (!glide::inv(Ja, Ia) || !glide::inv(Jb, Ib)) continue;
        glide::mul(Ia, Ma, A);
        glide::mul(Ib, Mb, B);
        const float ex = A[3] - B[3], ey = A[7] - B[7], ez = A[11] - B[11], d = std::sqrt(ex * ex + ey * ey + ez * ez);
        if (d < bd) bd = d, best = (int)j, std::memcpy(Ra, A, sizeof Ra), std::memcpy(Rb, B, sizeof Rb);
    }
    if (best < 0) return false;  // (no rigid joint: drawn as before)
    // (the joint as his CL is drawn at td)
    float Jd[12];
    float Ja[12], Jb[12];
    glide::mtx_of(cl[ha].second.data() + 12 * best, Ja);
    glide::mtx_of(cl[hb].second.data() + 12 * best, Jb);
    if (rp) {
        interp::blend_world_matrix(Ja, Jb, Jd, (float)frac);
    } else {
        const hist::Pick p = hist::pick(cl, td);
        if (p.a < 0 || cl[p.a].second.size() < cw || cl[p.b].second.size() < cw) return false;
        float A[12], B[12];
        glide::mtx_of(cl[p.a].second.data() + 12 * best, A);
        glide::mtx_of(cl[p.b].second.data() + 12 * best, B);
        if (p.f == 0.0f) std::memcpy(Jd, A, sizeof Jd);
        else interp::blend_world_matrix(A, B, Jd, p.f);
    }
    float R[12], T[12], O[12], Oi[12], D[12];
    interp::blend_world_matrix(Ra, Rb, R, (float)frac);
    glide::mul(Jd, R, T);
    glide::mtx_of(out, O);
    const float dx = T[3] - O[3], dy = T[7] - O[7], dz = T[11] - O[11];
    if (dx * dx + dy * dy + dz * dz > kJump * kJump) return false;  // (a large move: drawn as before)
    if (!glide::inv(O, Oi)) return false;
    glide::mul(T, Oi, D);
    glide::apply(D, out, 0, n - 1);
    return true;
}
}  // namespace t60::draw::attachpic
