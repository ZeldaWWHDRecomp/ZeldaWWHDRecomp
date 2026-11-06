// Runtime hooks on the game's process loop and shared game functions (tools/recomp/hooks.txt).
//
// Most of these were written for True 60 (game logic at 60 steps per second), which was retired.
// The hooks.txt entries stay so the generated game code is unchanged; this file keeps what the frame
// interpolation, the mods and the test aids still use, and runs the game's own code everywhere else
// (the pass-throughs at the end).
#include <cstdlib>
#include <cstring>

#include "game_hooks.h"
#include "guest_addr.h"
#include "runtime.h"
#include "mods/mods.h"
#include "mods/native_actor_profiles.h"

namespace interp {
bool enabled();          // frame interpolation is on
bool hold_pass();        // this is an in-between (hold) pass: no game logic
uint64_t logic_steps();  // logic steps so far
bool zoom_ease(Cpu* c, void (*orig)(Cpu*));
void fx_actor_draw(uint32_t proc, uint32_t exec, bool begin);
bool fx_hold_anim();
}
namespace test_aids {  // test_aids.cpp (debug dumps)
void after_execute(uint32_t proc, uint32_t fn, bool is_link);
void before_execute_link(uint32_t proc);
bool dumping();
bool digesting();  // WWHD_PROC_DIGEST (comparisons of two builds)
void proc_digest(uint32_t proc);
}

