// Vulkan renderer defaults on phones and the Steam Deck: smooth_defaults.h
#include "smooth_defaults.h"
#ifdef WWHD_VK_SMOOTH
#include "runtime.h"
#include "overlay/hostui.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace gfxvk::smooth_defaults {
namespace {
// The halves of the defaults, per build. Second-thread recording (record_worker(), record_mt()) was measured in the
// decomp build's fast mode (native draw packets). In the normal build (translated draws) the worker alone made the lava
// 1 % lows worse on a Xiaomi 14 at 60 fps (2026-11-14, n=2: 37.8 -> 34.9 fps, p99 26.4 -> 28.7 ms): the faster render
// thread put the game ahead of its step slots more often, and the passes after its vsync sleeps ran 40-50 % slower
// (cores clocked down). With those short waits spun (vsync_hot_wait_us(), hot_wait.h) the worker half wins there too
// (against the pacing half alone: lava +5.3 fps, p99 -0.1 ms, 1 % low +0.2 fps; cliff +7.9 fps, -2.2 ms, +3.9 fps), so
// every build has both halves; a build can still drop the worker half by setting its kWorkerHalf to false.
#ifdef WWHD_DECOMP_FAST
constexpr bool kWorkerHalf = true;
#define WWHD_SMOOTH_BUILD "decomp build fast mode"
#elif defined(WWHD_DECOMP)
constexpr bool kWorkerHalf = true;
#define WWHD_SMOOTH_BUILD "decomp build"
#else
constexpr bool kWorkerHalf = true;
#define WWHD_SMOOTH_BUILD "normal build"
#endif
#if defined(__linux__) && !defined(__ANDROID__)
std::string first_line(const char* path) {
    char buf[64] = {};
    FILE* f = std::fopen(path, "r");
    if (!f) return {};
    if (!std::fgets(buf, sizeof buf, f)) buf[0] = 0;
    std::fclose(f);
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}
#endif
// The worker half on this device: Android and the Steam Deck. The Deck had it off from 2026-11-15 (issue #178: the
// normal build crashed at start with VK_ERROR_DEVICE_LOST, "POD Vulkan operation failed: -4", after #576); the cause
// was gamescope's WSI layer (up to 3.16.31) corrupting its memory in vkGetPastPresentationTimingGOOGLE, and the
// renderer reads display timings safely since 33b1f17a1 (display_timing.h), so the Deck has both halves again
// (user decision 2026-11-19).
bool worker_half() { return worker_half_for(device() && !steam_deck(), steam_deck()); }
void log_once() {
    static const bool logged = [] {
        if (worker_half())
            LOG("[vulkan smooth defaults] %s, " WWHD_SMOOTH_BUILD ": graphics on a second thread with multi-threaded "
                "recording, 8 submission slots, late present acknowledgment, hot vsync waits, Smooth frame pacing %s (each environment "
                "variable still wins)", steam_deck() ? "Steam Deck" : "Android", smooth_pacing_setting() ? "on" : "off");
        else if (device())
            LOG("[vulkan smooth defaults] %s, " WWHD_SMOOTH_BUILD ": 8 submission slots, late present acknowledgment, "
                "Smooth frame pacing %s; graphics on one thread (each environment variable still wins)",
                steam_deck() ? "Steam Deck" : "Android", smooth_pacing_setting() ? "on" : "off");
        return true;
    }();
    (void)logged;
}
}
bool steam_deck() {
#if defined(__linux__) && !defined(__ANDROID__)
    static const bool value = [] {
        if (const char* e = std::getenv("SteamDeck"); e && !std::strcmp(e, "1")) return true;
        const std::string vendor = first_line("/sys/devices/virtual/dmi/id/board_vendor");
        const std::string product = first_line("/sys/devices/virtual/dmi/id/product_name");
        return vendor == "Valve" && (product == "Jupiter" || product == "Galileo");
    }();
    return value;
#else
    return false;
#endif
}
bool device() {
#ifdef __ANDROID__
    return true;
#else
    return steam_deck();
#endif
}
bool worker_half_for(bool android, bool deck) { return kWorkerHalf && (android || deck); }
bool record_worker() { log_once(); return worker_half(); }
unsigned record_mt() { return worker_half() ? 127u : 0u; }
unsigned submission_slots() { return device() ? 8u : 0u; }
bool late_present_ack() { return device(); }
bool smooth_pacing_setting() {
    static const bool value = [] { std::string v; return !::hostui::get("vkSmoothPacing", v) || v != "0"; }();
    return value;
}
int display_pacing() { return device() && smooth_pacing_setting() ? 11 : 0; }
bool even_pacing() { return device(); }
int vsync_hot_wait_us(bool thirty_fps) { return worker_half() && !thirty_fps ? 8000 : 0; }
int64_t vsync_hot_wait_applied_us(const char* env, bool thirty_fps) {
    return env && *env ? std::max<int64_t>(0, std::atoll(env)) : vsync_hot_wait_us(thirty_fps);
}
}  // namespace gfxvk::smooth_defaults
#endif
