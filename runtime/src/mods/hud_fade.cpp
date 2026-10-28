// SPDX-License-Identifier: MPL-2.0
// HUD fades (hud_fade.h has the rules) and the game's HUD parts the port hides: the options, the
// per-step reading, and what aspect.cpp's Pane hooks ask.
//
// Panes (the game's HUD layout, found with WWHD_PLAYCTX_LOG=2 / WWHD_ASPECT_TRACE_FRAME):
//   L_HeartAll_00 the hearts, L_Rupy_00 the rupee counter, L_MagicBar_00 the magic bar. Each is faded
//   through its own alpha (nw::lyt::Pane +0x45) for the matrix calculation, which passes it on to the
//   children that inherit alpha; fully faded it isn't drawn at all.
//   The button cluster hidden while the touch controls show the context buttons (context_touch.cpp):
//   N_X_00, N_Y_00, N_R_00 (the item buttons), P_B_00, P_ZR_00, L_CommandA_00, W_SetSeatA_00 and
//   P_SetSeatASpecial_00 (A and its seat): only in TV layouts (the GamePad's own screens keep theirs).
//
// Options (settings.ini hudFadeHearts, hudFadeRupees, hudFadeMagic; all off by default; tests:
// WWHD_HUD_FADE=hearts,rupees,magic, not saved). WWHD_HUD_FADE_LOG=1 logs what each wants on change.
#include "hud_fade.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "guest_addr.h"
#include "mods.h"
#include "play_context.h"
#include "../overlay/hostui.h"
#include "../runtime.h"

namespace mods {
namespace {
using namespace hudfade;

constexpr const char* kKeys[kCount] = {"hudFadeHearts", "hudFadeRupees", "hudFadeMagic"};
constexpr const char* kNames[kCount] = {"hearts", "rupees", "magic"};
constexpr const char* kPanes[kCount] = {"L_HeartAll_00", "L_Rupy_00", "L_MagicBar_00"};
constexpr const char* kCluster[] = {"N_X_00", "N_Y_00", "N_R_00", "P_B_00", "P_ZR_00", "L_CommandA_00", "W_SetSeatA_00",
                                    "P_SetSeatASpecial_00"};
constexpr uint32_t kPaneName = 0x80;

std::atomic<int> g_on[kCount] = {-1, -1, -1};
const char* test_env() { return getenv("WWHD_HUD_FADE"); }
bool option(int e) {
    int v = g_on[e].load(std::memory_order_relaxed);
    if (v >= 0) return v != 0;
    if (const char* t = test_env(); t && *t) v = strstr(t, kNames[e]) != nullptr;
    else {
        std::string s;
        v = hostui::get(kKeys[e], s) && s == "1";
    }
    g_on[e].store(v, std::memory_order_relaxed);
    return v != 0;
}
bool any_on() { return option(kHearts) || option(kRupees) || option(kMagic); }
bool log_on() {
    static const bool on = getenv("WWHD_HUD_FADE_LOG") != nullptr;
    return on;
}

std::mutex g_mu;
Rules g_rules;
std::atomic<float> g_alpha[kCount] = {1.f, 1.f, 1.f};
std::chrono::steady_clock::time_point g_last;

int element_of(const char* n) {
    if (n[0] != 'L') return -1;
    for (int e = 0; e < kCount; e++)
        if (!strncmp(n, kPanes[e], 24)) return e;
    return -1;
}
}  // namespace

bool hud_fade_on(int e) { return e >= 0 && e < kCount && option(e); }
void set_hud_fade_on(int e, bool on) {
    if (e < 0 || e >= kCount) return;
    g_on[e].store(on, std::memory_order_relaxed);
    if (!test_env()) hostui::set(kKeys[e], on ? "1" : "0");
}

void hud_fade_step(const playctx::PlayContext& c) {
    const auto now = std::chrono::steady_clock::now();
    const float seconds = g_last.time_since_epoch().count() ? std::min(0.25f, std::chrono::duration<float>(now - g_last).count()) : 0.f;
    g_last = now;
    if (!any_on()) {
        for (auto& a : g_alpha) a.store(1.f, std::memory_order_relaxed);
        return;
    }
    static const bool wanted = (play_context_want(true), true);  // (the reading starts with the next step)
    (void)wanted;
    In in;
    in.play = c.has(playctx::kInPlay) && c.has(playctx::kLink);
    in.sword = c.item_in_hand == playctx::kHandSword;
    in.locked = c.has(playctx::kLocked);
    in.shop = c.has(playctx::kShop);
    const uint32_t info = ld32(GD(0x101F84DC));  // the save info (play_context.h Addresses::save_info_ptr)
    if (c.has(playctx::kInGame) && info >= mem::kMem2Start && info <= mem::kMem2End - 0x100) {
        const uint32_t s = info + 0x20;  // dSv_player_c: max life, life, rupees (u16); magic +0x14 (cheats.cpp)
        in.max_life = ld16(s), in.life = ld16(s + 2), in.rupees = ld16(s + 4), in.magic = ld8(s + 0x14);
        in.save = in.max_life >= 12 && in.max_life <= 80 && in.life <= in.max_life;
    }
    bool want[kCount];
    std::lock_guard<std::mutex> lk(g_mu);
    g_rules.step(in, c.step, want);
    for (int e = 0; e < kCount; e++) {
        const float a = option(e) ? fade(g_alpha[e].load(std::memory_order_relaxed), want[e], seconds) : 1.f;
        if (log_on() && (a == 0.f || a == 1.f) && a != g_alpha[e].load(std::memory_order_relaxed))
            LOG("[hudfade] %s %s (step %llu)", kNames[e], a ? "shown" : "faded out", (unsigned long long)c.step);
        g_alpha[e].store(a, std::memory_order_relaxed);
    }
}

float hud_fade_alpha_of(uint32_t pane) {
    const int e = element_of(reinterpret_cast<const char*>(mem::ptr(pane + kPaneName)));
    return e < 0 ? 1.f : g_alpha[e].load(std::memory_order_relaxed);
}

int hud_pane_hide(uint32_t pane) {
    const char* n = reinterpret_cast<const char*>(mem::ptr(pane + kPaneName));
    if (const int e = element_of(n); e >= 0) return g_alpha[e].load(std::memory_order_relaxed) <= 0.f ? 1 : 0;
    if (!context_touch_hide_cluster()) return 0;
    if (n[0] != 'N' && n[0] != 'P' && n[0] != 'L' && n[0] != 'W') return 0;
    for (const char* c : kCluster)
        if (!strncmp(n, c, 24)) return 2;
    return 0;
}
}  // namespace mods
