// True 60 fps: Link's (daPy_lk_c) own per-step code. See true60.cpp for the framework.
//
// Shared primitives (smoothing, chasing, animation frames, morf) are scaled in true60.cpp. This file
// handles the per-step code that is inline in Link's functions, through instruction-level hooks
// (site_ADDR runs before the instruction at ADDR; "@ADDR" lines in tools/recomp/hooks.txt) and
// function hooks. Everything here is a no-op unless Link's execute runs with dt < 1.
//
// WWHD addresses are from the generated code (build/gen) of the functions named in build/names.tsv;
// the GameCube source lines are in tww/src/d/actor/d_a_player_main.cpp.
#include <cmath>

#include "runtime.h"
#include "true60.h"
#include "mods/mods.h"

extern "C" {
void f_02416230_orig(Cpu* c);  // daPy_lk_c::setNormalSpeedF
void f_0207A9A0_orig(Cpu* c);  // cLib_calcTimer<u8>
void f_022ED850_orig(Cpu* c);  // cLib_calcTimer<u8> (second copy)
void f_02055B64_orig(Cpu* c);  // cLib_calcTimer<s16>
void f_0211D2F8_orig(Cpu* c);  // cLib_calcTimer<s32>
void f_025AAE08_orig(Cpu* c);  // cLib_calcTimer<s32> (second copy)
}

namespace {
constexpr uint32_t kSpeed = 0x33C;  // fopAc_ac_c::speed

bool link60() { return true60::dt() < 1.0f && true60::exec_proc() != 0 && true60::exec_proc() == true60::link(); }

uint32_t scratch_vec() {
    return mem::fixed_slot(mem::kFixLinkScratch);
}
}  // namespace

// ---- daPy_lk_c::posMoveFromFootPos (023FCB9C; GameCube d_a_player_main.cpp:2352) ----

// foot-driven speed: f1 = |toe movement since the last step| (absXZ, before `f31_2 = ...`). The
// animation advanced dt frames, so the movement is per dt; the game wants it per step.
extern "C" void site_023FCEB0(Cpu* c) {
    if (link60()) c->f[1].ps0 = (float)(c->f[1].ps0 / true60::dt());
}

// gravity: `speed.y += gravity * 2.25f` (heavy boots, fmadds at 023FD338) and `speed.y += gravity`
// (fadds at 023FD35C); f9 = gravity
extern "C" void site_023FD338(Cpu* c) {
    if (link60()) c->f[9].ps0 = (float)(c->f[9].ps0 * true60::dt());
}
extern "C" void site_023FD35C(Cpu* c) {
    if (link60()) c->f[9].ps0 = (float)(c->f[9].ps0 * true60::dt());
}

// `current.pos += speed` (PSVECAdd(pos, speed, pos) at 023FD39C): pos += speed * dt. Gravity changed
// speed.y by dv during this step (f31 = speed.y before); adding dv * (1-dt)/2 makes the half steps
// land exactly on the 30 Hz path (v += g; p += v), so jump arcs keep their height and length.
extern "C" void site_023FD39C(Cpu* c) {
    const bool half = link60();
    // The vector argument is &Link->speed in every mode (r28 is Link at this site).
    const float factor = mods::link_move_factor(c->r[4] - kSpeed);
    if (!half && factor == 1.f) return; // preserve the stock path and all FP bits when off
    const float dt = half ? true60::dt() : 1.f;
    uint32_t sp = c->r[4];  // &speed
    float vx = u32_as_f32(ld32(sp)), vy = u32_as_f32(ld32(sp + 4)), vz = u32_as_f32(ld32(sp + 8));
    float dv = half ? vy - (float)c->f[31].ps0 : 0.f;
    uint32_t v = scratch_vec();
    st32(v, f32_as_u32(vx * dt * factor));
    st32(v + 4, half ? f32_as_u32(vy * dt + dv * (1.0f - dt) * 0.5f) : ld32(sp + 4));
    st32(v + 8, f32_as_u32(vz * dt * factor));
    c->r[4] = v;
}

// ---- daPy_lk_c::setNormalSpeedF(f32 accel, f32 scale, f32 maxStep, f32 minStep) (02416230;
// GameCube :2300): `mNormalSpeed += accel` per step, or cLib_addCalc (scaled in true60.cpp) ----
extern "C" void hook_02416230(Cpu* c) {
    if (link60()) c->f[1].ps0 = (float)(c->f[1].ps0 * true60::dt());
    f_02416230_orig(c);
}

