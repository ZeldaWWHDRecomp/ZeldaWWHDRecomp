// Native pass and copy descriptions (gx2/native_pass.h): the decompiled sites' entry points (game thread) and the
// render thread's open pass, its check against the registers, and the counters. Decomp build, fast mode,
// WWHD_NATIVE_DRAW=ON only.
#include "../../third_party/cemu/cemu_shim.h"  // (as the gx2 sources get it: -include)
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "gx2/native_pass.h"
#include "gx2/native_draw.h"
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
        const bool b = native_draw::pass_requested();
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
    // the words GX2SetColorBuffer writes (gx2_resources.cpp, the same computation)
    const auto dim = cb->surface.dim.value();
    const bool volume = dim == Latte::E_DIM::DIM_3D;
    const uint32_t slices = dim == Latte::E_DIM::DIM_2D_ARRAY ? std::max<uint32_t>(cb->surface.depth, 1)
                            : volume                          ? std::max<uint32_t>(cb->surface.depth >> cb->viewMip, 1)
                                                              : 1;
    a.regs[0] = a.image;
    a.regs[1] = cb->reg_size;
    a.regs[2] = cb->reg_view;
    a.regs[3] = cb->reg_info;
    a.regs[4] = std::max<uint32_t>(cb->surface.width >> cb->viewMip, 1) | (std::min<uint32_t>(slices, 0x7FFF) << 16) |
                (volume ? gx2::kColorTarget3D : 0);
    a.regs[5] = std::max<uint32_t>(cb->surface.height >> cb->viewMip, 1);
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
    // the words GX2SetDepthBuffer writes (gx2_resources.cpp, the same computation)
    uint32_t info = (uint32_t(db->surface.tileMode.value()) & 0xF) << 15;
    switch (db->surface.format.value()) {
    case Latte::E_GX2SURFFMT::D16_UNORM: info |= 1; break;
    case Latte::E_GX2SURFFMT::D24_S8_UNORM: info |= 3; break;
    case Latte::E_GX2SURFFMT::D24_S8_FLOAT: info |= 5; break;
    case Latte::E_GX2SURFFMT::D32_FLOAT: info |= 6; break;
    case Latte::E_GX2SURFFMT::D32_S8_FLOAT: info |= 7; break;
    default: break;
    }
    a.regs[0] = db->reg_size;
    a.regs[1] = db->surface.imagePtr;
    a.regs[2] = info;
    a.regs[3] = (db->viewFirstSlice & 0x7FF) | (((db->viewNumSlices + db->viewFirstSlice - 1) & 0x7FF) << 13);
    a.regs[4] = uint32_t(db->surface.width) << 16 | (uint32_t(db->surface.height) & 0xFFFF);
    a.regs[5] = db->surface.dim.value() == Latte::E_DIM::DIM_2D_ARRAY ? std::max<uint32_t>(db->surface.depth, 1) : 1;
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

// the open pass ends here (the site knows its end; store intent: the attachments are kept)
extern "C" void wwhd_native_pass_end() {
    if (!on()) return;
    const uint32_t store = 1;
    gx2::emit(gx2::OP_PASS_END, &store, 1);
}
static uint32_t fbits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
// the site sets the open pass's viewport: GX2SetViewport(X, Y, W, H, NEAR, FAR) follows (gx2_state.cpp: its words)
extern "C" void wwhd_native_pass_viewport(float x, float y, float w, float h, float n, float f) {
    if (!on()) return;
    Viewport v;
    v.kind = 0;
    const float in[6] = {x, y, w, h, n, f};
    for (int i = 0; i < 6; ++i) v.value[i] = fbits(in[i]);
    v.regs[0] = fbits(w * 0.5f);
    v.regs[1] = fbits(x + w * 0.5f);
    v.regs[2] = fbits(h * -0.5f);
    v.regs[3] = fbits(y + h * 0.5f);
    v.regs[4] = fbits((f - n) * 0.5f);
    v.regs[5] = fbits((n + f) * 0.5f);
    gx2::emit(gx2::OP_PASS_VIEWPORT, reinterpret_cast<const uint32_t*>(&v), kViewportWords);
}
// the site sets the open pass's scissor: GX2SetScissor(X, Y, W, H) follows (gx2_state.cpp: its words)
extern "C" void wwhd_native_pass_scissor(unsigned x, unsigned y, unsigned w, unsigned h) {
    if (!on()) return;
    Viewport v;
    v.kind = 1;
    v.value[0] = x; v.value[1] = y; v.value[2] = w; v.value[3] = h;
    const uint32_t tlx = std::min(x, 8192u), tly = std::min(y, 8192u), brx = std::min(x + w, 8192u),
                   bry = std::min(y + h, 8192u);
    Latte::LATTE_PA_SC_GENERIC_SCISSOR_TL tl;
    tl.set_TL_X(tlx).set_TL_Y(tly).set_WINDOW_OFFSET_DISABLE(true);
    Latte::LATTE_PA_SC_GENERIC_SCISSOR_BR br;
    br.set_BR_X(brx).set_BR_Y(bry);
    v.regs[0] = tl.getRawValue();
    v.regs[1] = br.getRawValue();
    gx2::emit(gx2::OP_PASS_VIEWPORT, reinterpret_cast<const uint32_t*>(&v), kViewportWords);
}

