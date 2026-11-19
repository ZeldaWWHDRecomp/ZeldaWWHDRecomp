// cheapib stage 2, the sun, moon and lens flare (WWHD_CHEAPIB_S2 bit 512); decomp build, fast mode. See cheapib_s2.h.
//
// A full in-between paints the sun packet (dKyr_drawSun 0256F1C4: 2 moon quads, 2 sun quads; the second sun quad is the
// glow, sized by the sun's visibility) and the lens flare packet (dKyr_drawLenzflare 02570210: 9 quads and 16 ray
// triangles) on every pass, placed around the eye the pass was drawn with (interp.cpp with_drawn_camera: the camera
// swapped, the sun's and the flare's positions moved with sky_follow_eye / sky_offset_blend). Their vertices are
// CPU-written into double-buffered slots; a replayed in-between showed the logic pass's.
//
// Here a replayed in-between runs the two draw functions again, through their interp hooks (so with the same camera
// and positions a full in-between would use), with the packets' render (dKankyo_sun_Packet::render 0257E9DC,
// dKankyo_sunlenz_Packet::render 0257EF00: uploads, draw commands, the slot flip) skipped by the decomp port point
// PORT_CHEAPIB_SKY_RENDER. The functions write the side the logic pass did not draw (the one the next logic pass fills;
// the last pass that bound it is two or more passes back, as with every pass of a full in-between); the port copies
// those bytes and sends them (OP_CHEAPIB_VTX) into the logic pass's side, then puts back the flare quads' selectors
// (drawLenzflare flips each one itself). Only the quads the logic pass drew and this pass wrote are sent: moon rows
// and sun rows by the render's flags, the flare quads when its render was reached, each ray when this pass wrote it
// (the buffer is filled with a marker first). The colours the functions set come from the step's sun state
// (visibility, alpha, falloff: dKyr_sun_move, logic only), the same values the logic pass set.
// Hooks: decomp d_kankyo_wether.cpp (PORT_CHEAPIB_SKY_RENDER); cheapib_models.h replay() -> s2_sun_rerun().
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
constexpr uint32_t kEnvSun = 0xA34, kEnvLenz = 0xA38;
constexpr uint32_t kViewMatrix = 0x104B45F8;  // weatherViewMatrix (the draws' first argument, unused by them)
constexpr uint32_t kDrawSun = 0x0256F1C4, kDrawLenz = 0x02570210;
constexpr uint32_t kSlotSize = 0x254, kGeometrySize = 0x4C0, kGeometrySelected = 0x4A8;
// sun packet: position +0x98, images +0xD4, geometry +0xF0 (moon 0, moon 1, sun 0, sun 1)
constexpr uint32_t kSunPos = 0x98, kSunImages = 0xD4, kSunGeometry = 0xF0;
// lens flare packet: positions +0xA4, quads +0x130 (9 Geometry), rays +0x51D4 (32 slots: ray i side s = 2 i + s,
// selector +0x4A80)
constexpr uint32_t kLenzPos = 0xA4, kLenzQuads = 0x130, kLenzRays = 0x51D4, kRaySelected = 0x4A80;
constexpr uint32_t kQuadBytes = 0x260, kRayBytes = 0x1C0;

struct Note {
    uint32_t pk = 0;
    uint64_t step = ~0ull;  // the logic step whose painter rendered the packet
    uint32_t a = 0, b = 0;  // (sun: moon rows, sun rows)
};
struct G {
    Note sun, lenz;
    bool capture = false;  // inside this port's own call of a draw function
    bool cap_sun = false, cap_lenz = false;
    uint32_t cap_a = 0, cap_b = 0;
    uint32_t sun_lo = 0, sun_hi = 0, lenz_lo = 0, lenz_hi = 0;  // the ranges registered
    // stats (per report)
    uint64_t passes = 0, ns = 0, sun_quads = 0, lenz_quads = 0, rays = 0, no_render = 0;
} g;

bool sun_on() { return (s2_mask() & 512) != 0; }
inline float wf(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }
inline bool guest_ok(uint32_t a) { return a >= 0x10000000 && a < 0x50000000; }
uint32_t slot_buf(uint32_t geometry, uint32_t side) { return ld32(geometry + side * kSlotSize); }

void reg(uint32_t& lo_out, uint32_t& hi_out, const std::vector<uint32_t>& bufs, uint32_t size) {
    uint32_t lo = ~0u, hi = 0;
    for (uint32_t b : bufs)
        if (b) lo = std::min(lo, b), hi = std::max(hi, b + size);
    if (lo >= hi || hi - lo > (1u << 20)) {  // (not allocated together: each on its own)
        for (uint32_t b : bufs)
            if (b) vtx_add_range(b, size);
        return;
    }
    if (lo_out != lo || hi_out != hi) vtx_add_range(lo, hi - lo);
    lo_out = lo, hi_out = hi;
}