// ---- cLib_calcTimer<T> (`if (*t != 0) --*t; return *t;`), one copy per type and file. With
// dt < 1 the timer counts on full passes only, so a pair of half steps counts one step. ----
template <int kBytes>
static bool calc_timer_held(Cpu* c) {
    if (true60::dt() >= 1.0f || !true60::half_pass()) return false;
    uint32_t p = c->r[3];
    c->r[3] = kBytes == 1 ? ld8(p) : kBytes == 2 ? (uint32_t)(int32_t)(int16_t)ld16(p) : ld32(p);
    return true;
}
extern "C" void hook_0207A9A0(Cpu* c) { if (!calc_timer_held<1>(c)) f_0207A9A0_orig(c); }
extern "C" void hook_022ED850(Cpu* c) { if (!calc_timer_held<1>(c)) f_022ED850_orig(c); }
extern "C" void hook_02055B64(Cpu* c) { if (!calc_timer_held<2>(c)) f_02055B64_orig(c); }
extern "C" void hook_0211D2F8(Cpu* c) { if (!calc_timer_held<4>(c)) f_0211D2F8_orig(c); }
extern "C" void hook_025AAE08(Cpu* c) { if (!calc_timer_held<4>(c)) f_025AAE08_orig(c); }

// JAIZelAnime::setAnimSound (0201BF08) is left alone: it tracks the animation frame itself (each
// sound key plays once when the frame passes it), and its rate argument sets the pitch of
// speed-dependent sounds, which is per original step.

// ---- the procedure call in daPy_lk_c::execute (0240CDD0; GameCube d_a_player_main.cpp:11346,
// `(this->*mCurProcFunc)()`; WWHD: member pointer at this+0x65F4, bctrl at 0240D6D8 / 0240D6F8) ----
// With true 60, Link's full pass is the 30 fps game's step and his half pass a preview that the next
// full pass takes back (true60.cpp, link_preview_begin). The preview advances what is continuous:
// animation (frame + rate/2), movement (speed/2, gravity, root motion), collision correction, model
// and item matrices, so he is drawn and followed by the camera at 60 Hz. It decides nothing: the
// procedure and the decision functions before it are skipped (each decision is taken once, on the
// full pass, at the step of the 30 fps game).
// The procedure's per-step outputs that the rest of the execute consumes are reset before the call
// every step (execute 0240D2D0..0240D3A4); for the preview they are set again from the last full
// pass, as if the procedure had decided the same:
//   m34C2 (+0x68DE): root-motion mode for posMove (1 = move by the animation's root translation;
//     the cut procedures set it every step). One-shot modes become their steady follow-up as
//     posMove does (3 -> 1, 4/7 -> 5).
//   mResetFlg0 (+0x3C0) bit 0x2 (daPyRFlg0_UNK2, "sword attack active": the sword blur in
//     setCollision follows the blade). Not bit 0x1 (attack start: blur init, collider reset, the
//     spin attack's music cue) nor the footstep bits.
namespace {
constexpr uint32_t kCurProc = 0x65F0;  // daPy_lk_c::mCurProc (GameCube 0x31D8)
constexpr uint32_t kM34C2 = 0x68DE;    // u8 (GameCube 0x34C2); execute 0240D370
constexpr uint32_t kResetFlg0 = 0x3C0;  // u32 mResetFlg0 (cleared at 0240D300)
constexpr uint32_t kReplayFlg0 = 0x2;
uint32_t g_noop = 0;  // guest address of a stand-in that does nothing
void noop_proc(Cpu* c) { c->r[3] = 1; }
struct ProcOut { uint64_t pass = ~0ull; uint32_t link = 0, flg0_pre = 0, flg0_set = 0; uint8_t m34c2 = 0; } g_out;
bool t_full_call = false;
void proc_call(Cpu* c) {
    uint32_t l = c->r[31];  // this (r3 = this + member delta)
    if (!true60::enabled() || l != true60::link()) return;
    if (link60()) {  // preview
        if (!g_noop) g_noop = dispatch::register_host(noop_proc, "true60_noop_proc");
        c->ctr = g_noop;
        if (g_out.link == l && g_out.pass + 1 == true60::pass()) {
            uint8_t m = g_out.m34c2;
            m = m == 3 ? 1 : (m == 4 || m == 7) ? 5 : m;
            if (ld8(l + kM34C2) == 0 && m) st8(l + kM34C2, m);
            st32(l + kResetFlg0, ld32(l + kResetFlg0) | (g_out.flg0_set & kReplayFlg0));
        }
        return;
    }
    if (true60::half_pass()) return;
    t_full_call = true;  // full pass: remember what the procedure decides (for the next preview)
    g_out.link = l;
    g_out.pass = ~0ull;
    g_out.flg0_pre = ld32(l + kResetFlg0);
}
}  // namespace
extern "C" void site_0240D6D8(Cpu* c) { proc_call(c); }
extern "C" void site_0240D6F8(Cpu* c) { proc_call(c); }
extern "C" void site_0240D6FC(Cpu* c) {
    if (!t_full_call) return;
    t_full_call = false;
    uint32_t l = c->r[31];
    if (l != g_out.link) return;
    g_out.pass = true60::pass();
    g_out.flg0_set = ld32(l + kResetFlg0) & ~g_out.flg0_pre;
    g_out.m34c2 = ld8(l + kM34C2);
}

