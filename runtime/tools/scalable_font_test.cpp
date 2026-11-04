// Tests of the scalable font stand-in (runtime/src/scalable_font.h): every entry point and slot against
// a fake guest memory and a fake bitmap font.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "scalable_font.h"

using namespace scalable_font;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

struct FakeMem final : Mem {
    std::vector<uint8_t> b = std::vector<uint8_t>(0x10000, 0xCD);
    uint8_t ld8(uint32_t ea) override { return b.at(ea); }
    void st8(uint32_t ea, uint8_t v) override { b.at(ea) = v; }
    uint32_t ld32(uint32_t ea) override {
        return (uint32_t)b.at(ea) << 24 | (uint32_t)b.at(ea + 1) << 16 | (uint32_t)b.at(ea + 2) << 8 | b.at(ea + 3);
    }
    void st32(uint32_t ea, uint32_t v) override {
        b.at(ea) = v >> 24; b.at(ea + 1) = v >> 16; b.at(ea + 2) = v >> 8; b.at(ea + 3) = v;
    }
};

// CKingMain's FINF/TGLP numbers (width 62, height 76, ascent 59, line feed 76, line top 58)
struct FakeFont final : Fallback {
    FakeMem* mem;
    uint32_t addr = 0;  // 0: not loaded
    int metrics_calls = 0;
    std::map<uint32_t, int32_t> widths{{'A', 40}, {'i', 18}, {'%', 50}, {'?', 33}};
    std::map<std::pair<uint32_t, uint32_t>, int32_t> kern{{{'A', 'V'}, -4}};
    uint32_t kind_value = 1;
    explicit FakeFont(FakeMem* m) : mem(m) {}
    uint32_t id() override { return addr; }
    Metrics metrics() override {
        metrics_calls++;
        Metrics m;
        m.width = 62; m.height = 76; m.ascent = 59; m.line_feed = 76; m.line_top = 58;
        m.kerning = 1; m.sheet_flip = 1;
        return m;
    }
    // a character the font lacks gets the replacement character's width ('?')
    int32_t advance(uint32_t ch) override { auto it = widths.find(ch); return it != widths.end() ? it->second : widths['?']; }
    void glyph(uint32_t record, uint32_t ch) override {
        const uint32_t c = widths.count(ch) ? ch : '?';
        mem->st32(record + 0x00, 0x20000000);        // image
        mem->st8(record + 0x04, 1);                  // left
        mem->st8(record + 0x05, (uint8_t)widths[c]); // width
        mem->st8(record + 0x06, (uint8_t)widths[c]); // advance
        mem->st8(record + 0x07, 74);                 // height (cell)
        mem->st32(record + 0x08, 0x04000400);        // sheet 1024 x 1024
        mem->st32(record + 0x0C, c << 16 | 7);       // x, y (the character, for the test)
        mem->st32(record + 0x10, 0x000C0000);        // format 12, flags, sheet
        mem->st32(record + 0x14, 0x30000000);        // texture: the bitmap font's sheet
    }
    int32_t kerning(uint32_t a, uint32_t b) override { auto it = kern.find({a, b}); return it != kern.end() ? it->second : 0; }
    uint32_t kind() override { return kind_value; }
};

constexpr uint32_t kMgr = 0x1000, kFont = kMgr + 0x3C, kCacheObj = kMgr + 0x64, kArg = 0x3000, kRec = 0x3100;
constexpr uint32_t kVt = 0x101880C0;

// the game's sequence (d/hd_scalable_font_mgr): construct both, defaults, then its settings, initialize
void boot(FakeMem& m, FakeFont& f) {
    forget();
    // FontInterface::create (the layout library): kerningEnabled 1, sheetFlip 1
    m.st8(kFont + kKerning, 1);
    m.st8(kFont + kSheetFlip, 1);
    construct(m, kFont, kVt);
    cache_construct(m, kCacheObj);
    cache_arg_defaults(m, kArg);
    font_arg_defaults(m, kArg + 0x40);
    m.st32(kArg + 0x40 + 0x00, kCacheObj);
    m.st32(kArg + 0x40 + 0x04, 25);
    m.st8(kArg + 0x40 + 0x0A, 0);
    m.st8(kArg + 0x40 + 0x0B, 1);
    initialize(m, kFont, kArg + 0x40, f);
}

