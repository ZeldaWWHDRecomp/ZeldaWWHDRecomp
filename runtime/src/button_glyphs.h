// Alternate button appearance: face colours/placement and L1/R1/L2/R2 shoulder labels.
// By label already binds Xbox A to game A: the letters must NOT be transposed. The same palette
// styles the dialog icon font and the HUD's button backgrounds; keyboard bindings are separate.
#pragma once
#include <cstdint>
#include <string_view>
#include "ppc.h"

namespace button_glyphs {

// Face order: A, B, X, Y. -1 means this is not a face-button picture.
constexpr int face_character(uint32_t code) { return code >= 0xE000 && code <= 0xE003 ? int(code - 0xE000) : -1; }
constexpr int shoulder_character(uint32_t code) { return code >= 0xE083 && code <= 0xE086 ? int(code - 0xE083) : -1; }
inline int shoulder_text(std::string_view name) {
    if (name == "T_L_00") return 0;
    if (name == "T_R_00") return 1;
    if (name == "T_ZL_00") return 2;
    if (name == "T_ZR_00") return 3;
    return -1;
}
inline int face_picture(std::string_view name) {
    if (name == "P_A_00" || name == "P_ASpecial_00") return 0;
    if (name == "P_B_00") return 1;
    if (name == "P_X_00") return 2;
    if (name == "P_Y_00") return 3;
    return -1;
}

constexpr uint32_t colour(int face) {
    constexpr uint32_t colours[] = {0x58BE36FF, 0xE34B49FF, 0x3B91E5FF, 0xEBC52EFF};
    return face >= 0 && face < 4 ? colours[face] : 0xFFFFFFFF;
}

// Packed RGBA, as the game keeps vertex colours. Multiply RGB to retain authored shading and
// animation; keep alpha exactly, including fully transparent glyphs and HUD fade-in/out.
constexpr uint32_t tint(uint32_t rgba, int face) {
    const uint32_t rgb = colour(face);
    uint32_t out = rgba & 0xFF;
    for (int shift = 24; shift > 0; shift -= 8) out |= (((rgba >> shift) & 0xFF) * ((rgb >> shift) & 0xFF) / 255) << shift;
    return out;
}

// A cached glyph quad's byte +0x2F uses only bit 0 (colour-font flag). The writer resets that
// byte on every new quad; record its face identity in unused bits so it follows cached text and
// save-state restores without a host-side map of guest heap addresses. Prefix D marks face
// icons; prefix E marks shoulder icons. The list builder writes only bit 0 and the glyph-list
// batcher inspects only bit 0.
constexpr uint8_t mark(uint8_t flags, int face) {
    return face >= 0 && face < 4 && !(flags & 0xFE) ? uint8_t(0xD0 | (face << 1) | flags) : flags;
}
constexpr int marked_face(uint8_t flags) { return (flags & 0xF0) == 0xD0 ? (flags >> 1) & 3 : -1; }
constexpr uint8_t mark_shoulder(uint8_t flags, int shoulder) {
    return shoulder >= 0 && shoulder < 4 && !(flags & 0xFE) ? uint8_t(0xE0 | (shoulder << 1) | flags) : flags;
}
constexpr int marked_shoulder(uint8_t flags) { return (flags & 0xF0) == 0xE0 ? (flags >> 1) & 3 : -1; }
constexpr bool styled(uint8_t flags) { return flags & 8; }
constexpr uint8_t rendered(uint8_t flags, bool xbox) { return uint8_t((flags & ~8u) | (xbox ? 8 : 0)); }

// Called before TextBox::DrawSelf checks its cached-vertex valid byte. A live style change must
// invalidate generated vertices as well as identify the face glyphs in the cached quad list.
void prepare_text_box(uint32_t text_box, Cpu* cpu = nullptr);
// Scope the HUD shoulder-letter panes while the existing text/RTL hook draws them.
void draw_text_box(Cpu* c, PpcFunc draw);

// Compose the HUD's face-button placement with the existing pane/aspect matrix calculation:
// by label puts A south, B east, X west and Y north, moving their complete groups together.
void calculate_pane(Cpu* c, PpcFunc calculate);

}  // namespace button_glyphs
