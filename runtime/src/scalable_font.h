// The game's scalable font (font #5, registered with the layouts as "CafeStd.bffnt"), replaced by the
// port: a stand-in that draws with the game's own CKingMain bitmap font. No font engine, no font file.
//
// The game creates one scalable font and its glyph cache in its scalable-font manager
// (d/hd_scalable_font_mgr: 0x025F96EC creates them, 0x025F99BC initializes them with size 25 from the
// console's shared font, OSGetSharedData(2), which the port doesn't have). Only the Miiverse comment
// layout (BtnMiiverseComment_00: Mii name, comment, topic tag, Yeah and comment counts) uses that font;
// Miiverse itself is stubbed. Everything else reaches the font only through its virtual functions, the
// nw::lyt FontInterface slots (vtable 0x101880C0; vptr at +4, slot n at vptr+8n+4).
//
// Which build uses it: only the decomp build (CMake WWHD_DECOMP=ON, and later the full native build), whose
// decompilation leaves this module out. The normal (recompiled) build runs the game's own recompiled font
// module unchanged: no hooks (recomp.py adds tools/recomp/decomp_only_hooks_font.txt only with
// --decomp-hooks), scalable_font_hooks.cpp compiles to nothing, and WWHD_FONT_GUARD / WWHD_FONT_LOG only
// print a notice. There the font keeps the original behaviour without the shared font (its metrics read
// 0x80000000), as on devel.
//
// In the decomp build the stand-in (decomp_only_hooks_font.txt, scalable_font_hooks.cpp) replaces the 12
// functions the game calls directly and the 12 slot functions; the original module's code never runs.
// WWHD_FONT_GUARD=1 makes any function of the module that starts running a fatal error;
// WWHD_FONT_LOG=n logs the first n advance and glyph requests. Every slot answers from CKingMain (font #0 of the game's font
// manager) through that font's own slot functions: metrics, advances, kerning, glyph records (which
// point at CKingMain's sheets) and its replacement character for characters it lacks.
//
// Metrics are CKingMain's own (kScaleNum/kScaleDen = 1): the layout code sizes a glyph quad as the
// record's width times (text size / slot-3 width), so the slot-3 width must be the width of the font
// the glyph records come from. Any other scale would need glyph records of a resampled sheet of our own.
// Side effect: hdTextPane_c::setAutomaticCapacity (0x02703C74) sizes a text pane's buffer as
// pane width / advance('i') * 3; with CKingMain's advances (font width 62 instead of the original's
// size 25) that buffer is about 2.5 times smaller than with the console's font.
//
// The logic here works on a Mem (guest memory) and a Fallback (the bitmap font), so it can be tested
// without the game (runtime/tools/scalable_font_test.cpp).
#pragma once
#include <cstdint>