void test_scale() {
    CHECK(scaled(0) == 0);
    CHECK(scaled(62) == 62);
    CHECK(scaled(-4) == -4);
}

void test_construct() {
    FakeMem m;
    FakeFont f(&m);
    forget();
    construct(m, kFont, kVt);
    CHECK(m.ld32(kFont + kVptr) == kVt);
    CHECK(m.ld32(kFont + kCache) == 0);
    CHECK(m.ld32(kFont + kWidth) == 0x28);
    CHECK(m.ld32(kFont + kHeight) == 0x32);
    CHECK(m.ld32(kFont + kAscent) == 0x28);
    CHECK(m.ld32(kFont + kLineTop) == 0x28);
    CHECK(m.ld32(kFont + kLineFeed) == 0x32);
    CHECK(m.ld32(kFont + kReplacement) == '?');
    cache_construct(m, kCacheObj);
    bool zero = true;
    for (uint32_t i = 0; i < kCacheSize; i++) zero &= m.ld8(kCacheObj + i) == 0;
    CHECK(zero);
    CHECK(m.ld8(kCacheObj + kCacheSize) == 0xCD);  // nothing past the cache
}

void test_arg_defaults() {
    FakeMem m;
    cache_arg_defaults(m, kArg);
    CHECK(m.ld32(kArg + 0x00) == 0x400);
    CHECK(m.ld32(kArg + 0x04) == 0x400);
    CHECK(m.ld32(kArg + 0x08) == 0);
    CHECK(m.ld32(kArg + 0x20) == 1);
    CHECK(m.ld32(kArg + 0x24) == 0x80000);
    CHECK(m.ld32(kArg + 0x28) == 0xC800);
    CHECK(m.ld32(kArg + 0x2C) == 0x400);
    CHECK(m.ld8(kArg + 0x30) == 0);
    CHECK(m.ld8(kArg + kCacheArgSize) == 0xCD);
    font_arg_defaults(m, kArg + 0x40);
    CHECK(m.ld32(kArg + 0x40) == 0);
    CHECK(m.ld32(kArg + 0x44) == 0x28);
    CHECK(m.ld32(kArg + 0x48) == 0);
    CHECK(m.ld8(kArg + 0x40 + kFontArgSize) == 0xCD);
}

// before the game has loaded its main font: the requested size everywhere, never 0x80000000
void test_unloaded() {
    FakeMem m;
    FakeFont f(&m);
    boot(m, f);
    CHECK(m.ld32(kFont + kCache) == kCacheObj);
    CHECK(slot_width(m, kFont, f) == 25);
    CHECK(slot_height(m, kFont, f) == 25);
    CHECK(slot_ascent(m, kFont, f) == 25);
    CHECK(slot_line_feed(m, kFont, f) == 25);
    CHECK(slot_line_top(m, kFont, f) == 25);
    CHECK(slot_advance(m, kFont, 'A', f) == 25);
    CHECK(slot_kerning(m, kFont, 'A', 'V', f) == 0);
    CHECK(slot_kind(m, kFont, f) == 0);
    for (uint32_t i = 0; i < kGlyphRecordSize; i++) m.st8(kRec + i, 0xEE);
    slot_glyph(m, kFont, kRec, 'A', f);
    bool zero = true;
    for (uint32_t i = 0; i < kGlyphRecordSize; i++) zero &= m.ld8(kRec + i) == 0;
    CHECK(zero);
    CHECK(m.ld8(kFont + kSizeBytes + 1) == 25 && m.ld8(kFont + kSizeBytes + 2) == 25);
    CHECK(m.ld8(kFont + kSizeBytes + 3) == 1);  // +0xB set: mode 1
    CHECK(m.ld32(kFont + kReplacement) == '?');
    CHECK(f.metrics_calls == 0);
}

