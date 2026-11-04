// The scalable font stand-in's logic (scalable_font.h); the hooks are in scalable_font_hooks.cpp.
#include "scalable_font.h"

#include <mutex>
#include <unordered_map>

namespace scalable_font {

int32_t scaled(int32_t v) {
    // round half away from zero, as an integer pixel value
    const int64_t n = (int64_t)v * kScaleNum;
    return (int32_t)(n >= 0 ? (n + kScaleDen / 2) / kScaleDen : -((-n + kScaleDen / 2) / kScaleDen));
}

namespace {
std::mutex g_mu;
std::unordered_map<uint32_t, uint32_t> g_applied;  // font object -> the fallback whose metrics it holds

void store_metrics(Mem& m, uint32_t font, const Metrics& mt) {
    m.st8(font + kKerning, mt.kerning);
    m.st8(font + kSheetFlip, mt.sheet_flip);
    m.st32(font + kWidth, (uint32_t)scaled(mt.width));
    m.st32(font + kHeight, (uint32_t)scaled(mt.height));
    m.st32(font + kAscent, (uint32_t)scaled(mt.ascent));
    m.st32(font + kLineTop, (uint32_t)scaled(mt.line_top));
    m.st32(font + kLineFeed, (uint32_t)scaled(mt.line_feed));
}
}  // namespace

void forget() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_applied.clear();
}

bool refresh(Mem& m, uint32_t font, Fallback& fb) {
    const uint32_t id = fb.id();
    if (!id || !font) return false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_applied.find(font);
        if (it != g_applied.end() && it->second == id) return true;
    }
    const Metrics mt = fb.metrics();
    store_metrics(m, font, mt);
    std::lock_guard<std::mutex> lk(g_mu);
    g_applied[font] = id;
    return true;
}

// ---- entry points ----

void construct(Mem& m, uint32_t font, uint32_t vtable) {
    // the state the game's font object has before initialize (measured on the original)
    m.st32(font + kVptr, vtable);
    m.st32(font + kCache, 0);
    m.st32(font + kWidth, 0x28);
    m.st32(font + kHeight, 0x32);
    m.st32(font + kAscent, 0x28);
    m.st32(font + kLineTop, 0x28);
    m.st32(font + kLineFeed, 0x32);
    m.st32(font + kSizeBytes, 0x00282802);
    m.st32(font + kReplacement, '?');
    std::lock_guard<std::mutex> lk(g_mu);
    g_applied.erase(font);
}

void cache_construct(Mem& m, uint32_t cache) {
    m.zero(cache, kCacheSize);  // nothing reads it: the stand-in has no glyph cache
}

void cache_arg_defaults(Mem& m, uint32_t arg) {
    // the original's defaults (the caller overwrites what it needs; the stand-in ignores all of it)
    m.zero(arg, kCacheArgSize);
    m.st32(arg + 0x00, 0x400);
    m.st32(arg + 0x04, 0x400);
    m.st32(arg + 0x20, 1);
    m.st32(arg + 0x24, 0x80000);
    m.st32(arg + 0x28, 0xC800);
    m.st32(arg + 0x2C, 0x400);
}

void font_arg_defaults(Mem& m, uint32_t arg) {
    m.zero(arg, kFontArgSize);
    m.st32(arg + 0x04, 0x28);  // size
}

void initialize(Mem& m, uint32_t font, uint32_t arg, Fallback& fb) {
    const uint32_t cache = m.ld32(arg + 0x00);
    const uint32_t size = m.ld32(arg + 0x04);
    const uint8_t wide_replacement = m.ld8(arg + 0x0A), mode_flag = m.ld8(arg + 0x0B);
    m.st32(font + kCache, cache);
    // until the bitmap font is loaded: every metric is the requested size (no zero, no 0x80000000)
    m.st32(font + kWidth, size);
    m.st32(font + kHeight, size);
    m.st32(font + kAscent, size);
    m.st32(font + kLineTop, size);
    m.st32(font + kLineFeed, size);
    m.st8(font + kSizeBytes + 0, 0);
    m.st8(font + kSizeBytes + 1, (uint8_t)size);
    m.st8(font + kSizeBytes + 2, (uint8_t)size);
    m.st8(font + kSizeBytes + 3, wide_replacement ? 0 : mode_flag ? 1 : 2);
    if (wide_replacement) m.st32(font + kReplacement, 0x3000);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_applied.erase(font);
    }
    refresh(m, font, fb);
}

void update(Mem& m, uint32_t font, Fallback& fb) { refresh(m, font, fb); }

// ---- slots ----

uint32_t slot_width(Mem& m, uint32_t font, Fallback& fb) { refresh(m, font, fb); return m.ld32(font + kWidth); }
uint32_t slot_height(Mem& m, uint32_t font, Fallback& fb) { refresh(m, font, fb); return m.ld32(font + kHeight); }
uint32_t slot_ascent(Mem& m, uint32_t font, Fallback& fb) { refresh(m, font, fb); return m.ld32(font + kAscent); }
uint32_t slot_line_feed(Mem& m, uint32_t font, Fallback& fb) { refresh(m, font, fb); return m.ld32(font + kLineFeed); }
uint32_t slot_line_top(Mem& m, uint32_t font, Fallback& fb) { refresh(m, font, fb); return m.ld32(font + kLineTop); }

uint32_t slot_advance(Mem& m, uint32_t font, uint32_t ch, Fallback& fb) {
    if (!refresh(m, font, fb)) return m.ld32(font + kWidth);
    return (uint32_t)scaled(fb.advance(ch & 0xFFFF));
}

void slot_glyph(Mem& m, uint32_t font, uint32_t record, uint32_t ch, Fallback& fb) {
    if (!record) return;
    if (!refresh(m, font, fb)) {
        m.zero(record, kGlyphRecordSize);  // nothing to draw yet
        return;
    }
    fb.glyph(record, ch & 0xFFFF);
}

uint32_t slot_kerning(Mem& m, uint32_t font, uint32_t first, uint32_t second, Fallback& fb) {
    if (!refresh(m, font, fb) || !m.ld8(font + kKerning)) return 0;
    return (uint32_t)scaled(fb.kerning(first & 0xFFFF, second & 0xFFFF));
}

uint32_t slot_encoding() { return 1; }

uint32_t slot_kind(Mem& m, uint32_t font, Fallback& fb) {
    if (!refresh(m, font, fb)) return 0;
    return fb.kind() ? 1 : 0;
}

}  // namespace scalable_font