namespace native_pass {

// ---------------------------------------------------------------- render thread
static Pass g_open;
static bool g_isOpen = false, g_drawn = false;
enum Counter {
    kPasses, kDescribedDraws, kOutsideDraws, kMismatchDraws, kLoadIntents, kLoadsAfterDraw, kEnds,
    kNScan, kNExpandDepth, kNExpandColor, kNSurface, kViewportsDescribed, kScissorsDescribed, kViewportDraws,
    kViewportUndescribed, kViewportMismatch, kTrusted, kTrustResolved, kTrustMismatch, kCounters
};
static bool g_lastDescribed = false;  // the last checked draw lay in a described pass, every target word matching
// the viewport and scissor last described (kept across passes: a site sets them for the targets it binds next, or
// for the pass it is in) and whether a register write since then set them otherwise
static Viewport g_viewport, g_scissor;
static bool g_hasViewport = false, g_hasScissor = false;
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
void viewport(const uint32_t* p, uint32_t n) {
    if (n != kViewportWords) return;
    if (p[0] == 0) {
        std::memcpy(&g_viewport, p, sizeof g_viewport);
        g_hasViewport = true;
        ++g_count[kViewportsDescribed];
    } else {
        std::memcpy(&g_scissor, p, sizeof g_scissor);
        g_hasScissor = true;
        ++g_count[kScissorsDescribed];
    }
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
int trust_mode() {
    static const int v = [] {
        const int m = on() ? native_draw::switch_value("WWHD_NATIVE_PASS_TRUST", 1) : 0;
        if (on()) LOG("[native-pass] backend trusting the open pass: %s (WWHD_NATIVE_PASS_TRUST=%d)",
                      m == 1 ? "on" : m == 2 ? "verify" : "off", m);
        return m == 1 || m == 2 ? m : 0;
    }();
    return v;
}
static const uint32_t* g_noteRegs = nullptr;  // the registers the last check read; one draw resolve may trust it
bool trusted(uint32_t& passId, const uint32_t* regs) {
    passId = g_open.id;
    const bool t = g_lastDescribed && g_isOpen && regs == g_noteRegs;
    g_lastDescribed = false;  // (the draw of this command only: not a replay or helper draw it makes itself)
    return t;
}
void count_trust(int what) { ++g_count[what == 1 ? kTrusted : what == 2 ? kTrustMismatch : kTrustResolved]; }
void note_draw(const uint32_t* regs) {
    g_lastDescribed = false;
    g_noteRegs = regs;
    if (!g_isOpen) {
        ++g_count[kOutsideDraws];
        return;
    }
    g_drawn = true;
    // the viewport and scissor (counted apart: they do not decide the targets)
    if (g_hasViewport && g_hasScissor) {
        bool v = true;
        for (uint32_t i = 0; v && i < 6; ++i) v = regs[Latte::REGADDR::PA_CL_VPORT_XSCALE + i] == g_viewport.regs[i];
        v = v && regs[Latte::REGADDR::PA_SC_GENERIC_SCISSOR_TL] == g_scissor.regs[0] &&
            regs[Latte::REGADDR::PA_SC_GENERIC_SCISSOR_BR] == g_scissor.regs[1];
        ++g_count[v ? kViewportDraws : kViewportMismatch];
    } else {
        ++g_count[kViewportUndescribed];
    }
    // every word the target binds write
    static const uint32_t depthRegs[kAttachmentRegs] = {mmDB_DEPTH_SIZE, mmDB_DEPTH_BASE, mmDB_DEPTH_INFO,
                                                        mmDB_DEPTH_VIEW, mmDB_HTILE_DATA_BASE, gx2::kDepthSlicesReg};
    static const uint32_t colorRegs[kAttachmentRegs] = {mmCB_COLOR0_BASE, mmCB_COLOR0_SIZE, mmCB_COLOR0_VIEW,
                                                        mmCB_COLOR0_INFO, mmCB_COLOR0_TILE, mmCB_COLOR0_FRAG};
    int bad = -1;
    for (uint32_t k = 0; bad < 0 && g_open.hasDepth && k < kAttachmentRegs; ++k)
        if (regs[depthRegs[k]] != g_open.depth.regs[k]) bad = int(100 + k);
    for (uint32_t i = 0; bad < 0 && i < kMaxColors; ++i)
        if (g_open.colorMask >> i & 1)
            for (uint32_t k = 0; bad < 0 && k < kAttachmentRegs; ++k)
                if (regs[colorRegs[k] + i] != g_open.colors[i].regs[k]) bad = int(i * 10 + k);
    if (bad < 0) {
        ++g_count[kDescribedDraws];
        g_lastDescribed = true;
        return;
    }
    ++g_count[kMismatchDraws];
    if (g_mismatchLogged++ < 16)
        LOG("[native-pass] draw in pass %u: targets differ from the description (%s, word %d; colour0 %08X vs %08X, "
            "depth %08X vs %08X)", g_open.id, bad >= 100 ? "depth" : "colour", bad >= 100 ? bad - 100 : bad,
            regs[mmCB_COLOR0_BASE], g_open.colors[0].image, regs[mmDB_DEPTH_BASE], g_open.depth.image);
}
void frame_end() {
    close_pass();
    if (++g_frames % 300) return;
    static const char* const names[kCounters] = {"passes", "described draws", "draws outside a described pass",
                                                 "draws whose targets differ", "load intents (clears)",
                                                 "clears after a draw", "ends", "scan-out copies", "depth expands",
                                                 "colour expands", "surface copies", "viewports described",
                                                 "scissors described", "draws with the described viewport and scissor",
                                                 "draws before any described viewport", "draws whose viewport or scissor differ",
                                                 "trusted target draws", "target draws resolved",
                                                 "trusted targets differing (verify)"};
    char buf[1000];
    int k = std::snprintf(buf, sizeof buf, "[native-pass] last 300 frames, per frame:");
    for (uint32_t c = 0; c < kCounters; ++c)
        k += std::snprintf(buf + k, sizeof buf - k, "%s %s %.1f", c ? "," : "", names[c], g_count[c] / 300.0);
    LOG("%s", buf);
    std::memset(g_count, 0, sizeof g_count);
}

}  // namespace native_pass
