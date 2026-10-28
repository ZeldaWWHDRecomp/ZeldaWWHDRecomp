// SPDX-License-Identifier: MPL-2.0
// The game-state reader behind the context buttons (play_context.h): once per full logic step it reads
// what the game offers the player and publishes one PlayContext. The rules that turn it into buttons
// are context_buttons.h; consumers: the Android touch controls (nativePlayContext below), later the
// overlay and a host-drawn HUD.
//
// - Reads happen on full logic steps only (interp.cpp calls play_context_step where the logic-step
//   counter advances, after true 60's half-pass preview was taken back), as mods/jump.cpp decides.
// - The game's own screens (event, message, menu) are hud_gate.cpp's reading, not read twice.
// - The message's parts, the Picto Box screens, the wind compass and the zoom bars are noted when
//   nw::lyt::Pane::Draw draws them (aspect.cpp's hook): a part counts as shown while it was drawn
//   (visible, alpha > 0) within the last kPaneSteps logic steps.
// - Off means identical: until a consumer wants the context (play_context_want) or the trace is on,
//   no guest reads at all, and the pane observer returns at once.
//
// WWHD_PLAYCTX_LOG=1 logs the context on every change ("[playctx] ..."); =2 also logs each layout
// pane name the first time it is drawn while in game (to find a screen's panes).
//
// Idea from the GreenNaugahyde Android fork (GreenNaugahyde/ZeldaWWHDRecompAndroid, MPL-2.0),
// mods/touch_state.cpp and hud.cpp; reimplemented, offsets re-verified (play_context.h).
#include "play_context.h"
#include "context_buttons.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

#include "guest_addr.h"
#include "hud_visibility.h"
#include "mods.h"
#include "runtime.h"

namespace mods {
namespace {
using namespace playctx;

struct GuestMem {
    uint8_t u8(uint32_t a) const { return ld8(a); }
    uint16_t u16(uint32_t a) const { return ld16(a); }
    uint32_t u32(uint32_t a) const { return ld32(a); }
    bool object(uint32_t p, uint32_t size) const { return (p & 3) == 0 && p >= mem::kMem2Start && p <= mem::kMem2End - size; }
};

int log_level() {
    static const int level = [] {
        const char* e = getenv("WWHD_PLAYCTX_LOG");
        return e && *e ? std::max(1, atoi(e)) : 0;
    }();
    return level;
}

std::atomic<int> g_wanted{0};
bool wanted() { return g_wanted.load(std::memory_order_relaxed) > 0 || log_level() > 0; }

const Addresses& addresses() {
    static const Addresses a{GD(0x1046F0B0), GD(0x101F36CC), GD(0x101F84DC)};
    return a;
}

std::mutex g_mu;
PlayContext g_last;
LockTracker g_lock;

// ---- the pane observer ----
// stamps: the logic step + 1 the part was last drawn on (0 never)
std::atomic<uint64_t> g_seen[kPaneSignalCount];
std::atomic<int> g_choice_count{0}, g_zoom_kind{0};
std::atomic<int> g_choice_y{0}, g_cursor_y{0}, g_msg_x{0}, g_msg_y{0}, g_msg_hw{0}, g_msg_hh{0};

constexpr uint32_t kPaneParent = 0x0C, kPaneChildren = 0x14, kPaneSize = 0x3C, kPaneFlags = 0x44, kPaneAlpha = 0x45,
                   kPaneGlobal = 0x48, kPaneName = 0x80;  // nw::lyt::Pane (aspect.cpp)
bool pane_visible(uint32_t p) { return (ld8(p + kPaneFlags) & 1) && ld8(p + kPaneAlpha) != 0; }
float global_x(uint32_t p) { return (float)ldf32(p + kPaneGlobal + 0xC); }
float global_y(uint32_t p) { return (float)ldf32(p + kPaneGlobal + 0x1C); }
int16_t clamp16(float v) { return int16_t(std::fmax(-32768.f, std::fmin(32767.f, v))); }

void note(PaneSignal s) { g_seen[s].store(step() + 1, std::memory_order_relaxed); }

PaneSignals aged_panes() {
    PaneSignals p;
    uint64_t seen[kPaneSignalCount];
    for (int i = 0; i < kPaneSignalCount; i++) seen[i] = g_seen[i].load(std::memory_order_relaxed);
    p.flags = aged_pane_flags(seen, step());
    if (p.flags & kChoice) {
        p.choice_count = uint8_t(g_choice_count.load(std::memory_order_relaxed));
        p.choice_y = int16_t(g_choice_y.load(std::memory_order_relaxed));
        p.cursor_y = int16_t(g_cursor_y.load(std::memory_order_relaxed));
    }
    if (p.flags & kZoomBar) p.zoom_kind = uint8_t(g_zoom_kind.load(std::memory_order_relaxed));
    if (p.flags & kMsgWindow) {
        p.msg_x = int16_t(g_msg_x.load(std::memory_order_relaxed));
        p.msg_y = int16_t(g_msg_y.load(std::memory_order_relaxed));
        p.msg_hw = int16_t(g_msg_hw.load(std::memory_order_relaxed));
        p.msg_hh = int16_t(g_msg_hh.load(std::memory_order_relaxed));
    }
    return p;
}

void log_new_pane(const char* name) {
    static std::mutex mu;
    static std::unordered_set<std::string> seen;
    std::lock_guard<std::mutex> lk(mu);
    if (seen.size() < 4096 && seen.insert(std::string(name, strnlen(name, 24))).second)
        LOG("[playctx] pane %.24s", name);
}
}  // namespace

void play_context_want(bool on) { g_wanted.fetch_add(on ? 1 : -1, std::memory_order_relaxed); }

playctx::PlayContext play_context() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_last;
}

