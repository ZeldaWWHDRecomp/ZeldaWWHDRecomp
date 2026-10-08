// Tests the right-to-left text logic (runtime/src/rtl_text.h): Arabic joining forms with a font that
// has every form and with one that, like the Arabic fan translation's, lacks the lam-alef ligatures
// and the harakat; bidi levels and visual order (numbers, Latin words, brackets, button icon tags,
// trailing spaces); the pen positions of a reordered line; messages without RTL letters untouched.
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "rtl_text.h"

using rtl::Form;

namespace {

bool full_font(uint32_t cp) { return cp != 0x064E; }  // everything but a fatha
// like the fan translation's font: base letters, U+FE80..U+FEF4, alef maksura initial/medial; no
// lam-alef ligatures (U+FEF5..U+FEFC), no harakat
bool small_font(uint32_t cp) {
    if (cp >= 0xFEF5 && cp <= 0xFEFC) return false;
    if (cp >= 0x064B && cp <= 0x0652) return false;
    return true;
}

std::vector<uint32_t> V(std::initializer_list<uint32_t> l) { return l; }

// the drawn codes of a message in visual order, line by line ('\n' kept), as the hooks lay them out
std::u16string visual(const std::u16string& s, const rtl::HasGlyph& has = full_font) {
    const rtl::Plan p = rtl::plan((const uint16_t*)s.data(), s.size(), has);
    std::u16string out;
    std::vector<size_t> line;
    auto flush = [&] {
        std::vector<uint8_t> lv;
        std::vector<bool> ws;
        for (size_t i : line) {
            lv.push_back(p.level[i]);
            ws.push_back(p.ws[i]);
        }
        for (size_t k : rtl::visual_order(lv, ws, p.rtl ? 1 : 0)) {
            const size_t i = line[k];
            if (s[i] == 0x0E) out += u"[" + std::u16string(1, (char16_t)('0' + s[i + 2])) + u"]";
            else out += (char16_t)p.code[i];
        }
        line.clear();
    };
    for (size_t i = 0; i < s.size();) {
        if (s[i] == 0x0E) {  // a tag: only button icons (group 3) draw something
            if (s[i + 1] == 3) line.push_back(i);
            i += 4 + s[i + 3] / 2;
            continue;
        }
        if (s[i] == 0x0F) {
            i += 3;
            continue;
        }
        if (s[i] == u'\n') {
            flush();
            out += u'\n';
        } else if (!p.skip[i]) {
            line.push_back(i);
        }
        i++;
    }
    flush();
    return out;
}

void forms() {
    assert(rtl::joining_type(0x0628) == rtl::Joining::Dual);
    assert(rtl::joining_type(0x0627) == rtl::Joining::Right);
    assert(rtl::joining_type(0x0621) == rtl::Joining::None);
    assert(rtl::joining_type(0x064E) == rtl::Joining::Transparent);
    assert(rtl::joining_type(0x0640) == rtl::Joining::Causing);
    assert(rtl::presentation_form(0x0628, Form::Isolated) == 0xFE8F);
    assert(rtl::presentation_form(0x0628, Form::Medial) == 0xFE92);
    assert(rtl::presentation_form(0x0627, Form::Final) == 0xFE8E);
    assert(rtl::presentation_form(0x0627, Form::Initial) == 0);
    assert(rtl::presentation_form(0x064A, Form::Medial) == 0xFEF4);
    assert(rtl::presentation_form(0x0649, Form::Medial) == 0xFBE9);
    assert(rtl::presentation_form(0x0671, Form::Final) == 0xFB51);
    assert(rtl::presentation_form(0x06CC, Form::Initial) == 0xFBFE);
    assert(rtl::lam_alef(0x0627, true) == 0xFEFC && rtl::lam_alef(0x0622, false) == 0xFEF5 && !rtl::lam_alef(0x0628, false));
}

void shaping() {
    // beh yeh teh: initial, medial, final
    assert(rtl::shape(u"بيت", full_font) == V({0xFE91, 0xFEF4, 0xFE96}));
    // a right-joining letter ends the joining: dal then beh -> isolated dal, isolated beh
    assert(rtl::shape(u"دب", full_font) == V({0xFEA9, 0xFE8F}));
    // seen lam alef meem: the lam-alef ligature (final, after seen) when the font has it ...
    assert(rtl::shape(u"سلام", full_font) == V({0xFEB3, 0xFEFC, 0xFEE1}));
    // ... else lam (medial) and alef (final) as two joined glyphs
    assert(rtl::shape(u"سلام", small_font) == V({0xFEB3, 0xFEE0, 0xFE8E, 0xFEE1}));
    // isolated lam-alef at the start of a word
    assert(rtl::shape(u"لا", full_font) == V({0xFEFB}));
    // a haraka the font lacks is not drawn and does not break the joining
    assert(rtl::shape(u"بَت", small_font) == V({0xFE91, 0xFE96}));
    assert(rtl::shape(u"بِت", full_font) == V({0xFE91, 0x0650, 0xFE96}));
    // spaces, digits and Latin letters break the joining
    assert(rtl::shape(u"ب ب", full_font) == V({0xFE8F, ' ', 0xFE8F}));
    assert(rtl::shape(u"ب1ب", full_font) == V({0xFE8F, '1', 0xFE8F}));
    // tatweel joins on both sides
    assert(rtl::shape(u"بـب", full_font) == V({0xFE91, 0x0640, 0xFE90}));
    // a form the font lacks falls back: medial -> final -> isolated -> the letter
    auto no_medial = [](uint32_t cp) { return cp != 0xFE92; };
    assert(rtl::shape(u"ببب", no_medial) == V({0xFE91, 0xFE90, 0xFE90}));
    auto none = [](uint32_t cp) { return cp < 0xFB00; };
    assert(rtl::shape(u"بب", none) == V({0x0628, 0x0628}));
    // Hebrew has no joining
    assert(rtl::shape(u"שלום", full_font) == V({0x05E9, 0x05DC, 0x05D5, 0x05DD}));
}

void bidi() {
    // Hebrew letters (no shaping) keep the expected strings readable: alef bet gimel dalet he vav
    const std::u16string A = u"א", B = u"ב", G = u"ג", D = u"ד", H = u"ה";
    // right to left words
    assert(visual(A + B + u" " + G + D) == D + G + u" " + B + A);
    // numbers stay left to right where they are
    assert(visual(A + B + u" 123 " + G + D) == D + G + u" 123 " + B + A);
    assert(visual(A + u" 1,234.5 " + B) == B + u" 1,234.5 " + A);
    assert(visual(A + u" 50% " + B) == B + u" 50% " + A);
    // a Latin word in the middle, and two Latin words with a space between them
    assert(visual(A + B + u" Link " + G) == G + u" Link " + B + A);
    assert(visual(A + u" Link Hero " + B) == B + u" Link Hero " + A);
    // a Latin word at the start of a right-to-left line
    assert(visual(u"Link " + A + B) == B + A + u" Link");
    // brackets are mirrored
    assert(visual(A + u"(" + B + u")") == u"(" + B + u")" + A);
    // the game fonts' decorative brackets around place names are mirrored like brackets
    assert(visual(u"\uE0A0 " + A + B + u" \uE0A1") == u"\uE0A0 " + B + A + u" \uE0A1");
    // punctuation at the end goes to the left
    assert(visual(A + B + u"!") == u"!" + B + A);
    // lines are ordered on their own
    assert(visual(A + B + u"\n" + G + D) == B + A + u"\n" + D + G);
    // a button icon (tag group 3) between words stays with them
    std::u16string icon = u"\x0E\x03\x05";
    icon += (char16_t)2;
    icon += u"\x01";
    assert(visual(A + B + u" " + icon + u" " + G) == G + u" [5] " + B + A);
    // a colour tag inside a word is no glyph
    std::u16string colour = u"\x0E";
    colour += u'\0';
    colour += u"\x03";
    colour += (char16_t)4;
    colour += u"\xFFFF\xFFFF";
    std::u16string end_tag = u"\x0F";
    end_tag += u'\0';
    end_tag += u"\x03";
    assert(visual(A + colour + B + end_tag + u" " + G) == G + u" " + B + A);
    // Arabic: shaped, then reversed; Arabic numbers after Arabic letters stay left to right
    assert(visual(u"بت 12", small_font) == u"12 ﺖﺑ");
    // trailing spaces go to the end of the line (L1): to the left in right to left lines
    {
        std::vector<uint8_t> lv = {1, 1, 1};
        assert((rtl::visual_order(lv, {false, false, true}, 1) == std::vector<size_t>{2, 1, 0}));
        lv = {2, 2, 1};
        assert((rtl::visual_order(lv, {false, false, true}, 1) == std::vector<size_t>{2, 0, 1}));
    }
    // text without right-to-left letters: no plan, the codes untouched
    {
        const std::u16string s = u"Hello (world) 12";
        const rtl::Plan p = rtl::plan((const uint16_t*)s.data(), s.size(), full_font);
        assert(!p.rtl);
        for (size_t i = 0; i < s.size(); i++) assert(p.code[i] == s[i] && !p.skip[i]);
    }
    // levels: L in R, numbers after Arabic letters (AN), European numbers after Hebrew (EN)
    {
        using rtl::Bidi;
        assert((rtl::resolve_levels({Bidi::R, Bidi::WS, Bidi::L, Bidi::WS, Bidi::R}, 1) ==
                std::vector<uint8_t>{1, 1, 2, 1, 1}));
        assert((rtl::resolve_levels({Bidi::AL, Bidi::EN, Bidi::CS, Bidi::EN}, 1) == std::vector<uint8_t>{1, 2, 2, 2}));
        assert((rtl::resolve_levels({Bidi::L, Bidi::WS, Bidi::R}, 0) == std::vector<uint8_t>{0, 0, 1}));
        assert(rtl::bidi_class(0xE081) == Bidi::ON && rtl::bidi_class(0x0627) == Bidi::AL && rtl::bidi_class(0x05D0) == Bidi::R);
    }
}

void positions() {
    // three right-to-left glyphs, advances 10, 20, 30, one unit of letter spacing between them
    std::vector<rtl::Glyph> g = {{0, 10, 1, false}, {11, 20, 1, false}, {32, 30, 1, false}};
    std::vector<float> x = rtl::reorder_line(g, 1);
    assert(x[2] == 0 && x[1] == 31 && x[0] == 52);
    // a left-to-right number (level 2) inside: its glyphs keep their order
    g = {{0, 10, 1, false}, {10, 5, 2, false}, {15, 5, 2, false}, {20, 10, 1, false}};
    x = rtl::reorder_line(g, 1);
    assert(x[3] == 0 && x[1] == 10 && x[2] == 15 && x[0] == 20);
    // a line that starts further right keeps its span
    g = {{100, 10, 1, false}, {110, 10, 1, false}};
    x = rtl::reorder_line(g, 1);
    assert(x[1] == 100 && x[0] == 110);
}

}  // namespace

int main() {
    forms();
    shaping();
    bidi();
    positions();
    puts("rtl_text_test: Arabic forms, shaping with full and partial fonts, bidi order, line positions passed");
}
