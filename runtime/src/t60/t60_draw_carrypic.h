// (2026-11-24; the carried items and the worn barrel's staircase).
// Included by t60_draw.cpp only (its model()); draw only, no logic.
//
// What Link carries (his grab actor, daPy_lk_c mActorKeepGrab.mActor +0x65A4: pots, barrels, Medli / Makar ...) is
// placed by his setGrabItemPos on each step, but parts of that placement are frame-grid values: the wear ease
// mGrabItemOffsetY (addCalc toward -85 / 0, max 20 a frame) lands on the tick30 step, so on the earlier pass the worn
// barrel sat at the frame start's offset while his hands moved on (route obt: exactly 10.0 units a pass, up to 93 mrad);
// and on a RATIOPIC frame his hands are drawn between the frame ends while the carried actor keeps its own step's
// placement (potm 3704 / 3708: 3.7 / 7.3 units). Form: on a frame's earlier pass, while the same actor is his grab actor
// at both frame ends and on the steps between, its models are drawn at his drawn left hand (CL joint 8, as model()
// and draw it) times the in-between of its frame ends' transforms relative to that hand (the carried
// actor keeps to the hand as interpolation between the frame ends shows it; the place only, its turn the frame ends'
// blend). Only while carried (actor_status 0x102000 at both ends and between). Frame ends: drawn as they are.
// NC: WWHD_T60_LINK_CARRYPIC_NEGCTRL=1 (drawn as before).
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
#include "t60/t60_draw_glide.h"
#include "t60/t60_draw_hist.h"
#include "t60/t60_draw_ratiopic.h"

namespace interp { void blend_world_matrix(const float* a, const float* b, float* out, float t); }

namespace t60::draw::carrypic {
inline const bool g_nc = [] {
    const char* e = getenv("WWHD_T60_LINK_CARRYPIC_NEGCTRL");
    return e && atoi(e) != 0;
}();
constexpr uint32_t kGrabActor = 0x65A0 + 4, kCLModel = 0x448, kHand = 8, kStatus = 0x2E0, kCarryNow = 0x102000;

struct Hist { uint32_t proc = 0, id = 0; std::deque<std::pair<double, uint32_t>> steps; };
inline Hist g_hist;            // Link's grab actor per step
inline uint32_t g_cl_jnt = 0;  // his CL model's joint block (its draw history key)

// (the grab actor while it is carried: its fopAc actor_status carry bits 0x102000 set; 0 otherwise: a pot Link has
// taken hold of but not lifted yet stays where it is)
inline void record(uint32_t link, uint32_t id, double time) {
    if (g_hist.proc != link || g_hist.id != id) g_hist = Hist{}, g_hist.proc = link, g_hist.id = id;
    const uint32_t g = ld32(link + kGrabActor);
    hist::stamp(g_hist.steps, time, g && (ld32(g + kStatus) & kCarryNow) ? g : 0u);
}
// the frame around td during which `actor` was Link's grab actor at both ends and between them
inline bool held(uint32_t actor, double td, double& F1, double& F2, double& frac) {
    if (g_nc || actor == 0) return false;
    F1 = std::floor(td + hist::kEps), F2 = F1 + 1.0, frac = td - F1;
    if (frac < hist::kEps || frac > 1.0 - hist::kEps) return false;
    const auto& st = g_hist.steps;
    int i1 = -1, i2 = -1;
    for (int i = 0; i < (int)st.size(); i++) {
        if (std::fabs(st[i].first - F1) < hist::kEps) i1 = i;
        if (std::fabs(st[i].first - F2) < hist::kEps) i2 = i;
    }
    if (i1 < 0 || i2 <= i1) return false;
    for (int i = i1; i <= i2; i++)
        if (st[i].second != actor) return false;
    return true;
}
// the carried actor's model `out` (n joints; its own history `steps`) at Link's drawn hand; `cl` / `ncl`: his CL history;
// `rp`: RATIOPIC draws his CL between the frame ends at this td
template <class Steps, class CL>
void apply(uint32_t link, const Steps& steps, const CL& cl, uint32_t ncl, double td, double F1, double F2, double frac,
           bool rp, uint32_t* out, uint32_t n) {
    if (n == 0 || ncl <= kHand) return;
    const int ma = ratiopic::find(steps, F1), mb = ratiopic::find(steps, F2);
    const int ha = ratiopic::find(cl, F1), hb = ratiopic::find(cl, F2);
    if (ma < 0 || mb < 0 || ha < 0 || hb < 0) return;
    const size_t mw = 12 * (size_t)n, cw = 12 * (size_t)ncl;
    if (steps[ma].second.size() < mw || steps[mb].second.size() < mw || cl[ha].second.size() < cw || cl[hb].second.size() < cw)
        return;
    float Ha[12], Hb[12], Hd[12], Ma[12], Mb[12], O[12];
    glide::mtx_of(cl[ha].second.data() + 12 * kHand, Ha);
    glide::mtx_of(cl[hb].second.data() + 12 * kHand, Hb);
    glide::mtx_of(steps[ma].second.data(), Ma);
    glide::mtx_of(steps[mb].second.data(), Mb);
    glide::mtx_of(out, O);
    if (rp) {  // (his hand as RATIOPIC draws it)
        interp::blend_world_matrix(Ha, Hb, Hd, (float)frac);
    } else {   // (his hand as model() draws it at td)
        const hist::Pick p = hist::pick(cl, td);
        if (p.a < 0 || cl[p.a].second.size() < cw || cl[p.b].second.size() < cw) return;
        float A[12], B[12];
        glide::mtx_of(cl[p.a].second.data() + 12 * kHand, A);
        glide::mtx_of(cl[p.b].second.data() + 12 * kHand, B);
        if (p.f == 0.0f) std::memcpy(Hd, A, sizeof Hd);
        else interp::blend_world_matrix(A, B, Hd, p.f);
    }
    // (its root: the frame ends' blend, its place at the drawn hand plus the frame ends' offset from the hand blended;
    // the hand's own turn is not put on it: setGrabItemPos places from his place, not the hand's frame)
    float T[12], Oi[12], D[12];
    if (!glide::inv(O, Oi)) return;
    interp::blend_world_matrix(Ma, Mb, T, (float)frac);
    for (int c = 0; c < 3; c++) {
        const int k = 4 * c + 3;
        T[k] = Hd[k] + (Ma[k] - Ha[k]) + (float)frac * ((Mb[k] - Hb[k]) - (Ma[k] - Ha[k]));
    }
    // (a large move is no carry: a throw's release, a teleport; drawn as it is)
    const float dx = T[3] - O[3], dy = T[7] - O[7], dz = T[11] - O[11];
    if (dx * dx + dy * dy + dz * dz > 60.0f * 60.0f) return;
    glide::mul(T, Oi, D);
    glide::apply(D, out, 0, n - 1);
    (void)link;
}
}  // namespace t60::draw::carrypic
