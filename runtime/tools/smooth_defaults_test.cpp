// smooth_defaults (gfx/vulkan/smooth_defaults.h) as the normal build compiles it: every default is off on a desktop,
// and on the Steam Deck (Linux, SteamDeck=1) the whole set is on, as on Android (worker_half_for, the device rule
// with its inputs given): second-thread recording with MT 127, hot vsync waits of 8 ms with frame interpolation and
// none at 30 fps, 8 slots, late acknowledgment, pool reset thread, display pacing, even pacing; "Smooth frame pacing" off leaves only display pacing off; WWHD_VSYNC_HOT_WAIT_US
// wins in every mode; WWHD_VK_RECORD_WORKER wins over the saved setting, which wins over the device default
// (record_worker_choice.h). Modes: desktop (no SteamDeck variable), deck (SteamDeck=1: on only on Linux),
// deck-pacing-off (and the saved setting vkSmoothPacing=0).
// (the source itself, without the renderer sources' forced cemu_shim.h include)
#include "gfx/vulkan/smooth_defaults.cpp"
#include "gfx/vulkan/record_worker_choice.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
bool pacingOff = false;
}
namespace hostui {
bool get(const char* key, std::string& value) {
    if (!pacingOff || std::strcmp(key, "vkSmoothPacing")) return false;
    value = "0";
    return true;
}
}  // namespace hostui
namespace {
std::string logged;  // every formatted log line
}
void log_msg(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    std::printf("log: %s\n", buf);
    logged += buf;
    logged += '\n';
}

int main(int argc, char** argv) {
    using namespace gfxvk::smooth_defaults;
    const std::string mode = argc > 1 ? argv[1] : "desktop";
    pacingOff = mode == "deck-pacing-off";
#ifdef _WIN32
    _putenv_s("SteamDeck", mode == "desktop" ? "" : "1");
#else
    if (mode == "desktop") unsetenv("SteamDeck");
    else setenv("SteamDeck", "1", 1);
#endif
#if defined(__linux__) && !defined(__ANDROID__)
    const bool deck = mode != "desktop";
#else
    const bool deck = false;  // (the variable means nothing elsewhere)
#endif
    assert(steam_deck() == deck);
    assert(device() == deck);
    // the worker half: Android and the Steam Deck (the rule with its inputs given, so every host checks it)
    assert(worker_half_for(true, false));    // Android
    assert(worker_half_for(false, true));    // Steam Deck (again since 2026-11-19, issue #178)
    assert(!worker_half_for(false, false));  // desktop
    assert(record_worker() == deck);
    assert(record_mt() == (deck ? 127u : 0u));
    assert(vsync_hot_wait_us(false) == (deck ? 8000 : 0));  // frame interpolation (40-240 fps)
    assert(vsync_hot_wait_us(true) == 0);                    // 30 fps
    if (deck) assert(logged.find("Steam Deck, ") != std::string::npos &&
                     logged.find("graphics on a second thread") != std::string::npos &&
                     logged.find("issue #178") == std::string::npos);
    else assert(logged.empty());
    // "Record graphics on a second thread": WWHD_VK_RECORD_WORKER wins, then the saved setting, then the default
    const bool deviceDefault = record_worker();
    assert(gfxvk::record_worker_choice(nullptr, nullptr, deviceDefault) == deviceDefault);
    assert(gfxvk::record_worker_choice(nullptr, nullptr, true));  // (Android's default)
    assert(gfxvk::record_worker_choice(nullptr, "1", deviceDefault));
    assert(!gfxvk::record_worker_choice(nullptr, "0", true));
    assert(gfxvk::record_worker_choice("1", nullptr, deviceDefault));
    assert(gfxvk::record_worker_choice("1", "0", deviceDefault));
    assert(!gfxvk::record_worker_choice("0", "1", true));
    assert(!gfxvk::record_worker_choice("", "1", true));  // (set: any value but exactly 1 is off)
    assert(!gfxvk::record_worker_choice("yes", nullptr, true));
    // GX2WaitForVsync's limit: the default per mode, and WWHD_VSYNC_HOT_WAIT_US wins in every mode
#ifdef _WIN32
    _putenv_s("WWHD_VSYNC_HOT_WAIT_US", "");
#else
    unsetenv("WWHD_VSYNC_HOT_WAIT_US");
#endif
    assert(vsync_hot_wait_applied_us(std::getenv("WWHD_VSYNC_HOT_WAIT_US"), false) == (deck ? 8000 : 0));
    assert(vsync_hot_wait_applied_us(std::getenv("WWHD_VSYNC_HOT_WAIT_US"), true) == 0);
#ifdef _WIN32
    _putenv_s("WWHD_VSYNC_HOT_WAIT_US", "3000");
#else
    setenv("WWHD_VSYNC_HOT_WAIT_US", "3000", 1);
#endif
    assert(vsync_hot_wait_applied_us(std::getenv("WWHD_VSYNC_HOT_WAIT_US"), true) == 3000);
    assert(vsync_hot_wait_applied_us(std::getenv("WWHD_VSYNC_HOT_WAIT_US"), false) == 3000);
#ifdef _WIN32
    _putenv_s("WWHD_VSYNC_HOT_WAIT_US", "0");
#else
    setenv("WWHD_VSYNC_HOT_WAIT_US", "0", 1);
#endif
    assert(vsync_hot_wait_applied_us(std::getenv("WWHD_VSYNC_HOT_WAIT_US"), false) == 0);
    assert(vsync_hot_wait_applied_us("8000", false) == 8000);  // a desktop can still turn them on
    assert(submission_slots() == (deck ? 8u : 0u));
    assert(late_present_ack() == deck);
    assert(async_pool_reset() == (deck ? 1 : 0));
    assert(even_pacing() == deck);
    assert(smooth_pacing_setting() == !pacingOff);
    assert(display_pacing() == (deck && !pacingOff ? 11 : 0));
    std::printf("smooth defaults %s: device %d, worker %d, mt %u, hot waits %d us, slots %u, ack %d, pool reset %d, pacing %d\n",
                mode.c_str(), int(device()), int(record_worker()), record_mt(), vsync_hot_wait_us(false), submission_slots(),
                int(late_present_ack()), async_pool_reset(), display_pacing());
    return 0;
}
