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
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <initializer_list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "guest_addr.h"
#include "interp_chain.h"
#include "interp_pacing.h"
#include "interp_fade.h"
#include "interp_slots.h"
#include "interp_trail.h"
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
void blend_world_matrix(const float* a, const float* b, float* out, float t);
uint64_t logic_steps();   // logic steps run so far
float display_t();        // blend fraction of the pass being drawn or painted (interp.cpp)
bool display_record();    // the pass being drawn or painted is the record pass
// 40 fps blended record pass (interp.cpp): drawn blended (pass_t()) and recorded for the next step;
// its records are stamped record_stamp() (the hold pass count the next pass sees), and its blends
// use the records they replace (pacing::replace_record)
bool record_blended();
uint64_t record_stamp();
bool display_record_blended();    // (the pass being drawn or painted)
uint64_t display_record_stamp();
void exact_after_pass(uint32_t ea, uint32_t value, int size);  // (blended record pass: put back before the next logic)
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
    static int left[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
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
std::unordered_map<uint32_t, AnmRec> g_anm, g_anm_before;  // (before: blended record pass)
std::vector<uint32_t> g_anm_blended;  // animators evaluated at a blended frame since the last logic pass
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
// as temp_blend_f32, for a word that may already be blended in this pass: its entry takes the new
// exact value (a value the game changed twice in one pass is put back to its last exact value)
void temp_blend_f32_again(uint32_t a, uint32_t prev, float t) {
    for (auto it = g_temp.rbegin(); it != g_temp.rend(); ++it)
        if (it->addr == a && it->kind == kF32) {
            it->exact = ld32(a);
            it->prev = prev;
            st32(a, temp_value(*it, t));
            return;
        }
    temp_blend_f32(a, prev, t);
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
// block, which true60.cpp (site_025B00B0) runs on full passes only. Each kind keeps animation slots
// (interp_slots.h) written by its calc (in that block): 8 shared wind sway slots as a function of
// mTimer, and slots of their own for plants that are hit, cut or walked through (bent grass and
// flowers, shaking trees, the falling top of a cut tree). Layout per kind: slot array (packet
// offset, stride, count); the s16 and f32 fields its update/draw reads (shared slots: the sway
// angles only); the data array (offset, stride, count) with each plant's state byte and slot index.
//   grass  dGrass_anm_c  +0x18F0C, 0x38 x 104: +2 rot Y, +4 rot X;          data +0x9C, 0x44 x 1500 (+0 state, +2 slot)
//   tree   dTree_anm_c   +0x2A9C, 0x84 x 72:   +2..+0xA angles, +0x18..+0x20 the cut top's offset;
//                                                                           data +0x9C, 0xA8 x 64 (+0 state, +2 slot)
//   flower dFlower_anm_c +0x35BC, 0x38 x 72:   +2 angle Y, +4 bend;         data +0x9C, 0x44 x 200 (+0 flags, +1 slot)
// On logic passes the slots are drawn blended between the previous step's and this step's values
// (blended hold passes re-apply them); the exact ones are back for the record pass.
const uint32_t kCounterTimer = GD(0x101FF560);
struct SwayKind {
    uint32_t base, stride;
    int slots;
    int shared_fields;  // the first fields of `s16` are the shared slots' sway angles
    int s16_fields, f32_fields;
    uint32_t s16[5], f32[3];
    uint32_t data, data_stride;
    int data_count;
    uint32_t data_slot;  // offset of the plant's slot index (s8) in its data entry
};
constexpr SwayKind kSway[3] = {
    {0x18F0C, 0x38, 104, 1, 2, 0, {4, 2}, {}, 0x9C, 0x44, 1500, 2},
    {0x2A9C, 0x84, 72, 2, 5, 3, {4, 6, 2, 8, 0xA}, {0x18, 0x1C, 0x20}, 0x9C, 0xA8, 64, 2},
    {0x35BC, 0x38, 72, 1, 2, 0, {4, 2}, {}, 0x9C, 0x44, 200, 1},
};
constexpr int kMaxSlots = 104;
struct SwayRec {
    uint32_t pkt = 0;
    uint64_t step = 0;  // logic pass it was recorded on (hold pass count)
    int16_t v[kMaxSlots][5];
    uint32_t f[kMaxSlots][3];  // host-order float words
    uint8_t state[kMaxSlots];
    std::vector<int32_t> owner;
} g_sway[3];

void sway_calc(Cpu* c, int k, void (*orig)(Cpu*)) {
    const SwayKind& sk = kSway[k];
    uint32_t pk = c->r[3];
    orig(c);
    if (!on(16) || !halfway()) return;
    SwayRec& r = g_sway[k];
    const bool valid = r.pkt == pk && r.step + 1 == interp::hold_pass_count();
    std::vector<int32_t> owner;
    interp::slots::owners(
        sk.data_count, sk.slots, [&](int i) { return ld8(pk + sk.data + sk.data_stride * i) != 0; },
        [&](int i) { return (int)(int8_t)ld8(pk + sk.data + sk.data_stride * i + sk.data_slot); }, owner);
    static int tr = trace_left(4);
    static int tr_own = trace_left(4);
    const float t = interp::pass_t();
    for (int i = 0; i < sk.slots; i++) {
        const uint32_t slot = pk + sk.base + sk.stride * i;
        const uint8_t state = ld8(slot);
        const bool blend = interp::slots::blend_slot(i, valid, r.state[i], state, r.owner.size() == (size_t)sk.slots ? r.owner[i] : -1, owner[i]);
        const int s16_fields = i < interp::slots::kShared ? sk.shared_fields : sk.s16_fields;
        const int f32_fields = i < interp::slots::kShared ? 0 : sk.f32_fields;
        for (int f = 0; f < s16_fields; f++) {
            const uint32_t a = slot + sk.s16[f];
            const int16_t cur = (int16_t)ld16(a);
            if (blend && r.v[i][f] != cur) {
                temp_blend_s16(a, r.v[i][f], t);
                if (tr && i == 0 && f == 0) {
                    tr--;
                    LOG("[interp-fx] %s sway slot 0: %d -> %d, drawn %d", k == 0 ? "grass" : k == 1 ? "tree" : "flower", r.v[i][f], cur, (int16_t)ld16(a));
                }
                if (tr_own && i >= interp::slots::kShared && f == 0) {
                    tr_own--;
                    LOG("[interp-fx] %s slot %d (plant %d): %d -> %d, drawn %d", k == 0 ? "grass" : k == 1 ? "tree" : "flower", i, owner[i], r.v[i][f], cur,
                        (int16_t)ld16(a));
                }
            }
            r.v[i][f] = cur;
        }
        for (int f = 0; f < f32_fields; f++) {
            const uint32_t a = slot + sk.f32[f];
            const uint32_t cur = ld32(a);
            if (blend && r.f[i][f] != cur && plausible(u32_as_f32(r.f[i][f])) && plausible(u32_as_f32(cur)))
                temp_blend_f32(a, r.f[i][f], t);
            r.f[i][f] = cur;
        }
        r.state[i] = state;
    }
    r.owner = std::move(owner);
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
// Fallen trees fade out in dTree_packet_c::update (025CA6EC, from the draw, every pass:
// chaseAlpha(data +3, 0, 8) for each of the 64 trees whose state has bit 8): 2x/4x/8x too fast with
// frame interpolation. Advanced once per logic step, shown at the pass's fraction
// (interp_fade.h bytes_call). Tree data: packet +0x9C, 0xA8 apart, alpha at +3.
extern "C" void f_025CA6EC_orig(Cpu* c);
extern "C" void hook_025CA6EC(Cpu* c) {
    if (!on(8) || true60::enabled() || interp::in_execute()) { f_025CA6EC_orig(c); return; }
    const uint32_t pk = c->r[3];
    static interp::fade::BytesGate<64> gate;
    uint8_t v[64];
    for (int i = 0; i < 64; i++) v[i] = ld8(pk + 0x9C + 0xA8 * i + 3);
    interp::fade::bytes_call(gate, pk, interp::logic_steps(), !interp::hold_pass(), interp::pass_t(), v, [&](uint8_t* x) {
        for (int i = 0; i < 64; i++) st8(pk + 0x9C + 0xA8 * i + 3, x[i]);
        f_025CA6EC_orig(c);
        for (int i = 0; i < 64; i++) x[i] = ld8(pk + 0x9C + 0xA8 * i + 3);
    });
    static int tr = trace_left(4);
    for (int i = 0; i < 64; i++) {
        if (tr && gate.before[i] != gate.after[i]) {
            tr--;
            LOG("[interp-fx] fallen tree %d fade t %.3f: %u -> %u, drawn %u", i, interp::pass_t(), gate.before[i], gate.after[i], v[i]);
        }
        st8(pk + 0x9C + 0xA8 * i + 3, v[i]);
        if (v[i] != gate.after[i]) interp::exact_after_pass(pk + 0x9C + 0xA8 * i + 3, gate.after[i], 1);
    }
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

// Custom cloth packets bypass both J3DModel and dCloth_packet_c. Their upload functions
// consume a local mesh AND its placement matrix. Blend both from the last exact draw before
// the upload, then restore the simulation inputs. The generated vertex/uniform buffers retain
// this pass's pose for the painter. In particular, daGrid_c is the King of Red Lions' sail;
// dCloth_packet_c is not its simulation. Never advance or scale a cloth simulation here.
struct PacketField { uint32_t addr, words; bool matrix = false; };
struct PacketPose { uint64_t record; std::vector<uint32_t> words; };
enum ClothPacketKind { kGridSail, kPirateSail, kFortressFlag, kBuoyFlag, kBuoyPole, kMantle, kPirateFlag, kLineMat, kLineMat0, kSandFloor, kTapestry,
                       kGoalFlag, kClothPacketKinds };
constexpr const char* kClothPacketNames[] = {"KoRL sail", "pirate sail", "fortress flag", "buoy flag", "buoy pole", "mantle", "pirate flag", "3D lines",
                                             "3D lines (mat0)", "Molgera sand floor", "tapestry", "boat-race goal flag"};
static_assert(sizeof kClothPacketNames / sizeof kClothPacketNames[0] == kClothPacketKinds);
constexpr bool is_line(ClothPacketKind k) { return k == kLineMat || k == kLineMat0; }
std::unordered_map<uint64_t, PacketPose> g_packet_poses, g_packet_before;  // (before: blended record pass)
template <typename Fields>
void cloth_packet_fields(Cpu* c, ClothPacketKind kind, void (*original)(Cpu*), const Fields& fields) {
    if (!on(16) || interp::in_execute()) { original(c); return; }
    const uint64_t key = (uint64_t(kind) << 32) | c->r[3];
    const uint64_t record = interp::hold_pass_count();
    std::vector<uint32_t> exact;
    for (const auto& f : fields) {
        const auto* w = reinterpret_cast<const uint32_t*>(ppc_ptr(f.addr));
        exact.insert(exact.end(), w, w + f.words);
    }
    if (interp::record_pass()) {
        g_packet_poses[key] = {record, std::move(exact)};
        original(c);
        return;
    }
    const PacketPose* from = nullptr;
    if (interp::record_blended()) {
        const uint64_t stamp = interp::record_stamp();
        from = interp::pacing::replace_record(g_packet_poses, g_packet_before, key, PacketPose{stamp, exact}, stamp,
                                              [](const PacketPose& p) { return p.record; });
    } else {
        auto it = g_packet_poses.find(key);
        if (it != g_packet_poses.end()) from = &it->second;
    }
    if (!halfway() || !from ||
        (is_line(kind) ? from->record != record : from->record + 1 < record) ||
        from->words.size() != exact.size()) { original(c); return; }
    const auto& before = from->words;
    // As with J3D models, do not smear a teleport or a non-finite placement matrix.
    uint32_t offset = 0;
    for (const auto& f : fields) {
        if (f.matrix || is_line(kind)) {
            for (uint32_t j = 0; j < f.words; j++) {
                if (!std::isfinite(wf(before[offset+j])) || !std::isfinite(wf(exact[offset+j]))) {
                    original(c);
                    return;
                }
            }
            if (f.matrix) {
                float distance2 = 0;
                for (uint32_t j : {3u, 7u, 11u}) {
                    float d = wf(before[offset + j]) - wf(exact[offset + j]);
                    distance2 += d * d;
                }
                if (!(distance2 <= 400.f * 400.f)) { original(c); return; }
            } else {
                for (uint32_t j = 0; j < f.words; j += 3) {
                    float distance2 = 0;
                    for (uint32_t axis = 0; axis < 3; ++axis) {
                        float d = wf(before[offset+j+axis]) - wf(exact[offset+j+axis]);
                        distance2 += d*d;
                    }
                    if (!(distance2 <= 400.f * 400.f)) { original(c); return; }
                }
            }
        }
        offset += f.words;
    }
    const float t = interp::pass_t();
    offset = 0;
    for (const auto& f : fields) {
        auto* w = reinterpret_cast<uint32_t*>(ppc_ptr(f.addr));
        if (f.matrix) {
            float a[12], b[12], out[12];
            for (uint32_t j = 0; j < 12; j++) { a[j] = wf(before[offset+j]); b[j] = wf(exact[offset+j]); }
            interp::blend_world_matrix(a, b, out, t);
            for (uint32_t j = 0; j < 12; j++) w[j] = fw(out[j]);
        } else {
            for (uint32_t j = 0; j < f.words; j++) w[j] = fw(lerp_f(wf(before[offset+j]), wf(exact[offset+j]), t));
        }
        offset += f.words;
    }
    static const int trace = getenv("WWHD_INTERP_FX_TRACE") ? atoi(getenv("WWHD_INTERP_FX_TRACE")) : 0;
    static std::array<int, kClothPacketKinds> traces = [] { std::array<int, kClothPacketKinds> a{}; a.fill(trace); return a; }();
    if (traces[kind] > 0) {
        offset = 0;
        for (const auto& f : fields) {
            bool logged = false;
            const auto* drawn = reinterpret_cast<const uint32_t*>(ppc_ptr(f.addr));
            for (uint32_t j = 0; j < f.words; j++) {
                if (std::fabs(wf(exact[offset+j]) - wf(before[offset+j])) < .01f) continue;
                LOG("[interp-fx] custom cloth %s packet %08X t %.3f: %.4f -> %.4f, drawn %.4f",
                    kClothPacketNames[kind], c->r[3], t, wf(before[offset+j]), wf(exact[offset+j]), wf(drawn[j]));
                traces[kind]--;
                logged = true;
                break;
            }
            if (logged) break;
            offset += f.words;
        }
    }
    original(c);
    offset = 0;
    for (const auto& f : fields) {
        memcpy(ppc_ptr(f.addr), exact.data() + offset, f.words * sizeof(uint32_t));
        offset += f.words;
    }
}
void cloth_packet(Cpu* c, ClothPacketKind kind, void (*original)(Cpu*), std::initializer_list<PacketField> fields) {
    cloth_packet_fields(c, kind, original, fields);
}

// mDoExt_3DlineMat1 (textured: ropes, grapple, bridges) and mDoExt_3DlineMat0 (plain: Ganondorf's
// hair, Puppet Ganon's strings, Moblin and Stalfos weapon ropes, chained items) generate their GPU
// vertices in update/updateWidths, outside J3DModel::viewCalc. Their control points (set by the
// actors' logic) are drawn at the same pass fraction as the models. The exact control points are
// back in guest memory after the synchronous upload. Layout: line count (u16), points per line
// (u16), line array (0x10 each, +0 the line's points): Mat1 +0x17C/+0x17E/+0x184, Mat0
// +0x13C/+0x13E/+0x144.
struct LineLayout { uint32_t count, points, lines; };
constexpr LineLayout kLineMat1Layout{0x17C, 0x17E, 0x184}, kLineMat0Layout{0x13C, 0x13E, 0x144};
std::unordered_map<uint64_t, std::vector<uint32_t>> g_line_layouts;  // (kind << 32 | packet) -> layout seen
void line_packet(Cpu* c, void (*original)(Cpu*), ClothPacketKind kind, const LineLayout& lay) {
    if (!on(16) || true60::enabled() || interp::in_execute()) { original(c); return; }
    static const bool off = getenv("WWHD_INTERP_LINES") && !atoi(getenv("WWHD_INTERP_LINES")); // diagnostic ablation
    if (off) { original(c); return; }
    const uint32_t packet = c->r[3];
    const uint32_t count = ld16(packet+lay.count);
    const uint32_t points = std::min(c->r[4] & 0xFFFFu, uint32_t(ld16(packet+lay.points)));
    const uint32_t lines = ld32(packet+lay.lines);
    if (!lines || !count || count > 256 || points < 2 || points > 4096) { original(c); return; }
    std::vector<PacketField> fields;
    std::vector<uint32_t> layout{count, points, lines};
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t pos = ld32(lines+i*0x10);
        if (!pos) { original(c); return; }
        fields.push_back({pos, points*3});
        layout.push_back(pos);
    }
    const uint64_t key = (uint64_t(kind)<<32)|packet;
    auto& old = g_line_layouts[key];
    if (old != layout) {
        g_packet_poses.erase(key);
        g_packet_before.erase(key);
        old = std::move(layout);
    }
    cloth_packet_fields(c, kind, original, fields);
}
extern "C" void f_025EC62C_orig(Cpu*);
extern "C" void f_025ED1BC_orig(Cpu*);
extern "C" void f_025EA548_orig(Cpu*);
extern "C" void f_025EAF58_orig(Cpu*);
extern "C" void hook_025EC62C(Cpu* c) { line_packet(c, f_025EC62C_orig, kLineMat, kLineMat1Layout); }
extern "C" void hook_025ED1BC(Cpu* c) { line_packet(c, f_025ED1BC_orig, kLineMat, kLineMat1Layout); }
extern "C" void hook_025EA548(Cpu* c) { line_packet(c, f_025EA548_orig, kLineMat0, kLineMat0Layout); }
extern "C" void hook_025EAF58(Cpu* c) { line_packet(c, f_025EAF58_orig, kLineMat0, kLineMat0Layout); }
extern "C" void f_021625DC_orig(Cpu*); // daHo_packet_c upload (KoRL sail)
extern "C" void hook_021625DC(Cpu* c) {
    const uint32_t p = c->r[3];
    cloth_packet(c, kGridSail, f_021625DC_orig, {{p+0xAC, 12, true}, {p+0xE0, 85*3*3}});
}
extern "C" void f_0245F638_orig(Cpu*); // pirate ship sail upload
extern "C" void hook_0245F638(Cpu* c) {
    const uint32_t p = c->r[3], i = ld8(p+0x2EBE);
    if (i > 1 || !on(16)) { f_0245F638_orig(c); return; }
    cloth_packet(c, kPirateSail, f_0245F638_orig, {{p+0x1294, 12, true},
        {p+0x1328+i*0x3F0, 84*3}, {p+0x1EF8+i*0x3F0, 84*3}, {p+0x26D8+i*0x3F0, 84*3}});
}
extern "C" void f_021B89B4_orig(Cpu*); // Forsaken Fortress flag upload
extern "C" void hook_021B89B4(Cpu* c) {
    const uint32_t p = c->r[3], i = ld8(p+0x1A2E);
    if (i > 1 || !on(16)) { f_021B89B4_orig(c); return; }
    cloth_packet(c, kFortressFlag, f_021B89B4_orig, {{p+0x1290, 12, true},
        {p+0x1344+i*0xFC, 21*3}, {p+0x153C+i*0xFC, 21*3}, {p+0x1734+i*0xFC, 21*3}});
}
extern "C" void f_0232B624_orig(Cpu*); // buoy flag upload
extern "C" void hook_0232B624(Cpu* c) {
    const uint32_t p = c->r[3], i = ld32(p+0xC18);
    if (i > 1 || !on(16)) { f_0232B624_orig(c); return; }
    cloth_packet(c, kBuoyFlag, f_0232B624_orig, {{p+0xC4C, 12, true}, {p+0x9C+i*0x4EC, 35*3*3}});
}
extern "C" void f_0232B984_orig(Cpu*); // buoy pole: keep the flag's attachment at the same fraction
extern "C" void hook_0232B984(Cpu* c) {
    cloth_packet(c, kBuoyPole, f_0232B984_orig, {{c->r[3]+0xC1C, 12, true}});
}
extern "C" void f_023D0E68_orig(Cpu*); // pirate ship skull flag upload
extern "C" void hook_023D0E68(Cpu* c) {
    const uint32_t p = c->r[3], i = ld8(p+0x906);
    if (i > 1 || !on(16)) { f_023D0E68_orig(c); return; }
    cloth_packet(c, kPirateFlag, f_023D0E68_orig, {{p+0x98, 12, true},
        {p+0xCC+i*0x12C, 25*3}, {p+0x324+i*0x12C, 25*3}, {p+0x57C+i*0x12C, 25*3}});
}
extern "C" void f_021BBC34_orig(Cpu*); // mantle/cape vertex fill and uniforms
extern "C" void hook_021BBC34(Cpu* c) {
    const uint32_t p = c->r[3];
    cloth_packet(c, kMantle, f_021BBC34_orig,
        {{p+0x98, 12, true}, {p+0xC8, 12, true}, {p+0xFC, 81*3*2}});
}
// Molgera's sand floor (daBwdg_packet_c::prepare 02103B30, from its draw): the vertex buffer is filled
// from the height field (+0xCC positions, +0xC6D8 normals, 0x1081 each) that its execute (wave_cont
// 02103FA4) moves once per step
extern "C" void f_02103B30_orig(Cpu*);
extern "C" void hook_02103B30(Cpu* c) {
    const uint32_t p = c->r[3];
    cloth_packet(c, kSandFloor, f_02103B30_orig, {{p+0xCC, 0x1081*3}, {p+0xC6D8, 0x1081*3}});
}
// tapestry (daObjTapestryPacket_c::setup_vtx 0239DD2C, from its draw): the cloth's current buffer
// (+0x106C + 0x6C0 * buffer +0x20BC: 48 positions, normals, back normals; simulated by its execute)
// and its world matrix (+0x2390)
extern "C" void f_0239DD2C_orig(Cpu*);
extern "C" void hook_0239DD2C(Cpu* c) {
    const uint32_t p = c->r[3], i = ld32(p+0x20BC);
    if (i > 1 || !on(16)) { f_0239DD2C_orig(c); return; }
    cloth_packet(c, kTapestry, f_0239DD2C_orig, {{p+0x2390, 12, true}, {p+0x106C+i*0x6C0, 48*3*3}});
}
// boat-race goal flag (daGFlag_packet_c::update 0215F9E4, from its draw): the current cloth arrays
// (+0x2648 index: positions +0x134C, normals +0x1BBC, back normals +0x1FF4; 45 each, 0x21C per array)
// and the flag's matrix (+0x1318)
extern "C" void f_0215F9E4_orig(Cpu*);
extern "C" void hook_0215F9E4(Cpu* c) {
    const uint32_t p = c->r[3], i = ld8(p+0x2648);
    if (i > 1 || !on(16)) { f_0215F9E4_orig(c); return; }
    cloth_packet(c, kGoalFlag, f_0215F9E4_orig, {{p+0x1318, 12, true},
        {p+0x134C+i*0x21C, 45*3}, {p+0x1BBC+i*0x21C, 45*3}, {p+0x1FF4+i*0x21C, 45*3}});
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
    if (!g_packet_before.empty()) g_packet_before.clear();  // (records a 40 fps blended record pass replaced: its own blends only)
    if (!g_anm_before.empty()) g_anm_before.clear();
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
// The grey-scale screen (mDoGph_calcMonotone 025F0840, from the environment's execute: once per
// step) chases l_graphics (101F4808) +0x18 mMonotoneRate towards its target by a fixed step: drawn
// between the step's start and end on blended passes (interp.cpp records the step's change).
namespace {
struct MonoStep { uint64_t step = ~0ull; int16_t before = 0, after = 0; } g_mono;
}  // namespace
namespace interp {
void fx_monotone_step(int16_t before, int16_t after) {
    if (g_mono.step != logic_steps()) g_mono = MonoStep{logic_steps(), before, after};
    else g_mono.after = after;
}
}  // namespace interp
extern "C" void hook_025AF8A0(Cpu* c) {
    // (already done at the pass start; a blended hold pass has its particles re-applied by now)
    if (!(interp::hold_pass() && !interp::record_pass())) ptcl_restore();
    if (interp::logic_pass()) fx_step_stats();
    if (interp::logic_pass() && on(32) && !true60::enabled() && g_mono.step == interp::logic_steps() && g_mono.before != g_mono.after &&
        (int16_t)ld16(GD(0x101F4808) + 0x18) == g_mono.after) {
        temp_blend_s16(GD(0x101F4808) + 0x18, g_mono.before, interp::pass_t());
        static int tr = trace_left(14);
        if (tr) {
            tr--;
            LOG("[interp-fx] monotone t %.3f: %d -> %d, drawn %d", interp::pass_t(), g_mono.before, g_mono.after, (int16_t)ld16(GD(0x101F4808) + 0x18));
        }
    }
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
    const AnmRec* from = nullptr;
    if (interp::record_blended() && !interp::in_execute()) {  // (drawn at step S+1, blended from S)
        const uint64_t stamp = interp::record_stamp();
        from = interp::pacing::replace_record(g_anm, g_anm_before, anm, AnmRec{ts, cur, stamp}, stamp, [](const AnmRec& r) { return r.pass; });
        if (!halfway()) from = nullptr;
    } else if (halfway()) {
        auto it = g_anm.find(anm);
        if (it != g_anm.end()) from = &it->second;
    }
    if (!from || from->ts != ts || from->pass != interp::hold_pass_count() || from->frame == cur) {
        f_027DF40C_orig(c);
        return;
    }
    float mid = anm_mid(from->frame, cur, (float)ldf32(ts + 4), (float)ldf32(ts + 8), ld32(ts + 0x10) == kLoopFn, interp::pass_t());
    static int tr = trace_left(2);
    if (tr) {
        tr--;
        LOG("[interp-fx] anim %08X frames %.2f -> %.2f, drawn %.2f (%s)", anm, from->frame, cur, mid,
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
    if (g_anm_blended.size() < 4096) g_anm_blended.push_back(anm);
}
// Before the next step's logic (interp.cpp, start of a logic pass): animators whose last evaluation
// in the step was a blended one (no exact evaluation followed: their caller evaluates only when it
// sees the frame change) are evaluated at the step's exact frame, so their values and their cache
// (+0x28) are what the 30 fps frame of the step left (the step's actors keep it in their memory).
// debug: WWHD_INTERP_ANM_SETTLE=0 leaves them as drawn
namespace interp {
void fx_anm_settle(Cpu* c) {
    static const bool off = getenv("WWHD_INTERP_ANM_SETTLE") && !atoi(getenv("WWHD_INTERP_ANM_SETTLE"));
    if (g_anm_blended.empty()) return;
    std::vector<uint32_t> list;
    list.swap(g_anm_blended);
    if (off || true60::enabled()) return;
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
    const Cpu saved = *c;
    int settled = 0;
    for (uint32_t anm : list) {
        const uint32_t ts = ld32(anm);
        if (ts < 0x10000000 || ts >= 0x50000000) continue;
        if (ld32(anm + 0x28) == ld32(ts)) continue;  // (evaluated at the exact frame since)
        c->r[3] = anm;
        f_027DF40C_orig(c);
        settled++;
    }
    *c = saved;
    static int tr = trace_left(14);
    if (tr && settled) {
        tr--;
        LOG("[interp-fx] %d material animations evaluated at the step's exact frame before its logic", settled);
    }
}
}  // namespace interp

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
        // A draw on a pass without logic must not consume random numbers (frame interpolation and
        // true 60): the logic keeps the 30 fps sequence (drops, AI choices, effects). It gets the next
        // value without using it. (With frame interpolation this was missing: a draw that called
        // cM_rnd more often on a hold pass than on its logic pass, or only there, advanced the
        // game's seeds once per extra drawn frame, and from then on the game drew other random
        // numbers than at 30 fps: the logic of a 120 fps run left the 30 fps one within seconds.)
        // debug: WWHD_INTERP_RND_KEEP=0 lets those calls consume the seeds again
        static const bool consume = getenv("WWHD_INTERP_RND_KEEP") && !atoi(getenv("WWHD_INTERP_RND_KEEP"));
        uint32_t seeds[3];
        const bool keep = true60::enabled() || !consume;
        if (keep)
            for (int k = 0; k < 3; k++) seeds[k] = ld32(GD(0x101FF9D4) + 4 * k);
        orig(c);
        if (keep)
            for (int k = 0; k < 3; k++) st32(GD(0x101FF9D4) + 4 * k, seeds[k]);
        static int tr = trace_left(15);
        if (tr) {
            tr--;
            LOG("[interp-fx] cM_rnd%c from %08X on a pass without logic, no logic-pass value: seeds %s", fn, c->lr, keep ? "kept" : "consumed");
        }
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
    // 40 fps blended record pass: the step recorded, drawn blended from the record before it
    static SeaRec before;
    if (interp::record_blended()) {
        const uint64_t stamp = interp::record_stamp();
        if (g_sea.pass != stamp) std::swap(before, g_sea);  // (its first record in this pass)
        g_sea.packet = pk;
        g_sea.pass = stamp;
        g_sea.min_x = min_x;
        g_sea.min_z = min_z;
        g_sea.h.assign(h, h + kSeaCells);
    }
    const SeaRec& from = interp::record_blended() ? before : g_sea;
    if (!halfway() || from.packet != pk || from.pass != interp::hold_pass_count() || from.h.size() != kSeaCells ||
        !(std::fabs(min_x - from.min_x) < 2000.0f && std::fabs(min_z - from.min_z) < 2000.0f)) {
        f_0246C5E8_orig(c);
        return;
    }
    const float t = interp::pass_t();
    std::vector<uint32_t> exact(h, h + kSeaCells);
    for (int i = 0; i < kSeaCells; i++) h[i] = fw(lerp_f(wf(from.h[i]), wf(exact[i]), t));
    stf32(pk + kSeaMinX, t == 0.5f ? 0.5f * (min_x + from.min_x) : lerp_f(from.min_x, min_x, t));
    stf32(pk + kSeaMinZ, t == 0.5f ? 0.5f * (min_z + from.min_z) : lerp_f(from.min_z, min_z, t));
    static int tr = trace_left(1);
    if (tr) {
        tr--;
        int k = 0;  // the vertex that moved most
        for (int i = 1; i < kSeaCells; i++)
            if (std::fabs(wf(exact[i]) - wf(from.h[i])) > std::fabs(wf(exact[k]) - wf(from.h[k]))) k = i;
        LOG("[interp-fx] sea vertex %d,%d height %.3f -> %.3f, drawn %.3f; origin x %.1f -> %.1f", k % 65, k / 65, wf(from.h[k]), wf(exact[k]),
            wf(h[k]), from.min_x, min_x);
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
    for (auto it = g_line_layouts.begin(); it != g_line_layouts.end();) {
        auto pose = g_packet_poses.find(it->first);
        if (pose == g_packet_poses.end() || pose->second.record + 2 < now) {
            if (pose != g_packet_poses.end()) g_packet_poses.erase(pose);
            it = g_line_layouts.erase(it);
        } else {
            ++it;
        }
    }
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

// ---------------------------------------------------------------------------------------------
// Swing trails (interp_trail.h): the sword (daPy_swBlur_c::draw 024142F0: segments +0x9C, blade
// tips +0xBC and bases +0x38C, 60 points each) and the boomerang (daBoomerang_blur_c::draw
// 020CDF78: segments +0x98, two rings of tips and roots at +0xA8, +0x378, +0x648, +0x918). Both are
// J3DPacket draws, painted after the pass: the record pass's history is kept, blended passes draw
// the history at their fraction (the head following the blended blade), then the exact one is put
// back.
namespace {
struct TrailRec { uint64_t record; interp::trail::Frame f; };
std::unordered_map<uint32_t, TrailRec> g_trails, g_trails_before;  // (before: blended record pass)
interp::trail::Frame read_trail(uint32_t pkt, uint32_t count, std::initializer_list<uint32_t> lanes) {
    interp::trail::Frame f;
    f.lanes = 0;
    f.count = (int32_t)ld32(pkt + count);
    for (uint32_t lane : lanes) {
        for (int i = 0; i < interp::trail::kPoints; i++)
            for (int x = 0; x < 3; x++) f.p[f.lanes][i][x] = (float)ldf32(pkt + lane + 12 * i + 4 * x);
        f.lanes++;
    }
    return f;
}
void trail_draw(Cpu* c, void (*orig)(Cpu*), uint32_t count, std::initializer_list<uint32_t> lanes) {
    static const bool off = getenv("WWHD_INTERP_TRAILS") && !atoi(getenv("WWHD_INTERP_TRAILS"));  // diagnostic ablation
    if (!on(16) || off) { orig(c); return; }
    const uint32_t pkt = c->r[3];
    const interp::trail::Frame b = read_trail(pkt, count, lanes);
    if (interp::display_record()) {
        if (g_trails.size() > 64) g_trails.clear();
        g_trails[pkt] = TrailRec{interp::hold_pass_count(), b};
        orig(c);
        return;
    }
    const float t = interp::display_t();
    const TrailRec* from = nullptr;
    if (interp::display_record_blended()) {  // 40 fps: recorded, drawn blended from the record before
        static uint64_t cleared = ~0ull;  // (painted after the pass: `before` is kept per pass here)
        if (cleared != interp::pass_count()) g_trails_before.clear(), cleared = interp::pass_count();
        if (g_trails.size() > 64) g_trails.clear();
        const uint64_t stamp = interp::display_record_stamp();
        from = interp::pacing::replace_record(g_trails, g_trails_before, pkt, TrailRec{stamp, b}, stamp, [](const TrailRec& r) { return r.record; });
    } else {
        auto it = g_trails.find(pkt);
        if (it != g_trails.end()) from = &it->second;
    }
    static interp::trail::Frame m;
    if (t >= 1.0f || !from || from->record != interp::hold_pass_count() || !interp::trail::at(from->f, b, t, m)) {
        orig(c);
        return;
    }
    static int tr = trace_left(7);
    if (tr) {
        tr--;
        LOG("[interp-fx] trail %08X t %.3f: %d -> %d segments, head %.1f %.1f %.1f (new %.1f %.1f %.1f), drawn %d", pkt, t, from->f.count, b.count,
            from->f.p[0][0][0], from->f.p[0][0][1], from->f.p[0][0][2], b.p[0][0][0], b.p[0][0][1], b.p[0][0][2], m.count);
    }
    std::vector<uint32_t> saved;
    saved.push_back(ld32(pkt + count));
    int l = 0;
    for (uint32_t lane : lanes) {
        for (int i = 0; i < interp::trail::kPoints; i++)
            for (int x = 0; x < 3; x++) {
                const uint32_t a = pkt + lane + 12 * i + 4 * x;
                saved.push_back(ld32(a));
                stf32(a, m.p[l][i][x]);
            }
        l++;
    }
    st32(pkt + count, (uint32_t)m.count);
    orig(c);
    size_t k = 0;
    st32(pkt + count, saved[k++]);
    for (uint32_t lane : lanes)
        for (int i = 0; i < interp::trail::kPoints; i++)
            for (int x = 0; x < 3; x++) st32(pkt + lane + 12 * i + 4 * x, saved[k++]);
}
}  // namespace
extern "C" void f_024142F0_orig(Cpu*);
extern "C" void f_020CDF78_orig(Cpu*);
extern "C" void hook_024142F0(Cpu* c) { trail_draw(c, f_024142F0_orig, 0x9C, {0xBC, 0x38C}); }
extern "C" void hook_020CDF78(Cpu* c) { trail_draw(c, f_020CDF78_orig, 0x98, {0xA8, 0x378, 0x648, 0x918}); }

// The hookshot's chain (daHookshot_shape_draw 021768B0, from daHookshot_c::draw, every pass) is
// built from Link's hookshot root (daPy_lk_c +0x72F0), the hookshot's shape angle (+0x328 x,
// +0x32A y) and link count (+0xD504, |tip - root| / 7 from its execute), all exact, while Link and
// the hook are drawn blended. On blended passes it is built from the blended root and angles (an
// angle only between two steps with a chain: interp_chain.h chain_angle), with the count from the
// blended root-tip distance (tip: current.pos +0x314). The shape's GX2 draw
// (021783F4, when the pass's lists are painted) then draws that many links, not the exact count
// (interp_chain.h, issue #304: links beyond the blended count were drawn from matrices of their last
// fill, the previous shot).
namespace {
struct HookRec { uint64_t record = ~0ull; uint32_t hs = 0; float root[3], tip[3]; int16_t ax, ay; int32_t count = 0; };
HookRec g_hook;
interp::ChainCount g_chain_drawn;
}  // namespace
extern "C" void f_021768B0_orig(Cpu*);
extern "C" void hook_021768B0(Cpu* c) {
    const uint32_t hs = ld32(c->r[3] + 0x18);
    const uint32_t link = ld32(GD(0x1046F0B0) + 0x5B34);
    if (!on(16) || !hs || !link || true60::enabled()) { f_021768B0_orig(c); return; }
    const uint32_t root = link + 0x72F0, tip = hs + 0x314, ang = hs + 0x328, cnt = hs + 0xD504;
    float r[3], p[3];
    for (int i = 0; i < 3; i++) { r[i] = (float)ldf32(root + 4 * i); p[i] = (float)ldf32(tip + 4 * i); }
    const int16_t ax = (int16_t)ld16(ang), ay = (int16_t)ld16(ang + 2);
    // (40 fps blended record pass: recorded, then drawn blended from the record before it)
    static HookRec before;
    const bool blendRecord = interp::record_blended();
    if (interp::record_pass() || blendRecord) {
        const uint64_t stamp = interp::record_stamp();
        if (blendRecord && g_hook.record != stamp) before = g_hook;
        g_hook.record = stamp;
        g_hook.hs = hs;
        for (int i = 0; i < 3; i++) { g_hook.root[i] = r[i]; g_hook.tip[i] = p[i]; }
        g_hook.ax = ax;
        g_hook.ay = ay;
        g_hook.count = (int32_t)ld32(cnt);
        if (!blendRecord) {
            f_021768B0_orig(c);
            return;
        }
    }
    const HookRec& from = blendRecord ? before : g_hook;
    static int ptr = getenv("WWHD_INTERP_CHAIN_TRACE") ? atoi(getenv("WWHD_INTERP_CHAIN_TRACE")) : 0;
    if (ptr > 0 && ((int32_t)ld32(cnt) > 0 || from.count > 0)) {
        ptr--;
        LOG("[interp-fx] hookshot shape draw pass %llu (step %llu, t %.3f): halfway %d, same hook %d, record %llu/%llu, count %d -> %d, angle %d %d -> %d %d",
            (unsigned long long)interp::pass_count(), (unsigned long long)interp::logic_steps(), interp::pass_t(), (int)halfway(), (int)(from.hs == hs),
            (unsigned long long)from.record, (unsigned long long)interp::hold_pass_count(), from.count, (int32_t)ld32(cnt), from.ax, from.ay, ax, ay);
    }
    // (no chain on either step: nothing to draw; the hook still in Link's hand)
    if (!halfway() || from.hs != hs || from.record != interp::hold_pass_count() || (from.count <= 0 && (int32_t)ld32(cnt) <= 0)) {
        f_021768B0_orig(c);
        return;
    }
    const float t = interp::pass_t();
    float br[3], bp[3], dr = 0, dp = 0, d = 0;
    for (int i = 0; i < 3; i++) {
        br[i] = lerp_f(from.root[i], r[i], t);
        bp[i] = lerp_f(from.tip[i], p[i], t);
        dr += (r[i] - from.root[i]) * (r[i] - from.root[i]);
        dp += (p[i] - from.tip[i]) * (p[i] - from.tip[i]);
        d += (bp[i] - br[i]) * (bp[i] - br[i]);
    }
    if (!(dr < 400.0f * 400.0f) || !(dp < 2000.0f * 2000.0f)) { f_021768B0_orig(c); return; }  // a jump: drawn as it is
    const uint32_t saved[6] = {ld32(root), ld32(root + 4), ld32(root + 8), ld32(ang), ld32(cnt), 0};
    for (int i = 0; i < 3; i++) stf32(root + 4 * i, br[i]);
    const int32_t exact_count = (int32_t)ld32(cnt);
    st16(ang, (uint16_t)interp::chain_angle(from.count, from.ax, exact_count, ax, t));
    st16(ang + 2, (uint16_t)interp::chain_angle(from.count, from.ay, exact_count, ay, t));
    int32_t n = (int32_t)(std::sqrt(d) / 7.0f);
    if (n > 300) n = 300;
    st32(cnt, (uint32_t)n);
    g_chain_drawn.filled(c->r[3], interp::pass_count(), n);
    static int tr = trace_left(7);
    if (tr) {
        tr--;
        LOG("[interp-fx] hookshot chain pass %llu t %.3f: root %.1f %.1f %.1f -> %.1f %.1f %.1f, drawn at %.1f %.1f %.1f, %d links (exact %d)", (unsigned long long)interp::pass_count(), t, from.root[0],
            from.root[1], from.root[2], r[0], r[1], r[2], br[0], br[1], br[2], n, (int32_t)saved[4]);
    }
    f_021768B0_orig(c);
    st32(root, saved[0]);
    st32(root + 4, saved[1]);
    st32(root + 8, saved[2]);
    st32(ang, saved[3]);
    st32(cnt, saved[4]);
}
// daHookshot_shape GX2 draw (021783F4, r3 the shape): on a pass whose links were filled for the
// blended count, that count (its user area +0x18 is the hookshot, +0xD504 the count)
extern "C" void f_021783F4_orig(Cpu*);
extern "C" void hook_021783F4(Cpu* c) {
    const uint32_t shape = c->r[3], hs = ld32(shape + 0x18);
    const int32_t exact = hs ? (int32_t)ld32(hs + 0xD504) : 0;
    const int32_t n = g_chain_drawn.drawn(shape, interp::pass_count(), exact);
    // debug: WWHD_INTERP_CHAIN_TRACE=n logs n GX2 draws of the chain (pass, links drawn, exact count)
    static int tr = getenv("WWHD_INTERP_CHAIN_TRACE") ? atoi(getenv("WWHD_INTERP_CHAIN_TRACE")) : 0;
    if (tr > 0 && exact > 0) {
        tr--;
        LOG("[interp-fx] hookshot chain GX2 draw pass %llu (step %llu, t %.3f, record %d): %d links (exact %d; filled pass %llu)",
            (unsigned long long)interp::pass_count(), (unsigned long long)interp::logic_steps(), interp::display_t(), (int)interp::display_record(), n,
            exact, (unsigned long long)g_chain_drawn.pass);
    }
    if (!hs || n == exact) {
        f_021783F4_orig(c);
        return;
    }
    st32(hs + 0xD504, (uint32_t)n);
    f_021783F4_orig(c);
    st32(hs + 0xD504, (uint32_t)exact);
}

// The King of Red Lions (daShip_c::draw 0247255C, actor Draw, every pass): the hull's texture
// scroll (+0x6B8, advanced 0..1 with wrap in the execute, written into the texture SRTs by the
// draw) is drawn between the steps' values. The cannon sight's arc (25 segments of the sight
// packet at +0x1B54: +0xA0 the segments, 0xA8 apart, matrix +0x74), placed by the execute along
// the shot's path every 8 path samples from a phase that advances 0.75 a step (101D0348), is drawn
// between the steps' positions (painted after the pass: blended on the logic pass, re-applied on the
// blended hold passes). When the phase wraps, every segment moved one place along the arc: segment i
// blends from the previous step's segment i - 1, and segment 0 is new. Segments not in use hold the
// packet's reset matrix (104A03EC).
namespace {
struct ShipRec { uint64_t record = ~0ull; uint32_t ship = 0; float scroll = 0, phase = 0; uint32_t seg[25][12]; };
ShipRec g_ship;
}  // namespace
extern "C" void f_0247255C_orig(Cpu*);
extern "C" void hook_0247255C(Cpu* c) {
    const uint32_t ship = c->r[3];
    if (!on(16) || true60::enabled()) { f_0247255C_orig(c); return; }
    const uint32_t scroll_a = ship + 0x6B8, segs = ld32(ship + 0x1B54 + 0xA0), phase_a = GD(0x101D0348), reset = GD(0x104A03EC);
    const float scroll = (float)ldf32(scroll_a), phase = (float)ldf32(phase_a);
    // (40 fps blended record pass: recorded, then drawn blended from the record before it)
    static ShipRec before;
    const bool blendRecord = interp::record_blended();
    if (interp::record_pass() || blendRecord) {
        const uint64_t stamp = interp::record_stamp();
        if (blendRecord && g_ship.record != stamp) before = g_ship;
        g_ship.record = stamp;
        g_ship.ship = ship;
        g_ship.scroll = scroll;
        g_ship.phase = phase;
        for (int i = 0; i < 25 && segs; i++)
            for (int k = 0; k < 12; k++) g_ship.seg[i][k] = ld32(segs + 0xA8 * i + 0x74 + 4 * k);
        if (!blendRecord) {
            f_0247255C_orig(c);
            return;
        }
    }
    const ShipRec& from = blendRecord ? before : g_ship;
    if (!halfway() || from.ship != ship || from.record != interp::hold_pass_count()) { f_0247255C_orig(c); return; }
    const float t = interp::pass_t();
    float d = scroll - from.scroll;  // the scroll wraps over 0..1
    if (d > 0.5f) d -= 1.0f;
    else if (d < -0.5f) d += 1.0f;
    float m = from.scroll + t * d;
    if (m >= 1.0f) m -= 1.0f;
    else if (m < 0.0f) m += 1.0f;
    if (d != 0.0f && std::fabs(d) < 0.25f) stf32(scroll_a, m);
    static int tr_scroll = trace_left(7);
    if (tr_scroll && d != 0.0f) {
        tr_scroll--;
        LOG("[interp-fx] ship hull scroll t %.3f: %.4f -> %.4f, drawn %.4f", t, from.scroll, scroll, (float)ldf32(scroll_a));
    }
    if (interp::logic_pass() && segs) {
        const bool wrapped = phase < from.phase;
        uint32_t reset_w[12];
        for (int k = 0; k < 12; k++) reset_w[k] = ld32(reset + 4 * k);
        auto unused = [&](const uint32_t* w) { return memcmp(w, reset_w, sizeof reset_w) == 0; };
        int blended = 0;
        for (int i = 0; i < 25; i++) {
            const int j = wrapped ? i - 1 : i;
            if (j < 0) continue;  // a new segment at the start of the arc
            const uint32_t a = segs + 0xA8 * i + 0x74;
            uint32_t cur[12];
            for (int k = 0; k < 12; k++) cur[k] = ld32(a + 4 * k);
            const uint32_t* prev = from.seg[j];
            if (unused(cur) || unused(prev) || !memcmp(cur, prev, sizeof cur)) continue;
            float d2 = 0;
            for (int k : {3, 7, 11}) d2 += (u32_as_f32(cur[k]) - u32_as_f32(prev[k])) * (u32_as_f32(cur[k]) - u32_as_f32(prev[k]));
            if (!(d2 < 1000.0f * 1000.0f)) continue;
            for (int k = 0; k < 12; k++)
                if (cur[k] != prev[k]) temp_blend_f32(a + 4 * k, prev[k], t);
            blended++;
        }
        static int tr = trace_left(7);
        if (tr && blended) {
            tr--;
            LOG("[interp-fx] cannon sight: %d segments blended, phase %.2f -> %.2f%s", blended, from.phase, phase, wrapped ? " (wrapped)" : "");
        }
    }
    f_0247255C_orig(c);
    stf32(scroll_a, scroll);
}

// ---------------------------------------------------------------------------------------------
// Sky back clouds (vrbox2): Execute (024D3248) sets the scroll speed (+0x3C8) from the wind, and Draw
// (024D1D44, every pass) adds it to the texture SRT translations of the cloud materials
// (scrollTexSrt, each followed by texScrollCheck 024D1C44, which wraps the value into 0..1) and then
// sets the speed to 0, so the logic pass's draw moves the clouds by the whole step and the other
// passes not at all. The translations the game computes stay exact; on the logic pass they are
// drawn between the record pass's values and the new ones (temp: put back at the next pass start,
// re-applied by the blended hold passes). One SRT is scrolled twice per draw (lambert122 uses the
// animation of lambert121): its second scroll starts from the exact value again.
namespace {
struct SkyTouch { uint32_t addr; bool blended; float exact; };
struct SkyDraw { bool active = false; float speed = 0; std::vector<SkyTouch> touched; };
SkyDraw g_sky;
struct SkyRec { uint64_t record = ~0ull; float value = 0; };
std::unordered_map<uint32_t, SkyRec> g_sky_rec;
}  // namespace
extern "C" void f_024D1D44_orig(Cpu*);
extern "C" void f_024D1C44_orig(Cpu*);
extern "C" void hook_024D1D44(Cpu* c) {
    if (!on(32) || true60::enabled() || interp::in_execute()) { f_024D1D44_orig(c); return; }
    g_sky.active = true;
    g_sky.speed = (float)ldf32(c->r[3] + 0x3C8);
    g_sky.touched.clear();
    f_024D1D44_orig(c);
    g_sky.active = false;
    // (40 fps blended record pass: drawn blended above, recorded here for the next step)
    if (interp::record_pass() || interp::record_blended()) {
        if (g_sky_rec.size() > 64) g_sky_rec.clear();
        for (const auto& t : g_sky.touched) g_sky_rec[t.addr] = SkyRec{interp::record_stamp(), t.exact};
    }
}
// texScrollCheck(f32* v): the wrap after each scroll
extern "C" void hook_024D1C44(Cpu* c) {
    if (!g_sky.active) { f_024D1C44_orig(c); return; }
    const uint32_t v = c->r[3];
    SkyTouch* e = nullptr;
    for (auto& t : g_sky.touched)
        if (t.addr == v) e = &t;
    // scrolled a second time in this draw: the game added the speed to the value drawn blended
    if (e && e->blended) stf32(v, e->exact + g_sky.speed);
    f_024D1C44_orig(c);
    if (!e) {
        g_sky.touched.push_back({v, false, 0.0f});
        e = &g_sky.touched.back();
    }
    e->blended = false;
    e->exact = (float)ldf32(v);
    if (!interp::logic_pass() || !halfway()) return;
    auto it = g_sky_rec.find(v);
    if (it == g_sky_rec.end() || it->second.record != interp::hold_pass_count()) return;
    const float cur = e->exact, prev = interp::fade::unwrap_near(it->second.value, cur, 1.0f);
    if (prev == cur || !(std::fabs(cur - prev) < 0.25f)) return;
    temp_blend_f32_again(v, f32_as_u32(prev), interp::pass_t());
    e->blended = true;
    static int tr = trace_left(8);
    if (tr) {
        tr--;
        LOG("[interp-fx] sky cloud scroll %08X t %.3f: %.5f -> %.5f, drawn %.5f", v, interp::pass_t(), prev, cur, (float)ldf32(v));
    }
}

// ---------------------------------------------------------------------------------------------
// Magma (Dragon Roost Cavern, Ganon's Tower; d_magma.cpp). MagmaPacket::calc (0258C7C8, world block:
// full passes only) scrolls the lava texture (l_kuroOrthoMtx [0][3] and [1][3], into magmaKuroMtx
// 104776E4: its [0][3] +0x0C and [1][3] +0x1C equal the scroll) and moves the lava balls up and down
// (dMagma_ball_c mPos +0xD50, y +0xD54). MagmaPacket::update (0258CA60, every drawn pass) builds the
// floors' and balls' render matrices from them (MagmaBallPath::update copies mPos.y into its
// texture-projection matrix). On blended passes update runs on the scroll and the ball heights
// between the record pass's and the step's values; the exact values are put back right after it.
// Packet: floors at +0x98 (8, 0xBE4 apart: balls +0xB30, count +0xB34, next +0xBDC, room +0xBE0).
namespace {
struct MagmaRec { uint64_t record = ~0ull; uint32_t pkt = 0; float scroll[2] = {0, 0}; std::unordered_map<uint32_t, std::array<float, 3>> balls; };
MagmaRec g_magma;
template <class F>
void magma_balls(uint32_t pk, F&& f) {
    for (int i = 0; i < 8; i++) {
        const uint32_t fl = pk + 0x98 + 0xBE4 * i, balls = ld32(fl + 0xB30);
        if (!balls || (!ld32(fl + 0xBDC) && !ld32(fl + 0xBE0))) continue;
        const uint32_t n = ld8(fl + 0xB34);
        for (uint32_t j = 0; j < n; j++)
            if (const uint32_t b = ld32(balls + 4 * j)) f(b);
    }
}
}  // namespace
extern "C" void f_0258CA60_orig(Cpu*);
extern "C" void hook_0258CA60(Cpu* c) {
    const uint32_t pk = c->r[3], kuro = GD(0x104776E4);
    if (!on(16) || true60::enabled()) { f_0258CA60_orig(c); return; }
    const float scroll[2] = {(float)ldf32(kuro + 0x0C), (float)ldf32(kuro + 0x1C)};
    if (interp::record_pass()) {
        g_magma.record = interp::hold_pass_count();
        g_magma.pkt = pk;
        g_magma.scroll[0] = scroll[0];
        g_magma.scroll[1] = scroll[1];
        g_magma.balls.clear();
        magma_balls(pk, [&](uint32_t b) { g_magma.balls[b] = {(float)ldf32(b + 0xD50), (float)ldf32(b + 0xD54), (float)ldf32(b + 0xD58)}; });
        f_0258CA60_orig(c);
        return;
    }
    // (40 fps blended record pass: drawn blended from the last record, then recorded)
    auto record_blended = [&] {
        if (!interp::record_blended()) return;
        g_magma.record = interp::record_stamp();
        g_magma.pkt = pk;
        g_magma.scroll[0] = scroll[0];
        g_magma.scroll[1] = scroll[1];
        g_magma.balls.clear();
        magma_balls(pk, [&](uint32_t b) { g_magma.balls[b] = {(float)ldf32(b + 0xD50), (float)ldf32(b + 0xD54), (float)ldf32(b + 0xD58)}; });
    };
    if (!halfway() || g_magma.pkt != pk || g_magma.record != interp::hold_pass_count()) { f_0258CA60_orig(c); record_blended(); return; }
    const float t = interp::pass_t();
    for (int k = 0; k < 2; k++) {
        const float prev = interp::fade::unwrap_near(g_magma.scroll[k], scroll[k], 1.0f);
        if (prev != scroll[k] && std::fabs(scroll[k] - prev) < 0.25f) stf32(kuro + (k ? 0x1C : 0x0C), lerp_f(prev, scroll[k], t));
    }
    std::vector<std::pair<uint32_t, float>> moved;
    magma_balls(pk, [&](uint32_t b) {
        auto it = g_magma.balls.find(b);
        if (it == g_magma.balls.end()) return;
        const float y = (float)ldf32(b + 0xD54);
        // (a ball placed anew, x/z changed: drawn where it is)
        if (it->second[1] == y || it->second[0] != (float)ldf32(b + 0xD50) || it->second[2] != (float)ldf32(b + 0xD58)) return;
        moved.push_back({b, y});
        stf32(b + 0xD54, lerp_f(it->second[1], y, t));
    });
    static int tr = trace_left(9);
    if (tr && (!moved.empty() || scroll[0] != g_magma.scroll[0])) {
        tr--;
        LOG("[interp-fx] magma t %.3f: scroll %.4f -> %.4f, drawn %.4f; %zu balls moving", t, g_magma.scroll[0], scroll[0], (float)ldf32(kuro + 0x0C), moved.size());
    }
    f_0258CA60_orig(c);
    for (const auto& m : moved) stf32(m.first + 0xD54, m.second);
    stf32(kuro + 0x0C, scroll[0]);
    stf32(kuro + 0x1C, scroll[1]);
    record_blended();
}

// ---------------------------------------------------------------------------------------------
// The swirl scene wipe (d_ovlp_fade2: the snapshot of the old scene turned and shrunk away, then
// the new one turned in). Its execute advances the angle (rotZ +0x114) and scale (+0x118) once per
// step; its draw (dOvlpFd2_Draw 025A1D10, every pass) builds the snapshot's matrix from them. Blended
// passes build it from the values between the record pass's and the step's.
namespace {
struct WipeRec { uint64_t record = ~0ull; uint32_t self = 0; int16_t rot = 0; float scale = 0; };
WipeRec g_wipe;
}  // namespace
extern "C" void f_025A1D10_orig(Cpu*);
extern "C" void hook_025A1D10(Cpu* c) {
    const uint32_t self = c->r[3];
    if (!on(32) || true60::enabled() || !self) { f_025A1D10_orig(c); return; }
    const int16_t rot = (int16_t)ld16(self + 0x114);
    const float scale = (float)ldf32(self + 0x118);
    if (interp::record_pass()) {
        g_wipe = WipeRec{interp::hold_pass_count(), self, rot, scale};
        f_025A1D10_orig(c);
        return;
    }
    const WipeRec from = g_wipe;
    if (interp::record_blended()) g_wipe = WipeRec{interp::record_stamp(), self, rot, scale};  // (40 fps: recorded, drawn blended from the last record)
    if (!halfway() || from.self != self || from.record != interp::hold_pass_count() || (from.rot == rot && from.scale == scale)) {
        f_025A1D10_orig(c);
        return;
    }
    const float t = interp::pass_t();
    st16(self + 0x114, (uint16_t)lerp_s16(from.rot, rot, t));
    stf32(self + 0x118, lerp_f(from.scale, scale, t));
    static int tr = trace_left(10);
    if (tr) {
        tr--;
        LOG("[interp-fx] swirl wipe t %.3f: angle %d -> %d, scale %.3f -> %.3f", t, from.rot, rot, from.scale, scale);
    }
    f_025A1D10_orig(c);
    st16(self + 0x114, (uint16_t)rot);
    stf32(self + 0x118, scale);
}

// ---------------------------------------------------------------------------------------------
// Eye pupils. Link (daPy_matAnm_c::calc 02413E80, from the material calculation of every drawn
// pass) writes the eye offset into the "texmtx1" texture SRT of his eye materials: the step's target
// (mNowOffset +0x74, eased by his execute) or, while m_morf_frame (101CEF1B) runs, a morph from the
// previous offset (mOldOffset +0x6C, set to the SRT after every call) 1/(n+1) of the way towards the
// SRT's value: per call, so the morph ran 2x/4x/8x per logic step. The call is gated (interp_fade.h
// floats_call): it advances once per logic step, and blended passes draw the SRT between the
// step's start and its new value. SRT: material (r4) +0 resource (+0x34 SRT table, +0x38 name
// table, relative), entry 0x14 bytes (+2 value offset) into +0x28 the SRT values (offsets +0x10/+0x14).
// Tetra/Zelda (daPz_matAnm_c 024556E8), Aryll (daNpc_Ls1 02281564) and Zelda (daNpc_Zl1 023086FC) copy
// their offset (+0x6C/+0x70, while +0x7C is set) into the SRT per call: blended passes copy the
// offset between the record pass's and the step's.
namespace {
// index of `name` in the material resource's name dictionary (NW resource dictionary: +0 size, +4
// entry count, +8 root node, then one 0x10-byte node per entry: +0 reference bit, +4 left/right,
// +8 name (offset from this field), +0xC data), or -1
int name_index(uint32_t dic, const char* name) {
    if (dic < 0x10000000 || dic >= 0x50000000) return -1;
    const uint32_t n = ld32(dic + 4);
    for (uint32_t i = 1; i <= n && i <= 256; i++) {
        const uint32_t field = dic + 8 + 0x10 * i + 8;
        const int32_t off = (int32_t)ld32(field);
        if (!off) continue;
        const uint32_t str = field + off;
        uint32_t k = 0;
        while (name[k] && ld8(str + k) == (uint8_t)name[k]) k++;
        if (!name[k] && !ld8(str + k)) return (int)i - 1;
    }
    return -1;
}
uint32_t link_eye_srt(uint32_t mat) {
    const uint32_t md = ld32(mat);
    if (md < 0x10000000 || md >= 0x50000000) return 0;
    const int32_t names = (int32_t)ld32(md + 0x38), table = (int32_t)ld32(md + 0x34);
    if (!names || !table) return 0;
    const int idx = name_index(md + 0x38 + names, "texmtx1");
    if (idx < 0) return 0;
    const uint32_t e = md + 0x34 + table + 0x14 * idx;
    return ld32(mat + 0x28) + ld16(e + 2);
}
struct EyeRec { uint32_t mat = 0, srt = 0; bool ok = false, wrote = false; float written[2] = {0, 0}; interp::fade::FloatsGate<4> gate; };
std::unordered_map<uint32_t, EyeRec> g_eyes;
struct EyeCopyRec { uint64_t record = ~0ull; float off[2] = {0, 0}; };
std::unordered_map<uint32_t, EyeCopyRec> g_eye_copies;
}  // namespace
extern "C" void f_02413E80_orig(Cpu*);
extern "C" void hook_02413E80(Cpu* c) {
    const uint32_t self = c->r[3], mat = c->r[4];
    if (!on(4) || true60::enabled()) { f_02413E80_orig(c); return; }
    if (g_eyes.size() > 16) g_eyes.clear();
    EyeRec& r = g_eyes[self];
    if (r.mat != mat) {
        r = EyeRec{};
        r.mat = mat;
        r.srt = link_eye_srt(mat);
        r.ok = r.srt != 0;
        static int found = trace_left(11);
        if (found) {
            found--;
            LOG("[interp-fx] Link eye %08X: material %08X, texmtx1 SRT %s %08X", self, mat, r.ok ? "at" : "not found", r.srt);
            const uint32_t md = ld32(mat);
            if (!r.ok && md >= 0x10000000 && md < 0x50000000) {
                const uint32_t nt = md + 0x38 + ld32(md + 0x38);
                LOG("[interp-fx]   resource %08X: +34 %08X +38 %08X; names %08X: %08X %08X %08X %08X %08X %08X", md, ld32(md + 0x34), ld32(md + 0x38), nt,
                    ld32(nt), ld32(nt + 4), ld32(nt + 8), ld32(nt + 12), ld32(nt + 16), ld32(nt + 20));
            }
        }
    }
    if (!r.ok) { f_02413E80_orig(c); return; }
    const uint32_t old = self + 0x6C, srt = r.srt + 0x10;
    float v[4] = {(float)ldf32(old), (float)ldf32(old + 4), (float)ldf32(srt), (float)ldf32(srt + 4)};
    if (r.wrote && v[2] == r.written[0] && v[3] == r.written[1]) {  // the offset drawn blended: the exact one back
        v[2] = r.gate.after[2];
        v[3] = r.gate.after[3];
    }
    r.wrote = false;
    bool valid = true;
    interp::fade::floats_call(r.gate, self, interp::logic_steps(), !interp::hold_pass(), interp::pass_t(), 0u, v, [&](float* x) {
        for (int i = 0; i < 2; i++) { stf32(old + 4 * i, x[i]); stf32(srt + 4 * i, x[2 + i]); }
        f_02413E80_orig(c);
        for (int i = 0; i < 2; i++) { x[i] = (float)ldf32(old + 4 * i); x[2 + i] = (float)ldf32(srt + 4 * i); }
        valid = ld32(old) == ld32(srt) && ld32(old + 4) == ld32(srt + 4);  // (the call ends with old = SRT)
    });
    for (int i = 0; i < 2; i++) { stf32(old + 4 * i, v[i]); stf32(srt + 4 * i, v[2 + i]); }
    if (!valid) {  // not the SRT the call wrote: left alone from now on
        r.ok = false;
        LOG("[interp-fx] Link's eye material %08X: texmtx1 SRT not found, eye offsets not blended", mat);
        return;
    }
    const float t = interp::pass_t();
    if (!halfway() || t >= 1.0f) return;
    // drawn between the offset at the step's start (the previous call's SRT, kept in mOldOffset) and the new one
    const float d[2] = {lerp_f(r.gate.before[0], r.gate.after[2], t), lerp_f(r.gate.before[1], r.gate.after[3], t)};
    if (d[0] == v[2] && d[1] == v[3]) return;
    stf32(srt, d[0]);
    stf32(srt + 4, d[1]);
    r.written[0] = (float)ldf32(srt);
    r.written[1] = (float)ldf32(srt + 4);
    r.wrote = true;
    static int tr = trace_left(11);
    if (tr) {
        tr--;
        LOG("[interp-fx] Link eye %08X t %.3f: %.4f %.4f -> %.4f %.4f, drawn %.4f %.4f", self, t, r.gate.before[0], r.gate.before[1], r.gate.after[2],
            r.gate.after[3], d[0], d[1]);
    }
}
static void eye_copy(Cpu* c, void (*orig)(Cpu*)) {
    const uint32_t self = c->r[3];
    if (!on(4) || true60::enabled()) { orig(c); return; }
    const float off[2] = {(float)ldf32(self + 0x6C), (float)ldf32(self + 0x70)};
    if (interp::record_pass()) {
        if (g_eye_copies.size() > 32) g_eye_copies.clear();
        g_eye_copies[self] = EyeCopyRec{interp::hold_pass_count(), {off[0], off[1]}};
        orig(c);
        return;
    }
    auto it = g_eye_copies.find(self);
    const EyeCopyRec from = it != g_eye_copies.end() ? it->second : EyeCopyRec{};
    if (interp::record_blended()) g_eye_copies[self] = EyeCopyRec{interp::record_stamp(), {off[0], off[1]}};  // (40 fps)
    if (!halfway() || !ld8(self + 0x7C) || from.record != interp::hold_pass_count() || (from.off[0] == off[0] && from.off[1] == off[1])) {
        orig(c);
        return;
    }
    const float t = interp::pass_t();
    stf32(self + 0x6C, lerp_f(from.off[0], off[0], t));
    stf32(self + 0x70, lerp_f(from.off[1], off[1], t));
    orig(c);
    stf32(self + 0x6C, off[0]);
    stf32(self + 0x70, off[1]);
}
extern "C" void f_024556E8_orig(Cpu*);
extern "C" void f_02281564_orig(Cpu*);
extern "C" void f_023086FC_orig(Cpu*);
extern "C" void hook_024556E8(Cpu* c) { eye_copy(c, f_024556E8_orig); }
extern "C" void hook_02281564(Cpu* c) { eye_copy(c, f_02281564_orig); }
extern "C" void hook_023086FC(Cpu* c) { eye_copy(c, f_023086FC_orig); }

// ---------------------------------------------------------------------------------------------
// Actor draws that advance their own state (fpcM_Draw, true60.cpp hook_025DF904, calls these around
// every process draw). At 30 fps an actor's Draw runs once per logic step; with frame interpolation
// it runs on every drawn pass, so state a Draw advances ran 2x/4x/8x per step. On the passes without
// logic (hold passes) these fields are put back after the draw, so they advance once per step, as at
// 30 fps (their execute does not read them; they are drawn as the step left them):
//   grappling hook (himo2, execute 0216E194): the coil and hand-rope smoothing (+0x21D8 5 floats,
//     +0x21EC the last hand position, +0x21F8 three angles, +0x2388)
//   King of Red Lions (daShip, 0247AE78): the HD sea shadow's fade (+0x630, chaseUC by 10 per draw)
//   Forest Firefly (nh, 021E1338): its light blend (+0x834, chaseF per draw)
//   Ganondorf (gnd, 02154EE0): the flash colour and fog (+0x1786 s16, +0x1788)
//   Helmaroc King (bdk, 02066A38): the tail-hide countdown (+0x40A, -1 per tail per draw)
//   Forsaken Fortress searchlights (obj_search, 0238DB44): the culling results its draw stores
//     (+0x8D6 the two beams, +0x8D8 the lamp; its execute reads the lamp's)
// "Replayed" draws (DrawKeepSpec::replay) also draw the hold passes from the values the step's logic
// pass drew with (its draw advanced them first): every pass of the step shows the 30 fps frame's flash.
//   Puppet Ganon (bgn, 0207E7B4; its draw daBgn_Draw 0207B2CC also draws the second and third forms,
//     bgn2/bgn3, found through the statics 10461A38/10461A3C): the arrow-hit fog flash of the body
//     (+0x4B4 s16, +0x4B8) and of each of the 75 parts (part_draw 0207ACD0: +0x27C s16, +0x280 of each
//     0x3F0 part from +0x4D0); bgn2 +0x3050/+0x3054; bgn3 +0x11E72/+0x11E74 and its 9 hair parts
//     (+0x1BE8, 0x19D8 each: +0x27C/+0x280)
//   Jabun's cave wall (obj_ajav, 02315A54): draw_flashing_normal (02313E0C) counts each part's flash
//     timer (+0x56 of the 6 parts of 0x90 from +0x9AC) up per draw and switches the part's draw proc
//     (+0x88, 8 bytes) back to the normal one at the end: 2x/4x/8x as fast
// Values the logic sets once per step and a draw reads (DrawKeepSpec::blend, f32) are drawn between
// the previous step's and this one's on blended passes:
//   Puppet Ganon's fog dim (the arena darkening between the forms: bgn +0x1575C mCC88, bgn2 +0x316C,
//     bgn3 +0x1213C, eased by their executes; a jump of more than 0.1 in one step is drawn as it is)
// Morth (ks, 021A7F94) is game logic: its execute calls draw_SUB (the wobble angle +0x576, the
// burning-enemy update enemy_fire of +0xCDC: fire timers, damage) and sets +0x549, and its Draw calls
// draw_SUB itself when +0x549 is clear, then clears it: once per step at 30 fps, but every later
// pass of the step ran it again. On hold passes the draw finds +0x549 set (draw_SUB is not run).
// debug: WWHD_INTERP_DRAW_KEEP=0 turns this off (WWHD_INTERP_FX bit 8 too); WWHD_INTERP_DRAW_REPLAY=0
// keeps the replayed draws to "put back" only; WWHD_INTERP_DRAW_BLEND=0 draws the blend fields exactly
namespace {
// a field of the process (global 0) or of the object a static pointer (global: its address) points to
struct DrawField { uint32_t global; uint32_t off; uint16_t len; };
struct DrawKeepSpec { uint32_t exec; std::vector<DrawField> keep; bool replay = false; std::vector<DrawField> blend = {}; };
std::vector<DrawField> fields(std::initializer_list<std::pair<uint32_t, uint16_t>> list, uint32_t global = 0) {
    std::vector<DrawField> v;
    for (const auto& f : list) v.push_back({global, f.first, f.second});
    return v;
}
// n structures of `stride` from `first`, each with the fields `list`
void add_parts(std::vector<DrawField>& v, uint32_t global, uint32_t first, uint32_t stride, uint32_t n,
               std::initializer_list<std::pair<uint32_t, uint16_t>> list) {
    for (uint32_t i = 0; i < n; i++)
        for (const auto& f : list) v.push_back({global, first + i * stride + f.first, f.second});
}
const std::vector<DrawKeepSpec>& draw_keep_specs() {
    static const std::vector<DrawKeepSpec> specs = [] {
        std::vector<DrawKeepSpec> s = {
            {GC(0x0216E194), fields({{0x21D8, 0x14}, {0x21EC, 0xC}, {0x21F8, 6}, {0x2388, 4}})},
            {GC(0x0247AE78), fields({{0x630, 1}})},
            {GC(0x021E1338), fields({{0x834, 4}})},
            {GC(0x02154EE0), fields({{0x1786, 2}, {0x1788, 4}})},
            {GC(0x021A7F94), fields({{0x576, 2}})},
            {GC(0x0238DB44), fields({{0x8D6, 3}})},
            {GC(0x02066A38), fields({{0x40A, 2}})},
        };
        // Puppet Ganon (bgn) and the forms it draws
        const uint32_t bgn2 = GD(0x10461A38), bgn3 = GD(0x10461A3C);
        DrawKeepSpec bgn{GC(0x0207E7B4), fields({{0x4B4, 2}, {0x4B8, 4}}), true};
        add_parts(bgn.keep, 0, 0x4D0, 0x3F0, 75, {{0x27C, 2}, {0x280, 4}});
        for (const auto& f : fields({{0x3050, 2}, {0x3054, 4}}, bgn2)) bgn.keep.push_back(f);
        for (const auto& f : fields({{0x11E72, 2}, {0x11E74, 4}}, bgn3)) bgn.keep.push_back(f);
        add_parts(bgn.keep, bgn3, 0x1BE8, 0x19D8, 9, {{0x27C, 2}, {0x280, 4}});
        bgn.blend = {{0, 0x1575C, 4}, {bgn2, 0x316C, 4}, {bgn3, 0x1213C, 4}};
        s.push_back(std::move(bgn));
        // Jabun's cave wall
        DrawKeepSpec ajav{GC(0x02315A54), {}, true};
        add_parts(ajav.keep, 0, 0x9AC, 0x90, 6, {{0x56, 2}, {0x88, 8}});
        s.push_back(std::move(ajav));
        return s;
    }();
    return specs;
}
uint32_t ks_execute() { static const uint32_t a = GC(0x021A7F94); return a; }
// the field's address (0: its object is not there)
uint32_t field_addr(uint32_t proc, const DrawField& f) {
    const uint32_t base = f.global ? ld32(f.global) : proc;
    if (base < 0x10000000 || base >= 0x50000000 || (base & 3)) return 0;
    return base + f.off;
}
struct DrawKept { uint32_t proc; const DrawKeepSpec* spec; std::vector<uint32_t> addrs; std::vector<uint8_t> bytes;
                  std::vector<std::pair<uint32_t, uint32_t>> blended; };
std::vector<DrawKept> g_draw_kept;
// replayed draws: what the step's logic pass drew with
struct DrawReplay { uint64_t step = ~0ull; std::vector<uint32_t> addrs; std::vector<uint8_t> bytes; };
std::unordered_map<uint32_t, DrawReplay> g_draw_replay;
// blend fields: the value of the step before and of this step
struct DrawBlendRec { uint64_t step = ~0ull; float before = 0, after = 0; };
std::unordered_map<uint32_t, DrawBlendRec> g_draw_blend;
const DrawKeepSpec* draw_keep_spec(uint32_t exec) {
    for (const auto& k : draw_keep_specs())
        if (exec && exec == k.exec) return &k;
    return nullptr;
}
std::vector<uint8_t> read_fields(const std::vector<uint32_t>& addrs, const DrawKeepSpec& spec) {
    std::vector<uint8_t> b;
    for (size_t j = 0; j < addrs.size(); j++)
        if (addrs[j])
            for (uint32_t i = 0; i < spec.keep[j].len; i++) b.push_back(ld8(addrs[j] + i));
    return b;
}
void write_fields(const std::vector<uint32_t>& addrs, const DrawKeepSpec& spec, const std::vector<uint8_t>& b) {
    size_t n = 0;
    for (size_t j = 0; j < addrs.size(); j++)
        if (addrs[j])
            for (uint32_t i = 0; i < spec.keep[j].len && n < b.size(); i++) st8(addrs[j] + i, b[n++]);
}
}  // namespace
namespace interp {
void fx_actor_draw(uint32_t proc, uint32_t exec, bool begin) {
    static const bool off = getenv("WWHD_INTERP_DRAW_KEEP") && !atoi(getenv("WWHD_INTERP_DRAW_KEEP"));
    static const bool replay_on = !(getenv("WWHD_INTERP_DRAW_REPLAY") && !atoi(getenv("WWHD_INTERP_DRAW_REPLAY")));
    static const bool blend_on = !(getenv("WWHD_INTERP_DRAW_BLEND") && !atoi(getenv("WWHD_INTERP_DRAW_BLEND")));
    if (begin) {
        const DrawKeepSpec* spec = off || !on(8) || true60::enabled() ? nullptr : draw_keep_spec(exec);
        DrawKept k{proc, spec, {}, {}, {}};
        if (!spec) { g_draw_kept.push_back(std::move(k)); return; }
        for (const auto& f : spec->keep) k.addrs.push_back(field_addr(proc, f));
        const uint64_t step = logic_steps();
        if (!hold_pass()) {  // the step's logic pass (or a 40 fps one): what its draw starts from
            if (spec->replay) {
                if (g_draw_replay.size() > 64) g_draw_replay.clear();
                g_draw_replay[proc] = {step, k.addrs, read_fields(k.addrs, *spec)};
            }
        } else {
            k.bytes = read_fields(k.addrs, *spec);  // (put back after the draw)
            auto it = g_draw_replay.find(proc);
            if (replay_on && spec->replay && it != g_draw_replay.end() && it->second.step == step && it->second.addrs == k.addrs &&
                it->second.bytes.size() == k.bytes.size()) {
                static int tr = trace_left(13);
                if (tr && it->second.bytes != k.bytes) {
                    tr--;
                    LOG("[interp-fx] process %08X (execute %08X): hold pass drawn from the logic pass's draw state (step %llu)", proc, exec,
                        (unsigned long long)step);
                }
                write_fields(k.addrs, *spec, it->second.bytes);
            }
            if (exec == ks_execute()) {
                static int tr = trace_left(13);
                if (tr && !ld8(proc + 0x549)) {
                    tr--;
                    LOG("[interp-fx] Morth %08X: draw_SUB kept to the step's logic on a hold pass", proc);
                }
                st8(proc + 0x549, 1);  // (draw_SUB ran in this step already)
            }
        }
        for (const auto& f : spec->blend) {
            const uint32_t a = field_addr(proc, f);
            if (!a) continue;
            const float v = (float)ldf32(a);
            DrawBlendRec& r = g_draw_blend[a];
            if (r.step != step) {
                r.before = r.step + 1 == step ? r.after : v;
                r.after = v;
                r.step = step;
            }
            const float t = pass_t();
            if (!blend_on || !halfway() || !(t < 1.0f) || r.after != v || r.before == v || !(std::fabs(v - r.before) <= 0.1f)) continue;
            k.blended.push_back({a, f32_as_u32(v)});
            stf32(a, lerp_f(r.before, v, t));
            static int tr = trace_left(13);
            if (tr) {
                tr--;
                LOG("[interp-fx] process %08X field %08X t %.3f: %.4f -> %.4f, drawn %.4f", proc, a, t, r.before, v, (float)ldf32(a));
            }
        }
        if (g_draw_blend.size() > 64) g_draw_blend.clear();
        g_draw_kept.push_back(std::move(k));
        return;
    }
    if (g_draw_kept.empty()) return;
    DrawKept k = std::move(g_draw_kept.back());
    g_draw_kept.pop_back();
    if (!k.spec || k.proc != proc) return;
    for (const auto& [a, v] : k.blended) st32(a, v);
    if (hold_pass()) write_fields(k.addrs, *k.spec, k.bytes);
}
}  // namespace interp

// ---------------------------------------------------------------------------------------------
// Chains of dChain_packet_c (the balance lifts' chains: daBalancelift_c fills the packet's points,
// +0xAC, +0xA8 of them, from its execute). The packet's draw (0251AE90, a J3DPacket: painted after
// the pass) builds the links from the exact points while the lift is drawn blended. Blended passes
// paint them between the record pass's points and the step's.
namespace {
struct ChainRec { uint64_t record = ~0ull; std::vector<float> p; };
std::unordered_map<uint32_t, ChainRec> g_chains;
}  // namespace
extern "C" void f_0251AE90_orig(Cpu*);
extern "C" void hook_0251AE90(Cpu* c) {
    const uint32_t pk = c->r[3];
    if (!on(16) || true60::enabled()) { f_0251AE90_orig(c); return; }
    const uint32_t n = ld32(pk + 0xA8), pt = ld32(pk + 0xAC);
    if (!n || n > 256 || pt < 0x10000000 || pt >= 0x50000000) { f_0251AE90_orig(c); return; }
    std::vector<float> cur(3 * n);
    for (uint32_t i = 0; i < 3 * n; i++) cur[i] = (float)ldf32(pt + 4 * i);
    if (interp::display_record()) {
        if (g_chains.size() > 32) g_chains.clear();
        g_chains[pk] = ChainRec{interp::hold_pass_count(), cur};
        f_0251AE90_orig(c);
        return;
    }
    const float t = interp::display_t();
    auto it = g_chains.find(pk);
    const ChainRec from = it != g_chains.end() ? it->second : ChainRec{};
    if (interp::display_record_blended()) g_chains[pk] = ChainRec{interp::display_record_stamp(), cur};  // (40 fps)
    if (t >= 1.0f || from.record != interp::hold_pass_count() || from.p.size() != cur.size() || from.p == cur) {
        f_0251AE90_orig(c);
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        float d2 = 0;
        for (int k = 0; k < 3; k++) d2 += (cur[3 * i + k] - from.p[3 * i + k]) * (cur[3 * i + k] - from.p[3 * i + k]);
        if (!(d2 < 500.0f * 500.0f)) { f_0251AE90_orig(c); return; }  // a jump: drawn as it is
    }
    for (uint32_t i = 0; i < 3 * n; i++) stf32(pt + 4 * i, lerp_f(from.p[i], cur[i], t));
    static int tr = trace_left(12);
    if (tr) {
        tr--;
        LOG("[interp-fx] chain %08X t %.3f: %u points, point 1 y %.2f -> %.2f", pk, t, n, from.p[n > 1 ? 4 : 1], cur[n > 1 ? 4 : 1]);
    }
    f_0251AE90_orig(c);
    for (uint32_t i = 0; i < 3 * n; i++) stf32(pt + 4 * i, cur[i]);
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
    g_anm_blended.clear();
    g_temp.clear();
    g_temp_keep.clear();
    g_wave.pkt = 0;
    g_wave_keep = 0;
    g_wave_pkt = 0;
    g_cloth_cur.clear();
    g_packet_poses.clear();
    g_line_layouts.clear();
    g_att.clear();
    g_trails.clear();
    g_hook = HookRec{};
    g_ship = ShipRec{};
    g_sky_rec.clear();
    g_magma = MagmaRec{};
    g_wipe = WipeRec{};
    g_eyes.clear();
    g_chains.clear();
    g_eye_copies.clear();
    for (auto& s : g_sway) s = SwayRec{};
    g_wood = WoodRec{};
    g_sea.packet = 0;
    g_sea.h.clear();
    g_sea_scroll_half = false;
}
}  // namespace interp

// ---------------------------------------------------------------------------------------------
// The frame blur (Swift Sail, camera shakes with blur, some boss attacks): the HD 3D layer's overlay
// pass (02732D3C, painted with every drawn frame) lays the previous frame's image over the new one
// with the alpha the game sets (101F4826, 0..255) through the blur matrix (1048CF84: a slight zoom or
// turn). It is a feedback effect: with N drawn frames per logic step the trail faded N times per
// step instead of once and the zoom compounded N times (shorter, stronger-zooming trails at 120 fps).
// Each frame uses the per-frame share of the step's blur: alpha (a/255)^(1/N) and the matrix 1/N of
// the way from the identity, N the drawn frames per step (4/3 at 40 fps).
// debug: WWHD_INTERP_BLUR=0 draws it as the game sets it (WWHD_INTERP_FX bit 32 too)
namespace interp { int output_fps(); }
extern "C" void f_02732D3C_orig(Cpu*);
extern "C" void hook_02732D3C(Cpu* c) {
    static const bool off = getenv("WWHD_INTERP_BLUR") && !atoi(getenv("WWHD_INTERP_BLUR"));
    const uint32_t enabled = GD(0x101F4825), alpha_a = GD(0x101F4826), mtx = GD(0x1048CF84);
    const float n = interp::enabled() && !true60::enabled() ? interp::output_fps() / 30.0f : 1.0f;
    if (off || !on(32) || !(n > 1.0f) || !ld8(enabled)) { f_02732D3C_orig(c); return; }
    const uint8_t alpha = ld8(alpha_a);
    uint32_t saved[12];
    for (int k = 0; k < 12; k++) saved[k] = ld32(mtx + 4 * k);
    const uint8_t a = (uint8_t)std::lround(255.0 * std::pow(alpha / 255.0, 1.0 / n));
    st8(alpha_a, a);
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 4; k++) {
            const float m = u32_as_f32(saved[4 * r + k]), id = (r == k) ? 1.0f : 0.0f;
            stf32(mtx + 4 * (4 * r + k), id + (m - id) / n);
        }
    static int tr = trace_left(15);
    if (tr) {
        tr--;
        LOG("[interp-fx] frame blur at %.2f frames per step: alpha %u -> %u, zoom %.4f -> %.4f", n, alpha, a, u32_as_f32(saved[0]), (float)ldf32(mtx));
    }
    f_02732D3C_orig(c);
    st8(alpha_a, alpha);
    for (int k = 0; k < 12; k++) st32(mtx + 4 * k, saved[k]);
}

// ---------------------------------------------------------------------------------------------
// Morph animations played from a draw (mDoExt_McaMorf::play 025E535C, mDoExt_McaMorf2::play 025E65FC;
// Valoo in the Gohma fight plays his in his draw): the frame update inside is already held on passes
// without logic (J3DFrameCtrl::update, fx_hold_anim), but the morph between two animations (mCurMorf
// chased towards 1 by mMorfStep) advanced on every drawn frame: 2x/4x/8x as fast. On those passes
// play does nothing and answers as the game's would (the animation stopped or its rate 0).
// debug: WWHD_INTERP_MORF_HOLD=0 lets them play (WWHD_INTERP_FX bit 8 too)
namespace {
void morf_play(Cpu* c, void (*orig)(Cpu*), uint32_t frame_ctrl) {
    static const bool off = getenv("WWHD_INTERP_MORF_HOLD") && !atoi(getenv("WWHD_INTERP_MORF_HOLD"));
    if (off || !interp::fx_hold_anim()) { orig(c); return; }
    const uint32_t o = c->r[3];
    const bool stopped = (ld8(o + frame_ctrl + 0xF) & 1) || ldf32(o + frame_ctrl) == 0.0;  // (mState bit 1, mRate)
    c->r[3] = stopped ? 1 : 0;
}
}  // namespace
extern "C" void f_025E535C_orig(Cpu*);
extern "C" void f_025E65FC_orig(Cpu*);
extern "C" void hook_025E535C(Cpu* c) { morf_play(c, f_025E535C_orig, 0x98); }
extern "C" void hook_025E65FC(Cpu* c) { morf_play(c, f_025E65FC_orig, 0xA4); }

// ---------------------------------------------------------------------------------------------
// Light beams (dDlst_mirrorPacket::update 0252DA64: the Mirror Shield's beam from Link's draw, Medli's
// harp beam from hers): the beam's quad is built on every draw from the matrix the draw passes (the
// shield's or harp's base matrix, exact), with collision line checks for where it ends, while the
// shield is drawn blended. Blended passes build it from the matrix between the record pass's and the
// step's (interp_world blend: rotation slerp, translation lerp); a jump (>400) is drawn as it is.
namespace {
struct BeamRec { uint64_t record = ~0ull; float m[12] = {}; };
std::unordered_map<uint32_t, BeamRec> g_beams;
}  // namespace
extern "C" void f_0252DA64_orig(Cpu*);
extern "C" void hook_0252DA64(Cpu* c) {
    const uint32_t pkt = c->r[3], mtx = c->r[4];
    if (!on(16) || true60::enabled() || mtx < 0x10000000 || mtx >= 0x50000000) { f_0252DA64_orig(c); return; }
    float cur[12];
    for (int k = 0; k < 12; k++) cur[k] = (float)ldf32(mtx + 4 * k);
    if (interp::record_pass()) {
        if (g_beams.size() > 16) g_beams.clear();
        BeamRec& r = g_beams[pkt];
        r.record = interp::hold_pass_count();
        memcpy(r.m, cur, sizeof cur);
        f_0252DA64_orig(c);
        return;
    }
    auto it = g_beams.find(pkt);
    const BeamRec from = it != g_beams.end() ? it->second : BeamRec{};
    if (interp::record_blended()) {  // (40 fps: recorded, drawn blended from the last record)
        BeamRec& r = g_beams[pkt];
        r.record = interp::record_stamp();
        memcpy(r.m, cur, sizeof cur);
    }
    const float t = interp::pass_t();
    bool same = memcmp(from.m, cur, sizeof cur) == 0;
    const float dx = cur[3] - from.m[3], dy = cur[7] - from.m[7], dz = cur[11] - from.m[11];
    if (!halfway() || t >= 1.0f || from.record != interp::hold_pass_count() || same || !(dx * dx + dy * dy + dz * dz < 400.0f * 400.0f)) {
        f_0252DA64_orig(c);
        return;
    }
    float m[12];
    interp::blend_world_matrix(from.m, cur, m, t);
    for (int k = 0; k < 12; k++) stf32(mtx + 4 * k, m[k]);
    static int tr = trace_left(12);
    if (tr) {
        tr--;
        LOG("[interp-fx] light beam %08X t %.3f: origin %.1f %.1f %.1f -> %.1f %.1f %.1f", pkt, t, from.m[3], from.m[7], from.m[11], cur[3], cur[7], cur[11]);
    }
    f_0252DA64_orig(c);
    for (int k = 0; k < 12; k++) stf32(mtx + 4 * k, cur[k]);
}

// ---------------------------------------------------------------------------------------------
// Sunken-treasure light columns (daSalvage_c::calcAlpha 02466578, from the actor's draw): fades the
// columns of the 160 salvage points in and out (cLib_chaseUC by 255/frames per call), counts down a
// delay and sets their draw modes on every draw: 2x/4x/8x as fast. On passes without logic it does
// not run: once per logic step, as at 30 fps. debug: WWHD_INTERP_FX bit 8
extern "C" void f_02466578_orig(Cpu*);
extern "C" void hook_02466578(Cpu* c) {
    if (interp::fx_hold_anim() && !true60::enabled()) return;
    f_02466578_orig(c);
}

// Forsaken Fortress searchlights (daObj_Search::Act_c::_draw 0238B258): the light spot on the ground
// is drawn from the beam's start and end (+0x740 / +0x758, two of each, set by the execute) while the
// lamp and beam models are drawn blended. Blended passes draw it between the record pass's and the
// step's ends.
namespace {
struct SearchRec { uint64_t record = ~0ull; float p[12] = {}; };
std::unordered_map<uint32_t, SearchRec> g_search;
}  // namespace
extern "C" void f_0238B258_orig(Cpu*);
extern "C" void hook_0238B258(Cpu* c) {
    const uint32_t a = c->r[3], base = a + 0x740;
    if (!on(16) || true60::enabled()) { f_0238B258_orig(c); return; }
    float cur[12];
    for (int k = 0; k < 12; k++) cur[k] = (float)ldf32(base + 4 * k);
    if (interp::record_pass()) {
        if (g_search.size() > 16) g_search.clear();
        SearchRec& r = g_search[a];
        r.record = interp::hold_pass_count();
        memcpy(r.p, cur, sizeof cur);
        f_0238B258_orig(c);
        return;
    }
    auto it = g_search.find(a);
    const SearchRec from = it != g_search.end() ? it->second : SearchRec{};
    if (interp::record_blended()) {  // (40 fps)
        SearchRec& r = g_search[a];
        r.record = interp::record_stamp();
        memcpy(r.p, cur, sizeof cur);
    }
    const float t = interp::pass_t();
    float d2 = 0;
    for (int k = 0; k < 12; k++) d2 = std::max(d2, std::fabs(cur[k] - from.p[k]));
    if (!halfway() || t >= 1.0f || from.record != interp::hold_pass_count() || d2 == 0.0f || !(d2 < 2000.0f)) {
        f_0238B258_orig(c);
        return;
    }
    for (int k = 0; k < 12; k++) stf32(base + 4 * k, lerp_f(from.p[k], cur[k], t));
    f_0238B258_orig(c);
    for (int k = 0; k < 12; k++) stf32(base + 4 * k, cur[k]);
}

// ---------------------------------------------------------------------------------------------
// Spilled water's mask (dWpotWater_c, a kankyo process: execute 025D39A8 bakes the mask's matrix as
// j3dSys's view at that moment x its placement into its alpha-volume packet, +0x198; draw 025D3968 only
// enters the packet): every pass painted it with the view of the pass before the step's logic while
// the scene is drawn with the blended camera. The placement is kept from the execute (the inverse view
// times the matrix) and the draw bakes it with the view of the pass it is drawn in (once the camera
// has drawn in it). The execute writes the packet's matrix again before anything reads it.
// debug: WWHD_INTERP_FX bit 16
namespace interp { bool view_drawn_this_pass(); }
namespace {
// 3x4 affine matrices (row-major, translation in column 3)
void mtx_concat(const float* a, const float* b, float* out) {
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 4; k++)
            out[4 * r + k] = a[4 * r] * b[k] + a[4 * r + 1] * b[4 + k] + a[4 * r + 2] * b[8 + k] + (k == 3 ? a[4 * r + 3] : 0.0f);
}
bool mtx_inverse(const float* m, float* out) {
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!(std::fabs(det) > 1e-12f)) return false;
    const float s = 1.0f / det;
    out[0] = (e * i - f * h) * s; out[1] = (c * h - b * i) * s; out[2] = (b * f - c * e) * s;
    out[4] = (f * g - d * i) * s; out[5] = (a * i - c * g) * s; out[6] = (c * d - a * f) * s;
    out[8] = (d * h - e * g) * s; out[9] = (b * g - a * h) * s; out[10] = (a * e - b * d) * s;
    for (int r = 0; r < 3; r++) out[4 * r + 3] = -(out[4 * r] * m[3] + out[4 * r + 1] * m[7] + out[4 * r + 2] * m[11]);
    return true;
}
struct WpotRec { uint64_t step = ~0ull; float place[12] = {}; };
std::unordered_map<uint32_t, WpotRec> g_wpot;
}  // namespace
extern "C" void f_025D39A8_orig(Cpu*);
extern "C" void hook_025D39A8(Cpu* c) {
    const uint32_t self = c->r[3];
    float view[12], inv[12], m[12];
    for (int k = 0; k < 12; k++) view[k] = (float)ldf32(GD(0x104B45F8) + 4 * k);
    f_025D39A8_orig(c);
    if (!interp::enabled() || true60::enabled()) return;
    if (g_wpot.size() > 32) g_wpot.clear();
    for (int k = 0; k < 12; k++) m[k] = (float)ldf32(self + 0x198 + 4 * k);
    if (!mtx_inverse(view, inv)) { g_wpot.erase(self); return; }
    WpotRec& r = g_wpot[self];
    r.step = interp::logic_steps();
    mtx_concat(inv, m, r.place);
}
extern "C" void f_025D3968_orig(Cpu*);
extern "C" void hook_025D3968(Cpu* c) {
    const uint32_t self = c->r[3];
    auto it = g_wpot.find(self);
    if (!on(16) || true60::enabled() || it == g_wpot.end() || it->second.step != interp::logic_steps() || !interp::view_drawn_this_pass()) {
        f_025D3968_orig(c);
        return;
    }
    float view[12], m[12];
    for (int k = 0; k < 12; k++) view[k] = (float)ldf32(GD(0x104B45F8) + 4 * k);
    mtx_concat(view, it->second.place, m);
    static int tr = trace_left(13);
    if (tr) {
        tr--;
        LOG("[interp-fx] spilled water %08X t %.3f: mask matrix z %.2f -> %.2f (view of this pass)", self, interp::pass_t(),
            (float)ldf32(self + 0x198 + 44), m[11]);
    }
    for (int k = 0; k < 12; k++) stf32(self + 0x198 + 4 * k, m[k]);
    f_025D3968_orig(c);
}

// ---------------------------------------------------------------------------------------------
// HD shadow cascades (hdLayerShadow drawShadowPass 02736C10, painted with every drawn frame): the
// split distances (surface +0x264 -> mCascadeCount +0x254 + 1 floats, set before updateShadowSurface
// 027987BC) and the cascade fade ranges (environment +0x1D4: near start, 1/(near end - start), far
// start, 1/(far end - start), before uploadShadowEnvironment 027FB678) come from the camera's exact eye
// and target (dCamera_c +0x258/+0x264, with a line check from the eye) once per step: the cascade
// edges moved in steps while the camera moves smoothly. Frames painted between two steps use the
// distances between the previous step's and this one's. The split distances are put back after the
// pass is painted: the culling cascade job (027C93A8) of the next pass reads them (a pass before a
// logic pass must leave the 30 fps values; the passes painted before such a job are exact or not
// blended here: the record pass, a 40 fps blended record pass). A change of more than half is
// drawn as it is (a camera cut, an event's ranges). debug: WWHD_INTERP_SHADOW_SPLIT=0
namespace interp { float display_t(); bool display_record(); bool display_record_blended(); }
namespace {
// (painted on the HD render threads, the TV's and the GamePad's layers each with their own surface:
// the records are per surface / environment, under a lock)
struct SplitRec { uint64_t step = ~0ull; std::vector<float> before, after; };
std::mutex g_split_lock;
std::unordered_map<uint32_t, SplitRec> g_split_dist, g_split_fade;
thread_local bool g_in_shadow_pass = false;
thread_local std::vector<std::pair<uint32_t, uint32_t>> g_split_put_back;
bool split_blend_on() {
    static const bool off = getenv("WWHD_INTERP_SHADOW_SPLIT") && !atoi(getenv("WWHD_INTERP_SHADOW_SPLIT"));
    return !off && on(16) && !true60::enabled() && g_in_shadow_pass;
}
// the step's exact values `cur` (recorded); true and `out` the drawn ones when this frame blends
bool split_blend(SplitRec& r, const std::vector<float>& cur, std::vector<float>& out, bool fade) {
    const uint64_t step = interp::logic_steps();
    if (r.step != step) {
        r.before = r.step + 1 == step ? r.after : std::vector<float>{};
        r.after = cur;
        r.step = step;
    }
    const float t = interp::display_t();
    if (!(t < 1.0f) || interp::display_record() || interp::display_record_blended() || r.after != cur || r.before.size() != cur.size() ||
        r.before == cur)
        return false;
    out.resize(cur.size());
    if (!fade) {
        for (size_t i = 0; i < cur.size(); i++) {
            const float a = r.before[i], b = cur[i];
            if (!std::isfinite(a) || !std::isfinite(b) || std::fabs(b - a) > 0.5f * std::max(std::fabs(a), std::fabs(b))) return false;
            out[i] = lerp_f(a, b, t);
        }
        return true;
    }
    for (int k = 0; k < 4; k += 2) {  // (start, 1/(end - start)) pairs: the ends blended
        const float sa = r.before[k], ia = r.before[k + 1], sb = cur[k], ib = cur[k + 1];
        if (!(ia > 0.0f) || !(ib > 0.0f) || !std::isfinite(sa) || !std::isfinite(sb)) {
            if (sa != sb || ia != ib) return false;
            out[k] = sb; out[k + 1] = ib;
            continue;
        }
        const float ea = sa + 1.0f / ia, eb = sb + 1.0f / ib;
        if (std::fabs(eb - ea) > 0.5f * std::max(std::fabs(ea), std::fabs(eb)) || std::fabs(sb - sa) > 0.5f * std::max(std::fabs(sa), std::fabs(sb)))
            return false;
        const float s = lerp_f(sa, sb, t), e = lerp_f(ea, eb, t);
        if (!(e > s)) return false;
        out[k] = s;
        out[k + 1] = 1.0f / (e - s);
    }
    return true;
}
}  // namespace
namespace interp_fx {
bool in_shadow_pass() { return g_in_shadow_pass; }  // fog_pin.cpp
}
extern "C" void f_02736C10_orig(Cpu*);
extern "C" void hook_02736C10(Cpu* c) {
    const bool saved = g_in_shadow_pass;
    g_in_shadow_pass = true;
    f_02736C10_orig(c);
    g_in_shadow_pass = saved;
    for (const auto& [a, v] : g_split_put_back) st32(a, v);
    g_split_put_back.clear();
}
extern "C" void f_027987BC_orig(Cpu*);
extern "C" void hook_027987BC(Cpu* c) {
    const uint32_t s = c->r[3], n = ld32(s + 0x254), d = ld32(s + 0x264);
    if (!split_blend_on() || n == 0 || n > 8 || d < 0x10000000 || d >= 0x50000000) { f_027987BC_orig(c); return; }
    std::vector<float> cur(n + 1), out, before;
    for (uint32_t i = 0; i <= n; i++) cur[i] = (float)ldf32(d + 4 * i);
    bool blend;
    {
        std::lock_guard<std::mutex> lock(g_split_lock);
        if (g_split_dist.size() > 8) g_split_dist.clear();
        SplitRec& r = g_split_dist[s];
        blend = split_blend(r, cur, out, false);
        before = r.before;
    }
    if (blend) {
        static std::atomic<int> tr{trace_left(14)};
        if (tr.load() > 0 && tr-- > 0)
            LOG("[interp-fx] shadow split %08X t %.3f: middle %.1f -> %.1f, drawn %.1f; far %.1f -> %.1f, drawn %.1f", s, interp::display_t(),
                before[std::min(1u, n)], cur[std::min(1u, n)], out[std::min(1u, n)], before[std::min(2u, n)], cur[std::min(2u, n)],
                out[std::min(2u, n)]);
        for (uint32_t i = 0; i <= n; i++) {
            g_split_put_back.push_back({d + 4 * i, ld32(d + 4 * i)});
            stf32(d + 4 * i, out[i]);
        }
    }
    f_027987BC_orig(c);
}
extern "C" void f_027FB678_orig(Cpu*);
extern "C" void hook_027FB678(Cpu* c) {
    const uint32_t e = c->r[3];
    if (!split_blend_on() || e < 0x10000000 || e >= 0x50000000) { f_027FB678_orig(c); return; }
    std::vector<float> cur(4), out;
    for (int i = 0; i < 4; i++) cur[i] = (float)ldf32(e + 0x1D4 + 4 * i);
    bool blend;
    {
        std::lock_guard<std::mutex> lock(g_split_lock);
        if (g_split_fade.size() > 8) g_split_fade.clear();
        blend = split_blend(g_split_fade[e], cur, out, true);
    }
    if (!blend) { f_027FB678_orig(c); return; }
    uint32_t exact[4];
    for (int i = 0; i < 4; i++) { exact[i] = ld32(e + 0x1D4 + 4 * i); stf32(e + 0x1D4 + 4 * i, out[i]); }
    f_027FB678_orig(c);
    for (int i = 0; i < 4; i++) st32(e + 0x1D4 + 4 * i, exact[i]);
}