void play_context_pane(uint32_t pane) {
    if (!wanted()) return;
    const char* n = (const char*)mem::ptr(pane + kPaneName);
    if (log_level() >= 2 && n[0] && ld8(addresses().game_info + kStage)) log_new_pane(n);
    const PaneSignal s = classify_pane(n);
    switch (s) {
        case kPaneNone: return;
        case kPaneChoice2: case kPaneChoice3:
            if (!pane_visible(pane)) return;
            g_choice_count.store(s == kPaneChoice2 ? 2 : 3, std::memory_order_relaxed);
            g_choice_y.store(clamp16(global_y(pane)), std::memory_order_relaxed);
            note(kPaneChoice2);  // (one stamp for both: the choice box)
            return;
        case kPaneCursor:
            if (!pane_visible(pane)) return;
            g_cursor_y.store(clamp16(global_y(pane)), std::memory_order_relaxed);
            note(s);
            return;
        case kPaneMsgWindow: {
            if (!pane_visible(pane)) return;
            // its centre and half size: the global matrix's scale times the pane's size
            const uint32_t m = pane + kPaneGlobal;
            g_msg_x.store(clamp16(global_x(pane)), std::memory_order_relaxed);
            g_msg_y.store(clamp16(global_y(pane)), std::memory_order_relaxed);
            g_msg_hw.store(clamp16((float)ldf32(pane + kPaneSize) * std::hypot((float)ldf32(m), (float)ldf32(m + 0x10)) / 2),
                           std::memory_order_relaxed);
            g_msg_hh.store(clamp16((float)ldf32(pane + kPaneSize + 4) * std::hypot((float)ldf32(m + 4), (float)ldf32(m + 0x14)) / 2),
                           std::memory_order_relaxed);
            note(s);
            return;
        }
        case kPaneZoom: {
            // N_Zoom_00's first child tells the Picto Box's bar from the telescope's; the game hides it in places
            if (!(ld8(pane + kPaneFlags) & 1)) return;
            const uint32_t first = ld32(pane + kPaneChildren);
            if (!first || first == pane + kPaneChildren) return;
            const int kind = zoom_kind((const char*)mem::ptr(first + kPaneName));
            if (!kind) return;
            g_zoom_kind.store(kind, std::memory_order_relaxed);
            note(s);
            return;
        }
        case kPaneFinder: case kPaneAlbum:
            note(s);  // (the screen's frame: drawn while the screen is up)
            return;
        default:
            if (pane_visible(pane)) note(s);
            return;
    }
}

void play_context_step() {
    if (!wanted()) {
        hud_fade_step(PlayContext{});  // (asks for the reading when a HUD fade is on)
        return;
    }
    const GameUi ui = game_ui_now();
    const PlayContext c = read(GuestMem{}, addresses(), {ui.event, ui.message, ui.menu}, aged_panes(), g_lock, step());
    PlayContext previous;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        previous = g_last;
        g_last = c;
    }
    context_touch_step(c);
    hud_fade_step(c);
    if (log_level() && (!same_for_log(previous, c) || previous.step == 0)) {
        char buf[512], buttons[160];
        format(c, buf, sizeof buf);
        ctxbtn::format(ctxbtn::context_buttons(c), buttons, sizeof buttons);  // what the context buttons would show
        LOG("[playctx] %s buttons=[%s]", buf, buttons);
    }
}

}  // namespace mods

#ifdef __ANDROID__
#include <jni.h>
// TouchControls.nativePlayContext(int[] out): the last PlayContext as play_context.h pack() lays it out;
// returns the layout's version. The first call makes the reader run (play_context_want).
extern "C" JNIEXPORT jint JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativePlayContext(JNIEnv* env, jclass, jintArray out) {
    static const bool wanted = (mods::play_context_want(true), true);
    (void)wanted;
    int32_t v[mods::playctx::kPackLen];
    mods::playctx::pack(mods::play_context(), v, mods::playctx::kPackLen);
    if (out) {
        const jsize n = env->GetArrayLength(out);
        env->SetIntArrayRegion(out, 0, n < mods::playctx::kPackLen ? n : mods::playctx::kPackLen, reinterpret_cast<const jint*>(v));
    }
    return mods::playctx::kPackVersion;
}
#endif
