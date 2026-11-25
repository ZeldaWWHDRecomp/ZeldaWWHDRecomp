// (2026-11-24; from the pot route, frame 3682).
// Included by t60_draw.cpp only (its model()); draw only, no logic.
//
// Link's animation blend (the under / upper anm ratio packs at daPy_lk_c 0x57F8, set from m_pbCalc's blend ratios by
// setAnimeRatioHD; the animation heaps at 0x5848) is a frame-grid value: a procedure's cross-fade puts the frame's whole
// ratio change (cs crouch: +0.412 a frame) and its slot swaps (heap 0126/012e -> 012e/0070) on the frame's FIRST step and
// holds them through the tick30 step. The earlier step's pose then shows the frame's whole blend change and the tick30
// step only the animations' own play (cs 3618-3620, 3642, 3646: 0.73-0.93 of the frame's motion on the earlier pass, up
// to 9.3 units / 33 mrad; plain interpolation draws 0.5). A slot swap cannot be drawn as a blend of the ratios.
// Form: Link's models are drawn one step behind his newest step, so on a frame's earlier pass both frame ends are kept.
// When his blend words (the five ratio arrays and the heaps) changed by the frame's earlier steps and held from there to
// the frame end, his CL model is drawn on those passes between the frame ends' skeletons (each joint blended as frame
// interpolation draws it); his other body models (sword, shield, hands, the carried items' models of his process) move
// with the CL joint they are nearest to (within 40 units). Other frames: drawn as before.
// NC: WWHD_T60_LINK_RATIOPIC_NEGCTRL=1 (drawn as before). Debug WWHD_T60_LINK_RATIOPIC_LOG=1: each frame drawn so.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <utility>
#include <vector>

#include "guest_addr.h"
#include "interp_joints.h"
#include "t60/t60_draw_glide.h"
#include "t60/t60_draw_hist.h"