void add(std::vector<uint32_t>& ranges, std::vector<uint8_t>& bytes, uint32_t to, uint32_t from, uint32_t size) {
    ranges.insert(ranges.end(), {to, size, (uint32_t)bytes.size()});
    const uint8_t* p = ppc_ptr(from);
    bytes.insert(bytes.end(), p, p + size);
}

}  // namespace

// the decomp's port point: a sky packet's render is about to run (any pass)
int sky_render(int kind, uint32_t self, uint32_t a, uint32_t b) {
    // debug: WWHD_CHEAPIB_SUN_DBG=n logs the sun's glow quad (sun 1) as the next render binds it (any pass, also with
    // stage 2 off) and as a replayed in-between computes it, n lines each
    static int dbg = getenv("WWHD_CHEAPIB_SUN_DBG") ? atoi(getenv("WWHD_CHEAPIB_SUN_DBG")) : 0;
    if (dbg > 0 && kind == 1) {
        dbg--;
        LOG("[cheapib] sun dbg lenz %s step %llu: falloff %.4f", g.capture ? "replay" : "render",
            (unsigned long long)interp::logic_steps(), (double)wf(ld32(self + 0x124)));
    }
    if (dbg > 0 && kind == 0) {
        dbg--;
        const uint32_t geo = self + kSunGeometry + kGeometrySize * 3, buf = slot_buf(geo, ld32(geo + kGeometrySelected) & 1);
        auto f = [&](uint32_t o) { return (double)wf(ld32(buf + o)); };
        LOG("[cheapib] sun dbg %s step %llu: glow v0 %.3f %.3f %.3f v2 %.3f %.3f %.3f vis %.4f pos %.2f %.2f %.2f",
            g.capture ? "replay" : "render", (unsigned long long)interp::logic_steps(),
            f(0), f(4), f(8), f(0x130), f(0x134), f(0x138), (double)wf(ld32(self + 0xC8)), (double)wf(ld32(self + 0x98)),
            (double)wf(ld32(self + 0x9C)), (double)wf(ld32(self + 0xA0)));
    }
    if (kind >= 2) return weather_render(kind, self);
    if (!sun_on()) return 0;
    if (g.capture) {
        if (kind == 0) g.cap_sun = true, g.cap_a = a, g.cap_b = b;
        else g.cap_lenz = true;
        return 1;
    }
    if (replaying() || verifying()) return 0;
    Note& n = kind == 0 ? g.sun : g.lenz;
    n.pk = self;
    n.step = interp::logic_steps();
    n.a = a, n.b = b;
    return 0;
}