namespace {
const uint32_t kRndSeeds = GD(0x101FF9D4);       // cM_rnd's three seeds
constexpr uint32_t kSubMethod = 0xF0;             // fopAc_ac_c::sub_method (GameCube 0xEC)
const uint32_t kDaPyExecute = GC(0x0240EBB0);     // daPy_Execute (tail-calls daPy_lk_c::execute 0240CDD0)
constexpr uint32_t kCurProc = 0x65F0;             // daPy_lk_c::mCurProc (GameCube 0x31D8)
constexpr uint32_t kPos = 0x314, kSpeed = 0x33C, kSpeedF = 0x370;
constexpr uint32_t kShapeAngle = 0x328, kAngle = 0x320;
constexpr uint32_t kFrameCtrlUnder = 0x5898;      // daPy_lk_c::mFrameCtrlUnder[0] (J3DFrameCtrl: rate +0, frame +4)
constexpr uint32_t kNormalSpeed = 0x6A14;         // daPy_lk_c::mNormalSpeed
constexpr uint32_t kCamMtd = 0x228;               // camera_class::mpMtd (GameCube 0x224)
const uint32_t kCameraExecute = GC(0x024FFA3C);   // camera_execute
constexpr uint32_t kDCamera = 0x248;              // camera_process_class::mCamera

uint64_t g_pass = 0;        // per-frame passes (interp.cpp calls new_pass)
uint32_t g_link = 0;        // Link's process (daPy_lk_c), once seen
uint64_t g_link_steps = 0;  // Link's executes since the last save-state load (test clock)
bool g_state_loaded = false;
bool g_follow_called = false;  // followCamera ran during the camera's execute (camera trace)

uint32_t actor_execute_fn(uint32_t proc) {
    uint32_t sub = ld32(proc + kSubMethod);
    if (sub < 0x10000000 || sub >= 0x50000000)
        return native_actor_profiles::guest_execute_method(ld16(proc+0xE),sub);
    return ld32(sub + 8);
}
bool is_camera(uint32_t proc) {
    uint32_t m = ld32(proc + kCamMtd);
    return m >= 0x10000000 && m < 0x50000000 && ld32(m + 8) == kCameraExecute;
}

// WWHD_LINK_TRACE=path: one line per Link execute: pass, logic step, time, full pass (always 1 now),
// dt (always 1), procedure, pos, speedF, speed, angles, animation frame and rate, normal speed.
// The mods' runtime tests read this format.
FILE* g_trace = [] {
    const char* p = getenv("WWHD_LINK_TRACE");
    return p ? fopen(p, "w") : nullptr;
}();
void trace_link(uint32_t proc) {
    if (!g_trace) return;
    double t = (double)timebase::now() / timebase::kTicksPerSec;  // guest time (the test origin is logged as [test])
    fprintf(g_trace, "%llu %llu %.4f %d %.2f %d %.3f %.3f %.3f %.4f %.4f %.4f %.4f %d %d %.3f %.3f %.3f\n", (unsigned long long)g_pass,
            (unsigned long long)interp::logic_steps(), t, 1, 1.0f, (int)ld32(proc + kCurProc), ldf32(proc + kPos), ldf32(proc + kPos + 4),
            ldf32(proc + kPos + 8), ldf32(proc + kSpeedF), ldf32(proc + kSpeed), ldf32(proc + kSpeed + 4), ldf32(proc + kSpeed + 8),
            (int)(int16_t)ld16(proc + kShapeAngle + 2), (int)(int16_t)ld16(proc + kAngle + 2), ldf32(proc + kFrameCtrlUnder + 4),
            ldf32(proc + kFrameCtrlUnder), ldf32(proc + kNormalSpeed));
    static int n = 0;
    if (++n % 60 == 0) fflush(g_trace);
}
// WWHD_CAM_TRACE=path: one line per camera execute: pass, logic step, full pass (1), dt (1), follow
// camera used, eye xyz, center xyz, fovy; then a line of counters and follow-camera work words
FILE* g_cam_trace = [] {
    const char* p = getenv("WWHD_CAM_TRACE");
    return p ? fopen(p, "w") : nullptr;
}();
void trace_camera(uint32_t proc) {
    if (!g_cam_trace) return;
    fprintf(g_cam_trace, "%llu %llu %d %.2f %d %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", (unsigned long long)g_pass,
            (unsigned long long)interp::logic_steps(), 1, 1.0f, g_follow_called ? 1 : 0, ldf32(proc + 0xDC), ldf32(proc + 0xE0),
            ldf32(proc + 0xE4), ldf32(proc + 0xE8), ldf32(proc + 0xEC), ldf32(proc + 0xF0), ldf32(proc + 0xD4));
    uint32_t d = proc + kDCamera;
    fprintf(g_cam_trace, "    m100 %d m108 %d work", (int)ld32(d + 0x100), (int)ld32(d + 0x108));
    for (uint32_t o = 0x36C; o <= 0x388; o += 4) fprintf(g_cam_trace, " %08X", ld32(d + o));
    fprintf(g_cam_trace, " type %u mode %u style %u V %d", ld32(d + 0x51C), ld32(d + 0x13C), ld32(d + 0x514), (int16_t)ld16(d + 0x40));
    fprintf(g_cam_trace, "\n");
    static int n = 0;
    if (++n % 60 == 0) fflush(g_cam_trace);
}

// debug: WWHD_WATCH_LINK=off (or WWHD_WATCH_ADDR=addr) logs which process execute/draw changes the
// word at Link+off
uint32_t watch_addr() {
    static const long abs_a = getenv("WWHD_WATCH_ADDR") ? strtol(getenv("WWHD_WATCH_ADDR"), nullptr, 16) : -1;
    if (abs_a >= 0) return (uint32_t)abs_a;
    static const long off = getenv("WWHD_WATCH_LINK") ? strtol(getenv("WWHD_WATCH_LINK"), nullptr, 16) : -1;
    return off >= 0 && g_link ? g_link + (uint32_t)off : 0;
}
void watch_link(const char* what, uint32_t proc, uint32_t before) {
    uint32_t wa = watch_addr();
    if (!wa) return;
    uint32_t v = ld32(wa);
    if (v != before)
        LOG("[watch] pass %llu step %llu %s %s %08X (exec fn %08X): %08X -> %08X", (unsigned long long)g_pass,
            (unsigned long long)interp::logic_steps(), interp::hold_pass() ? "half" : "full", what, proc, actor_execute_fn(proc), before, v);
}
uint32_t watch_val() {
    uint32_t wa = watch_addr();
    return wa ? ld32(wa) : 0;
}
// debug: WWHD_RND_EXEC=path logs each process execute that draws random numbers
FILE* g_rx = getenv("WWHD_RND_EXEC") ? fopen(getenv("WWHD_RND_EXEC"), "w") : nullptr;
void rx_log(uint32_t proc, uint32_t s0) {
    if (g_rx && ld32(kRndSeeds) != s0)
        fprintf(g_rx, "%llu %d %08X %08X %08X\n", (unsigned long long)interp::logic_steps(), (int)interp::hold_pass(), proc, actor_execute_fn(proc), s0);
}
}  // namespace

namespace game_hooks {
void new_pass() { g_pass++; }
uint64_t pass() { return g_pass; }
uint32_t link() { return g_link; }
uint64_t link_steps() { return g_link_steps; }
bool state_loaded() { return g_state_loaded; }
void ss_reset() {
    g_link_steps = 0;
    g_state_loaded = true;
    g_link = 0;
    g_follow_called = false;
}
}  // namespace game_hooks

extern "C" {
void f_025DF940_orig(Cpu* c);  // fpcM_Execute
void f_025DF904_orig(Cpu* c);  // fpcM_Draw
void f_0200ECD4_orig(Cpu* c);  // cLib_addCalc
void f_027F2FC4_orig(Cpu* c);  // J3DFrameCtrl::update
void f_027F2BF8_orig(Cpu* c);  // J3DFrameCtrl::checkPass
void f_025028B8_orig(Cpu* c);  // dCamera_c::followCamera
void f_025CB6D4_orig(Cpu* c);  // dVibration_c::Pause
void f_02593B10_orig(Cpu* c);  // d_meter per-step update
}