void test_loaded() {
    FakeMem m;
    FakeFont f(&m);
    boot(m, f);
    f.addr = 0x5000;  // the game loads its fonts
    update(m, kFont, f);
    CHECK(f.metrics_calls == 1);
    CHECK(m.ld32(kFont + kWidth) == 62);
    CHECK(m.ld32(kFont + kHeight) == 76);
    CHECK(m.ld32(kFont + kAscent) == 59);
    CHECK(m.ld32(kFont + kLineFeed) == 76);
    CHECK(m.ld32(kFont + kLineTop) == 58);
    CHECK(slot_width(m, kFont, f) == 62);
    CHECK(slot_height(m, kFont, f) == 76);
    CHECK(slot_ascent(m, kFont, f) == 59);
    CHECK(slot_line_feed(m, kFont, f) == 76);
    CHECK(slot_line_top(m, kFont, f) == 58);
    CHECK(f.metrics_calls == 1);  // applied once per loaded font
    CHECK(m.ld8(kFont + kKerning) == 1);
    CHECK(m.ld8(kFont + kSheetFlip) == 1);
    CHECK(m.ld32(kFont + kVptr) == kVt);  // the vtable stays the scalable font's
    // advances: the bitmap font's, its replacement character's for a missing character
    CHECK(slot_advance(m, kFont, 'A', f) == 40);
    CHECK(slot_advance(m, kFont, '%', f) == 50);
    CHECK(slot_advance(m, kFont, 0x4E00, f) == 33);
    CHECK(slot_advance(m, kFont, 0x10000 | 'i', f) == 18);  // a 16-bit code unit
    // the glyph record is the bitmap font's, pointing at its sheet
    slot_glyph(m, kFont, kRec, 'A', f);
    CHECK(m.ld8(kRec + 5) == 40);
    CHECK(m.ld32(kRec + 0x0C) >> 16 == 'A');
    CHECK(m.ld32(kRec + 0x14) == 0x30000000);
    slot_glyph(m, kFont, kRec, 0x4E00, f);
    CHECK(m.ld32(kRec + 0x0C) >> 16 == '?');
    slot_glyph(m, kFont, 0, 'A', f);  // no record: nothing written
    // kerning: the bitmap font's pairs, only while kerning is enabled on the scalable font
    CHECK((int32_t)slot_kerning(m, kFont, 'A', 'V', f) == -4);
    CHECK(slot_kerning(m, kFont, 'V', 'A', f) == 0);
    m.st8(kFont + kKerning, 0);
    CHECK(slot_kerning(m, kFont, 'A', 'V', f) == 0);
    m.st8(kFont + kKerning, 1);
    // encoding: UTF-16; kind: the bitmap font's
    CHECK(slot_encoding() == 1);
    CHECK(slot_kind(m, kFont, f) == 1);
    f.kind_value = 0;
    CHECK(slot_kind(m, kFont, f) == 0);
    f.kind_value = 7;
    CHECK(slot_kind(m, kFont, f) == 1);
}

// a reloaded main font (another address) is applied again; initialize resets what was applied
void test_reload() {
    FakeMem m;
    FakeFont f(&m);
    boot(m, f);
    f.addr = 0x5000;
    CHECK(slot_width(m, kFont, f) == 62);
    CHECK(f.metrics_calls == 1);
    f.addr = 0x6000;
    CHECK(slot_height(m, kFont, f) == 76);
    CHECK(f.metrics_calls == 2);
    // initialize when the main font is already loaded: its metrics at once
    font_arg_defaults(m, kArg + 0x40);
    m.st32(kArg + 0x40, kCacheObj);
    m.st32(kArg + 0x44, 25);
    initialize(m, kFont, kArg + 0x40, f);
    CHECK(f.metrics_calls == 3);
    CHECK(m.ld32(kFont + kWidth) == 62);
    CHECK(m.ld8(kFont + kSizeBytes + 3) == 2);  // neither flag: mode 2
    // the wide replacement flag (+0xA): U+3000, mode 0
    font_arg_defaults(m, kArg + 0x40);
    m.st32(kArg + 0x40, kCacheObj);
    m.st32(kArg + 0x44, 25);
    m.st8(kArg + 0x4A, 1);
    initialize(m, kFont, kArg + 0x40, f);
    CHECK(m.ld32(kFont + kReplacement) == 0x3000);
    CHECK(m.ld8(kFont + kSizeBytes + 3) == 0);
    // a font that is constructed again starts over
    construct(m, kFont, kVt);
    CHECK(m.ld32(kFont + kWidth) == 0x28);
    CHECK(slot_width(m, kFont, f) == 62);
}

}  // namespace

int main() {
    test_scale();
    test_construct();
    test_arg_defaults();
    test_unloaded();
    test_loaded();
    test_reload();
    if (g_failures) {
        std::fprintf(stderr, "scalable_font_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("scalable_font_test: all passed");
    return 0;
}
