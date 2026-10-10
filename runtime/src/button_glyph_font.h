// Compose shoulder captions from the player's actual HUD font; no font data is shipped.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace button_glyphs {

// Native nw::font::Glyph records for L, R, 1, 2 (0x18 bytes each). Output is a linear
// 128x256 RGBA atlas, matching the font-sheet binding path. Empty means unsupported/not ready.
std::vector<uint8_t> shoulder_font_atlas(const std::array<uint32_t, 4>& glyphs);

}  // namespace button_glyphs