namespace t60::draw::ratiopic {
inline const bool g_nc = [] {
    const char* e = getenv("WWHD_T60_LINK_RATIOPIC_NEGCTRL");
    return e && atoi(e) != 0;
}();
inline const bool g_log = [] {
    const char* e = getenv("WWHD_T60_LINK_RATIOPIC_LOG");
    return e && atoi(e) != 0;
}();
constexpr uint32_t kPacks = 0x57F8, kHeaps = 0x5848, kHeapWords = 0x50 / 4, kCLModel = 0x448;
constexpr float kAttach = 40.0f;

struct Hist { uint32_t proc = 0, id = 0; std::deque<std::pair<double, std::vector<uint32_t>>> steps; };
inline Hist g_hist;
inline uint32_t g_cl_jnt = 0;   // the CL model's joint block (its draw history key)
inline double g_logged = -1.0;  // (the last frame logged)

// the blend words of the newest step: heaps, then each pack's ratio array pointer and ratios
inline std::vector<uint32_t> words(uint32_t proc) {
    std::vector<uint32_t> v;
    v.reserve(kHeapWords + 5 * 44);
    for (uint32_t i = 0; i < kHeapWords; i++) v.push_back(ld32(proc + kHeaps + 4 * i));
    for (uint32_t p = 0; p < 5; p++) {
        const uint32_t pk = proc + kPacks + 0x10 * p, cnt = ld32(pk + 8), arr = ld32(pk + 0xC);
        v.push_back(arr);
        if (arr && cnt <= 64)
            for (uint32_t j = 0; j < cnt; j++) v.push_back(ld32(arr + 4 * j));
    }
    return v;
}
inline void record(uint32_t proc, uint32_t id, double time) {
    if (g_hist.proc != proc || g_hist.id != id) g_hist = Hist{}, g_hist.proc = proc, g_hist.id = id;
    hist::stamp(g_hist.steps, time, words(proc));
}
// the frame around td whose blend words changed by its earlier steps and held to its end: its two ends' times, frac
inline bool active(uint32_t proc, double td, double& F1, double& F2, double& frac) {
    if (g_nc || g_hist.proc != proc) return false;
    F1 = std::floor(td + hist::kEps), F2 = F1 + 1.0, frac = td - F1;
    if (frac < hist::kEps || frac > 1.0 - hist::kEps) return false;  // (a frame end: drawn as it is)
    const auto& st = g_hist.steps;
    int i1 = -1, i2 = -1;
    for (int i = 0; i < (int)st.size(); i++) {
        if (std::fabs(st[i].first - F1) < hist::kEps) i1 = i;
        if (std::fabs(st[i].first - F2) < hist::kEps) i2 = i;
    }
    if (i1 < 0 || i2 < i1 + 2) return false;
    if (st[i1].second == st[i2].second) return false;  // (no blend change this frame)
    for (int i = i1 + 1; i < i2; i++)
        if (st[i].second != st[i2].second) return false;  // (changed on the tick30 step: the frame-grid change is its end)
    if (g_log && std::fabs(g_logged - F2) > hist::kEps) {
        g_logged = F2;
        fprintf(stderr, "[t60] ratiopic frame %.1f\n", F2);
    }
    return true;
}
// the index of the kept step at time t (-1: none)
template <class Steps> int find(const Steps& steps, double t) {
    for (int i = 0; i < (int)steps.size(); i++)
        if (std::fabs(steps[i].first - t) < hist::kEps) return i;
    return -1;
}
// Link's CL model: drawn between its frame ends at frac (out: as drawn so far)
template <class Steps>
void body(const Steps& steps, double F1, double F2, double frac, uint32_t* out, uint32_t n) {
    const int a = find(steps, F1), b = find(steps, F2);
    if (a < 0 || b < 0) return;
    const size_t words = 12 * (size_t)n;
    if (steps[a].second.size() < words || steps[b].second.size() < words) return;
    std::vector<uint32_t> v(words);
    const auto r = interp::joints::blend(steps[a].second.data(), steps[b].second.data(), nullptr, n, 400.0f, (float)frac,
                                         v.data(), interp::blend_world_matrix);
    if (r.cut) return;  // (a jump: drawn as it is)
    std::memcpy(out, v.data(), 4 * words);
}
// another body model of Link's: moved with its nearest CL joint (CL drawn at td from its history vs the frame ends' blend)
template <class Steps>
void attached(const Steps& cl, uint32_t ncl, double td, double F1, double F2, double frac, uint32_t* out, uint32_t n) {
    const int a = find(cl, F1), b = find(cl, F2);
    if (a < 0 || b < 0 || n == 0) return;
    const size_t words = 12 * (size_t)ncl;
    const hist::Pick p = hist::pick(cl, td);
    if (p.a < 0 || cl[a].second.size() < words || cl[b].second.size() < words || cl[p.a].second.size() < words ||
        cl[p.b].second.size() < words)
        return;
    const float mx = interp::joints::word_f(out[3]), my = interp::joints::word_f(out[7]), mz = interp::joints::word_f(out[11]);
    // the drawn CL at td (as model() draws it) and the frame ends' blend, joint by joint for the nearest one
    std::vector<uint32_t> drawn(words), want(words);
    if (p.f == 0.0f) std::memcpy(drawn.data(), cl[p.a].second.data(), 4 * words);
    else interp::joints::blend(cl[p.a].second.data(), cl[p.b].second.data(), nullptr, ncl, 400.0f, p.f, drawn.data(),
                               interp::blend_world_matrix);
    const auto r = interp::joints::blend(cl[a].second.data(), cl[b].second.data(), nullptr, ncl, 400.0f, (float)frac,
                                         want.data(), interp::blend_world_matrix);
    if (r.cut) return;
    int best = -1;
    float bd = kAttach * kAttach;
    for (uint32_t j = 0; j < ncl; j++) {
        const float dx = interp::joints::word_f(drawn[12 * j + 3]) - mx, dy = interp::joints::word_f(drawn[12 * j + 7]) - my,
                    dz = interp::joints::word_f(drawn[12 * j + 11]) - mz, d = dx * dx + dy * dy + dz * dz;
        if (d < bd) bd = d, best = (int)j;
    }
    if (best < 0) return;  // (not on his body: the shadow, the gust, world-placed models)
    float Dw[12], Wt[12], Di[12], D[12];
    glide::mtx_of(drawn.data() + 12 * best, Dw);
    glide::mtx_of(want.data() + 12 * best, Wt);
    if (!glide::inv(Dw, Di)) return;
    glide::mul(Wt, Di, D);  // (the joint's change: want x drawn^-1)
    glide::apply(D, out, 0, n - 1);
}
}  // namespace t60::draw::ratiopic
