// Frame interpolation of effects. See interp.cpp for the pass structure: logic pass (logic
// S -> S+1, everything drawn at t = 1/(N+1)) and hold passes (no logic; hold pass k drawn at
// t = (k+1)/(N+1), the last one, the record pass, at S+1). At 60 fps (N = 1) the logic pass is
// drawn halfway and the one hold pass is the record pass. In true 60 (true60.cpp) the full passes
// are the logic passes and the half passes the hold passes for every 30 Hz process; things driven
// by 60 Hz processes (Link, the follow camera) are not blended.
//
// Per-step work in drawing code is held back on hold passes (it ran twice per step before):
// particle calc, J3DFrameCtrl::update from Draw, the sea's scroll counter, bush calc; the world
// systems in dScnPly_Draw (grass/tree/wood/flower calc, g_Counter.mTimer, ...) are held back by
// true60.cpp (site_025B00B0).
// On blended passes these are drawn between the previous step and this one at the pass's fraction
// (the exact values are back at the start of the next pass, after the painter has drawn the pass).
// Values that only logic computes (particles, sprites, sway, waves) are blended on the logic pass,
// which keeps the step's before/after pairs; blended hold passes re-apply them at their own
// fraction (fx_hold_blend). Values computed while drawing (sea grid, material animations, cloth)
// are blended by the drawing hooks on every blended pass:
//   - particles: position, size, axis, alpha, colours, rotation; particles born this step are drawn
//     (1-t) of a step back along their velocity and their emitter's movement (wind trails, trails of
//     moving actors),
//   - sea: wave heights and grid origin, texture scroll, wave crest sprites,
//   - weather and sky sprites: rain, snow/ash, spores, fog, poison fog, sky clouds, stars,
//   - material animations (btk, brk, ...): the frame they are evaluated at,
//   - grass/tree/flower sway slots, bush animation matrices, cloth (flags, sails) vertices.
// Billboards need nothing here: they are J3D models (viewCalc blending) drawn with the blended
// camera. The attention arrow too, except in true 60 (see hook_024EC1C8).
//
// debug: WWHD_INTERP_FX=mask turns parts off (1 particles, 2 sea, 4 material animations, 8 hold
// back per-step work on hold passes, 16 grass/tree/flower/bush/cloth, 32 weather and sky sprites,
// 64 particles born this step, 128 particle colour/alpha/rotation/axis); WWHD_INTERP_FX_TRACE=n
// logs n samples per part; WWHD_INTERP_FX_STATS=1 logs counts every 300 steps.
// test aids: WWHD_SEA_WAVES=1 (full waves everywhere), WWHD_FORCE_RAIN=n (rain count).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "guest_addr.h"
#include "interp_pacing.h"
#include "runtime.h"
#include "true60.h"

namespace interp {
bool enabled();
bool hold_pass();       // pass without logic (blended hold pass or record pass)
bool logic_pass();      // inside the loop body of a logic pass (drawing blended)
bool blend_draw();      // inside the loop body of a blended pass (logic pass or blended hold pass)
bool record_pass();     // the step's last hold pass: drawn exactly, recorded for the next step
float pass_t();         // blend fraction of this pass (1 = exact)
bool in_execute();      // inside fpcEx_Handler (actor Execute)
uint64_t hold_pass_count();  // record passes so far (step stamp of the histories)
uint64_t pass_count();       // every pass
uint64_t logic_steps();   // logic steps run so far
}  // namespace interp

extern "C" {
void f_0282167C_orig(Cpu* c);  // JPAEmitterManager::calc(group)
void f_025AF8A0_orig(Cpu* c);  // dScnPly_Draw
void f_027DF40C_orig(Cpu* c);  // key animator evaluation (btk/brk/...)
void f_027F2FC4_orig(Cpu* c);  // J3DFrameCtrl::update
void f_0246C5E8_orig(Cpu* c);  // daSea_packet_c: vertex grid from the height table
void f_0246C7C8_orig(Cpu* c);  // daSea_packet_c: material setup (texture matrices, colours)
void f_028E93CC_orig(Cpu* c);  // PSMTXTrans
void f_0246BD4C_orig(Cpu* c);  // daSea_packet_c::CalcFlatInter
void f_025A8148_orig(Cpu* c);  // dPa_control_c: particle calc of groups 7-8 (then g_Counter.mTimer++)
void f_0254C6C4_orig(Cpu* c);  // dGrass_packet_c::calc
void f_025C9948_orig(Cpu* c);  // dTree_packet_c::calc
void f_02548370_orig(Cpu* c);  // dFlower_packet_c::calc
void f_0256A448_orig(Cpu* c);  // wave_move (kankyo wave sprites)
void f_025D0994_orig(Cpu* c);  // dWood::Packet_c::calc
void f_02566B88_orig(Cpu* c);  // dKyr_rain_move
void f_02568BD4_orig(Cpu* c);  // dKyr_snow_move
void f_02569408_orig(Cpu* c);  // dKyr_kazanbai_move
void f_02567F68_orig(Cpu* c);  // dKyr_housi_move
void f_0256BB6C_orig(Cpu* c);  // cloud_shadow_move (moya)
void f_0256CA54_orig(Cpu* c);  // poison_move
void f_0256DDF8_orig(Cpu* c);  // vrkumo_move
void f_0256A388_orig(Cpu* c);  // dKyr_star_move
void f_0251D864_orig(Cpu* c);  // dCloth_packet_c: vertex buffer fill
void f_0281FE40_orig(Cpu* c);  // JPABaseEmitter::calc
void f_024EC1C8_orig(Cpu* c);  // dAttDraw_c::draw(pos, camera inverse rotation)
}