// daPy_lk_c::checkItemAction (023FB230; GameCube d_a_player_main.cpp, called from execute right
// after changeBoomerangCatchProc, as in WWHD 0240D5CC): item buttons and the upper-body item
// animations (taking out / putting away the sword and items, their ends by frame thresholds, the
// face texture reset with its random blink timer). Buttons only change on full passes, and the
// frame thresholds must be met at the same step as at 30 fps, so it runs on full passes only.
extern "C" void f_023FB230_orig(Cpu* c);
extern "C" void hook_023FB230(Cpu* c) {
    if (link60() && true60::half_pass()) return;
    f_023FB230_orig(c);
}

// daPy_lk_c::playTextureAnime (023FBCEC; called right after the procedure, execute 0240D78C as in
// GameCube execute): eye/mouth texture animations, one frame per step, with random blink timers.
// Full passes only (at 60 Hz they ran twice as fast and drew random numbers half a step early).
extern "C" void f_023FBCEC_orig(Cpu* c);
extern "C" void hook_023FBCEC(Cpu* c) {
    if (link60() && true60::half_pass()) return;
    f_023FBCEC_orig(c);
}

// ---- fences for previews (half-pass executes of 60 Hz processes, taken back by the next full pass) ----
// Decisions of Link's execute (GameCube execute order: changeDemoProc, changeDeadProc,
// changeAutoJumpProc, changeSwimProc, changeDamageProc, changeBoomerangCatchProc, checkItemAction,
// the procedure): skipped in his preview, they run on the full pass. setGetDemo (item-get event at
// the top of execute), startRestartRoom (void-out), deleteEquipItem (deletes a held actor) likewise.
// Lasting effects outside the previewed process are not made at all in any preview: process
// creation (fpcSCtRq_Request, fpcM_FastCreate), particle emitters (JPAEmitterManager::
// createSimpleEmitterID), colliders entering the collision check (cCcS::Set: so a hit found on the
// full pass stays visible to the next full step, as at 30 fps), events and fades, save-data writes.
#define T60_FENCE_LINK(addr, ret)                                   \
    extern "C" void f_##addr##_orig(Cpu* c);                        \
    extern "C" void hook_##addr(Cpu* c) {                           \
        if (link60() && true60::half_pass()) { ret; return; }       \
        f_##addr##_orig(c);                                         \
    }
#define T60_FENCE_ANY(addr, ret)                                    \
    extern "C" void f_##addr##_orig(Cpu* c);                        \
    extern "C" void hook_##addr(Cpu* c) {                           \
        if (true60::enabled() && true60::preview()) { ret; return; }\
        f_##addr##_orig(c);                                         \
    }
T60_FENCE_LINK(023F695C, c->r[3] = 0)  // daPy_lk_c::changeDemoProc
T60_FENCE_LINK(023F7820, c->r[3] = 0)  // daPy_lk_c::changeDeadProc
T60_FENCE_LINK(023F81A4, c->r[3] = 0)  // daPy_lk_c::changeAutoJumpProc
T60_FENCE_LINK(023F8F80, c->r[3] = 0)  // daPy_lk_c::changeSwimProc
T60_FENCE_LINK(023FA578, c->r[3] = 0)  // daPy_lk_c::changeDamageProc
T60_FENCE_LINK(023FB020, c->r[3] = 0)  // daPy_lk_c::changeBoomerangCatchProc
T60_FENCE_LINK(023DBDD0, c->r[3] = 0)  // daPy_lk_c::setGetDemo
T60_FENCE_LINK(023FD4E4, (void)0)      // daPy_lk_c::startRestartRoom
T60_FENCE_LINK(023DC7AC, (void)0)      // daPy_lk_c::deleteEquipItem
T60_FENCE_ANY(025E14A8, c->r[3] = 0xFFFFFFFFu)  // fpcSCtRq_Request: no process created
T60_FENCE_ANY(025DFAB8, c->r[3] = 0)            // fpcM_FastCreate
T60_FENCE_ANY(02821448, c->r[3] = 0)            // JPAEmitterManager::createSimpleEmitterID
T60_FENCE_ANY(0200E240, (void)0)                // cCcS::Set
T60_FENCE_ANY(0253EC0C, c->r[3] = 0)            // dEvt_control_c::order
T60_FENCE_ANY(0253ED80, c->r[3] = 0)            // dEvt_control_c::orderOld
T60_FENCE_ANY(025F0658, (void)0)                // mDoGph_gInf_c::fadeOut
T60_FENCE_ANY(025B8AF4, (void)0)                // dSv_event_c::setEventReg
T60_FENCE_ANY(025B51DC, (void)0)                // dSv_player_item_c::setBottleItemIn
