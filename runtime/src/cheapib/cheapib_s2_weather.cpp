// cheapib stage 2, the weather sprites (WWHD_CHEAPIB_S2 bit 1024); decomp build, fast mode. See cheapib_s2.h and
// cheapib_s2_sun.cpp (the same way, for the sun and lens flare).
//
// Rain and its sea spray (dKankyo_rain_Packet::draw 0257F32C: dKyr_drawSibuki 02571B1C, dKyr_drawRain 025712A8), snow and
// volcanic ash (dKankyo_snow_Packet::draw 02580204: dKyr_drawKazanbai 02572FC4), stars (dKankyo_star_Packet::draw
// 02580FB8: dKyr_drawStar 02574144), poison mist (0258142C), spores (housi, 02581B50) and cloud shadows (moya, 02582010)
// are painted on every pass of a full in-between: each sprite's vertices CPU-written (from its records, which interp_fx
// blends per pass, and the camera) into the selected side of its Geometry (two buffers, the selector at +0x4A8; a star:
// two buffers per side, selector +0x950), the selector flipped after the write; the packet's render (a separate
// function) then binds the side written. A replayed in-between showed the logic pass's sprites.
//
// Here a replayed in-between calls the packet's draw again (the draw functions through their interp hooks, as the
// painter would), with the packet's render skipped by the decomp port point PORT_CHEAPIB_SKY_RENDER; only packets whose
// render the logic pass of this step reached. The sprites written now are the ones whose selector flipped: their bytes
// (the side the logic pass did not draw: the one the next logic pass fills) are sent with OP_CHEAPIB_VTX into the side
// the logic pass drew, and every selector is put back. A sprite the logic pass drew but this pass does not keeps the
// logic pass's bytes. Colours (per-sprite uniforms set by the draws, e.g. the rain's alpha by distance) are uploaded by
// the render: they stay the logic pass's. Game state the draws write (the drawn flags) is written as on every pass of a
// full in-between.
#include "cheapib/cheapib_s2.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "cheapib/cheapib.h"
#include "guest_addr.h"
#include "ppc.h"
#include "runtime.h"

namespace interp {
uint64_t logic_steps();
}  // namespace interp