// fpcM_Execute(process): Link's step clock for the test scenarios, traces and test dumps
// (was True 60's per-process gate)
extern "C" void hook_025DF940(Cpu* c) {
    uint32_t proc = c->r[3];
    mods::DoorExecuteWrap door_wrap{proc};  // Quick doors' walk-through mode (mods/walk_doors.cpp)
    uint32_t rx_s0 = ld32(kRndSeeds);
    uint32_t wv = watch_val();
    const uint32_t fn = actor_execute_fn(proc);
    const bool is_link = fn == kDaPyExecute;
    const bool is_cam = g_cam_trace && is_camera(proc);
    if (is_cam) g_follow_called = false;
    const uint32_t dump_fn = test_aids::dumping() ? fn : 0;
    if (is_link) {
        g_link = proc;
        if (dump_fn) test_aids::before_execute_link(proc);
    }
    f_025DF940_orig(c);
    if (test_aids::digesting()) test_aids::proc_digest(proc);
    if (dump_fn) test_aids::after_execute(proc, dump_fn, is_link);
    if (is_cam) trace_camera(proc);
    if (is_link) {
        trace_link(proc);
        g_link_steps++;
    }
    rx_log(proc, rx_s0);
    watch_link("execute", proc, wv);
}

// fpcM_Draw(process): frame interpolation's actor draws that advance their own state, once per
// logic step (interp_fx.cpp: replayed draws and fields blended between the steps)
extern "C" void hook_025DF904(Cpu* c) {
    uint32_t proc = c->r[3];
    uint32_t wv = watch_val();
    const uint32_t keep_exec = interp::enabled() ? actor_execute_fn(proc) : 0;
    if (keep_exec) interp::fx_actor_draw(proc, keep_exec, true);
    f_025DF904_orig(c);
    if (keep_exec) interp::fx_actor_draw(proc, keep_exec, false);
    watch_link("draw", proc, wv);
}

// cLib_addCalc: camera_draw's ease of the HD projection zoom, once per logic step with frame
// interpolation (interp.cpp)
extern "C" void hook_0200ECD4(Cpu* c) {
    static const uint32_t zoom_call = GC(0x0250018C);
    if (c->lr == zoom_call && interp::zoom_ease(c, f_0200ECD4_orig)) return;
    f_0200ECD4_orig(c);
}

// J3DFrameCtrl::update: held back on in-between passes (animations played from a draw), and the
// quick-use mod's animation speed-up (mods/quick_use.cpp)
extern "C" void hook_027F2FC4(Cpu* c) {
    if (interp::fx_hold_anim()) return;
    mods::quick_use_animation_call(c, f_027F2FC4_orig);
}
// J3DFrameCtrl::checkPass: the quick-use mod's key frames (was True 60's one-step window)
extern "C" void hook_027F2BF8(Cpu* c) { mods::quick_use_animation_call(c, f_027F2BF8_orig, false); }

// dCamera_c::followCamera: marks the camera's execute as using the follow camera (camera trace)
extern "C" void hook_025028B8(Cpu* c) {
    g_follow_called = true;
    f_025028B8_orig(c);
}

// The play scene's draw (dScnPly_Draw 025AF8A0) also steps world systems, inside
// `if (!dMenu_flag() && pauseTimer == 0) { ... }`: vibration, ice, magma, grass/tree/wood/flower
// (sway and reactions), poison light, moving collision (dBgS::Move), snapshots, particles,
// cCt_execCounter (g_Counter.mTimer++). With frame interpolation the draw runs on every pass, so
// on in-between passes the condition is made false (site before the `cmpwi r3, 0` that tests the
// pause check's result, 025B00B0), and the else branch's dVibration_c::Pause is skipped.
// debug: WWHD_WORLD_EVERY_PASS=1 restores the old behaviour.
static bool hold_world() {
    static const bool every = getenv("WWHD_WORLD_EVERY_PASS") != nullptr;
    return interp::enabled() && interp::hold_pass() && !every;
}
extern "C" void site_025B00B0(Cpu* c) {
    if (hold_world()) c->r[3] = 1;
}
extern "C" void hook_025CB6D4(Cpu* c) {
    if (c->lr == GC(0x025B01F0u) && hold_world()) return;
    f_025CB6D4_orig(c);
}

// The HUD (d_meter) updates its state from the play scene's draw (02593B10: button labels, gauges,
// the magic gauge sparkle with its random numbers). It runs on logic passes only (in-between passes
// still draw it), so HUD timing and its random numbers are those of the 30 fps game; the Pirate's
// Charm ring while Tetra calls starts from here every 20 updates (issue #262).
extern "C" void hook_02593B10(Cpu* c) {
    static const bool every = getenv("WWHD_HUD_EVERY_PASS") != nullptr;  // debug: old behaviour
    if (interp::hold_pass() && !every) return;
    f_02593B10_orig(c);
}

