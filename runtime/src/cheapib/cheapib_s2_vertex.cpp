// cheapib stage 2, CPU-written vertex sites (WWHD_CHEAPIB_S2 bit 64; decomp build, fast mode). See cheapib_s2.h.
//
// A replayed in-between calls the logic pass's lists again: their draws bind the vertex buffer the logic pass's site
// filled, and the render thread reads its bytes from guest memory when it executes them. So the site's bytes for a
// replayed pass are made on the game thread and written into that guest range on the render thread, in order: after
// the earlier passes' draws have read it, before this pass's calls (gx2 OP_CHEAPIB_VTX). Every vertex read path
// (draw.cpp's binding loop, the native packets' multi-draw, range and stream batches, the buffer cache: page write
// tracking sees the writes) then reads this pass's bytes. b1 notes the slices holding a site's bytes on the logic pass
// (cheapib_b1.h VRec) and rebuilds them from guest memory on its replays.
//
// Sea grid (daSea_packet_c::fillVtxBuf 0246C5E8, from daSea_Draw every pass): the 65x65 grid from the height table
// into mpVtxBuf's entry[mCurrentBuffer] (32-byte vertices, 0x21020 bytes), then a flip. The logic pass fills buffer L
// and leaves the index on the other one, N. A replayed pass runs the builder through interp_fx's hook (heights blended
// at the pass's fraction, the record pass exact and recorded, as on a full in-between) into N: N is the buffer the
// next logic pass fills anyway, and nothing reads it before then (the render thread read it for the previous step's
// logic pass, four passes back; the game's own double buffering assumes less than two). The index is put back, N's
// bytes go to L.
// The sea's material (daSea_packet_c::setUniforms 0246C7C8, a RenderUniformGroup_l at mpMaterial; the list binds
// each pair's mBuffers[mPreviousIndex]): the frame uniforms' camera fields get this pass's camera (as bit 4 does for
// the sway sites), and the "yura" scroll translation (the material values' texture matrix at +0x140, PSMTXTrans
// (0, counter / 300, 0); on a blended pass the halfway rule of interp_fx's PSMTXTrans hook) is written into the field
// of the logic buffer that holds it (found by its words on the logic pass, checked each step).
#include "cheapib/cheapib_s2.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "cheapib/cheapib.h"
#include "drawoff/drawoff.h"
#include "guest_addr.h"
#include "ppc.h"
#include "runtime.h"
#include "write_watch.h"

extern "C" void hook_0246C5E8(Cpu* c);
namespace interp {
bool hold_pass();
uint64_t logic_steps();
float pass_t();
uint64_t pass_count();
#ifdef WWHD_DECOMP_FAST  // (the fast build's: interp.cpp, interp_fx.cpp)
bool cheapib_blended_hold();
bool cheapib_set_blend_draw(bool on);
bool fx_sea_scroll(uint32_t pk, bool blended, float t, float& y);  // interp_fx.cpp
#endif
}  // namespace interp
#ifdef WWHD_DECOMP_FAST
namespace gx2 {
void cheapib_vtx_emit(void* writes);  // (gx2_core.cpp, fast build)
}
#endif