namespace cheapib {
namespace {

constexpr uint32_t kEnv = 0x10475A68, kGameInfo = 0x1046F0B0, kGiView = 0x5FA4;
constexpr uint32_t kSlotSize = 0x254, kSlotBytes = 0x150;  // BufferSlot: buffer +0, size +0x150

struct Array {
    uint32_t off, count, stride, sel;
    uint32_t nbuf;  // buffers per side: side s buffer j at slot (s + 2 j)
};
struct Site {
    const char* name;
    uint32_t env, draw;
    int kinds[2];  // render port point kinds
    Array arr[2];
};
// (layouts: d_kankyo_rain_cloudShadowMove.h RainReadable, the ones the draw functions use; star: drawSibuki.h)
const Site kSites[] = {
    {"rain", 0xA44, 0x0257F32C, {2, 3}, {{0x389C, 1125, 0x4C0, 0x4A8, 1}, {0, 0, 0, 0, 0}}},  // 1000 rain + 125 spray
    {"snow", 0xA50, 0x02580204, {8, 9}, {{0x376C, 5000, 0x4C0, 0x4A8, 1}, {0, 0, 0, 0, 0}}},  // 1000 snow + 4000 ash
    {"star", 0xA60, 0x02580FB8, {6, -1}, {{0x8A8, 2000, 0x968, 0x950, 2}, {0, 0, 0, 0, 0}}},
    {"poison", 0xA6C, 0x0258142C, {5, -1}, {{0xC028, 1000, 0x4C0, 0x4A8, 1}, {0, 0, 0, 0, 0}}},
    {"housi", 0xA78, 0x02581B50, {4, -1}, {{0x60CC, 600, 0x4C0, 0x4A8, 1}, {0, 0, 0, 0, 0}}},
    {"moya", 0xA84, 0x02582010, {7, -1}, {{0x994, 30, 0x4C0, 0x4A8, 1}, {0, 0, 0, 0, 0}}},
};
constexpr int kNumSites = sizeof(kSites) / sizeof(kSites[0]);
constexpr int kKinds = 10;

struct G {
    uint32_t pk[kNumSites] = {};
    uint32_t reg_pk[kNumSites] = {};  // the packet whose buffers are registered (b1)
    uint64_t step[kNumSites];  // the logic step whose painter reached the packet's render
    bool capture = false;
    std::vector<uint32_t> sel;  // selectors before the call (all arrays of a site)
    uint64_t passes = 0, ns = 0, sprites[kNumSites] = {}, calls[kNumSites] = {}, bad = 0;
    G() { std::fill(std::begin(step), std::end(step), ~0ull); }
} g;

bool weather_on() { return (s2_mask() & 1024) != 0; }
inline bool guest_ok(uint32_t a) { return a >= 0x10000000 && a < 0x50000000; }
int site_of_kind(int kind) {
    for (int s = 0; s < kNumSites; s++)
        for (int k : kSites[s].kinds)
            if (k == kind) return s;
    return -1;
}

}  // namespace

// the decomp's port point for kinds 2..9 (cheapib_s2_sun.cpp routes them here)
int weather_render(int kind, uint32_t self) {
    if (!weather_on()) return 0;
    if (g.capture) return 1;
    if (replaying() || verifying()) return 0;
    const int s = site_of_kind(kind);
    if (s < 0) return 0;
    g.pk[s] = self;
    g.step[s] = interp::logic_steps();
    return 0;
}

// a replayed in-between (cheapib_models.h replay, after stage 1's camera and models)
void s2_weather_rerun(Cpu* c) {
    if (!weather_on() || !replaying() || verifying()) return;
    const uint64_t step = interp::logic_steps();
    const uint32_t env = GD(kEnv);
    bool any = false;
    for (int s = 0; s < kNumSites; s++) any |= g.step[s] == step;
    if (!any || !ld32(GD(kGameInfo) + kGiView)) return;
    const auto t0 = std::chrono::steady_clock::now();
    Cpu cc = *c;
    cc.r[1] = ((cc.r[1] - 0x400) & ~0x3Fu) - 0x100;
    std::vector<uint32_t> ranges;
    std::vector<uint8_t> bytes;
    g.passes++;
    for (int s = 0; s < kNumSites; s++) {
        const Site& site = kSites[s];
        const uint32_t pk = ld32(env + site.env);
        if (g.step[s] != step || !guest_ok(pk) || pk != g.pk[s]) continue;
        if (g.reg_pk[s] != pk) {  // (b1: one range per array, all its buffers; they are allocated together)
            g.reg_pk[s] = pk;
            for (const Array& a : site.arr) {
                if (!a.count) continue;
                uint32_t lo = ~0u, hi = 0;
                for (uint32_t i = 0; i < a.count; i++)
                    for (uint32_t j = 0; j < 2 * a.nbuf; j++) {
                        const uint32_t slot = pk + a.off + a.stride * i + j * kSlotSize, b = ld32(slot);
                        const uint32_t size = ld32(slot + kSlotBytes);
                        if (!guest_ok(b) || !size || size > 0x400) continue;  // (as the copy below: a slot never set up)
                        lo = std::min(lo, b), hi = std::max(hi, b + size);
                    }
                if (lo < hi && hi - lo <= (8u << 20)) vtx_add_range(lo, hi - lo);
                else LOG("[cheapib] s2 weather: %s buffers not together (%08X..%08X): not registered for b1", site.name, lo, hi);
            }
        }
        g.sel.clear();
        for (const Array& a : site.arr)
            for (uint32_t i = 0; i < a.count; i++) g.sel.push_back(ld32(pk + a.off + a.stride * i + a.sel));
        g.capture = true;
        guest_call(&cc, site.draw, {pk});
        g.capture = false;
        g.calls[s]++;
        size_t k = 0;
        for (const Array& a : site.arr)
            for (uint32_t i = 0; i < a.count; i++, k++) {
                const uint32_t geo = pk + a.off + a.stride * i;
                const uint32_t before = g.sel[k], now = ld32(geo + a.sel);
                if (now == before) continue;
                st32(geo + a.sel, before);  // (the logic pass's sides stay the drawn ones)
                if (before > 1 || now > 1) {
                    g.bad++;
                    continue;
                }
                for (uint32_t j = 0; j < a.nbuf; j++) {
                    const uint32_t wslot = geo + (before + 2 * j) * kSlotSize, lslot = geo + ((before ^ 1) + 2 * j) * kSlotSize;
                    const uint32_t wb = ld32(wslot), lb = ld32(lslot), size = ld32(wslot + kSlotBytes);
                    if (!guest_ok(wb) || !guest_ok(lb) || !size || size > 0x400 || ld32(lslot + kSlotBytes) != size) {
                        g.bad++;
                        continue;
                    }
                    ranges.insert(ranges.end(), {lb, size, (uint32_t)bytes.size()});
                    const uint8_t* p = ppc_ptr(wb);
                    bytes.insert(bytes.end(), p, p + size);
                }
                g.sprites[s]++;
            }
    }
    vtx_emit(std::move(ranges), std::move(bytes));
    g.ns += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    if (g.passes % 900 == 0) {
        auto per = [](uint64_t v, uint64_t d) { return d ? double(v) / double(d) : 0.0; };
        char sites[512];
        int n = 0;
        for (int s = 0; s < kNumSites; s++)
            if (g.calls[s])
                n += snprintf(sites + n, sizeof sites - n, " %s %.1f/%llu", kSites[s].name, per(g.sprites[s], g.calls[s]),
                              (unsigned long long)g.calls[s]);
        LOG("[cheapib] s2 weather: per replayed pass %.1f us; sprites per call / calls:%s; skipped (layout) %llu",
            per(g.ns, g.passes) / 1000.0, n ? sites : " none", (unsigned long long)g.bad);
        g.passes = g.ns = g.bad = 0;
        std::fill(std::begin(g.sprites), std::end(g.sprites), 0);
        std::fill(std::begin(g.calls), std::end(g.calls), 0);
    }
}

}  // namespace cheapib
