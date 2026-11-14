// smooth_defaults (gfx/vulkan/smooth_defaults.h) as the normal build compiles it: every default is off on a desktop,
// and on the Steam Deck (Linux, SteamDeck=1) the whole set is on (second-thread recording with MT 127, hot vsync waits
// of 8 ms, 8 slots, late acknowledgment, pool reset thread, display pacing, even pacing); "Smooth frame pacing" off
// leaves only display pacing off. Modes: desktop (no SteamDeck variable), deck (SteamDeck=1: on only on Linux), deck-pacing-off (and the saved
// setting vkSmoothPacing=0).
// (the source itself, without the renderer sources' forced cemu_shim.h include)
#include "gfx/vulkan/smooth_defaults.cpp"
#include <cassert>
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
void log_msg(const char* fmt, ...) { std::printf("log: %s\n", fmt); }

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
    assert(record_worker() == deck);
    assert(record_mt() == (deck ? 127u : 0u));
    assert(vsync_hot_wait_us() == (deck ? 8000 : 0));
    assert(submission_slots() == (deck ? 8u : 0u));
    assert(late_present_ack() == deck);
    assert(async_pool_reset() == (deck ? 1 : 0));
    assert(even_pacing() == deck);
    assert(smooth_pacing_setting() == !pacingOff);
    assert(display_pacing() == (deck && !pacingOff ? 11 : 0));
    std::printf("smooth defaults %s: device %d, worker %d, mt %u, hot waits %d us, slots %u, ack %d, pool reset %d, pacing %d\n",
                mode.c_str(), int(device()), int(record_worker()), record_mt(), vsync_hot_wait_us(), submission_slots(),
                int(late_present_ack()), async_pool_reset(), display_pacing());
    return 0;
}