namespace scalable_font {

// ---- the original module's address range and entry points (USA build) ----
// The module spans 028B2204-028E81E4 and the font's vtable is 101880C0 (USA addresses); the hooks translate
// them for the running build where they use them (GC/GD in scalable_font_hooks.cpp).

// the font object (0x28 bytes); fields as the outside sees them through the slots
constexpr uint32_t kFontSize = 0x28;
constexpr uint32_t kKerning = 0x00;    // u8 FontInterface::kerningEnabled
constexpr uint32_t kSheetFlip = 0x01;  // u8 FontInterface::sheetFlip (read by GlyphWriter)
constexpr uint32_t kVptr = 0x04;
constexpr uint32_t kCache = 0x08;      // the glyph cache (initialize argument +0)
constexpr uint32_t kWidth = 0x0C;      // slot 3
constexpr uint32_t kHeight = 0x10;     // slot 4
constexpr uint32_t kAscent = 0x14;     // slot 5
constexpr uint32_t kLineTop = 0x18;    // slot 21
constexpr uint32_t kLineFeed = 0x1C;   // slot 10
constexpr uint32_t kSizeBytes = 0x20;  // u8 [0] 0, [1] [2] the size, [3] the replacement mode
constexpr uint32_t kReplacement = 0x24;
// the glyph cache object embedded in the manager (only its address is passed around)
constexpr uint32_t kCacheSize = 0x1524;
constexpr uint32_t kCacheArgSize = 0x34, kFontArgSize = 0xC;
// glyph record (slot 17 output)
constexpr uint32_t kGlyphRecordSize = 0x18;

// CKingMain's metrics are used as they are (see above)
constexpr int32_t kScaleNum = 1, kScaleDen = 1;
static_assert(kScaleNum == kScaleDen, "glyph records come from the bitmap font's sheets: no other scale");
int32_t scaled(int32_t v);

// ---- environment ----
struct Mem {
    virtual ~Mem() = default;
    virtual uint8_t ld8(uint32_t ea) = 0;
    virtual void st8(uint32_t ea, uint8_t v) = 0;
    virtual uint32_t ld32(uint32_t ea) = 0;
    virtual void st32(uint32_t ea, uint32_t v) = 0;
    void zero(uint32_t ea, uint32_t n) { for (uint32_t i = 0; i < n; i++) st8(ea + i, 0); }
};

struct Metrics {
    int32_t width = 0, height = 0, ascent = 0, line_feed = 0, line_top = 0;
    uint8_t kerning = 0, sheet_flip = 1;
};

// the bitmap font the stand-in draws with
struct Fallback {
    virtual ~Fallback() = default;
    virtual uint32_t id() = 0;  // the font object's guest address, 0 while it isn't loaded
    virtual Metrics metrics() = 0;
    virtual int32_t advance(uint32_t ch) = 0;
    virtual void glyph(uint32_t record, uint32_t ch) = 0;  // fills the 0x18-byte record
    virtual int32_t kerning(uint32_t first, uint32_t second) = 0;
    virtual uint32_t kind() = 0;  // slot 30
};

// ---- the 12 entry points the game calls ----
// The FontInterface base constructor/destructor stay the layout library's (the hooks call them);
// these set up and tear down the rest.
// after the base constructor; vtable = the font's vtable translated to the running build (GD)
void construct(Mem& m, uint32_t font, uint32_t vtable);
void cache_construct(Mem& m, uint32_t cache);
void cache_arg_defaults(Mem& m, uint32_t arg);
void font_arg_defaults(Mem& m, uint32_t arg);
void initialize(Mem& m, uint32_t font, uint32_t arg, Fallback& fb);
// the per-frame cache updates: the stand-in has no cache; they take a newly loaded fallback's metrics
void update(Mem& m, uint32_t font, Fallback& fb);

// ---- the slots ----
uint32_t slot_width(Mem& m, uint32_t font, Fallback& fb);      // 3
uint32_t slot_height(Mem& m, uint32_t font, Fallback& fb);     // 4
uint32_t slot_ascent(Mem& m, uint32_t font, Fallback& fb);     // 5
uint32_t slot_line_feed(Mem& m, uint32_t font, Fallback& fb);  // 10
uint32_t slot_advance(Mem& m, uint32_t font, uint32_t ch, Fallback& fb);  // 15
void slot_glyph(Mem& m, uint32_t font, uint32_t record, uint32_t ch, Fallback& fb);  // 17
uint32_t slot_kerning(Mem& m, uint32_t font, uint32_t first, uint32_t second, Fallback& fb);  // 19
uint32_t slot_encoding();                                      // 20: UTF-16
uint32_t slot_line_top(Mem& m, uint32_t font, Fallback& fb);   // 21
uint32_t slot_kind(Mem& m, uint32_t font, Fallback& fb);       // 30
// slot 29 (set components) does nothing

// the font fields from the fallback's metrics, when the fallback is loaded and wasn't applied to this
// font yet; true if the fields hold the fallback's metrics
bool refresh(Mem& m, uint32_t font, Fallback& fb);
void forget();  // tests: drop what refresh() remembers

}  // namespace scalable_font
