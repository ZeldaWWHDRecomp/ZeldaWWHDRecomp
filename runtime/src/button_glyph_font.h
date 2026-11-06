// Compose shoulder captions from the player's actual HUD font; no font data is shipped.
#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <span>

namespace button_glyphs {

// The composed button atlas: 128x2048 RGBA, linear, in 32 rows of 64x64 cells. Column 0 is the
// framed/disc version (dialog icons), column 1 the bare one (HUD panes, which have their own
// button pictures). Rows: 0-3 L1 R1 L2 R2, 4-7 LB RB LT RT (for the game's L R ZL ZR),
// 8-11 the PlayStation shapes for A B X Y in colour, 12-15 the same shapes without colour,
// 16-18 A1 A2 A3, 19 unused, 20-23 GameCube face buttons, 24-27 GameCube shoulders, 28 C-stick.
constexpr int kAtlasWidth = 128, kAtlasHeight = 2048, kAtlasRows = 32;
constexpr int numbered_row(int shoulder) { return shoulder; }
constexpr int xbox_row(int shoulder) { return 4 + shoulder; }
constexpr int shape_row(int face, bool colour) { return (colour ? 8 : 12) + face; }
constexpr int item_name_row(int item_button) { return 16 + item_button; }  // 0 X (A1), 1 Y (A2), 2 R (A3)
constexpr int gamecube_row(int face) { return 20 + face; }
constexpr int gamecube_shoulder_row(int shoulder) { return 24 + shoulder; }
constexpr int gamecube_cstick_row() { return 28; }
constexpr int gamecube_start_row() { return 29; }

// Native nw::font::Glyph records for L, R, 1, 2, B, T, A, 3 (0x18 bytes each). Empty means
// unsupported/not ready.
constexpr int kAtlasGlyphs = 8;
std::vector<uint8_t> button_atlas(const std::array<uint32_t, kAtlasGlyphs>& glyphs);

std::vector<uint8_t> keycap_pixels(std::span<const uint32_t> records);

}  // namespace button_glyphs
