// The scalable font stand-in's hooks (scalable_font.h, tools/recomp/decomp_only_hooks_font.txt): the 12
// functions the game's scalable-font manager calls and the 12 slot functions of the font's vtable.
// DECOMP BUILD ONLY (WWHD_DECOMP; recomp.py --decomp-hooks adds the list): the normal build runs the game's
// own recompiled font module unchanged, and this file compiles to nothing there. In the decomp build the
// decompiled manager calls these addresses through the dispatch table (runtime/src/decomp/decomp.cpp
// gmem_call), whose entry for a hooked address is the hook; the layout code's virtual calls get there too.
//
// WWHD_FONT_LOG=n: logs the first n advance and glyph requests (character, caller, answer).
// WWHD_FONT_GUARD=1: any function of the replaced module (0x028B2204-0x028E81E4) that starts running is
// a fatal error naming it (trace.cpp calls scalable_font_guard_enter at every function entry).
#if WWHD_DECOMP
#include <atomic>
#include <cstdlib>

#include "guest_addr.h"
#include "runtime.h"
#include "scalable_font.h"

namespace {

using namespace scalable_font;

struct GuestMem final : Mem {
    uint8_t ld8(uint32_t ea) override { return ::ld8(ea); }
    void st8(uint32_t ea, uint8_t v) override { ::st8(ea, v); }
    uint32_t ld32(uint32_t ea) override { return ::ld32(ea); }
    void st32(uint32_t ea, uint32_t v) override { ::st32(ea, v); }
};

// font #0 of the game's font manager (0x101F4A50: the manager; +0x10 its 7 fonts): CKingMain
struct MainFont final : Fallback {
    Cpu* c;
    uint32_t font = 0;
    explicit MainFont(Cpu* cpu) : c(cpu) {
        const uint32_t manager = ::ld32(GD(0x101F4A50));
        const uint32_t f = manager ? ::ld32(manager + 0x10) : 0;
        // a loaded nw::lyt::FontResource: vtable at +4, its font information at +0xC
        if (f && ::ld32(f + 4) && ::ld32(f + 0xC)) font = f;
    }
    uint32_t vcall(uint32_t slot, std::initializer_list<uint32_t> args) {
        const uint32_t fn = ::ld32(::ld32(font + 4) + 8 * slot + 4);
        return guest_call(c, fn, args);
    }
    uint32_t id() override { return font; }
    Metrics metrics() override {
        Metrics m;
        m.width = (int32_t)vcall(3, {font});
        m.height = (int32_t)vcall(4, {font});
        m.ascent = (int32_t)vcall(5, {font});
        m.line_feed = (int32_t)vcall(10, {font});
        m.line_top = (int32_t)vcall(21, {font});
        m.kerning = ::ld8(font + kKerning);
        m.sheet_flip = ::ld8(font + kSheetFlip);
        return m;
    }
    // characters CKingMain lacks: its slot 15 / 17 use its replacement character
    int32_t advance(uint32_t ch) override { return (int32_t)vcall(15, {font, ch}); }
    void glyph(uint32_t record, uint32_t ch) override { vcall(17, {font, record, ch}); }
    int32_t kerning(uint32_t a, uint32_t b) override { return (int32_t)vcall(19, {font, a, b}); }
    uint32_t kind() override { return vcall(30, {font}); }
};

std::atomic<bool> g_logged_unloaded{false};
std::atomic<bool> g_logged_ready{false};

void note(MainFont& fb, uint32_t font) {
    if (!fb.id()) {
        if (!g_logged_unloaded.exchange(true))
            log_msg("[font] scalable font %08X used before the game's main font is loaded: provisional metrics", font);
    } else if (!g_logged_ready.exchange(true)) {
        GuestMem m;
        log_msg("[font] scalable font %08X draws with the main font %08X: width %d height %d ascent %d line feed %d",
                font, fb.id(), (int)m.ld32(font + kWidth), (int)m.ld32(font + kHeight), (int)m.ld32(font + kAscent),
                (int)m.ld32(font + kLineFeed));
    }
}

std::atomic<int> g_log_left{-1};
void log_request(Cpu* c, const char* what, uint32_t ch, uint32_t answer) {
    int left = g_log_left.load(std::memory_order_relaxed);
    if (left < 0) {
        const char* e = getenv("WWHD_FONT_LOG");
        left = e ? atoi(e) : 0;
        g_log_left.store(left);
    }
    if (left == 0 || g_log_left.fetch_sub(1) <= 0) return;
    log_msg("[font] %s U+%04X lr=%08X -> %d", what, ch & 0xFFFF, c->lr, (int)answer);
}

uint32_t operator_new(Cpu* c, uint32_t size) { return guest_call(c, GC(0x0273AD10), {size}); }
void operator_delete(Cpu* c, uint32_t p) { guest_call(c, GC(0x0273AF40), {p}); }

}  // namespace

void scalable_font_guard_enter(uint32_t addr) {
    static const uint32_t lo = GC(0x028B2204), hi = GC(0x028E81E4);
    if (addr - lo < hi - lo) {
        Cpu* c = threads::current();
        fatal("[font] WWHD_FONT_GUARD: the replaced scalable font module ran f_%08X (lr %08X)", addr, c ? c->lr : 0);
    }
}

