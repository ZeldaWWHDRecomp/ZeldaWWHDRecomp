// Original button frame only. Lettering is sampled at run time from the game's HUD font.
#pragma once
#include <cstdint>

namespace button_glyphs {

constexpr uint32_t shoulder_background(int x, int y) {
    if (x < 2 || x > 61 || y < 10 || y > 53) return 0;
    const int cx = x < 8 ? 8 : x > 55 ? 55 : x;
    const int cy = y < 16 ? 16 : y > 47 ? 47 : y;
    const int distance = (x-cx)*(x-cx) + (y-cy)*(y-cy);
    if (distance > 36) return 0;
    const uint32_t shade = distance > 16 || x < 4 || x > 59 || y < 12 || y > 51 ? 112 : 218 + (53-y)/3;
    return (shade << 24) | (shade << 16) | (shade << 8) | 255;
}

}  // namespace button_glyphs
