// Native pass and copy descriptions (gx2/native_pass.h): the decompiled sites' entry points (game thread) and the
// render thread's open pass, its check against the registers, and the counters. Decomp build, fast mode,
// WWHD_NATIVE_DRAW=ON only.
#include "../../third_party/cemu/cemu_shim.h"  // (as the gx2 sources get it: -include)
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "gx2/native_pass.h"
#include "gx2/gx2.h"
#include "gx2/gx2_cmd.h"
#include "gx2/gx2_regs.h"
#include "gx2_texture_regs.h"
#include "runtime.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gx2 {
uint32 color_buffer_address(const GX2::GX2ColorBuffer* cb);  // gx2_resources.cpp: CB_COLOR*_BASE
}

namespace native_pass {

bool on() {
    static const bool v = [] {
        const char* e = std::getenv("WWHD_NATIVE_PASS");
        const bool b = e && *e && std::atoi(e) != 0;
        LOG("[native-pass] pass and copy descriptions %s (WWHD_NATIVE_PASS=%d)", b ? "on" : "off", b ? 1 : 0);
        return b;
    }();
    return v;
}

// ---------------------------------------------------------------- game thread: the sites
static uint32_t samples_of(uint32_t aa) { return aa < 4 ? 1u << aa : 1u; }
static Attachment color_attachment(uint32_t addr) {
    const auto* cb = reinterpret_cast<const GX2::GX2ColorBuffer*>(mem::ptr(addr));
    const uint32_t mip = cb->viewMip;
    Attachment a;
    a.image = gx2::color_buffer_address(cb);
    a.format = uint32_t(cb->surface.format.value());
    a.width = std::max<uint32_t>(uint32_t(cb->surface.width) >> mip, 1);
    a.height = std::max<uint32_t>(uint32_t(cb->surface.height) >> mip, 1);
    a.mip = mip;
    a.firstSlice = cb->viewFirstSlice;
    a.slices = cb->viewNumSlices;
    a.samples = samples_of(cb->surface.aa);
    return a;
}
static Attachment depth_attachment(uint32_t addr) {
    const auto* db = reinterpret_cast<const GX2::GX2DepthBuffer*>(mem::ptr(addr));
    Attachment a;
    a.image = db->surface.imagePtr;  // (DB_DEPTH_BASE)
    a.format = uint32_t(db->surface.format.value());
    a.width = db->surface.width;
    a.height = db->surface.height;
    a.mip = db->viewMip;
    a.firstSlice = db->viewFirstSlice;
    a.slices = db->viewNumSlices;
    a.samples = samples_of(db->surface.aa);
    return a;
}
static thread_local Pass t_pass;
static std::atomic<uint32_t> g_nextId{1};

}  // namespace native_pass

using namespace native_pass;

// the pass's colour attachment INDEX: the GX2ColorBuffer the site binds there (as it binds it). Zero: off
extern "C" unsigned wwhd_native_pass_color(unsigned index, unsigned colorBuffer) {
    if (!on() || index >= kMaxColors || !colorBuffer) return 0;
    t_pass.colors[index] = color_attachment(colorBuffer);
    t_pass.colorMask |= 1u << index;
    return 1;
}
// the pass's depth attachment: the GX2DepthBuffer the site binds
extern "C" unsigned wwhd_native_pass_depth(unsigned depthBuffer) {
    if (!on() || !depthBuffer) return 0;
    t_pass.depth = depth_attachment(depthBuffer);
    t_pass.hasDepth = 1;
    return 1;
}
// the attachments given since the last begin form a pass: described (OP_PASS_BEGIN). Zero: off
extern "C" unsigned wwhd_native_pass_begin() {
    if (!on()) return 0;
    t_pass.id = g_nextId.fetch_add(1, std::memory_order_relaxed);
    static_assert(sizeof(Pass) == kPassWords * 4);
    gx2::emit(gx2::OP_PASS_BEGIN, reinterpret_cast<const uint32_t*>(&t_pass), kPassWords);
    t_pass = Pass{};
    return 1;
}
// the site clears attachments of the open pass (load intent): BITS (LoadBits), colour attachment INDEX
extern "C" void wwhd_native_pass_load(unsigned bits, unsigned index, float r, float g, float b, float a, double depth,
                                      unsigned stencil) {
    if (!on() || !bits) return;
    Load l;
    l.bits = bits;
    l.colorIndex = index;
    const float rgba[4] = {r, g, b, a};
    std::memcpy(l.rgba, rgba, sizeof rgba);
    const float d = float(depth);
    std::memcpy(&l.depth, &d, 4);
    l.stencil = stencil;
    gx2::emit(gx2::OP_PASS_LOAD, reinterpret_cast<const uint32_t*>(&l), kLoadWords);
}
// a copy of KIND (CopyKind) from the GX2ColorBuffer (or, IS_DEPTH, GX2DepthBuffer) SRC; DEST: the scan-out target
extern "C" void wwhd_native_pass_copy(unsigned kind, unsigned dest, unsigned src, unsigned isDepth) {
    if (!on() || !src) return;
    Copy c;
    c.kind = kind;
    c.dest = dest;
    c.src = isDepth ? depth_attachment(src) : color_attachment(src);
    gx2::emit(gx2::OP_PASS_COPY, reinterpret_cast<const uint32_t*>(&c), kCopyWords);
}

