// The Graphics tab of the item menu's settings page (game_settings.cpp register_graphics): how its choices map
// onto the Settings window's graphics options (overlay.cpp tab_graphics), as pure code for the tests.
#pragma once
#include <cmath>
#include <cstddef>

namespace game_settings_graphics {

// Internal resolution: the Settings window's four scales
inline constexpr float kScales[] = {1.0f, 1.5f, 2.0f, 3.0f};
inline constexpr const char* kScaleNames[] = {"1x (1280x720)", "1.5x (1920x1080)", "2x (2560x1440)", "3x (3840x2160)"};
inline constexpr size_t kScaleCount = sizeof kScales / sizeof kScales[0];
// the choice for a scale: the nearest one (a scale set some other way shows as its neighbour)
inline int scale_choice(float scale) {
    int best = 0;
    for (size_t i = 1; i < kScaleCount; i++)
        if (std::fabs(kScales[i] - scale) < std::fabs(kScales[best] - scale)) best = int(i);
    return best;
}

// Frame rate: interp::mode() 0 (30 fps), 1 (frame interpolation at interp::fps()), and 60 fps (generated)
// (framegen.h: mode 0 with frame generation chosen; offered on Vulkan only, as the Settings window offers
// it: the last choice, left out of the list elsewhere)
inline constexpr const char* kRateNames[] = {"30 fps (original)", "40 fps", "60 fps", "120 fps", "240 fps",
                                             "60 fps (generated, experimental)"};
inline constexpr int kRates[] = {30, 40, 60, 120, 240};  // the interpolated choices' fps (index 1..4)
inline constexpr int kRateCount = 6, kGenerated = 5;
// generated: frame generation chosen and available (it runs at mode 0 only)
inline int rate_choice(int mode, int fps, bool generated = false) {
    if (mode != 1) return generated ? kGenerated : 0;
    for (int i = 1; i < 5; i++)
        if (kRates[i] == fps) return i;
    return 2;  // (an fps the menu doesn't offer: shown as 60)
}
// fps 0: keep the interpolation's; generated: frame generation on (else off: every other choice turns it off)
struct Rate { int mode, fps; bool generated; };
inline Rate rate_of(int choice) {
    if (choice <= 0 || choice >= kRateCount) return {0, 0, false};
    if (choice == kGenerated) return {0, 0, true};
    return {1, kRates[choice], false};
}

// Dynamic resolution: the lowest factor (0: off), as the Settings window offers it
inline constexpr float kDynMins[] = {0.0f, 0.75f, 0.5f};
inline constexpr const char* kDynNames[] = {"Off", "Down to 75%", "Down to 50%"};
inline int dyn_choice(float min) {
    for (int i = 0; i < 3; i++)
        if (std::fabs(kDynMins[i] - min) < 0.01f) return i;
    return min <= 0 ? 0 : min < 0.625f ? 2 : 1;
}

// Clouds: render::cloud_style() 0..2
inline constexpr const char* kCloudNames[] = {"Original", "Toon (Lite)", "Toon (High)"};

}  // namespace game_settings_graphics