// a replayed in-between (cheapib_models.h replay, after stage 1's camera and models)
void s2_sun_rerun(Cpu* c) {
    if (!sun_on() || !replaying() || verifying()) return;
    const uint64_t step = interp::logic_steps();
    const uint32_t env = GD(kEnv);
    const uint32_t sun = ld32(env + kEnvSun), lenz = ld32(env + kEnvLenz);
    const bool do_sun = guest_ok(sun) && g.sun.pk == sun && g.sun.step == step && (g.sun.a || g.sun.b);
    const bool do_lenz = guest_ok(lenz) && g.lenz.pk == lenz && g.lenz.step == step && guest_ok(sun);
    if (!do_sun && !do_lenz) return;
    if (!ld32(GD(kGameInfo) + kGiView)) return;  // (drawSun would note an occlusion step then: game state)
    const auto t0 = std::chrono::steady_clock::now();
    Cpu cc = *c;
    cc.r[1] = ((cc.r[1] - 0x400) & ~0x3Fu) - 0x100;
    std::vector<uint32_t> ranges;
    std::vector<uint8_t> bytes;
    g.passes++;

    if (do_sun) {
        // the side each quad's next write goes to (free) and the logic pass's (drawn: render flipped after the write)
        std::vector<uint32_t> all;
        uint32_t freeb[4], logicb[4];
        for (int i = 0; i < 4; i++) {
            const uint32_t geo = sun + kSunGeometry + kGeometrySize * i;
            const uint32_t sel = ld32(geo + kGeometrySelected) & 1;
            freeb[i] = slot_buf(geo, sel);
            logicb[i] = slot_buf(geo, sel ^ 1);
            all.push_back(freeb[i]);
            all.push_back(logicb[i]);
        }
        reg(g.sun_lo, g.sun_hi, all, kQuadBytes);
        g.capture = true;
        g.cap_sun = false;
        guest_call(&cc, kDrawSun, {GD(kViewMatrix), sun + kSunPos, sun + kSunImages});
        g.capture = false;
        if (g.cap_sun) {
            for (int i = 0; i < 4; i++) {
                const bool moon = i < 2;
                if (!(moon ? (g.sun.a && g.cap_a) : (g.sun.b && g.cap_b))) continue;
                if (!freeb[i] || !logicb[i]) continue;
                add(ranges, bytes, logicb[i], freeb[i], kQuadBytes);
                g.sun_quads++;
            }
        } else {
            g.no_render++;
        }
    }

    if (do_lenz) {
        std::vector<uint32_t> all;
        uint32_t qsel[9], qfree[9], qlogic[9];
        for (int i = 0; i < 9; i++) {
            const uint32_t geo = lenz + kLenzQuads + kGeometrySize * i;
            qsel[i] = ld32(geo + kGeometrySelected);
            qfree[i] = slot_buf(geo, qsel[i] & 1);
            qlogic[i] = slot_buf(geo, (qsel[i] & 1) ^ 1);
            all.push_back(qfree[i]);
            all.push_back(qlogic[i]);
        }
        reg(g.lenz_lo, g.lenz_hi, all, kQuadBytes);
        // rays: drawLenzflare writes ray i into slot 2 i + selector; the render flips the selector after
        const uint32_t rays = lenz + kLenzRays;
        const uint32_t rsel = ld32(rays + kRaySelected) & 1;
        uint32_t rfree[16], rlogic[16], rword[16];
        std::vector<uint32_t> rall;
        for (int i = 0; i < 16; i++) {
            rfree[i] = ld32(rays + (2 * i + rsel) * kSlotSize);
            rlogic[i] = ld32(rays + (2 * i + (rsel ^ 1)) * kSlotSize);
            rall.push_back(rfree[i]);
            rall.push_back(rlogic[i]);
            rword[i] = rfree[i] ? ld32(rfree[i]) : 0;
            if (rfree[i]) st32(rfree[i], 0xFFFFFFFFu);  // (marker: the write clears the buffer first)
        }
        static uint32_t rlo = 0, rhi = 0;
        reg(rlo, rhi, rall, kRayBytes);
        g.capture = true;
        g.cap_lenz = false;
        guest_call(&cc, kDrawLenz, {GD(kViewMatrix), lenz + kLenzPos, sun + kSunPos});
        g.capture = false;
        if (g.cap_lenz) {
            for (int i = 0; i < 9; i++) {
                if (!qfree[i] || !qlogic[i]) continue;
                add(ranges, bytes, qlogic[i], qfree[i], kQuadBytes);
                g.lenz_quads++;
            }
            for (int i = 0; i < 16; i++) {
                if (!rfree[i] || !rlogic[i] || ld32(rfree[i]) == 0xFFFFFFFFu) continue;
                add(ranges, bytes, rlogic[i], rfree[i], kRayBytes);
                g.rays++;
            }
        } else {
            g.no_render++;
        }
        for (int i = 0; i < 16; i++)  // (a ray not written now: its buffer as it was)
            if (rfree[i] && ld32(rfree[i]) == 0xFFFFFFFFu) st32(rfree[i], rword[i]);
        // drawLenzflare flips each quad's selector after its write: the logic pass's sides stay the drawn ones
        for (int i = 0; i < 9; i++) st32(lenz + kLenzQuads + kGeometrySize * i + kGeometrySelected, qsel[i]);
    }

    vtx_emit(std::move(ranges), std::move(bytes));
    g.ns += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    if (g.passes % 900 == 0) {
        auto per = [](uint64_t v, uint64_t d) { return d ? double(v) / double(d) : 0.0; };
        LOG("[cheapib] s2 sun: per replayed pass %.1f us; sun/moon quads %.2f, flare quads %.2f, rays %.2f per pass "
            "(draws that did not reach their render %llu)",
            per(g.ns, g.passes) / 1000.0, per(g.sun_quads, g.passes), per(g.lenz_quads, g.passes),
            per(g.rays, g.passes), (unsigned long long)g.no_render);
        g.passes = g.ns = g.sun_quads = g.lenz_quads = g.rays = g.no_render = 0;
    }
}

}  // namespace cheapib

extern "C" int wwhd_cheapib_sky_render(int kind, unsigned self, unsigned a, unsigned b) {
    return cheapib::sky_render(kind, self, a, b);
}
