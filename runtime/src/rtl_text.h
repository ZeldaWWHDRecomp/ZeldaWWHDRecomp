// Right-to-left text (Arabic, Hebrew) for fan translations: the text logic, independent of the game.
// rtl_text_hooks.cpp applies it to the game's text writer; docs/rtl-text.md describes the whole feature.
//
// * Arabic shaping (Unicode ArabicShaping.txt joining types, the Arabic Presentation Forms-A/B code
//   points of each letter's isolated, final, initial and medial form, lam-alef ligatures). Only forms
//   the game font actually has are used; a missing form falls back to the base letter.
// * Bidirectional order: a compact form of the Unicode Bidirectional Algorithm (UAX #9) without
//   explicit embeddings: weak types (W1-W7), neutrals (N1-N2), implicit levels (I1-I2), trailing
//   whitespace (L1), reordering of each line (L2) and mirrored brackets (L4). The paragraph level is a
//   higher-level choice (HL1): right-to-left when the message has any right-to-left letter.
//
// Text here is UTF-16 code units as the game keeps them (the font maps UTF-16 units, no surrogates).
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rtl {

// ---- characters ----

enum class Joining : uint8_t { None, Right, Dual, Transparent, Causing };
Joining joining_type(uint32_t cp);

enum class Form : uint8_t { Isolated, Final, Initial, Medial };
// The presentation form code point of an Arabic letter, 0 when Unicode has none (or no letter).
uint32_t presentation_form(uint32_t cp, Form form);
// Lam followed by alef (with madda, hamza above/below, or plain): the ligature, 0 if cp is no alef.
uint32_t lam_alef(uint32_t alef, bool final_form);

// A right-to-left letter (Arabic or Hebrew script letter, or a presentation form of one).
bool is_rtl_letter(uint32_t cp);
// A combining mark of a right-to-left script (Arabic harakat, Hebrew points).
bool is_rtl_mark(uint32_t cp);

enum class Bidi : uint8_t { L, R, AL, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON };
Bidi bidi_class(uint32_t cp);
// The mirrored glyph of a bracket-like character (L4), or cp itself.
uint32_t mirror(uint32_t cp);

// ---- shaping ----

// Which code points the font can draw.
using HasGlyph = std::function<bool(uint32_t)>;

struct Shaped {
    uint32_t code;  // the code to draw
    bool skip;      // drawn by the previous character (lam-alef ligature) or not drawable (a mark)
};

// The form of the character at i of text[0..n): its neighbours are the nearest non-transparent
// characters (marks are skipped); any other character (space, digit, control code) breaks the
// joining. `at(i)` returns the code unit at i (only called for 0 <= i < n).
Shaped shape_at(const std::function<uint32_t(size_t)>& at, size_t i, size_t n, const HasGlyph& has);
// Convenience for tests: the drawn codes of a plain string (skipped characters left out).
std::vector<uint32_t> shape(const std::u16string& s, const HasGlyph& has);

// ---- bidirectional levels ----

// Resolved embedding levels of one paragraph (classes as from bidi_class; BN entries keep the level of
// the previous character and take no part). para_level 0 = left to right, 1 = right to left.
std::vector<uint8_t> resolve_levels(const std::vector<Bidi>& cls, int para_level);

// Visual order of one line: the indices of items (logical order) from left to right. L1: trailing
// whitespace (ws[i]) takes the paragraph level; L2: reverse every run at or above each odd level.
std::vector<size_t> visual_order(std::vector<uint8_t> levels, const std::vector<bool>& ws, int para_level);

// ---- a message ----

// One message (or any text the writer prints): per code unit, what to draw and its level.
// Control sequences of the game's message format stay as they are: 0x0E group type size params...
// (tag; group 3 draws a button icon and counts as a neutral character), 0x0F group type (end tag);
// other codes below 0x20 are line breaks (0x0A) or ignored.
struct Plan {
    bool rtl = false;              // the paragraph level is right to left (the text has an RTL letter)
    std::vector<uint32_t> code;    // per code unit: the code to draw (shaped / mirrored), else the unit
    std::vector<uint8_t> skip;     // per code unit: not drawn
    std::vector<uint8_t> level;    // per code unit: resolved bidi level (tags: the level of their icon)
    std::vector<uint8_t> ws;       // per code unit: whitespace for L1
};
Plan plan(const uint16_t* text, size_t n, const HasGlyph& has);

// ---- a line of drawn glyphs ----

struct Glyph {
    float pen;      // pen position before the glyph (logical layout, left to right)
    float advance;  // advance of the glyph
    uint8_t level;  // bidi level
    bool ws;        // whitespace
};
// New pen positions (same order as `line`) for the glyphs of one line in right-to-left order: the
// visual order of their levels, laid out over the same span the logical layout used, keeping the
// spacing between logical neighbours.
std::vector<float> reorder_line(const std::vector<Glyph>& line, int para_level);

}  // namespace rtl

// Runtime glue (rtl_text_hooks.cpp): hle/fs.cpp reports each 2D language pack (permanent_2d_*.pack)
// the game opens; right-to-left text turns on when its message font has Arabic or Hebrew letters
// (WWHD_RTL=0 keeps it off, WWHD_RTL=1 forces it on).
namespace rtl_text {
void language_pack_opened(const std::string& host_path);
}
