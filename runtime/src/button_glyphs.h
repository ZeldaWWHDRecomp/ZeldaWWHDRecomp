// Alternate button appearance (input_map::prompt_style): Xbox face colours/placement or
// PlayStation shapes, and L1/R1/L2/R2 or LB/RB/LT/RT shoulder labels. By label already binds
// Xbox A to game A: the letters must NOT be transposed. The same palette styles the dialog icon
// font and the HUD's button backgrounds; keyboard bindings are separate.
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
// The HUD's face letters: a one-letter text pane on its button picture (T_A_00 on P_A_00, ...).
inline int face_text(std::string_view name) {
    if (name == "T_A_00") return 0;
    if (name == "T_B_00") return 1;
    if (name == "T_X_00") return 2;
    if (name == "T_Y_00") return 3;
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

// A cached glyph quad's byte +0x2F uses only bit 0 (coverage glyph: an alpha-font letter that
// takes the text colours; the icon font's RGBA glyphs have it clear). The generated code
// resets that byte on every rebuild, and every reader masks out only its own bits (+0x2C 16-bit
// value, +0x2E page, bit 0 flag — see docs/button-glyphs.md). Record face identity in the spare
// high bits so it follows cached text and save-state restores without a host-side registry.
// Prefix D marks face icons; prefix E marks shoulder icons. Nothing is written until a restyled look
// has been enabled once in the session; that first activation rebuilds each cached text box once.
constexpr uint8_t mark(uint8_t flags, int face) {
    return face >= 0 && face < 4 && !(flags & 0xFE) ? uint8_t(0xD0 | (face << 1) | flags) : flags;
}
constexpr int marked_face(uint8_t flags) { return (flags & 0xF0) == 0xD0 ? (flags >> 1) & 3 : -1; }
constexpr uint8_t mark_shoulder(uint8_t flags, int shoulder) {
    return shoulder >= 0 && shoulder < 4 && !(flags & 0xFE) ? uint8_t(0xE0 | (shoulder << 1) | flags) : flags;
}
constexpr int marked_shoulder(uint8_t flags) { return (flags & 0xF0) == 0xE0 ? (flags >> 1) & 3 : -1; }
// Bit 3: the quad's vertices were generated with a restyled look. A change between two restyled
// looks (colours, shapes, shoulder names) is caught by the style signature instead, which
// rebuilds every cached text box once.
constexpr bool styled(uint8_t flags) { return flags & 8; }
constexpr uint8_t rendered(uint8_t flags, bool restyled) { return uint8_t((flags & ~8u) | (restyled ? 8 : 0)); }

// Atlas cell of a composed label/shape, as the quad's texture coordinates (+0x18..+0x24).
struct Cell { float u0, v0, u1, v1; };
constexpr Cell atlas_cell(int row, bool frame) {
    return {frame ? 0.0f : 0.5f, row / 20.0f, frame ? 0.5f : 1.0f, (row + 1) / 20.0f};  // (button_glyph_font.h kAtlasRows)
}

// Called before TextBox::DrawSelf checks its cached-vertex valid byte. A live style change must
// invalidate generated vertices as well as identify the face glyphs in the cached quad list.
void prepare_text_box(uint32_t text_box, Cpu* cpu = nullptr);
// Scope the HUD shoulder and face-letter panes while the existing text/RTL hook draws them.
void draw_text_box(Cpu* c, PpcFunc draw);

// Compose the HUD's face-button placement with the existing pane/aspect matrix calculation:
// each restyled face's complete group moves to the spot of the pad button bound to it
// (PromptStyle::spots; by label puts A south, B east, X west and Y north).
void calculate_pane(Cpu* c, PpcFunc calculate);
// Pane::Draw for the CommandGuide cluster (aspect.cpp's hook): with a restyled look its action words
// are drawn after its buttons, so moved groups never cover another button's word.
void draw_pane(Cpu* c, PpcFunc draw);
// Scope the displayed command-guide button set while calculating its layout.
void calculate_parts(Cpu* c, PpcFunc calculate);

// a save state was loaded: forget which text boxes were rebuilt for by label (savestate.cpp)
void ss_reset();

}  // namespace button_glyphs
