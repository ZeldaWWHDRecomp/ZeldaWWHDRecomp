// SPDX-License-Identifier: MPL-2.0
// Game menu sounds in the Settings overlay (overlay/menu_sounds.h has the rules): the overlay reports each
// frame (render thread), a sound that may play is queued, and the game thread plays it on its next pad read
// (VPADRead, hle/system_stubs.cpp) through the game's own playSe(name), as the item menu's settings page
// does. The game keeps running while Settings is open, so the sound goes through the game's mix and output
// like any other game sound.
//
// Option audioMenuSounds (WWHD_MENU_SOUNDS=0|1 for tests, not saved then); WWHD_MENU_SOUNDS_LOG=1 logs each
// sound queued and played ("[menu-sounds] ..."); WWHD_MENU_SOUNDS_FOCUS=1 counts a test run's hidden window
// as in front.
#include "menu_sounds_port.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>

#include "../audio_out.h"
#include "../input.h"
#include "../overlay/hostui.h"
#include "guest_addr.h"
#include "runtime.h"
#include "story_notebook_game.h"

namespace menu_sounds {
namespace {
std::atomic<int> g_option{-1};
std::mutex g_mu;
Tracker g_tracker;
Sound g_pending = Sound::None;
double g_pending_at = 0;
constexpr double kStale = 0.3;  // a sound the game thread couldn't play within this is dropped

bool log_on() {
    static const bool on = [] {
        const char* e = getenv("WWHD_MENU_SOUNDS_LOG");
        return e && *e && *e != '0';
    }();
    return on;
}
double now_seconds() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

// the game's sound system is up: the chain its playSe goes through (sound manager 1018EC64 -> +0x10 -> +0xC,
// and the sead heap singleton 101F8BA8 its name lookup asks; hd_snd 02030B38 -> 020307E0 / 02031920)
bool guest_obj(uint32_t a) { return a >= mem::kMem2Start && a < mem::kMem2End - 0x400 && !(a & 3); }
bool game_sound_ready() {
    const uint32_t mgr = ld32(GD(0x1018EC64));
    if (!guest_obj(mgr)) return false;
    const uint32_t x = ld32(mgr + 0x10);
    if (!guest_obj(x) || !guest_obj(ld32(x + 0xC))) return false;
    const uint32_t heap = ld32(GD(0x101F8BA8));
    return guest_obj(heap) && guest_obj(ld32(heap + 0x1C));
}
}  // namespace

bool option() {
    int v = g_option.load(std::memory_order_relaxed);
    if (v >= 0) return v != 0;
    v = 1;
    std::string s;
    if (const char* e = getenv("WWHD_MENU_SOUNDS"); e && *e) v = atoi(e) != 0;
    else if (hostui::get("audioMenuSounds", s) && !s.empty()) v = atoi(s.c_str()) != 0;
    g_option.store(v, std::memory_order_relaxed);
    return v != 0;
}
void set_option(bool on) {
    g_option.store(on ? 1 : 0, std::memory_order_relaxed);
    if (!getenv("WWHD_MENU_SOUNDS")) hostui::set("audioMenuSounds", on ? "1" : "0");
}

void observe(Frame f) {
    f.time = now_seconds();
    std::lock_guard<std::mutex> lk(g_mu);
    const Sound s = g_tracker.step(f);  // (always stepped: the tracker keeps up with the overlay)
    if (s == Sound::None) return;
    Gate g;
    g.option = option();
    // (WWHD_MENU_SOUNDS_FOCUS=1: a test run's hidden window counts as in front)
    static const bool test_focus = [] { const char* e = getenv("WWHD_MENU_SOUNDS_FOCUS"); return e && *e == '1'; }();
    g.focused = input::window_focused() || test_focus;
    g.master_volume = audio::master_volume();
    g.game_sound_ready = true;  // checked on the game thread (service)
    if (!audible(g)) {
        if (log_on()) LOG("[menu-sounds] %s not played (option %d, focused %d, volume %.2f)", label(s), g.option, g.focused, g.master_volume);
        return;
    }
    // one sound waits at a time: a newer one replaces it (the game thread plays on its next pad read)
    g_pending = s, g_pending_at = f.time;
    if (log_on()) LOG("[menu-sounds] queued %s", label(s));
}

void service(Cpu* c) {
    Sound s;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        s = g_pending;
        if (s == Sound::None) return;
        g_pending = Sound::None;
        if (now_seconds() - g_pending_at > kStale) return;
    }
    if (!c || !game_sound_ready()) {
        if (log_on()) LOG("[menu-sounds] %s not played: the game's sound system isn't up", label(s));
        return;
    }
    const auto& ui = story_notebook::game_ui();
    if (!ui.sound) return;
    if (log_on()) LOG("[menu-sounds] play %s", label(s));
    ui.sound(c, label(s));
}

}  // namespace menu_sounds
