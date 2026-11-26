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
// True logic (decomp build only, WWHD_DECOMP: t60/t60.h): the game logic itself runs N steps per 30 Hz frame (N 2: 60,
// N 4: 120 steps a second), drawn at 30 N fps. The two choices come after the generated one (kTrue60, kTrue120), so
// the choices above keep their numbers; the normal build never lists them. Saved as the steps (t60Steps), not as an index.
inline constexpr const char* kTrueRateNames[] = {"60 fps (true logic)", "120 fps (true logic, experimental)"};
inline constexpr int kTrue60 = kRateCount, kTrue120 = kRateCount + 1, kRateCountTrue = kRateCount + 2;
// the steps per 30 Hz frame of a choice (1: not a true-logic choice), and the choice of N steps (-1: none)
inline int steps_of(int choice) { return choice == kTrue60 ? 2 : choice == kTrue120 ? 4 : 1; }
inline int true_choice(int steps) { return steps == 2 ? kTrue60 : steps == 4 ? kTrue120 : -1; }
// the choice shown: a true-logic one while N > 1 is chosen, else rate_choice's
inline int rate_choice_true(int mode, int fps, bool generated, int steps) {
    return steps > 1 && true_choice(steps) >= 0 ? true_choice(steps) : rate_choice(mode, fps, generated);
}
// the choices a menu lists, in order, into out (room for kRateCountTrue); returns how many. generated: the generated
// choice is offered (Vulkan); true_logic: the decomp build's two true-logic choices at the end
inline int rate_list(int* out, bool generated, bool true_logic) {
    int n = 0;
    for (int c = 0; c < kGenerated; c++) out[n++] = c;
    if (generated) out[n++] = kGenerated;
    if (true_logic) out[n++] = kTrue60, out[n++] = kTrue120;
    return n;
}
// a choice's place in such a list (0, the first, when it is not listed)
inline int rate_index(const int* list, int count, int choice) {
    for (int i = 0; i < count; i++)
        if (list[i] == choice) return i;
    return 0;
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
