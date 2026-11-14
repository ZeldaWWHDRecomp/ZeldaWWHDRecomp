// Vulkan renderer defaults on phones and the Steam Deck: smooth_defaults.h
#include "smooth_defaults.h"
#ifdef WWHD_VK_SMOOTH
#include "runtime.h"
#include "overlay/hostui.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace gfxvk::smooth_defaults {
namespace {
// The halves of the defaults, per build. Second-thread recording (record_worker(), record_mt()) was measured in the
// decomp build's fast mode (native draw packets). The normal build (translated draws, many more commands) keeps the
// pacing half alone: on a Xiaomi 14 at 60 fps (2026-11-14, n=2) the worker made the lava 1 % lows worse
// (37.8 -> 34.9 fps, p99 26.4 -> 28.7 ms) although it helped at the cliff; the pacing half alone improved both
// (cliff 1 % low 38.2 -> 40.8, lava 37.8 -> 39.5). WWHD_VK_RECORD_WORKER=1 / WWHD_VK_RECORD_MT=127 still turn it on.
#ifdef WWHD_DECOMP_FAST
constexpr bool kWorkerHalf = true;
#define WWHD_SMOOTH_BUILD "decomp build fast mode"
#elif defined(WWHD_DECOMP)
constexpr bool kWorkerHalf = true;
#define WWHD_SMOOTH_BUILD "decomp build"
#else
constexpr bool kWorkerHalf = false;
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
void log_once() {
    static const bool logged = [] {
        if (device() && kWorkerHalf)
            LOG("[vulkan smooth defaults] %s, " WWHD_SMOOTH_BUILD ": graphics on a second thread with multi-threaded "
                "recording, 8 submission slots, late present acknowledgment, Smooth frame pacing %s (each environment "
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
bool record_worker() { log_once(); return kWorkerHalf && device(); }
unsigned record_mt() { return kWorkerHalf && device() ? 127u : 0u; }
unsigned submission_slots() { return device() ? 8u : 0u; }
bool late_present_ack() { return device(); }
bool smooth_pacing_setting() {
    static const bool value = [] { std::string v; return !::hostui::get("vkSmoothPacing", v) || v != "0"; }();
    return value;
}
int display_pacing() { return device() && smooth_pacing_setting() ? 11 : 0; }
bool even_pacing() { return device(); }
}  // namespace gfxvk::smooth_defaults
#endif