extern "C" {

// ---- the entry points (r3 = this or the argument struct) ----

// font constructor (placement; r3 0 allocates): FontInterface base constructor, then our fields
void hook_028B2204(Cpu* c) {
    uint32_t font = c->r[3];
    if (!font) font = operator_new(c, kFontSize);
    if (!font) { c->r[3] = 0; return; }
    guest_call(c, GC(0x0286EC28), {font});  // nw::lyt::FontInterface::create
    GuestMem m;
    construct(m, font, GD(0x101880C0));
    c->r[3] = font;
}

// font destructor (r4 bit 0: free)
void hook_028B229C(Cpu* c) {
    const uint32_t font = c->r[3], flags = c->r[4];
    if (!font) return;
    guest_call(c, GC(0x0286EC74), {font, flags});  // nw::lyt::FontInterface::destroy (frees on bit 0)
}

// glyph cache constructor (placement; 0x1524 bytes)
void hook_028B28F8(Cpu* c) {
    uint32_t cache = c->r[3];
    if (!cache) cache = operator_new(c, kCacheSize);
    if (!cache) { c->r[3] = 0; return; }
    GuestMem m;
    cache_construct(m, cache);
    c->r[3] = cache;
}

// glyph cache destructor (r4 bit 0: free)
void hook_028B2A40(Cpu* c) {
    const uint32_t cache = c->r[3], flags = c->r[4];
    if (cache && (flags & 1)) operator_delete(c, cache);
}

void hook_028B4230(Cpu* c) { GuestMem m; cache_arg_defaults(m, c->r[3]); }  // cache init-arg defaults
void hook_028B289C(Cpu* c) { GuestMem m; font_arg_defaults(m, c->r[3]); }   // font init-arg defaults
void hook_028B2B64(Cpu* c) { (void)c; }  // cache initialize: no cache, no shared font needed
void hook_028B3124(Cpu* c) { (void)c; }  // cache finalize: nothing allocated

// font initialize (r3 = font, r4 = argument: +0 cache, +4 size, +8 face, +0xA, +0xB flags)
void hook_028B239C(Cpu* c) {
    GuestMem m;
    MainFont fb(c);
    initialize(m, c->r[3], c->r[4], fb);
    note(fb, c->r[3]);
}

// the three per-frame cache updates (r3 = cache): the font sits right before its cache in the manager
static void cache_update(Cpu* c) {
    GuestMem m;
    MainFont fb(c);
    const uint32_t cache = c->r[3];
    // the manager embeds the font at +0x3C and the cache at +0x64
    const uint32_t font = cache - (0x64 - 0x3C);
    if (cache && m.ld32(font + kVptr) == GD(0x101880C0) && m.ld32(font + kCache) == cache) {
        update(m, font, fb);
        note(fb, font);
    }
}
void hook_028B3540(Cpu* c) { cache_update(c); }
void hook_028B3804(Cpu* c) { cache_update(c); }
void hook_028B3E8C(Cpu* c) { cache_update(c); }

// ---- the slots (r3 = font) ----
void hook_028B24C0(Cpu* c) { GuestMem m; MainFont fb(c); c->r[3] = slot_width(m, c->r[3], fb); }      // 3
void hook_028B24C8(Cpu* c) { GuestMem m; MainFont fb(c); c->r[3] = slot_height(m, c->r[3], fb); }     // 4
void hook_028B24D0(Cpu* c) { GuestMem m; MainFont fb(c); c->r[3] = slot_ascent(m, c->r[3], fb); }     // 5
void hook_028B24D8(Cpu* c) { GuestMem m; MainFont fb(c); c->r[3] = slot_line_feed(m, c->r[3], fb); }  // 10
void hook_028B24E0(Cpu* c) {  // 15: advance of r4
    GuestMem m;
    MainFont fb(c);
    const uint32_t ch = c->r[4];
    const uint32_t font = c->r[3];
    c->r[3] = slot_advance(m, font, ch, fb);
    note(fb, font);
    log_request(c, "advance", ch, c->r[3]);
}
void hook_028B25C4(Cpu* c) {  // 17: glyph record r4 of character r5
    GuestMem m;
    MainFont fb(c);
    const uint32_t record = c->r[4], ch = c->r[5];
    slot_glyph(m, c->r[3], record, ch, fb);
    note(fb, c->r[3]);
    log_request(c, "glyph", ch, record ? ld8(record + 5) : 0);
}
void hook_028B2840(Cpu* c) {  // 19: kerning of the pair r4 r5
    GuestMem m;
    MainFont fb(c);
    c->r[3] = slot_kerning(m, c->r[3], c->r[4], c->r[5], fb);
}
void hook_028B28EC(Cpu* c) { c->r[3] = slot_encoding(); }                                                // 20
void hook_028B2864(Cpu* c) { GuestMem m; MainFont fb(c); c->r[3] = slot_line_top(m, c->r[3], fb); }    // 21
void hook_028B28F4(Cpu* c) { (void)c; }                                                                  // 29
void hook_028B286C(Cpu* c) { GuestMem m; MainFont fb(c); c->r[3] = slot_kind(m, c->r[3], fb); }        // 30

}  // extern "C"

#endif  // WWHD_DECOMP
