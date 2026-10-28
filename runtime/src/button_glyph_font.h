// Compose shoulder captions from the player's actual HUD font; no font data is shipped.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace button_glyphs {

// The composed button atlas: 128x1280 RGBA, linear, in 20 rows of 64x64 cells. Column 0 is the
// framed/disc version (dialog icons), column 1 the bare one (HUD panes, which have their own
// button pictures). Rows: 0-3 L1 R1 L2 R2, 4-7 LB RB LT RT (for the game's L R ZL ZR),
// 8-11 the PlayStation shapes for A B X Y in colour, 12-15 the same shapes without colour,
// 16-18 A1 A2 A3 (the item buttons X Y R by the item ring's names, mods/item_ring.h), 19 unused.
constexpr int kAtlasWidth = 128, kAtlasHeight = 1280, kAtlasRows = 20;
constexpr int numbered_row(int shoulder) { return shoulder; }
constexpr int xbox_row(int shoulder) { return 4 + shoulder; }
constexpr int shape_row(int face, bool colour) { return (colour ? 8 : 12) + face; }
constexpr int item_name_row(int item_button) { return 16 + item_button; }  // 0 X (A1), 1 Y (A2), 2 R (A3)

// Native nw::font::Glyph records for L, R, 1, 2, B, T, A, 3 (0x18 bytes each). Empty means
// unsupported/not ready.
constexpr int kAtlasGlyphs = 8;
std::vector<uint8_t> button_atlas(const std::array<uint32_t, kAtlasGlyphs>& glyphs);

}  // namespace button_glyphs