namespace cheapib {
namespace {

// the registered site ranges (game thread adds, any thread reads)
constexpr int kMaxRanges = 64;
std::atomic<uint64_t> g_ranges[kMaxRanges];  // lo | hi << 32
std::atomic<int> g_nranges{0};
void add_range(uint32_t lo, uint32_t size) {
    if (!lo || !size) return;
    const uint64_t v = uint64_t(lo) | (uint64_t(lo + size) << 32);
    const int n = g_nranges.load(std::memory_order_acquire);
    for (int i = 0; i < n; i++)
        if (g_ranges[i].load(std::memory_order_relaxed) == v) return;
    if (n >= kMaxRanges) return;
    g_ranges[n].store(v, std::memory_order_relaxed);
    g_nranges.store(n + 1, std::memory_order_release);
}

// the writes of one replayed pass (game thread builds, render thread applies and frees)
struct Writes {
    std::vector<uint32_t> ranges;  // (guest address, size, offset in bytes) per range
    std::vector<uint8_t> bytes;
};

// drawoff c2 (one pass in flight): the render thread reads a list's dynamic vertex ranges from the copy c2 takes when
// the game calls the list, not from guest memory. A replayed pass's writes reach guest memory on the render thread
// (OP_CHEAPIB_VTX), after the game has called its lists and c2 has copied the logic pass's bytes; so the game thread
// keeps this pass's writes too, and c2's copies of the pass take them (c2_vtx_patch, drawoff.cpp vsnap_build).
struct C2Pending {
    uint64_t pass = ~0ull;
    std::vector<uint32_t> ranges;  // (as Writes)
    std::vector<uint8_t> bytes;
} g_c2;
void send(Writes* w) {
#ifdef WWHD_DECOMP_FAST  // (drawoff and the render thread's OP_CHEAPIB_VTX are the fast build's; stage 2 is off without it)
    if (drawoff::g_vertex_on) {  // (c2; drawoff::copies_on() is c1's ring copies, off with c2)
        const uint64_t p = interp::pass_count();
        if (g_c2.pass != p) {
            g_c2.pass = p;
            g_c2.ranges.clear();
            g_c2.bytes.clear();
        }
        const uint32_t base = (uint32_t)g_c2.bytes.size();
        for (size_t i = 0; i + 3 <= w->ranges.size(); i += 3)
            g_c2.ranges.insert(g_c2.ranges.end(), {w->ranges[i], w->ranges[i + 1], w->ranges[i + 2] + base});
        g_c2.bytes.insert(g_c2.bytes.end(), w->bytes.begin(), w->bytes.end());
    }
    gx2::cheapib_vtx_emit(w);
#else
    delete w;
#endif
}

// daSea_packet_c (d_a_sea.h)
constexpr uint32_t kSeaVtxBuf = 0x250, kSeaMaterial = 0x274;
constexpr uint32_t kSetCurrent = 0x4A8, kEntry = 0x254, kEntrySize = 0x150, kGridBytes = 0x21020;
// RenderUniformGroup_l: frames span +0 (count, storage), matrix pair +0xC, material pair +0xB4; a pair's mBuffers[2] at
// +0x10 (28 bytes each: +0 format, +4 CPU storage), mPreviousIndex +0x4C; the material values at pair +0x74
constexpr uint32_t kMaterialPair = 0xB4, kPairBuffers = 0x10, kBufferSize = 28, kPairPrevious = 0x4C;
constexpr uint32_t kScrollMtx = 0x128 + 0x140;  // (in the group: the material values' scroll matrix, 3x4)
constexpr uint32_t kTypeTable = 0x10169D50;
constexpr uint32_t kView = 0x104B45F8, kProj = 0x104B470C;
constexpr uint32_t kCopy34 = 0x028E90D4, kCompose = 0x0272D680, kWriteProj = 0x028E8970;
constexpr uint32_t kFramePrimary = 0x74, kFrameSecondary = 0xA4, kFrameFinal = 0x1FC;

struct Sea {
    uint32_t pk = 0, set = 0, logic_buf = 0, size = 0;
    uint64_t step = ~0ull;  // the logic step it was noted on
    // material: the frame uniforms object and the logic buffers of the frames and the material pair
    uint32_t frame = 0, frame_buf = 0, mat_buf = 0;
    uint32_t scroll_at = 0;  // the scroll translation word in the material buffer's CPU storage (0: not found)
    uint32_t cam[28] = {};   // the view (12) and projection (16) the frame buffer holds now
};
struct S {
    Sea sea;
    uint64_t passes = 0, written = 0, bytes = 0, ns = 0, skipped = 0, same = 0, cam = 0, scroll = 0, scroll_bad = 0;
    uint64_t logic = 0, c2_patched = 0, c2_older = 0;
    // render thread
    uint64_t applied = 0, applied_bytes = 0;
} g;

bool vtx_on() { return (s2_mask() & 64) != 0; }

// agl's writeWords layout (as cheapib_s2_sway.cpp write_field): COUNT elements of field INDEX, word-reversed
bool write_field(uint32_t buffer, uint32_t index, const uint32_t* src, int count) {
    const uint32_t format = ld32(buffer), cpu = ld32(buffer + 4);
    const uint32_t fields = format ? ld32(format) : 0;
    if (!cpu || !fields || index >= ld16(format + 6)) return false;
    const uint32_t f = fields + 6 * index, type = ld8(f), offset = ld16(f + 4);
    const uint32_t types = GD(kTypeTable);
    const uint32_t words = ld8(types + 3 * type), stride = ld8(types + 3 * type + 2);
    uint32_t base = cpu + offset;
    for (int e = 0; e < count; e++, base += 4 * stride)
        for (uint32_t w = 0; w < words; w++) st32(base + 4 * w, __builtin_bswap32(*src++));
    wwatch::hint(cpu + offset, 4 * stride * count);
    return true;
}
// the field of BUFFER whose 3 rows of 4 words hold the matrix M (host-order words): the address of its row 1 word 3
// (the translation y); 0 when none or more than one
uint32_t find_matrix(uint32_t buffer, const uint32_t m[12]) {
    const uint32_t format = ld32(buffer), cpu = ld32(buffer + 4);
    const uint32_t fields = format ? ld32(format) : 0;
    if (!cpu || !fields) return 0;
    const uint32_t n = ld16(format + 6), types = GD(kTypeTable);
    uint32_t found = 0;
    int hits = 0;
    for (uint32_t i = 0; i < n && i < 64; i++) {
        const uint32_t f = fields + 6 * i, type = ld8(f), count = ld16(f + 2), offset = ld16(f + 4);
        const uint32_t words = ld8(types + 3 * type), stride = ld8(types + 3 * type + 2);
        if (words != 4 || count < 3) continue;
        bool same = true;
        for (int r = 0; r < 3 && same; r++)
            for (int w = 0; w < 4 && same; w++) same = __builtin_bswap32(ld32(cpu + offset + 4 * (stride * r + w))) == m[4 * r + w];
        if (same) {
            hits++;
            found = cpu + offset + 4 * (stride * 1 + 3);
        }
    }
    return hits == 1 ? found : 0;
}

}  // namespace

// shared with the other vertex sites (cheapib_s2_sky.cpp)
void vtx_add_range(uint32_t lo, uint32_t size) { add_range(lo, size); }
void vtx_emit(std::vector<uint32_t>&& ranges, std::vector<uint8_t>&& bytes) {
    if (ranges.empty()) return;
    Writes* w = new Writes;
    w->ranges = std::move(ranges);
    w->bytes = std::move(bytes);
    send(w);
}

bool vtx_site(uint32_t addr, uint32_t size) {
    if (ptcl_site(addr, size)) return true;  // (bit 2048: the particles' vertex buffers, cheapib_s2_particles.cpp)
    const int n = g_nranges.load(std::memory_order_acquire);
    const uint64_t end = uint64_t(addr) + size;
    for (int i = 0; i < n; i++) {
        const uint64_t v = g_ranges[i].load(std::memory_order_relaxed);
        if (addr < (v >> 32) && end > (v & 0xFFFFFFFFull)) return true;
    }
    return false;
}

// game thread (drawoff.cpp vsnap_build, c2): DST holds guest [ADDR, END) as c2 copied it now; this pass's replayed
// writes overlapping it go in
void c2_vtx_patch(uint32_t addr, uint32_t end, uint8_t* dst) {
    if (g_c2.ranges.empty()) return;
    if (g_c2.pass != interp::pass_count()) {  // (an earlier pass's writes: not this call's)
        g.c2_older++;
        return;
    }
    for (size_t i = 0; i + 3 <= g_c2.ranges.size(); i += 3) {
        const uint32_t a = g_c2.ranges[i], n = g_c2.ranges[i + 1], at = g_c2.ranges[i + 2];
        const uint32_t lo = std::max(a, addr), hi = (uint32_t)std::min<uint64_t>(uint64_t(a) + n, end);
        if (lo >= hi) continue;
        memcpy(dst + (lo - addr), g_c2.bytes.data() + at + (lo - a), hi - lo);
        g.c2_patched++;
    }
}

// render thread (OP_CHEAPIB_VTX): this pass's bytes into the sites' guest ranges
void vtx_apply(void* p) {
    Writes* w = static_cast<Writes*>(p);
    if (!w) return;
    for (size_t i = 0; i + 3 <= w->ranges.size(); i += 3) {
        const uint32_t addr = w->ranges[i], size = w->ranges[i + 1], at = w->ranges[i + 2];
        memcpy(PPC_MEM_BASE + addr, w->bytes.data() + at, size);
        wwatch::hint(addr, size);
        g.applied++;
        g.applied_bytes += size;
    }
    delete w;
}

// interp_fx hook_0246C5E8 (the sea grid builder), before it runs: on the logic pass, the buffer it fills
void s2_sea_fill(uint32_t pk) {
    if (!vtx_on() || interp::hold_pass()) return;
    const uint32_t set = ld32(pk + kSeaVtxBuf);
    if (!set) return;
    const uint32_t idx = ld32(set + kSetCurrent) & 1;
    Sea& s = g.sea;
    s.pk = pk;
    s.set = set;
    s.logic_buf = ld32(set + kEntry * idx);
    const uint32_t size = ld32(set + kEntry * idx + kEntrySize);
    s.size = size >= kGridBytes && size <= 2 * kGridBytes ? size : kGridBytes;
    s.step = interp::logic_steps();
    for (uint32_t i = 0; i < 2; i++) add_range(ld32(set + kEntry * i), s.size);
    g.logic++;
}

// the logic pass's per-frame function returned: the sea material's logic buffers (the setup advanced its pairs)
void s2_vtx_logic_done() {
    if (!vtx_on()) return;
    Sea& s = g.sea;
    if (s.step != interp::logic_steps() || !s.pk) return;
    const uint32_t mat = ld32(s.pk + kSeaMaterial);
    s.frame = s.frame_buf = s.mat_buf = 0;
    if (!mat) return;
    if ((int32_t)ld32(mat) >= 1 && ld32(mat + 4)) {
        s.frame = ld32(mat + 4);
        s.frame_buf = s.frame + kPairBuffers + kBufferSize * (ld32(s.frame + kPairPrevious) & 1);
        for (int i = 0; i < 12; i++) s.cam[i] = ld32(GD(kView) + 4 * i);
        for (int i = 0; i < 16; i++) s.cam[12 + i] = ld32(GD(kProj) + 4 * i);
    }
    const uint32_t pair = mat + kMaterialPair;
    s.mat_buf = pair + kPairBuffers + kBufferSize * (ld32(pair + kPairPrevious) & 1);
    s.scroll_at = 0;  // (found at the first replay: the logic pass's upload has landed by then)
}

// a replayed in-between (cheapib_models.h replay, after the models' drain)
void s2_vtx_rerun(Cpu* c) {
#ifndef WWHD_DECOMP_FAST  // (stage 2 exists in the fast build only: vtx_on() is false without it)
    (void)c;
#else
    if (!vtx_on() || !replaying() || verifying()) return;
    Sea& s = g.sea;
    if (!s.pk || s.step != interp::logic_steps()) return;  // (this step's logic pass drew no sea)
    const auto t0 = std::chrono::steady_clock::now();
    g.passes++;
    // the grid into the other buffer, then into the logic buffer on the render thread
    const uint32_t idx = ld32(s.set + kSetCurrent);
    const uint32_t other = ld32(s.set + kEntry * (idx & 1));
    if (!other || other == s.logic_buf) {
        g.skipped++;
    } else {
        Cpu cc = *c;
        cc.r[3] = s.pk;
        const bool blend = interp::cheapib_set_blend_draw(interp::cheapib_blended_hold());
        hook_0246C5E8(&cc);
        interp::cheapib_set_blend_draw(blend);
        st32(s.set + kSetCurrent, idx);
        Writes* w = new Writes;
        w->ranges = {s.logic_buf, s.size, 0};
        w->bytes.assign(PPC_MEM_BASE + other, PPC_MEM_BASE + other + s.size);
        send(w);
        g.written++;
        g.bytes += s.size;
    }
    // the material: this pass's camera in the frame uniforms, the scroll in the material values
    if (s.frame) {
        uint32_t cam[28];
        for (int i = 0; i < 12; i++) cam[i] = ld32(GD(kView) + 4 * i);
        for (int i = 0; i < 16; i++) cam[12 + i] = ld32(GD(kProj) + 4 * i);
        if (memcmp(cam, s.cam, sizeof cam) != 0) {
            Cpu cc = *c;
            const uint32_t f = s.frame;
            guest_call(&cc, kCopy34, {GD(kView), f + kFramePrimary});
            guest_call(&cc, kCompose, {f + kFrameSecondary, GD(kProj), GD(kView)});
            guest_call(&cc, kWriteProj, {GD(kProj), f + kFrameFinal});
            uint32_t w[16];
            bool ok = true;
            for (int i = 0; i < 12; i++) w[i] = ld32(f + kFramePrimary + 4 * i);
            ok &= write_field(s.frame_buf, 0, w, 3);
            for (int i = 0; i < 16; i++) w[i] = ld32(f + kFrameSecondary + 4 * i);
            ok &= write_field(s.frame_buf, 1, w, 4);
            for (int i = 0; i < 16; i++) w[i] = ld32(f + kFrameFinal + 4 * i);
            ok &= write_field(s.frame_buf, 8, w, 4);
            if (!ok) s.frame = 0;
            memcpy(s.cam, cam, sizeof cam);
            g.cam++;
        }
    }
    if (s.mat_buf) {
        const uint32_t mat = ld32(s.pk + kSeaMaterial);
        if (!s.scroll_at) {
            uint32_t m[12];
            for (int i = 0; i < 12; i++) m[i] = ld32(mat + kScrollMtx + 4 * i);
            s.scroll_at = find_matrix(s.mat_buf, m);
            if (!s.scroll_at) {
                g.scroll_bad++;
                s.mat_buf = 0;
            }
        }
        float y;
        if (s.scroll_at && interp::fx_sea_scroll(s.pk, interp::cheapib_blended_hold(), interp::pass_t(), y)) {
            uint32_t yw;
            memcpy(&yw, &y, 4);
            st32(s.scroll_at, __builtin_bswap32(yw));
            wwatch::hint(s.scroll_at, 4);
            g.scroll++;
        }
    }
    g.ns += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    if (g.passes % 900 == 0) {
        auto per = [](uint64_t v, uint64_t d) { return d ? double(v) / double(d) : 0.0; };
        LOG("[cheapib] s2 vertex: per replayed pass %.1f us; sea grids written %.2f (%.1f KB each, skipped %llu), camera "
            "%.2f, scroll %.2f (not found %llu); logic sea fills %llu; c2 copies patched %llu (calls with an earlier pass's writes %llu); render thread: ranges applied "
            "%llu (%.1f KB)",
            per(g.ns, g.passes) / 1000.0, per(g.written, g.passes), per(g.bytes, g.written) / 1024.0,
            (unsigned long long)g.skipped, per(g.cam, g.passes), per(g.scroll, g.passes), (unsigned long long)g.scroll_bad,
            (unsigned long long)g.logic, (unsigned long long)g.c2_patched, (unsigned long long)g.c2_older, (unsigned long long)g.applied,
            double(g.applied_bytes) / 1024.0);
        g.passes = g.written = g.bytes = g.ns = g.skipped = g.cam = g.scroll = g.logic = g.c2_patched = g.c2_older = 0;
        g.applied = g.applied_bytes = 0;
    }
#endif
}

}  // namespace cheapib