namespace {
uint32_t mask() {
    static const uint32_t m = getenv("WWHD_INTERP_FX") ? (uint32_t)strtoul(getenv("WWHD_INTERP_FX"), nullptr, 0) : 0xFF;
    return m;
}
bool on(uint32_t bit) { return interp::enabled() && (mask() & bit); }
bool hold_back() { return interp::hold_pass() && on(8); }
// blended frames: logic pass or blended hold pass, outside actor Execute
bool halfway() { return interp::blend_draw() && !interp::in_execute(); }
// blends at fraction t (interp_pacing.h); at t = 1/2 the original halfway arithmetic (60 fps draws
// bit-identical frames)
using interp::pacing::lerp_f;
using interp::pacing::lerp_s16;
using interp::pacing::lerp_u8;

int trace_left(int part) {
    static int left[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    if (left[part] < 0) left[part] = getenv("WWHD_INTERP_FX_TRACE") ? atoi(getenv("WWHD_INTERP_FX_TRACE")) : 0;
    return left[part] > 0 ? left[part]-- : 0;
}

// ---------------------------------------------------------------------------------------------
// Particles (JPA). JPAEmitterManager: +0x50 + 12 * group = emitter list (JSUPtrList; head link at
// +0). JSUPtrLink: +0 object, +0xC next. JPABaseEmitter: +0x1AC particles, +0x1B8 children,
// +0x22C global translation. JPABaseParticle (same layout as the GameCube one up to 0xD0): +0x10
// offset (emitter centre at birth), +0x1C local position, +0x28 global position (what the draw
// visitors read), +0x34 velocity (local units per step), +0x78 age (frames, +1 per calc), draw
// params from +0x8C: +0x8C axis (3 f32), +0x9C/+0xA0 size, +0xAC alpha, +0xB8 prim colour (RGBA8),
// +0xBC env colour, +0xC0 rotation (u16 angle; +0xC2 its speed). These are computed by the calc
// (the draw visitors only read them), so halfway values drawn on logic passes are exact halfway
// frames. The static emitter info (104B5730) holds the emitter being calculated: +0xE0 its centre,
// +0xEC its scale (global = local * scale + offset).
constexpr uint32_t kMgrGroups = 0x50, kEmtrPtcls = 0x1AC, kEmtrChildren = 0x1B8;
constexpr uint32_t kPtclAge = 0x78, kPtclVel = 0x34;
const uint32_t kEmtrInfo = GD(0x104B5730), kInfoCenter = 0xE0, kInfoScale = 0xEC;
constexpr int kPW = 12;  // words per particle state
constexpr uint32_t kPtclWord[kPW] = {0x28, 0x2C, 0x30, 0x9C, 0xA0, 0x8C, 0x90, 0x94, 0xAC, 0xB8, 0xBC, 0xC0};
enum { kWPos = 0, kWSize = 3, kWAxis = 5, kWAlpha = 8, kWPrm = 9, kWEnv = 10, kWRot = 11 };

struct PtclState {
    uint32_t w[kPW];  // raw guest words
    float age;
};
struct Applied {
    uint32_t ptcl;
    uint32_t w[kPW];  // exact values
    uint32_t a[kPW];  // the previous step's values (blended particles)
    float born[3];    // born this step: its whole first step (velocity and emitter movement)
    bool is_born, extras;
};
std::unordered_map<uint32_t, PtclState> g_ptcl_before;  // step N, per particle (current group)
std::vector<Applied> g_ptcl_applied;                    // particles currently showing halfway values
struct { uint32_t blended = 0, fresh = 0, born = 0; } g_pstats;
// emitter centre and scale at its last calc (recorded by hook_0281FE40)
struct EmtrRec {
    float prev[3], now[3], scale[3];
    uint64_t step = 0, prev_step = 0;
};
std::unordered_map<uint32_t, EmtrRec> g_emtr;

float wf(uint32_t w) { return u32_as_f32(__builtin_bswap32(w)); }
uint32_t fw(float f) { return __builtin_bswap32(f32_as_u32(f)); }

PtclState read_ptcl(uint32_t p) {
    PtclState s;
    for (int i = 0; i < kPW; i++) s.w[i] = *(const uint32_t*)ppc_ptr(p + kPtclWord[i]);
    s.age = (float)ldf32(p + kPtclAge);
    return s;
}
void write_ptcl(uint32_t p, const uint32_t* w) {
    for (int i = 0; i < kPW; i++) *(uint32_t*)ppc_ptr(p + kPtclWord[i]) = w[i];
}
// between two particle states at t: floats, colours per channel, the rotation angle the short way
void mid_ptcl(const uint32_t* a, const uint32_t* b, uint32_t* m, bool extras, float t) {
    for (int i = 0; i < kPW; i++) m[i] = b[i];
    for (int i = kWPos; i < kWSize + 2; i++) m[i] = fw(lerp_f(wf(a[i]), wf(b[i]), t));
    if (!extras) return;
    for (int i = kWAxis; i <= kWAlpha; i++) m[i] = fw(lerp_f(wf(a[i]), wf(b[i]), t));
    for (int i : {kWPrm, kWEnv}) {
        const uint8_t* x = (const uint8_t*)&a[i];
        const uint8_t* y = (const uint8_t*)&b[i];
        uint8_t* o = (uint8_t*)&m[i];
        for (int k = 0; k < 4; k++) o[k] = lerp_u8(x[k], y[k], t);
    }
    // rotation word: big-endian u16 angle in the first two bytes, its speed in the last two
    uint16_t ra = (uint16_t)(((const uint8_t*)&a[kWRot])[0] << 8 | ((const uint8_t*)&a[kWRot])[1]);
    uint16_t rb = (uint16_t)(((const uint8_t*)&b[kWRot])[0] << 8 | ((const uint8_t*)&b[kWRot])[1]);
    uint16_t rm = (uint16_t)lerp_s16((int16_t)ra, (int16_t)rb, t);
    ((uint8_t*)&m[kWRot])[0] = (uint8_t)(rm >> 8);
    ((uint8_t*)&m[kWRot])[1] = (uint8_t)rm;
}

template <class F>
void for_each_ptcl(uint32_t mgr, uint32_t group, F&& f) {
    int guard = 0;
    for (uint32_t el = ld32(mgr + kMgrGroups + 12 * group); el && guard < 100000; el = ld32(el + 0xC), guard++) {
        uint32_t emtr = ld32(el);
        if (!emtr) continue;
        for (uint32_t list : {kEmtrPtcls, kEmtrChildren})
            for (uint32_t pl = ld32(emtr + list); pl && guard < 100000; pl = ld32(pl + 0xC), guard++)
                if (uint32_t p = ld32(pl)) f(p, emtr);
    }
}

void ptcl_restore() {
    for (const Applied& a : g_ptcl_applied) write_ptcl(a.ptcl, a.w);
    g_ptcl_applied.clear();
}
// a particle's drawn values at t (from its exact values and what the logic pass recorded)
void ptcl_mid(const Applied& ap, float t, uint32_t* mid) {
    if (ap.is_born) {  // drawn as if born (1-t) of a step earlier
        memcpy(mid, ap.w, sizeof ap.w);
        for (int i = 0; i < 3; i++) mid[kWPos + i] = fw(wf(ap.w[kWPos + i]) - (1 - t) * ap.born[i]);
    } else {
        mid_ptcl(ap.a, ap.w, mid, ap.extras, t);
    }
}
// blended hold pass: the particles blended on the logic pass, at this pass's fraction (they were
// put back to their exact values at the pass start, and the list kept)
std::vector<Applied> g_ptcl_keep;
void ptcl_reapply(float t) {
    for (const Applied& ap : g_ptcl_keep) {
        uint32_t mid[kPW];
        ptcl_mid(ap, t, mid);
        write_ptcl(ap.ptcl, mid);
    }
    g_ptcl_applied.swap(g_ptcl_keep);
    g_ptcl_keep.clear();
}

// debug trace: how far the blended particles moved this step, and where the blended value sits on
// that step (0 = before, 1 = exact; the pass's t expected)
struct PtclTrace {
    double step = 0, frac = 0;
    int n = 0;
    void add(const PtclState& a, const PtclState& b, const uint32_t* mid) {
        double d[3], m[3], dd = 0, dm = 0;
        for (int i = 0; i < 3; i++) {
            d[i] = wf(b.w[i]) - wf(a.w[i]);
            m[i] = wf(mid[i]) - wf(a.w[i]);
            dd += d[i] * d[i];
            dm += d[i] * m[i];
        }
        float aa = wf(a.w[kWAlpha]), ab = wf(b.w[kWAlpha]);
        if (aa != ab) afrac += (wf(mid[kWAlpha]) - aa) / (ab - aa), an++;
        auto rot = [](const uint32_t* w) { return (int)(((const uint8_t*)&w[kWRot])[0] << 8 | ((const uint8_t*)&w[kWRot])[1]); };
        int16_t rd = (int16_t)(rot(b.w) - rot(a.w));
        if (rd) rfrac += (double)(int16_t)(rot(mid) - rot(a.w)) / rd, rn++;
        int ca = ((const uint8_t*)&a.w[kWPrm])[0], cb = ((const uint8_t*)&b.w[kWPrm])[0];  // prim colour red
        if (ca != cb) cfrac += (double)(((const uint8_t*)&mid[kWPrm])[0] - ca) / (cb - ca), cn++;
        if (dd < 0.01) return;  // (float rounding at large coordinates moves "still" particles by an ulp)
        step += std::sqrt(dd);
        frac += dm / dd;
        n++;
    }
    double afrac = 0, rfrac = 0, cfrac = 0;
    int an = 0, rn = 0, cn = 0;
    void log(uint32_t group) {
        if (n || an || rn || cn)
            LOG("[interp-fx] particles group %u: %d moving, mean step %.2f units, halfway at %.3f of the step; alpha %.3f (%d), rotation %.3f (%d), "
                "colour %.3f (%d)",
                group, n, n ? step / n : 0.0, n ? frac / n : 0.0, an ? afrac / an : 0.0, an, rn ? rfrac / rn : 0.0, rn, cn ? cfrac / cn : 0.0, cn);
    }
};

// ---------------------------------------------------------------------------------------------
// Material animations. The key animator (027DF40C, this = animator) evaluates its keys at the frame
// in its time block (*(this+0): +0 frame (after wrap/clamp), +4 start, +8 end, +0x10 the frame
// mapping function: 027DA9E8 loops, 027DAAA0 clamps) and caches the frame at this+0x28. btk/brk
// entry and many actors call it from Draw on every pass.
const uint32_t kLoopFn = GC(0x027DA9E8);
struct AnmRec {
    uint32_t ts = 0;
    float frame = 0;
    uint64_t pass = 0;
};
std::unordered_map<uint32_t, AnmRec> g_anm;
struct { uint32_t blended = 0, wrapped = 0, jumped = 0; } g_astats;

// frame at t between a (drawn at step S) and b (step S+1); returns b when it is not a small step
float anm_mid(float a, float b, float start, float end, bool loop, float t) {
    float d = b - a;
    float len = end - start;
    if (loop && len > 0) {
        if (d < -0.5f * len) d += len, g_astats.wrapped++;
        else if (d > 0.5f * len) d -= len, g_astats.wrapped++;
    }
    if (!(std::fabs(d) <= 4.0f)) {  // a jump (new animation, frame set by hand) or NaN
        g_astats.jumped++;
        return b;
    }
    float m = a + t * d;  // (0.5f * d at 60 fps)
    if (loop && len > 0) {
        if (m >= end) m -= len;
        else if (m < start) m += len;
    }
    return m;
}

// ---------------------------------------------------------------------------------------------
// Sea (daSea_packet_c, one static instance). execute (logic only) fills the 65x65 height table
// (+0x20C, f32) around the player, grid origin min x/z at +0x1FC/+0x200 (800 units per cell); Draw
// (0246CAEC, every pass) builds the vertex grid from them (0246C5E8, double-buffered vertex buffer)
// and sets up the material (0246C7C8): the "yura" texture scrolls by the counter at +0x22C (s16,
// +1 per call, 0..300, translation counter / 300 via PSMTXTrans at 0246C954).
constexpr int kSeaCells = 65 * 65;
constexpr uint32_t kSeaHeights = 0x20C, kSeaMinX = 0x1FC, kSeaMinZ = 0x200, kSeaScroll = 0x22C;
struct SeaRec {
    uint32_t packet = 0;
    uint64_t pass = 0;
    float min_x = 0, min_z = 0;
    std::vector<uint32_t> h;
} g_sea;
bool g_sea_scroll_half = false;  // the next PSMTXTrans from the sea material setup gets the halfway scroll
}  // namespace

namespace {
// ---------------------------------------------------------------------------------------------
// Values shown blended on a logic pass and put back at the start of the next pass (after the
// painter has drawn the pass): address, exact word (host order, as ld32), the previous step's value
// and how to blend them, restored in reverse order. Blended hold passes apply them again at their
// own fraction (temp_reapply).
struct Temp {
    uint32_t addr, exact, prev;
    int8_t kind;  // kF32: host-order float word; kS16: the s16 at bit `shift` of the word
    int8_t shift;
};
enum { kF32, kS16 };
std::vector<Temp> g_temp, g_temp_keep;
// the word to show at t; an s16 replaces only its half of the word as it is now (the other half
// may be another blended s16: tree sway slots keep two in one word)
uint32_t temp_value(const Temp& e, float t) {
    if (e.kind == kF32) return f32_as_u32(lerp_f(u32_as_f32(e.prev), u32_as_f32(e.exact), t));
    int16_t b = (int16_t)(e.exact >> e.shift);
    uint16_t m = (uint16_t)lerp_s16((int16_t)e.prev, b, t);
    return (ld32(e.addr) & ~(0xFFFFu << e.shift)) | ((uint32_t)m << e.shift);
}
// the word at a shown at t between prev (a host-order float word) and its exact value
void temp_blend_f32(uint32_t a, uint32_t prev, float t) {
    Temp e{a, ld32(a), prev, kF32, 0};
    g_temp.push_back(e);
    st32(a, temp_value(e, t));
}
// the s16 at a (halfword aligned) shown at t between prev and its exact value
void temp_blend_s16(uint32_t a, int16_t prev, float t) {
    uint32_t w = a & ~3u;
    Temp e{w, ld32(w), (uint32_t)(uint16_t)prev, kS16, (int8_t)((a & 2) ? 0 : 16)};  // (big-endian word)
    g_temp.push_back(e);
    st32(w, temp_value(e, t));
}
void temp_restore() {
    for (auto it = g_temp.rbegin(); it != g_temp.rend(); ++it) st32(it->addr, it->exact);
    g_temp_keep.swap(g_temp);
    g_temp.clear();
}
void temp_reapply(float t) {  // blended hold pass, after temp_restore at the pass start
    for (const Temp& e : g_temp_keep) st32(e.addr, temp_value(e, t));
    g_temp.swap(g_temp_keep);
    g_temp_keep.clear();
}
bool plausible(float v) { return v == 0.0f || (std::fabs(v) > 1e-12f && std::fabs(v) < 1e9f); }

// ---------------------------------------------------------------------------------------------
// Grass, trees and flowers (and other drawing effects: cloth, hookshot chain, Link's sparkles) use
// g_Counter.mTimer (101FF560) as their clock; dScnPly_Draw increments it in its world-system
// block, which true60.cpp (site_025B00B0) runs on full passes only. The sway of each kind is 8
// shared animation slots written by its calc (in that block) as a function of mTimer: grass s16 +4
// (slots at +0x18F0C, 0x38 apart), trees s16 +4/+6 (+0x2A9C, 0x84 apart), flowers s16 +4
// (+0x35BC, 0x38 apart). On logic passes the slots are drawn blended between the previous step's
// and this step's values (blended hold passes re-apply them); the exact ones are back for the record
// pass.
const uint32_t kCounterTimer = GD(0x101FF560);
struct SwayKind {
    uint32_t base, stride;
    int fields;
    uint32_t field[2];
};
constexpr SwayKind kSway[3] = {{0x18F0C, 0x38, 1, {4, 0}}, {0x2A9C, 0x84, 2, {4, 6}}, {0x35BC, 0x38, 1, {4, 0}}};
struct SwayRec {
    uint32_t pkt = 0;
    uint64_t step = 0;  // logic pass it was recorded on (hold pass count)
    int16_t v[8][2];
} g_sway[3];

void sway_calc(Cpu* c, int k, void (*orig)(Cpu*)) {
    const SwayKind& sk = kSway[k];
    uint32_t pk = c->r[3];
    orig(c);
    if (!on(16) || !halfway()) return;
    SwayRec& r = g_sway[k];
    bool valid = r.pkt == pk && r.step + 1 == interp::hold_pass_count();
    static int tr = trace_left(4);
    for (int i = 0; i < 8; i++)
        for (int f = 0; f < sk.fields; f++) {
            uint32_t a = pk + sk.base + sk.stride * i + sk.field[f];
            int16_t cur = (int16_t)ld16(a);
            if (valid) {
                temp_blend_s16(a, r.v[i][f], interp::pass_t());
                if (tr && i == 0 && f == 0) {
                    tr--;
                    LOG("[interp-fx] %s sway slot 0: %d -> %d, drawn %d", k == 0 ? "grass" : k == 1 ? "tree" : "flower", r.v[i][f], cur, (int16_t)ld16(a));
                }
            }
            r.v[i][f] = cur;
        }
    r.pkt = pk;
    r.step = interp::hold_pass_count();
}

// ---------------------------------------------------------------------------------------------
// Bushes (dWood::Packet_c): calc (025D0994, world-system block: full passes only) plays 72 shared
// animations (Anm_c at packet +0x1C8D8, 0x8C apart: +0 sway matrix, +0x30 trunk matrix, 3x4 each);
// update (025D15B0, in drawing, every pass) builds each bush's matrices from them. On logic passes
// the animation matrices are drawn halfway between the previous step's and this step's.
constexpr uint32_t kWoodAnm = 0x1C8D8, kWoodAnmStride = 0x8C;
constexpr int kWoodAnms = 72, kWoodWords = 24;
struct WoodRec {
    uint32_t pkt = 0;
    uint64_t step = 0;
    std::vector<uint32_t> w;  // 24 words per animation
} g_wood;

// ---------------------------------------------------------------------------------------------
// Weather and sky sprites (environment Execute, logic only; drawn by the painter). Each is an array
// of effect records in a packet that g_env_light (10475A68) points to; WWHD's packets keep the
// GameCube records (the J3DPacket base grew). On logic passes every float field of every record
// that did not respawn this step (same status byte, small position change) is drawn halfway.
const uint32_t kEnvLight = GD(0x10475A68);
struct KankyoKind {
    const char* name;
    uint32_t env_off, base, stride;
    int count;
    int status;      // offset of the status byte (-1: none)
    float max_step;  // larger position changes are respawns
};
constexpr KankyoKind kKankyo[] = {
    {"rain", 0xA44, 0xA0, 0x38, 250, 0, 400.0f},     // dKyr_rain_move 02566B88 (RAIN_EFF)
    {"snow", 0xA50, 0x9C, 0x38, 250, 0, 400.0f},     // dKyr_snow_move 02568BD4, kazanbai 02569408 (SNOW_EFF)
    {"housi", 0xA78, 0x9C, 0x50, 300, 0, 400.0f},    // dKyr_housi_move 02567F68 (HOUSI_EFF, spores)
    {"moya", 0xA84, 0xA0, 0x4C, 100, 0, 600.0f},     // cloud_shadow_move 0256BB6C (CLOUD_EFF, fog)
    {"poison", 0xA6C, 0x98, 0x30, 1000, 0, 400.0f},  // poison_move 0256CA54 (POISON_EFF)
    {"vrkumo", 0xA94, 0xA4, 0x2C, 100, 0, 5000.0f},  // vrkumo_move 0256DDF8 (VRKUMO_EFF, sky clouds)
    {"star", 0xA60, 0x9C, 0x34, 1, -1, 1e9f},        // dKyr_star_move 0256A388 (twinkle)
};
enum { kRain, kSnow, kHousi, kMoya, kPoison, kVrkumo, kStar };
struct KankyoTrace { int moving = 0, words = 0; double frac = 0; };

void kankyo_move(Cpu* c, int k, void (*orig)(Cpu*)) {
    const KankyoKind& kk = kKankyo[k];
    uint32_t pk = ld32(kEnvLight + kk.env_off);
    if (!on(32) || !interp::logic_pass() || pk < 0x10000000 || pk >= 0x50000000) {
        orig(c);
        return;
    }
    const uint32_t bytes = kk.stride * kk.count;
    std::vector<uint32_t> before((const uint32_t*)ppc_ptr(pk + kk.base), (const uint32_t*)ppc_ptr(pk + kk.base) + bytes / 4);
    orig(c);
    if (ld32(kEnvLight + kk.env_off) != pk) return;  // recreated
    static int tr[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    if (tr[k] < 0) tr[k] = getenv("WWHD_INTERP_FX_TRACE") ? atoi(getenv("WWHD_INTERP_FX_TRACE")) : 0;
    KankyoTrace t;
    const uint32_t* now = (const uint32_t*)ppc_ptr(pk + kk.base);
    for (int i = 0; i < kk.count; i++) {
        uint32_t e = kk.stride * i;
        const uint32_t* a = &before[e / 4];
        const uint32_t* b = now + e / 4;
        if (kk.status >= 0 && ((const uint8_t*)a)[kk.status] != ((const uint8_t*)b)[kk.status]) continue;
        if (kk.status >= 0 && ((const uint8_t*)b)[kk.status] == 0) continue;  // not in use
        float d2 = 0;
        for (int q = 1; q <= 3 && kk.status >= 0; q++) d2 += (wf(b[q]) - wf(a[q])) * (wf(b[q]) - wf(a[q]));
        if (!(d2 <= kk.max_step * kk.max_step)) continue;
        bool moved = false;
        for (uint32_t q = kk.status >= 0 ? 1 : 0; q < kk.stride / 4; q++) {
            if (a[q] == b[q]) continue;
            float x = wf(a[q]), y = wf(b[q]);
            if (!plausible(x) || !plausible(y) || std::fabs(y - x) > 1e5f) continue;
            temp_blend_f32(pk + kk.base + e + 4 * q, f32_as_u32(x), interp::pass_t());  // (host-order word)
            moved = true;
            if (tr[k] && q == 1 && x != y) t.frac += ((float)ldf32(pk + kk.base + e + 4 * q) - x) / (y - x), t.words++;  // as drawn
        }
        t.moving += moved;
    }
    if (tr[k] && t.moving) {
        tr[k]--;
        LOG("[interp-fx] %s sprites: %d moving, halfway at %.3f of the step (x, %d samples)", kk.name, t.moving, t.words ? t.frac / t.words : 0.0,
            t.words);
    }
}
}  // namespace

namespace {
// The crest code reads and writes through ld32/st32 (host-order words), so it converts with hf/fh,
// not wf/fw (those byte-swap raw memory words). Mixing them averaged byte-swapped floats and wrote
// garbage into every moving crest on logic frames: crests vanished/popped every other frame.
inline float hf(uint32_t w) { return u32_as_f32(w); }
inline uint32_t fh(float f) { return f32_as_u32(f); }
// Wave crests on the sea ("usonami", dKankyo_wave_Packet): 300 sprites moved by wave_move
// (0256A448, environment Execute, logic only) and drawn by the painter. WAVE_EFF array at packet
// +0xA0, 0x38 bytes each (as on GameCube: +0 position, +0xC base position, +0x1C scale, +0x24 wobble
// counter, +0x28 alpha, +0x2C strength (height), +0x34 status) and the packet's skew (+0x4240 width,
// +0x4244 direction, from wind and camera direction). Everything the draw reads that wave_move changes
// per step is blended; otherwise crests change shape only on logic frames. Logic passes draw them
// halfway; the exact values are back at the next pass.
constexpr uint32_t kWaveEff = 0xA0, kWaveStride = 0x38;
constexpr int kWaves = 300;
constexpr int kWaveFields = 7;
constexpr uint32_t kWaveField[kWaveFields] = {0x0, 0x4, 0x8, 0x1C, 0x24, 0x28, 0x2C};  // blended fields
constexpr uint32_t kWaveSkew[2] = {0x4240, 0x4244};
struct WaveApplied {
    uint32_t pkt = 0;
    std::vector<uint32_t> exact, before;  // kWaveFields words per sprite: this step, the step before
    std::vector<uint8_t> blend;           // per sprite: drawn blended (not respawned, small move)
    uint32_t skew[2], skew_before[2];
} g_wave;
uint32_t g_wave_keep = 0;  // packet of the last restored blend (re-applied on blended hold passes)

void wave_restore() {
    if (!g_wave.pkt) return;
    for (int i = 0; i < kWaves; i++)
        for (int f = 0; f < kWaveFields; f++)
            st32(g_wave.pkt + kWaveEff + kWaveStride * i + kWaveField[f], g_wave.exact[kWaveFields * i + f]);
    for (int k = 0; k < 2; k++) st32(g_wave.pkt + kWaveSkew[k], g_wave.skew[k]);
    g_wave_keep = g_wave.pkt;
    g_wave.pkt = 0;
}
// the crests (and the packet's skew) at t; the exact values are in g_wave.exact
void wave_apply(float t) {
    const uint32_t pk = g_wave.pkt;
    for (int k = 0; k < 2; k++) st32(pk + kWaveSkew[k], fh(lerp_f(hf(g_wave.skew_before[k]), hf(g_wave.skew[k]), t)));
    for (int i = 0; i < kWaves; i++) {
        if (!g_wave.blend[i]) continue;
        uint32_t e = pk + kWaveEff + kWaveStride * i;
        const uint32_t* a = &g_wave.before[kWaveFields * i];
        const uint32_t* w = &g_wave.exact[kWaveFields * i];
        for (int f = 0; f < kWaveFields; f++) st32(e + kWaveField[f], fh(lerp_f(hf(a[f]), hf(w[f]), t)));
    }
}
void wave_reapply(float t) {  // blended hold pass, after wave_restore at the pass start
    if (!g_wave_keep) return;
    g_wave.pkt = g_wave_keep;
    g_wave_keep = 0;
    wave_apply(t);
}
}  // namespace

// wave_move() (no arguments): the packet address comes from its draw (hook_02582500, painter,
// between passes), and is only trusted if that draw was seen after the previous pass (the packet
// is recreated on stage changes)
static uint32_t g_wave_pkt = 0;
static uint32_t g_wave_env_pkt = 0;  // g_env_light.mpWavePacket (debug output)
static uint64_t g_wave_pkt_seen = 0, g_passes = 0;
extern "C" void f_02555D0C(Cpu* c);  // dKy_getEnvlight: r3 = g_env_light
extern "C" void hook_0256A448(Cpu* c) {
    wave_restore();
    // the wave packet as wave_move itself finds it: g_env_light.mpWavePacket (+0xAA0). (The pointer
    // seen by hook_02582500 is not this packet: blending through it wrote halfway values into another
    // object every logic frame, which made crests vanish on alternate frames.)
    Cpu tmp = *c;
    f_02555D0C(&tmp);
    uint32_t pk = tmp.r[3] ? ld32(tmp.r[3] + 0xAA0) : 0;
    if (pk < 0x10000000 || pk >= 0x50000000) pk = 0;
    g_wave_env_pkt = pk;
    // debug: WWHD_WAVE_RAW=n dumps, after n logic-pass wave_move calls, the raw words of the first crests
    static int raw_left = getenv("WWHD_WAVE_RAW") ? atoi(getenv("WWHD_WAVE_RAW")) : 0;
    if (raw_left > 0 && pk && --raw_left == 0) {
        LOG("[waveraw] packet %08X env %08X count %d", pk, tmp.r[3], (int)(int16_t)ld16(tmp.r[3] + 0x9F8));
        for (int i = 0; i < 6; i++) {
            char b[400];
            int n = snprintf(b, sizeof b, "[waveraw] %2d:", i);
            for (uint32_t o = 0; o < kWaveStride; o += 4) {
                uint32_t v = ld32(pk + kWaveEff + kWaveStride * i + o);
                n += snprintf(b + n, sizeof b - n, " %02X=%08X(%g)", o, v, (double)hf(v));
            }
            LOG("%s", b);
        }
    }
    if (!on(2) || !interp::logic_pass() || !pk) {
        f_0256A448_orig(c);
        return;
    }
    std::vector<uint32_t> before(kWaveFields * kWaves), base(3 * kWaves), status(kWaves);
    uint32_t skew_before[2] = {ld32(pk + kWaveSkew[0]), ld32(pk + kWaveSkew[1])};
    for (int i = 0; i < kWaves; i++) {
        uint32_t e = pk + kWaveEff + kWaveStride * i;
        for (int f = 0; f < kWaveFields; f++) before[kWaveFields * i + f] = ld32(e + kWaveField[f]);
        for (int k = 0; k < 3; k++) base[3 * i + k] = ld32(e + 0xC + 4 * k);  // mBasePos: changes only on a respawn
        status[i] = ld8(e + 0x34);
    }
    f_0256A448_orig(c);
    g_wave_keep = 0;
    g_wave.pkt = pk;
    g_wave.exact.resize(kWaveFields * kWaves);
    g_wave.before = before;
    g_wave.blend.assign(kWaves, 0);
    for (int k = 0; k < 2; k++) {
        g_wave.skew[k] = ld32(pk + kWaveSkew[k]);
        g_wave.skew_before[k] = skew_before[k];
    }
    const float t = interp::pass_t();
    static int tr = trace_left(5);
    int moving = 0;
    double frac = 0;
    for (int i = 0; i < kWaves; i++) {
        uint32_t e = pk + kWaveEff + kWaveStride * i;
        uint32_t* w = &g_wave.exact[kWaveFields * i];
        for (int f = 0; f < kWaveFields; f++) w[f] = ld32(e + kWaveField[f]);
        const uint32_t* a = &before[kWaveFields * i];
        float d2 = 0;
        for (int k = 0; k < 3; k++) d2 += (hf(w[k]) - hf(a[k])) * (hf(w[k]) - hf(a[k]));
        // respawned (new spot, maybe close to the old one) or switched state: draw it exact, never
        // halfway between its old and new life (a ghost crest in between)
        bool respawned = ld32(e + 0xC) != base[3 * i] || ld32(e + 0x10) != base[3 * i + 1] || ld32(e + 0x14) != base[3 * i + 2] ||
                         ld8(e + 0x34) != status[i];
        if (respawned || !(d2 <= 300.0f * 300.0f) || !(std::fabs(hf(w[4]) - hf(a[4])) < 1000.0f)) continue;
        g_wave.blend[i] = 1;
        if (tr && d2 > 1e-4f) {
            moving++;
            frac += (lerp_f(hf(a[0]), hf(w[0]), t) - hf(a[0])) / (hf(w[0]) - hf(a[0]) != 0 ? (hf(w[0]) - hf(a[0])) : 1.0f);
        }
    }
    wave_apply(t);
    if (tr && moving) {
        tr--;
        LOG("[interp-fx] wave sprites: %d moving, drawn at %.3f of the step (x)", moving, frac / moving);
    }
}
extern "C" void f_02582500_orig(Cpu* c);
// dKankyo_wave_Packet::draw (painter): remembers the packet
extern "C" void hook_02582500(Cpu* c) {
    g_wave_pkt = c->r[3];
    g_wave_pkt_seen = g_passes;
    // debug: WWHD_WAVE_STATS=1 logs how often the crests are drawn after logic / hold passes and
    // how many are visible (alpha > 0)
    // debug: WWHD_WAVE_DUMP=n logs, for n passes, every field of 4 visible crests at each draw
    static int dump_left = getenv("WWHD_WAVE_DUMP") ? atoi(getenv("WWHD_WAVE_DUMP")) : 0;
    static int pick[4] = {-1, -1, -1, -1};
    if (dump_left > 0 && interp::enabled()) {
        uint32_t pk = g_wave_env_pkt;
        if (!pk) goto no_dump;
        if (pick[0] < 0)
            for (int i = 0, n = 0; i < kWaves && n < 4; i++)
                if (hf(ld32(pk + kWaveEff + kWaveStride * i + 0x28)) > 0.3f) pick[n++] = i;
        dump_left--;
        for (int n = 0; n < 4 && pick[n] >= 0; n++) {
            uint32_t e = pk + kWaveEff + kWaveStride * pick[n];
            LOG("[wavedump] pass %llu %s crest %d pos %.1f %.1f %.1f base %.0f %.0f scale %.3f cspd %.4f counter %.4f sin %.3f alpha %.3f str %.3f st %d skew %.3f %.3f",
                (unsigned long long)g_passes, interp::hold_pass() ? "H" : "L", pick[n], hf(ld32(e)), hf(ld32(e + 4)), hf(ld32(e + 8)),
                hf(ld32(e + 0xC)), hf(ld32(e + 0x14)), hf(ld32(e + 0x1C)), hf(ld32(e + 0x20)), hf(ld32(e + 0x24)),
                std::sin(hf(ld32(e + 0x24))), hf(ld32(e + 0x28)), hf(ld32(e + 0x2C)), ld8(e + 0x34), hf(ld32(pk + 0x4240)),
                hf(ld32(pk + 0x4244)));
        }
    }
no_dump:
    static const bool stats = getenv("WWHD_WAVE_STATS") != nullptr;
    if (stats) {
        static int n[2] = {0, 0}, vis = 0, total = 0;
        static uint64_t last_pass = 0;
        n[interp::hold_pass() ? 1 : 0]++;
        int v = 0;
        for (int i = 0; g_wave_env_pkt && i < kWaves; i++) v += hf(ld32(g_wave_env_pkt + kWaveEff + kWaveStride * i + 0x28)) > 0.01f;
        vis += v;
        total++;
        if (g_passes - last_pass >= 120) {
            LOG("[waves] last 120 passes: drawn %d times after logic passes, %d after hold passes; visible crests %.1f",
                n[0], n[1], total ? (double)vis / total : 0.0);
            n[0] = n[1] = vis = total = 0;
            last_pass = g_passes;
        }
    }
    f_02582500_orig(c);
}

// particle calc of groups 7-8 in dScnPly_Draw, followed by g_Counter.mTimer++
extern "C" void hook_025A8148(Cpu* c) {
    if (hold_back()) {
        st32(kCounterTimer, ld32(kCounterTimer) - 1);  // cancels the increment that follows
        return;  // particles are held back on hold passes anyway
    }
    f_025A8148_orig(c);
}
// bushes (dWood::Packet_c::calc): halfway animation matrices on logic passes
extern "C" void hook_025D0994(Cpu* c) {
    if (hold_back()) return;
    uint32_t pk = c->r[3];
    f_025D0994_orig(c);
    if (!on(16) || !halfway()) return;
    bool valid = g_wood.pkt == pk && g_wood.step + 1 == interp::hold_pass_count() && g_wood.w.size() == kWoodAnms * kWoodWords;
    std::vector<uint32_t> cur(kWoodAnms * kWoodWords);
    for (int i = 0; i < kWoodAnms; i++)
        for (int q = 0; q < kWoodWords; q++) cur[i * kWoodWords + q] = ld32(pk + kWoodAnm + kWoodAnmStride * i + 4 * q);
    static int tr = trace_left(7);
    int changed = 0;
    if (valid)
        for (int i = 0; i < kWoodAnms; i++) {
            const uint32_t* a = &g_wood.w[i * kWoodWords];
            const uint32_t* b = &cur[i * kWoodWords];
            if (!memcmp(a, b, 4 * kWoodWords)) continue;
            float d2 = 0;  // translation change (a reset or a cut bush): not blended
            for (int r = 0; r < 2; r++)
                for (int q : {3, 7, 11}) d2 += (u32_as_f32(a[12 * r + q]) - u32_as_f32(b[12 * r + q])) * (u32_as_f32(a[12 * r + q]) - u32_as_f32(b[12 * r + q]));
            if (!(d2 < 400.0f * 400.0f)) continue;
            for (int q = 0; q < kWoodWords; q++)
                if (a[q] != b[q]) temp_blend_f32(pk + kWoodAnm + kWoodAnmStride * i + 4 * q, a[q], interp::pass_t());
            if (tr && !changed) {
                tr--;
                LOG("[interp-fx] bush anim %d: sway matrix [0][1] %.4f -> %.4f, halfway %.4f", i, u32_as_f32(a[1]), u32_as_f32(b[1]), u32_as_f32(ld32(pk + kWoodAnm + kWoodAnmStride * i + 4)));
            }
            changed++;
        }
    g_wood.pkt = pk;
    g_wood.step = interp::hold_pass_count();
    g_wood.w = std::move(cur);
}
extern "C" void hook_0254C6C4(Cpu* c) { sway_calc(c, 0, f_0254C6C4_orig); }
extern "C" void hook_025C9948(Cpu* c) { sway_calc(c, 1, f_025C9948_orig); }
extern "C" void hook_02548370(Cpu* c) { sway_calc(c, 2, f_02548370_orig); }

// weather and sky sprites
extern "C" void hook_02566B88(Cpu* c) { kankyo_move(c, kRain, f_02566B88_orig); }
extern "C" void hook_02568BD4(Cpu* c) { kankyo_move(c, kSnow, f_02568BD4_orig); }
extern "C" void hook_02569408(Cpu* c) { kankyo_move(c, kSnow, f_02569408_orig); }
extern "C" void hook_02567F68(Cpu* c) { kankyo_move(c, kHousi, f_02567F68_orig); }
extern "C" void hook_0256BB6C(Cpu* c) { kankyo_move(c, kMoya, f_0256BB6C_orig); }
extern "C" void hook_0256CA54(Cpu* c) { kankyo_move(c, kPoison, f_0256CA54_orig); }
extern "C" void f_0257E7C0(Cpu* c);  // dKyw_rain_set(int count)
extern "C" void hook_0256DDF8(Cpu* c) {
    // test aid: WWHD_FORCE_RAIN=n sets the rain count (dKyw_rain_set) every step
    static const int rain = getenv("WWHD_FORCE_RAIN") ? atoi(getenv("WWHD_FORCE_RAIN")) : -1;
    if (rain >= 0) {
        uint32_t lr = c->lr, r3 = c->r[3];
        c->r[3] = (uint32_t)rain;
        f_0257E7C0(c);
        c->lr = lr, c->r[3] = r3;
    }
    kankyo_move(c, kVrkumo, f_0256DDF8_orig);
}
extern "C" void hook_0256A388(Cpu* c) { kankyo_move(c, kStar, f_0256A388_orig); }

// Cloth (flags, sails: dCloth_packet_c, WWHD layout: +0x98 fly / +0x9C hoist grid size, +0xB0 /
// +0xB8 / +0xC0 position / normal / back-normal buffers [2], +0x1C0 current buffer). cloth_move
// switches buffers and simulates one step into the new one, so the other buffer holds the previous
// step. The vertex fill (0251D864, from the cloth's draw, every pass) copies the current buffers
// into the vertex buffer; on blended passes of a step with a simulation step it gets the blended grid.
// The buffers are read in place (guest memory, big-endian words): wf/fw, not the host-order hf/fh
// (blending byte-swapped words turned every moving vertex into garbage: issue #36, the Rito flags on
// Dragon Roost Island drawn as huge stretched polygons on every in-between frame).
constexpr uint32_t kClothFly = 0x98, kClothHoist = 0x9C, kClothPos = 0xB0, kClothCur = 0x1C0;
struct ClothRec { uint8_t cur; uint64_t stepped_at; };  // buffer index at the last fill, logic step it changed
std::unordered_map<uint32_t, ClothRec> g_cloth_cur;
extern "C" void hook_0251D864(Cpu* c) {
    uint32_t pk = c->r[3];
    uint8_t cur = (uint8_t)ld8(pk + kClothCur);
    auto it = g_cloth_cur.find(pk);
    const uint64_t step = interp::logic_steps();
    uint64_t stepped_at = it != g_cloth_cur.end() ? it->second.stepped_at : ~0ull;
    const bool changed = it != g_cloth_cur.end() && it->second.cur != cur;
    if (changed) stepped_at = step;
    // the step's blended hold passes see no change: the simulation step was the logic pass's
    bool stepped = changed || (interp::hold_pass() && stepped_at == step);
    g_cloth_cur[pk] = ClothRec{cur, stepped_at};
    int32_t fly = (int32_t)ld32(pk + kClothFly), hoist = (int32_t)ld32(pk + kClothHoist);
    if (!on(16) || !halfway() || !stepped || cur > 1 || fly <= 0 || hoist <= 0 || fly * hoist > 4096) {
        f_0251D864_orig(c);
        return;
    }
    const uint32_t n = 3 * (uint32_t)(fly * hoist);
    const float t = interp::pass_t();
    std::vector<std::pair<uint32_t, std::vector<uint32_t>>> saved;
    static int tr = trace_left(6);
    for (uint32_t arr = 0; arr < 3; arr++) {  // positions, normals, back normals
        uint32_t now = ld32(pk + kClothPos + 8 * arr + 4 * cur), prev = ld32(pk + kClothPos + 8 * arr + 4 * (cur ^ 1));
        if (now < 0x10000000 || now >= 0x50000000 || prev < 0x10000000 || prev >= 0x50000000) continue;
        uint32_t* b = (uint32_t*)ppc_ptr(now);
        const uint32_t* a = (const uint32_t*)ppc_ptr(prev);
        saved.emplace_back(now, std::vector<uint32_t>(b, b + n));
        if (tr && arr == 0) {
            tr--;
            LOG("[interp-fx] cloth %08X vertex 0 x %.3f -> %.3f, drawn %.3f", pk, wf(a[0]), wf(b[0]), lerp_f(wf(a[0]), wf(b[0]), t));
        }
        for (uint32_t q = 0; q < n; q++) b[q] = fw(lerp_f(wf(a[q]), wf(b[q]), t));
    }
    f_0251D864_orig(c);
    for (auto& [addr, w] : saved) memcpy(ppc_ptr(addr), w.data(), 4 * w.size());
}

namespace { void fx_step_stats(); }
// start of every pass (interp.cpp, per-frame function): the blended values only live until the
// pass has been painted (the painter runs after the per-frame function, before the next one). What
// was blended is kept for the pass: a blended hold pass applies it again at its fraction.
namespace interp {
void light_trace_flush();
void fx_pass_start() {
    light_trace_flush();
    g_passes++;
    for (const Applied& a : g_ptcl_applied) write_ptcl(a.ptcl, a.w);
    g_ptcl_keep = std::move(g_ptcl_applied);
    g_ptcl_applied.clear();
    g_wave_keep = 0;
    wave_restore();
    temp_restore();
}
// blended hold pass (after fx_pass_start and the save-state service, which want the exact values):
// particles, wave crests, sprites, sway and bush animations at this pass's fraction. (Values that
// drawing computes - sea grid, material animations, cloth, models, camera - are blended by their
// hooks while the pass draws.)
void fx_hold_blend(float t) {
    if (on(1)) ptcl_reapply(t);
    wave_reapply(t);
    temp_reapply(t);
}
}  // namespace interp

// dScnPly_Draw: start of drawing on every pass
extern "C" void hook_025AF8A0(Cpu* c) {
    // (already done at the pass start; a blended hold pass has its particles re-applied by now)
    if (!(interp::hold_pass() && !interp::record_pass())) ptcl_restore();
    if (interp::logic_pass()) fx_step_stats();
    f_025AF8A0_orig(c);
}

// JPAEmitterManager::calc(mgr, group)
extern "C" void hook_0282167C(Cpu* c) {
    if (hold_back()) return;  // no logic on hold passes (it ran twice per step: double speed)
    uint32_t mgr = c->r[3], group = c->r[4];
    if (!on(1) || !halfway() || group >= 16) {
        f_0282167C_orig(c);
        return;
    }
    g_ptcl_before.clear();
    for_each_ptcl(mgr, group, [&](uint32_t p, uint32_t) { g_ptcl_before[p] = read_ptcl(p); });
    static int tr = trace_left(0);
    PtclTrace trace;
    f_0282167C_orig(c);
    static const float kCut = 600.0f;
    static int tr_born = trace_left(5);
    const uint64_t step = interp::hold_pass_count();
    const float t = interp::pass_t();
    for_each_ptcl(mgr, group, [&](uint32_t p, uint32_t emtr) {
        auto it = g_ptcl_before.find(p);
        PtclState cur = read_ptcl(p);
        if (it == g_ptcl_before.end() || cur.age != it->second.age + 1.0f) {  // born this step (or a reused slot)
            g_pstats.fresh++;
            if (!on(64)) return;
            // drawn as if born (1-t) of a step earlier: back by that much of its own first step and
            // of the emitter's movement since its last calc (half a step at 60 fps)
            auto e = g_emtr.find(emtr);
            if (e == g_emtr.end() || e->second.step != step) return;
            const EmtrRec& er = e->second;
            bool moved = er.prev_step + 1 == step;
            float full[3], d2 = 0;
            for (int i = 0; i < 3; i++) {
                full[i] = wf(*(const uint32_t*)ppc_ptr(p + kPtclVel + 4 * i)) * er.scale[i] + (moved ? er.now[i] - er.prev[i] : 0.0f);
                float d = 0.5f * full[i];  // (the gate below as at 60 fps, on half a step)
                d2 += d * d;
            }
            if (!(d2 > 1e-8f && d2 < kCut * kCut / 4)) return;
            Applied ap{p};
            memcpy(ap.w, cur.w, sizeof ap.w);
            memcpy(ap.born, full, sizeof full);
            ap.is_born = true;
            uint32_t mid[kPW];
            ptcl_mid(ap, t, mid);
            if (tr_born && d2 > 0.01f) {
                tr_born--;
                LOG("[interp-fx] particle born at %.2f %.2f %.2f, drawn halfway at %.2f %.2f %.2f (emitter moved %.2f %.2f %.2f)",
                    wf(cur.w[0]), wf(cur.w[1]), wf(cur.w[2]), wf(mid[0]), wf(mid[1]), wf(mid[2]), moved ? er.now[0] - er.prev[0] : 0.0f,
                    moved ? er.now[1] - er.prev[1] : 0.0f, moved ? er.now[2] - er.prev[2] : 0.0f);
            }
            g_ptcl_applied.push_back(ap);
            write_ptcl(p, mid);
            g_pstats.born++;
            return;
        }
        const PtclState& a = it->second;
        float d2 = 0;
        for (int i = 0; i < 3; i++) d2 += (wf(a.w[i]) - wf(cur.w[i])) * (wf(a.w[i]) - wf(cur.w[i]));
        if (!(d2 <= kCut * kCut)) return;  // teleported with its emitter
        uint32_t mid[kPW];
        mid_ptcl(a.w, cur.w, mid, on(128), t);
        if (!memcmp(mid, cur.w, sizeof mid)) return;
        Applied ap{p};
        memcpy(ap.w, cur.w, sizeof ap.w);
        memcpy(ap.a, a.w, sizeof ap.a);
        ap.is_born = false;
        ap.extras = on(128);
        g_ptcl_applied.push_back(ap);
        write_ptcl(p, mid);
        g_pstats.blended++;
        if (tr) trace.add(a, cur, mid);
    });
    if (tr && (trace.n || trace.an || trace.rn)) {
        trace.log(group);
        tr--;
    }
}

// JPABaseEmitter::calc: the emitter's centre and scale (static emitter info) after its step
extern "C" void hook_0281FE40(Cpu* c) {
    uint32_t e = c->r[3];
    f_0281FE40_orig(c);
    if (!interp::enabled()) return;
    EmtrRec& r = g_emtr[e];
    const uint64_t step = interp::hold_pass_count();
    if (r.step != step) {
        memcpy(r.prev, r.now, sizeof r.prev);
        r.prev_step = r.step;
    }
    for (int i = 0; i < 3; i++) {
        r.now[i] = (float)ldf32(kEmtrInfo + kInfoCenter + 4 * i);
        r.scale[i] = (float)ldf32(kEmtrInfo + kInfoScale + 4 * i);
    }
    r.step = step;
}

// key animator evaluation (btk texture SRT, brk TEV colours, ...)
extern "C" void hook_027DF40C(Cpu* c) {
    if (!on(4) || true60::drawing_60()) {  // (60 Hz processes advance their animations every pass)
        f_027DF40C_orig(c);
        return;
    }
    uint32_t anm = c->r[3];
    uint32_t ts = ld32(anm);
    if (!ts) {
        f_027DF40C_orig(c);
        return;
    }
    float cur = (float)ldf32(ts);
    if (interp::record_pass()) {  // drawn at step S+1: the "before" of the next step's blended frames
        AnmRec& r = g_anm[anm];
        r.ts = ts;
        r.frame = cur;
        r.pass = interp::hold_pass_count();
        f_027DF40C_orig(c);
        return;
    }
    auto it = g_anm.end();
    if (halfway()) it = g_anm.find(anm);
    if (it == g_anm.end() || it->second.ts != ts || it->second.pass != interp::hold_pass_count() || it->second.frame == cur) {
        f_027DF40C_orig(c);
        return;
    }
    float mid = anm_mid(it->second.frame, cur, (float)ldf32(ts + 4), (float)ldf32(ts + 8), ld32(ts + 0x10) == kLoopFn, interp::pass_t());
    static int tr = trace_left(2);
    if (tr) {
        tr--;
        LOG("[interp-fx] anim %08X frames %.2f -> %.2f, drawn %.2f (%s)", anm, it->second.frame, cur, mid,
            ld32(ts + 0x10) == kLoopFn ? "loop" : "clamp");
    }
    if (mid == cur) {
        f_027DF40C_orig(c);
        return;
    }
    g_astats.blended++;
    stf32(ts, mid);
    f_027DF40C_orig(c);
    stf32(ts, cur);  // the evaluator's cache (+0x28) keeps mid, so the next exact frame re-evaluates
}

// J3DFrameCtrl::update: animation frames advanced from Draw (e.g. daSalvage_c::_draw) are logic too
// (the hook itself is in true60.cpp, which also scales the step for 60 Hz processes)
namespace interp {
bool fx_hold_anim() { return hold_back() && !in_execute(); }
}

// Random numbers drawn while DRAWING (candle / torch light flicker in
// dScnKy_env_light_c::settingTevStruct_plightcol_plus and dKy_setLight, and any other draw-time
// randomness): the game draws them once per drawn frame. At 60 fps that is twice as many random
// changes per second (jittery candle light) and twice the RNG use. On frames without logic every
// draw-time call of cM_rnd / cM_rndF / cM_rndFX gets the value the same call got on the last logic
// frame (keyed by the light's tevstr inside plightcol_plus, otherwise by the caller and how often
// it has called this frame) and the RNG is left alone, so flicker and random sequence match 30 fps.
namespace {
uint32_t g_plight_tevstr = 0;  // tevstr of the running plightcol_plus, 0 outside it
struct RndRec {
    std::unordered_map<uint64_t, std::vector<double>> vals;  // key -> values (as fraction of the max) in call order
    std::unordered_map<uint64_t, uint32_t> next;              // key -> calls so far this frame
    uint64_t frame = ~0ull;
};
RndRec g_rnd_logic, g_rnd_hold;  // recorded on logic frames, replayed on frames without logic
uint64_t rnd_key(Cpu* c, char fn) {
    if (g_plight_tevstr) return (1ull << 63) | ((uint64_t)fn << 32) | g_plight_tevstr;
    return ((uint64_t)fn << 32) | c->lr;
}
}
// debug: WWHD_LIGHT_TRACE=1 logs per pass the draw-time light state (dKy_setLight target/colour,
// plightcol_plus tevstr colour and light position)
namespace {
const bool g_light_trace = getenv("WWHD_LIGHT_TRACE") != nullptr;
std::string g_light_line;
}
namespace interp {
void light_trace_flush() {
    if (!g_light_trace || g_light_line.empty()) return;
    LOG("[light] step %llu %s%s", (unsigned long long)interp::logic_steps(), interp::hold_pass() ? "H" : "L", g_light_line.c_str());
    g_light_line.clear();
}
void light_trace_add(const char* fmt, ...) {
    if (!g_light_trace) return;
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    g_light_line += b;
}
}  // namespace interp
extern "C" void f_025615B8_orig(Cpu* c);  // dScnKy_env_light_c::settingTevStruct_plightcol_plus
// It also moves the tevstr's light position (+0x84, world) towards the light with cLib_addCalc on
// every call: on hold passes the position from before the logic pass's call is put back first (once
// per pass), so the hold pass draws the logic pass's light instead of smoothing twice per step.
namespace {
struct PlightPos {
    uint64_t step = ~0ull, restored = ~0ull;
    uint32_t w[3];
};
std::unordered_map<uint32_t, PlightPos> g_plight_pos;  // tevstr -> light position before the step's first call
void plight_pos_hold(uint32_t ts) {
    if (!on(8) || interp::in_execute()) return;
    const uint64_t step = interp::logic_steps();
    if (!interp::hold_pass()) {
        if (g_plight_pos.size() > 4096) g_plight_pos.clear();
        PlightPos& p = g_plight_pos[ts];
        if (p.step == step) return;
        p.step = step;
        for (int i = 0; i < 3; i++) p.w[i] = ld32(ts + 0x84 + 4 * i);
        return;
    }
    auto it = g_plight_pos.find(ts);
    if (it == g_plight_pos.end() || it->second.step != step || it->second.restored == interp::pass_count()) return;
    it->second.restored = interp::pass_count();  // (once per hold pass)
    for (int i = 0; i < 3; i++) st32(ts + 0x84 + 4 * i, it->second.w[i]);
}
}  // namespace
extern "C" void hook_025615B8(Cpu* c) {
    uint32_t saved = g_plight_tevstr;
    uint32_t ts = c->r[5];
    g_plight_tevstr = c->r[5];  // (this, pos, tevstr, c0, k0, timer)
    plight_pos_hold(ts);
    f_025615B8_orig(c);
    g_plight_tevstr = saved;
    if (g_light_trace)
        interp::light_trace_add(" | %X r%u w(%.1f,%.1f,%.1f) v(%.1f,%.1f,%.1f)", ts & 0xFFFFFF, ld8(ts + 0x18), ldf32(ts + 0x84), ldf32(ts + 0x88),
                                ldf32(ts + 0x8C), ldf32(ts + 0), ldf32(ts + 4), ldf32(ts + 8));
}
// debug: WWHD_RND_TRACE=1 logs, every 300 calls on frames without logic, which code draws
// random numbers there (cM_rnd / cM_rndF / cM_rndFX callers by return address)
void rnd_trace(Cpu* c, char fn, bool replayed) {
    static const bool on_ = getenv("WWHD_RND_TRACE") != nullptr;
    if (!on_ || !interp::fx_hold_anim()) return;
    static std::unordered_map<uint64_t, int> n;
    static int total = 0, hits = 0;
    n[((uint64_t)fn << 32) | c->lr]++;
    hits += replayed;
    if (++total % 300) return;
    std::vector<std::pair<int, uint64_t>> v;
    for (auto& e : n) v.push_back({e.second, e.first});
    std::sort(v.rbegin(), v.rend());
    LOG("[rnd] last 300 calls on frames without logic: %d replayed", hits);
    for (size_t i = 0; i < v.size() && i < 12; i++)
        LOG("[rnd]   caller %08X (cM_rnd%c): %d", (uint32_t)v[i].second, (char)(v[i].second >> 32), v[i].first);
    n.clear();
    hits = 0;
}
// fn: ' ' cM_rnd (fraction), 'F' cM_rndF(max), 'X' cM_rndFX(max) = (fraction - 0.5) * 2 * max
static void rnd_call(Cpu* c, char fn, void (*orig)(Cpu*)) {
    static int depth = 0;  // cM_rndF / cM_rndFX call cM_rnd: only the outer call counts
    // debug: WWHD_RND_LOG=path logs every outer call: logic step, hold pass, in execute, caller, seed r0 before
    static FILE* lf = getenv("WWHD_RND_LOG") ? fopen(getenv("WWHD_RND_LOG"), "w") : nullptr;
    static const uint32_t bt_lo = getenv("WWHD_RND_BT") ? (uint32_t)strtoul(getenv("WWHD_RND_BT"), nullptr, 16) : 0;
    if (lf && bt_lo && (c->lr >> 12) == (bt_lo >> 12)) {  // debug: back chain of callers in that 4 KB page
        uint32_t sp = c->r[1];
        fprintf(lf, "BT");
        for (int k = 0; k < 10 && sp >= 0x10000000; k++) { sp = ld32(sp); if (sp < 0x10000000) break; fprintf(lf, " %08X", ld32(sp + 4)); }
        fprintf(lf, "\n");
    }
    if (lf && !depth && (c->lr == GC(0x025616CCu) || getenv("WWHD_RND_REGS_ALL")))
        fprintf(lf, "REGS %08X %08X %08X %08X %08X %08X %08X L %08X %08X %08X %08X %08X\n", c->r[24], c->r[26], c->r[27], c->r[28], c->r[29], c->r[30], c->r[31],
                ld32(c->r[24] + 0x14), ld32(c->r[24] + 0x18), ld32(c->r[24] + 0x1C), ld32(c->r[24] + 0x28), ld32(c->r[24] + 0x2C));
    if (lf && !depth) fprintf(lf, "%llu %d %d %c %08X %08X\n", (unsigned long long)interp::logic_steps(), (int)interp::hold_pass(),
                               (int)interp::in_execute(), fn, c->lr, ld32(GD(0x101FF9D4)));
    if (!on(8) || interp::in_execute() || depth) {
        orig(c);
        return;
    }
    struct Nest { Nest() { depth++; } ~Nest() { depth--; } } nest;
    double max = fn == ' ' ? 1.0 : c->f[1].ps0;
    auto to_frac = [&](double v) { return max == 0 ? 0.0 : fn == 'X' ? (v / max + 1.0) * 0.5 : v / max; };
    auto from_frac = [&](double f) { return fn == 'X' ? (f - 0.5) * 2.0 * max : f * max; };
    uint64_t key = rnd_key(c, fn);
    if (interp::fx_hold_anim()) {
        RndRec& h = g_rnd_hold;
        if (h.frame != interp::pass_count()) h.frame = interp::pass_count(), h.next.clear();  // (call counts per pass)
        uint32_t i = h.next[key]++;
        auto it = g_rnd_logic.vals.find(key);
        bool hit = it != g_rnd_logic.vals.end() && i < it->second.size();
        rnd_trace(c, fn, hit);
        if (hit) {
            c->f[1].ps0 = (float)from_frac(it->second[i]);
            return;
        }
        // true 60: a draw on a half pass must not consume random numbers either (the full passes
        // keep the 30 fps sequence: drops, AI choices); it gets the next value without using it
        uint32_t seeds[3];
        bool keep = true60::enabled();
        if (keep)
            for (int k = 0; k < 3; k++) seeds[k] = ld32(GD(0x101FF9D4) + 4 * k);
        orig(c);
        if (keep)
            for (int k = 0; k < 3; k++) st32(GD(0x101FF9D4) + 4 * k, seeds[k]);
        return;
    }
    RndRec& r = g_rnd_logic;
    if (r.frame != interp::logic_steps()) r.frame = interp::logic_steps(), r.vals.clear();
    orig(c);
    auto& v = r.vals[key];
    if (v.size() < 64) v.push_back(to_frac(c->f[1].ps0));
}
extern "C" void f_02019788_orig(Cpu* c);  // cM_rnd
extern "C" void f_020198D8_orig(Cpu* c);  // cM_rndF(max)
extern "C" void f_02019918_orig(Cpu* c);  // cM_rndFX(max)
extern "C" void hook_02019788(Cpu* c) { rnd_call(c, ' ', f_02019788_orig); }
extern "C" void hook_020198D8(Cpu* c) { rnd_call(c, 'F', f_020198D8_orig); }
extern "C" void hook_02019918(Cpu* c) { rnd_call(c, 'X', f_02019918_orig); }

// sea vertex grid from the height table
extern "C" void hook_0246C5E8(Cpu* c) {
    uint32_t pk = c->r[3];
    uint32_t tab = ld32(pk + kSeaHeights);
    if (!on(2) || !tab) {
        f_0246C5E8_orig(c);
        return;
    }
    uint32_t* h = (uint32_t*)ppc_ptr(tab);
    float min_x = (float)ldf32(pk + kSeaMinX), min_z = (float)ldf32(pk + kSeaMinZ);
    if (interp::record_pass()) {
        g_sea.packet = pk;
        g_sea.pass = interp::hold_pass_count();
        g_sea.min_x = min_x;
        g_sea.min_z = min_z;
        g_sea.h.assign(h, h + kSeaCells);
        f_0246C5E8_orig(c);
        return;
    }
    if (!halfway() || g_sea.packet != pk || g_sea.pass != interp::hold_pass_count() || g_sea.h.size() != kSeaCells ||
        !(std::fabs(min_x - g_sea.min_x) < 2000.0f && std::fabs(min_z - g_sea.min_z) < 2000.0f)) {
        f_0246C5E8_orig(c);
        return;
    }
    const float t = interp::pass_t();
    std::vector<uint32_t> exact(h, h + kSeaCells);
    for (int i = 0; i < kSeaCells; i++) h[i] = fw(lerp_f(wf(g_sea.h[i]), wf(exact[i]), t));
    stf32(pk + kSeaMinX, t == 0.5f ? 0.5f * (min_x + g_sea.min_x) : lerp_f(g_sea.min_x, min_x, t));
    stf32(pk + kSeaMinZ, t == 0.5f ? 0.5f * (min_z + g_sea.min_z) : lerp_f(g_sea.min_z, min_z, t));
    static int tr = trace_left(1);
    if (tr) {
        tr--;
        int k = 0;  // the vertex that moved most
        for (int i = 1; i < kSeaCells; i++)
            if (std::fabs(wf(exact[i]) - wf(g_sea.h[i])) > std::fabs(wf(exact[k]) - wf(g_sea.h[k]))) k = i;
        LOG("[interp-fx] sea vertex %d,%d height %.3f -> %.3f, drawn %.3f; origin x %.1f -> %.1f", k % 65, k / 65, wf(g_sea.h[k]), wf(exact[k]),
            wf(h[k]), g_sea.min_x, min_x);
    }
    f_0246C5E8_orig(c);
    memcpy(h, exact.data(), 4 * kSeaCells);
    stf32(pk + kSeaMinX, min_x);
    stf32(pk + kSeaMinZ, min_z);
}

// sea material setup: the texture scroll counter advances once per call
extern "C" void hook_0246C7C8(Cpu* c) {
    uint32_t pk = c->r[3];
    if (hold_back() && on(2)) {  // keep the counter where the logic pass left it
        int16_t v = (int16_t)ld16(pk + kSeaScroll);
        st16(pk + kSeaScroll, (uint16_t)(v <= 0 ? 300 : v - 1));
    }
    g_sea_scroll_half = on(2) && halfway();
    f_0246C7C8_orig(c);
    g_sea_scroll_half = false;
}

// daSea_packet_c::CalcFlatInter: wave strength (+0x130, 0 = flat near islands, 1 = open sea)
// test aid: WWHD_SEA_WAVES=1 forces full waves everywhere (to check the wave blending on Outset)
extern "C" void hook_0246BD4C(Cpu* c) {
    static const bool force = getenv("WWHD_SEA_WAVES") != nullptr;
    uint32_t pk = c->r[3];
    f_0246BD4C_orig(c);
    if (force) {
        stf32(pk + 0x130, 1.0f);  // flat inter
        stf32(pk + 0xF0 + 0x24, 1.0f);  // daSea_WaveInfo::mCurScale (eases to the area's wave height)
    }
}

// PSMTXTrans(m, x, y, z): the sea's scroll translation, (1-t) of a step back on blended frames
extern "C" void hook_028E93CC(Cpu* c) {
    if (g_sea_scroll_half && c->lr == GC(0x0246C958)) {
        static int tr = trace_left(3);
        double y = c->f[2].ps0 - (1.0 - (double)interp::pass_t()) / 300.0;  // (0.5 / 300 at 60 fps)
        if (tr) {
            tr--;
            LOG("[interp-fx] sea scroll %.5f, drawn %.5f", c->f[2].ps0, y);
        }
        c->f[2].ps0 = y;
    }
    f_028E93CC_orig(c);
}

namespace {
// once per logic step: statistics and pruning
void fx_step_stats() {
    static uint64_t n = 0;
    if (++n % 300) return;
    static const bool stats = getenv("WWHD_INTERP_FX_STATS") != nullptr;
    if (stats)
        LOG("[interp-fx] per step: %.1f particles blended, %.1f new (%.1f drawn half a step back); anims %.1f blended, %.2f wrapped, %.2f jumped",
            g_pstats.blended / 300.0, g_pstats.fresh / 300.0, g_pstats.born / 300.0, g_astats.blended / 300.0, g_astats.wrapped / 300.0, g_astats.jumped / 300.0);
    g_pstats = {};
    g_astats = {};
    uint64_t now = interp::hold_pass_count();
    for (auto it = g_anm.begin(); it != g_anm.end();) it = it->second.pass + 2 < now ? g_anm.erase(it) : std::next(it);
    for (auto it = g_emtr.begin(); it != g_emtr.end();) it = it->second.step + 2 < now ? g_emtr.erase(it) : std::next(it);
}
}  // namespace

// The attention arrow (dAttDraw_c::draw 024EC1C8, from dAttention_c::Draw in the play scene's draw,
// every pass): model base = translate(target's attention position) x inverse camera rotation (it
// faces the camera); its animation (mDoExt_McaMorf at dAttDraw_c +0, frame control +0x98: rate,
// +0x9C frame) is played by dAttention_c::Run in the play scene's execute (30 Hz).
// In true 60 the camera runs at 60 Hz: blending the arrow's model matrices between the half pass and
// the full pass also blended its facing between the camera of the half pass and the current one (a
// quarter step behind the drawn camera). There, the full pass draws it unblended, built from the
// halfway target position and animation frame (the 30 Hz convention) and the current camera.
// Frame interpolation (no 60 Hz camera) keeps the model blending.
namespace {
constexpr uint32_t kMorfFrameCtrl = 0x98;
struct AttRec {
    float pos[3], frame;
    uint64_t pass;  // hold pass count when recorded
};
std::unordered_map<uint32_t, AttRec> g_att;
}  // namespace
extern "C" void hook_024EC1C8(Cpu* c) {
    uint32_t self = c->r[3], pos = c->r[4];
    uint32_t morf = ld32(self);
    if (!true60::enabled() || !interp::enabled() || !on(16) || morf < 0x10000000 || morf >= 0x50000000) {
        f_024EC1C8_orig(c);
        return;
    }
    const uint32_t fc = morf + kMorfFrameCtrl;
    if (interp::record_pass()) {  // half pass: exact; remembered for the next full pass
        AttRec& r = g_att[self];
        for (int i = 0; i < 3; i++) r.pos[i] = (float)ldf32(pos + 4 * i);
        r.frame = (float)ldf32(fc + 4);
        r.pass = interp::hold_pass_count();
        f_024EC1C8_orig(c);
        return;
    }
    if (!halfway()) {
        f_024EC1C8_orig(c);
        return;
    }
    const uint32_t mid_pos = mem::fixed_slot(mem::kFixFxMidPos);
    auto it = g_att.find(self);
    bool valid = it != g_att.end() && it->second.pass == interp::hold_pass_count();
    float d2 = 0, mp[3];
    for (int i = 0; i < 3; i++) {
        float cur = (float)ldf32(pos + 4 * i);
        mp[i] = valid ? lerp_f(it->second.pos[i], cur, interp::pass_t()) : cur;
        d2 += (mp[i] - cur) * (mp[i] - cur);
    }
    if (!(d2 < 200.0f * 200.0f)) for (int i = 0; i < 3; i++) mp[i] = (float)ldf32(pos + 4 * i);  // new target
    for (int i = 0; i < 3; i++) stf32(mid_pos + 4 * i, mp[i]);
    float frame = (float)ldf32(fc + 4);
    float mf = valid ? anm_mid(it->second.frame, frame, (float)(int16_t)ld16(fc + 8), (float)(int16_t)ld16(fc + 0xA), true, interp::pass_t()) : frame;
    static int tr = trace_left(6);
    if (tr) {
        tr--;
        LOG("[interp-fx] attention arrow (true 60, full pass): target %.2f %.2f %.2f -> drawn at %.2f %.2f %.2f, frame %.2f -> %.2f",
            ldf32(pos), ldf32(pos + 4), ldf32(pos + 8), mp[0], mp[1], mp[2], frame, mf);
    }
    stf32(fc + 4, mf);
    c->r[4] = mid_pos;
    true60::force_draw_60(true);  // no model blending: built from halfway values and the current camera
    f_024EC1C8_orig(c);
    true60::force_draw_60(false);
    stf32(fc + 4, frame);
}

// save state loaded: forget every effect history (the guest state jumped); interp.cpp also bumps the
// hold pass count, which invalidates the step-stamped records
namespace interp {
void fx_ss_reset() {
    g_ptcl_before.clear();
    g_ptcl_applied.clear();
    g_ptcl_keep.clear();
    g_emtr.clear();
    g_anm.clear();
    g_temp.clear();
    g_temp_keep.clear();
    g_wave.pkt = 0;
    g_wave_keep = 0;
    g_wave_pkt = 0;
    g_cloth_cur.clear();
    g_att.clear();
    for (auto& s : g_sway) s = SwayRec{};
    g_wood = WoodRec{};
    g_sea.packet = 0;
    g_sea.h.clear();
    g_sea_scroll_half = false;
}
}  // namespace interp