// (site_023FD39C and hook_02416230, the movement mods' Link hooks: mods/link_move_hooks.cpp)

// ---- retired True 60 hooks: the game's own code (kept so the generated code stays unchanged) ----
#define PASS(addr)                       \
    extern "C" void f_##addr##_orig(Cpu*); \
    extern "C" void hook_##addr(Cpu* c) { f_##addr##_orig(c); }
#define SITE(addr) extern "C" void site_##addr(Cpu*) {}
// shared primitives that True 60 scaled by the step length
PASS(0200ED84) PASS(0200EDC8) PASS(0200EE00) PASS(0200EF78) PASS(0200F164) PASS(0200F268)  // cLib_addCalc*
PASS(0200F378) PASS(0200F428) PASS(0200F474)                                               // cLib_addCalcAngle*
PASS(0200F4FC) PASS(0200F564) PASS(0200F5C8) PASS(0200F62C) PASS(0200F764) PASS(0200F8D0)  // cLib_chase*
PASS(025E3EC8)  // mDoExt_MtxCalcOldFrame::decOldFrameMorfCounter
PASS(025D67A8)  // fopAcM_calcSpeed
PASS(025D6800)  // fopAcM_posMove
PASS(0207A9A0) PASS(022ED850) PASS(02055B64) PASS(0211D2F8) PASS(025AAE08)  // cLib_calcTimer copies
PASS(024EF968)  // dBgS::MoveBgCrrPos
PASS(02018D40)  // cM3dGSph::SetC
// Link: per-step code, full-pass-only parts and half-pass preview fences
SITE(023FCEB0) SITE(023FD338) SITE(023FD35C)                 // posMoveFromFootPos: foot movement, gravity
SITE(0240D6D8) SITE(0240D6F8) SITE(0240D6FC)                 // daPy_lk_c::execute: procedure call
PASS(023FB230)  // daPy_lk_c::checkItemAction
PASS(023FBCEC)  // daPy_lk_c::playTextureAnime
PASS(023F22FC)  // daPy_lk_c::setDemoData
PASS(023F695C) PASS(023F7820) PASS(023F81A4) PASS(023F8F80) PASS(023FA578) PASS(023FB020)  // daPy_lk_c::change*Proc
PASS(023DBDD0)  // daPy_lk_c::setGetDemo
PASS(023FD4E4)  // daPy_lk_c::startRestartRoom
PASS(023DC7AC)  // daPy_lk_c::deleteEquipItem
PASS(025E14A8)  // fpcSCtRq_Request
PASS(025DFAB8)  // fpcM_FastCreate
PASS(02821448)  // JPAEmitterManager::createSimpleEmitterID
PASS(0200E240)  // cCcS::Set
PASS(0253EC0C) PASS(0253ED80)  // dEvt_control_c::order, orderOld
PASS(025F0658)  // mDoGph_gInf_c::fadeOut
PASS(025B8AF4)  // dSv_event_c::setEventReg
PASS(025B51DC)  // dSv_player_item_c::setBottleItemIn
// follow camera smoothing ratios (hooks.txt "true60 sites")
SITE(024FE89C) SITE(024FEB00) SITE(02502CCC) SITE(02502CD0) SITE(02502E44) SITE(02502E48)
SITE(02502E8C) SITE(02502E90) SITE(02502ECC) SITE(02502ED0) SITE(0250329C) SITE(025032CC)
SITE(025034AC) SITE(025034B0) SITE(025034D0) SITE(02503540) SITE(02503544) SITE(02503564)
SITE(025035BC) SITE(02503670) SITE(02503674) SITE(025037B0) SITE(025037B4) SITE(02503E20)
SITE(02503E24) SITE(02503EC8) SITE(02503ECC) SITE(025042F8) SITE(025043C8) SITE(0250447C)
SITE(02504620) SITE(02504624) SITE(02504660) SITE(02504664) SITE(025046C0) SITE(025046C4)
SITE(025046FC) SITE(02504700) SITE(025047B8) SITE(025047BC) SITE(02504808) SITE(0250480C)
SITE(025049CC) SITE(025049D0) SITE(02504B1C) SITE(02504B70) SITE(02504B74) SITE(02504B78)
SITE(02504B7C) SITE(02504B94) SITE(02504B98) SITE(02504BA8) SITE(02504BAC) SITE(02504C00)
SITE(02504C04) SITE(02504C14) SITE(02504C18) SITE(02504C68) SITE(02504C6C) SITE(02504D50)
SITE(02504DA0) SITE(02504DA4) SITE(02504DB0) SITE(02504DB4) SITE(02504ECC) SITE(02504F5C)
#undef PASS
#undef SITE