namespace native_pass {

// ---------------------------------------------------------------- render thread
static Pass g_open;
static bool g_isOpen = false, g_drawn = false;
enum Counter {
    kPasses, kDescribedDraws, kOutsideDraws, kMismatchDraws, kLoadIntents, kLoadsAfterDraw, kEnds,
    kNScan, kNExpandDepth, kNExpandColor, kNSurface, kCounters
};
static uint64_t g_count[kCounters], g_frames = 0, g_mismatchLogged = 0;

static void close_pass() { g_isOpen = false; }

void begin(const uint32_t* p, uint32_t n) {
    if (n != kPassWords) return;
    std::memcpy(&g_open, p, sizeof g_open);
    g_isOpen = true;
    g_drawn = false;
    ++g_count[kPasses];
}
void load(const uint32_t* p, uint32_t n) {
    if (n != kLoadWords) return;
    ++g_count[kLoadIntents];
    if (g_isOpen && g_drawn) ++g_count[kLoadsAfterDraw];  // (a clear in the middle of the pass: not a load intent)
}
void end(const uint32_t*, uint32_t) {
    ++g_count[kEnds];
    close_pass();
}
void copy(const uint32_t* p, uint32_t n) {
    if (n != kCopyWords) return;
    switch (p[0]) {
    case kCopyScanOut: ++g_count[kNScan]; break;
    case kCopyExpandDepth: ++g_count[kNExpandDepth]; break;
    case kCopyExpandColor: ++g_count[kNExpandColor]; break;
    default: ++g_count[kNSurface]; break;
    }
    close_pass();  // (the pass is stored, then copied)
}
// the targets every draw derives from the registers (CB_COLOR*_BASE, DB_DEPTH_BASE) against the open pass
void note_draw(const uint32_t* regs) {
    if (!g_isOpen) {
        ++g_count[kOutsideDraws];
        return;
    }
    g_drawn = true;
    bool same = !g_open.hasDepth || regs[mmDB_DEPTH_BASE] == g_open.depth.image;
    for (uint32_t i = 0; same && i < kMaxColors; ++i)
        if (g_open.colorMask >> i & 1) same = regs[mmCB_COLOR0_BASE + i] == g_open.colors[i].image;
    if (same) {
        ++g_count[kDescribedDraws];
        return;
    }
    ++g_count[kMismatchDraws];
    if (g_mismatchLogged++ < 16)
        LOG("[native-pass] draw in pass %u: targets differ from the description (colour0 %08X vs %08X, depth %08X vs %08X)",
            g_open.id, regs[mmCB_COLOR0_BASE], g_open.colors[0].image,
            regs[mmDB_DEPTH_BASE], g_open.depth.image);
}
void frame_end() {
    close_pass();
    if (++g_frames % 300) return;
    static const char* const names[kCounters] = {"passes", "described draws", "draws outside a described pass",
                                                 "draws whose targets differ", "load intents (clears)",
                                                 "clears after a draw", "ends", "scan-out copies", "depth expands",
                                                 "colour expands", "surface copies"};
    char buf[600];
    int k = std::snprintf(buf, sizeof buf, "[native-pass] last 300 frames, per frame:");
    for (uint32_t c = 0; c < kCounters; ++c)
        k += std::snprintf(buf + k, sizeof buf - k, "%s %s %.1f", c ? "," : "", names[c], g_count[c] / 300.0);
    LOG("%s", buf);
    std::memset(g_count, 0, sizeof g_count);
}

}  // namespace native_pass
