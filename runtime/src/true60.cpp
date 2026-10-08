// True 60 fps: game logic at 60 steps per second (not interpolation).
//
// Pass structure (shared with interp.cpp): the per-frame function runs every vsync. Passes alternate:
//   full pass: everything runs, as on a normal 30 Hz frame
//   half pass: only "60 Hz" processes execute (interp.cpp's hold pass + this file's per-process gate)
// Every process is classified before each execute (fpcM_Execute hook):
//   converted (60 Hz):   executes on every pass with the step length dt = 0.5
//   unconverted (30 Hz): executes on full passes only, with dt = 1, and is drawn interpolated
//                        (interp.cpp's model and camera blending), so it stays smooth on screen
// Conversion is per process AND per state: Link is 60 Hz only in the procedures listed in
// kLinkProcs (the rest still run at 30 Hz and are interpolated), so unconverted actions stay correct.
//
// Units: every quantity keeps its original per-30-Hz-step unit (speeds, rates, thresholds), so the
// game's own comparisons stay valid. Only the places that ACCUMULATE per step are scaled by dt:
// shared primitives (cLib_addCalc*/chase*, J3DFrameCtrl::update, fopAcM_posMove, morf counters,
// cLib_calcTimer) are hooked below and read the current step length t_dt (1 outside a converted
// execute); inline per-step code in converted actors is patched with instruction-level hooks
// ("@ADDR" in tools/recomp/hooks.txt, site_ADDR functions in true60_link.cpp).
// Integer per-step amounts are split over the two half steps so that each pair adds up to exactly
// one original step: the half pass takes the rounded-up half, the full pass the rest.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <filesystem>
#include <bit>
#include <string>
#include <vector>

#include "guest_addr.h"
#include "runtime.h"
#include "true60.h"

extern "C" {
void f_025DF940_orig(Cpu* c);  // fpcM_Execute
void f_025DF904_orig(Cpu* c);  // fpcM_Draw
void f_0200ECD4_orig(Cpu* c);  // cLib_addCalc
void f_0200ED84_orig(Cpu* c);  // cLib_addCalc2
void f_0200EDC8_orig(Cpu* c);  // cLib_addCalc0
void f_0200EE00_orig(Cpu* c);  // cLib_addCalcPos
void f_0200EF78_orig(Cpu* c);  // cLib_addCalcPosXZ
void f_0200F164_orig(Cpu* c);  // cLib_addCalcPos2
void f_0200F268_orig(Cpu* c);  // cLib_addCalcPosXZ2
void f_0200F378_orig(Cpu* c);  // cLib_addCalcAngleS
void f_0200F428_orig(Cpu* c);  // cLib_addCalcAngleS2
void f_0200F474_orig(Cpu* c);  // cLib_addCalcAngleL
void f_0200F4FC_orig(Cpu* c);  // cLib_chaseUC
void f_0200F564_orig(Cpu* c);  // cLib_chaseS
void f_0200F5C8_orig(Cpu* c);  // cLib_chaseF
void f_0200F62C_orig(Cpu* c);  // cLib_chasePos
void f_0200F764_orig(Cpu* c);  // cLib_chasePosXZ
void f_0200F8D0_orig(Cpu* c);  // cLib_chaseAngleS
void f_027F2FC4_orig(Cpu* c);  // J3DFrameCtrl::update
void f_025E3EC8_orig(Cpu* c);  // mDoExt_MtxCalcOldFrame::decOldFrameMorfCounter
void f_025D67A8_orig(Cpu* c);  // fopAcM_calcSpeed
void f_025D6800_orig(Cpu* c);  // fopAcM_posMove
void f_025028B8_orig(Cpu* c);  // dCamera_c::followCamera
void f_0282167C_orig(Cpu* c);  // JPAEmitterManager::calc
void f_025CB6D4_orig(Cpu* c);  // dVibration_c::Pause
void f_024EF968_orig(Cpu* c);  // dBgS::MoveBgCrrPos
}

namespace true60_test {  // true60_test.cpp (debug dumps)
void after_execute(uint32_t proc, uint32_t fn, bool is_link, float dt);
void before_execute_link(uint32_t proc);
uint64_t origin_step();
bool dumping();
}
namespace interp {
bool hold_pass();          // interp.cpp: this is a half pass (no 30 Hz logic)
uint64_t logic_steps();    // full logic steps so far
}

namespace true60 {

static std::atomic<bool> g_on{[] { const char* e = getenv("WWHD_TRUE60"); return e && atoi(e) != 0; }()};
bool enabled() { return g_on.load(std::memory_order_relaxed); }
void set_enabled(bool v) {
    g_on = v;
    LOG("[true60] game logic at 60 steps/s %s", v ? "on" : "off");
}

thread_local float t_dt = 1.0f;  // step length of the execute in progress (main thread)
thread_local uint32_t t_proc = 0;  // process whose execute is in progress
float dt() { return t_dt; }
uint32_t exec_proc() { return t_proc; }
bool half_pass() { return interp::hold_pass(); }

uint64_t g_pass = 0;  // counts per-frame passes (interp.cpp calls new_pass)
int g_link_ride = 0;  // passes left in which Link counts as riding a moving 30 Hz collision
int g_cam_fallback = 0;  // passes left with the camera at 30 Hz after a 60 Hz step went NaN
void new_pass() {
    g_pass++;
    if (g_cam_fallback > 0) g_cam_fallback--;
    if (g_link_ride > 0) g_link_ride--;
}
uint64_t pass() { return g_pass; }

// integer per-step amount for this half step: the half pass rounds up, the full pass gets the rest
int32_t split(int32_t v) {
    if (t_dt >= 1.0f) return v;
    int32_t a = v >= 0 ? (v + 1) / 2 : -((-v + 1) / 2);
    return half_pass() ? a : v - a;
}

// exponential smoothing fraction for a shorter step: 1-(1-s)^dt
static double frac(double s) {
    if (t_dt >= 1.0f || s <= 0.0 || s >= 1.0) return s;
    return 1.0 - std::pow(1.0 - s, (double)t_dt);
}

thread_local int t_saved_reg = -1;
thread_local double t_saved = 0;
void ratio_begin(Cpu* c, int r) {
    if (t_dt >= 1.0f) return;
    t_saved_reg = r;
    t_saved = c->f[r].ps0;
    c->f[r].ps0 = (float)frac(c->f[r].ps0);
}
void ratio_end(Cpu* c, int r) {
    if (t_saved_reg != r) return;
    c->f[r].ps0 = t_saved;
    t_saved_reg = -1;
}
void ratio_arg(Cpu* c, int r) {
    if (t_dt < 1.0f) c->f[r].ps0 = (float)frac(c->f[r].ps0);
}

// ---- processes ----
namespace {
const uint32_t kRndSeeds = GD(0x101FF9D4);  // cM_rnd's three seeds r0, r1, r2 (cM_rnd 02019788: -0x62C/-0x628/-0x624 from 0x10200000)
constexpr uint32_t kSubMethod = 0xF0;     // fopAc_ac_c::sub_method (GameCube 0xEC)
const uint32_t kDaPyExecute = GC(0x0240EBB0);  // daPy_Execute (tail-calls daPy_lk_c::execute 0240CDD0)
constexpr uint32_t kCurProc = 0x65F0;     // daPy_lk_c::mCurProc (GameCube 0x31D8)
constexpr uint32_t kPos = 0x314, kOld = 0x300, kSpeed = 0x33C, kSpeedF = 0x370, kGravity = 0x374, kMaxFall = 0x378;
constexpr uint32_t kShapeAngle = 0x328, kAngle = 0x320;
constexpr uint32_t kFrameCtrlUnder = 0x5898;  // daPy_lk_c::mFrameCtrlUnder[0] (J3DFrameCtrl: rate +0, frame +4)
constexpr uint32_t kNormalSpeed = 0x6A14;     // daPy_lk_c::mNormalSpeed

struct ProcInfo {
    uint64_t pass = 0;  // last pass it executed on
    float dt = 1.0f;    // step length of that execute
};
std::unordered_map<uint32_t, ProcInfo> g_procs;
uint32_t g_link = 0;  // Link's process (daPy_lk_c), once seen
uint64_t g_link_steps = 0;  // Link's full-pass executes since the last save-state load (test clock)

uint32_t actor_execute_fn(uint32_t proc) {
    uint32_t sub = ld32(proc + kSubMethod);
    if (sub < 0x10000000 || sub >= 0x50000000) return 0;
    return ld32(sub + 8);
}

// Conversion groups. WWHD_TRUE60_GROUPS=+name,-name,... switches groups on/off relative to the
// default; a list without +/- replaces the default ("all", "none" also work). Names: kGroupNames.
// Only groups that passed the gates (docs/decomp-notes.md, "True 60 fps") are on by default.
const char* const kGroupNames[kNumGroups] = {"loco", "camera", "sword", "items", "swim", "sail", "bk", "mo2", "cc", "ki"};
constexpr uint32_t kGroupDefault = (1u << kGrpLoco) | (1u << kGrpCamera) | (1u << kGrpSword);
uint32_t g_groups = [] {
    uint32_t m = kGroupDefault;
    const char* e = getenv("WWHD_TRUE60_GROUPS");
    bool replaced = false;
    for (const char* p = e; p && *p;) {
        size_t n = strcspn(p, ",");
        std::string w(p, n);
        p += n + (p[n] == ',');
        char sign = w.empty() ? 0 : w[0];
        if (sign == '+' || sign == '-') w = w.substr(1);
        else if (!replaced) m = 0, replaced = true;
        uint32_t bit = 0;
        if (w == "all") bit = (1u << kNumGroups) - 1;
        else if (w == "none") bit = 0, m = sign == '+' ? m : 0;
        for (int g = 0; g < kNumGroups; g++)
            if (w == kGroupNames[g]) bit = 1u << g;
        if (sign == '-') m &= ~bit;
        else m |= bit;
    }
    std::string on;
    for (int g = 0; g < kNumGroups; g++)
        if (m & (1u << g)) on += std::string(on.empty() ? "" : ",") + kGroupNames[g];
    LOG("[true60] groups: %s", on.c_str());
    return m;
}();

// Link's procedures that run at 60 Hz (daPy_lk_c::daPy_PROC values) and how (link_proc_mode);
// all others stay at 30 Hz. The lists hold the procedures measured against the 30 fps game
// (docs/decomp-notes.md, "True 60 fps"). WWHD_TRUE60_LINK=audited|all|none|n,n,... (decimal or 0x
// hex; a trailing 's' runs that procedure's own logic on full passes only) overrides it (testing).
uint8_t g_link_proc[256];
const int kAudited[] = {
// 108 procedures: no inline per-step code in the GameCube source of the procedure itself
// (tools/true60/proc_audit.py), or only per-procedure timer countdowns (held generically);
// without ship, rope, hookshot and carrying procedures (they move with 30 Hz actors) and swimming
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05,  // Scope Subjectivity Call ControllWait Wait FreeWait
    0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,  // Move AtnMove AtnActorWait AtnActorMove SideStep SideStepLand
    0x0C, 0x0D, 0x0E, 0x0F, 0x12, 0x17,  // CrouchDefense CrouchDefenseSlip Crouch CrawlStart CrawlEnd WaitTurn
    0x18, 0x19, 0x1C, 0x1D, 0x1F, 0x20,  // MoveTurn Slip SlideFrontLand SlideBackLand FrontRollCrash NockBackEnd
    0x21, 0x22, 0x23, 0x25, 0x26, 0x27,  // SideRoll BackJump BackJumpLand Land LandDamage Fall
    0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D,  // SlowFall SmallJump VerticalJump HangStart HangFallStart HangUp
    0x2E, 0x30, 0x31, 0x32, 0x33, 0x34,  // HangWait HangClimb HangWallCatch PushPullWait PushMove PullMove
    0x38, 0x39, 0x3A, 0x3B, 0x3C,  // LadderUpStart LadderUpEnd LadderDownStart LadderDownEnd LadderMove
    // (SwimUp 0x35 left out: measured 27.5 steps instead of 35; swimming's buoyancy is not converted)
    0x3D, 0x3E, 0x3F, 0x41, 0x42, 0x43,  // ClimbUpStart ClimbDownStart ClimbMoveUpDown CutA CutF CutR
    0x44, 0x47, 0x48, 0x4A, 0x4B, 0x4C,  // CutL CutExA CutExB CutKesa WeaponNormalSwing WeaponSideSwing
    0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52,  // WeaponFrontSwingReady WeaponFrontSwing WeaponFrontSwingEnd WeaponThrow HammerSideSwing HammerFrontSwingReady
    0x53, 0x54, 0x55, 0x57, 0x58, 0x5A,  // HammerFrontSwing HammerFrontSwingEnd CutTurn CutRollEnd CutTurnCharge CutReverse
    0x5B, 0x5C, 0x5E, 0x5F, 0x62, 0x63,  // JumpCut JumpCutLand BtJumpCut BtSlide BtVerticalJump BtVerticalJumpCut
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69,  // BtVerticalJumpLand GuardCrash Damage PolyDamage LargeDamage LargeDamageUp
    0x6A, 0x6B, 0x6C, 0x6D, 0x80, 0x81,  // LargeDamageWall LavaDamage ElecDamage GuardSlip BoomerangSubject BoomerangMove
    0x82, 0x94, 0x95, 0x96, 0x98, 0x99,  // BoomerangCatch BowSubject BowMove VomitReady VomitJump VomitLand
    0x9C, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2,  // TactPlayEnd IceSlipFall IceSlipFallUp IceSlipAlmostFall BootsEquip NotUse
    0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,  // BottleDrink BottleOpen BottleSwing BottleGet FoodThrow FoodSet
};
struct ProcGroup { Group g; uint8_t mode; std::vector<int> procs; };
const ProcGroup kLinkGroups[] = {
    // locomotion with physics and steering: the procedure runs on every pass with dt = 0.5
    {kGrpLoco, 1, {
        0x06,  // MOVE
        0x07,  // ATN_MOVE (Z-target)
        0x0A,  // SIDE_STEP (side hop)
        0x22,  // BACK_JUMP (back flip)
        0x24,  // AUTO_JUMP
        0x27,  // FALL
    }},
    // locomotion that ends on animation frames or picks idle animations at random: the procedure's
    // own logic on full passes (same step and same random numbers as at 30 fps)
    {kGrpLoco, 2, {
        0x04,  // WAIT
        0x05,  // FREE_WAIT
        0x1E,  // FRONT_ROLL
        0x23,  // BACK_JUMP_LAND
        0x25,  // LAND
    }},
    // sword actions: animation, motion and collision at 60 Hz, the procedure's own decisions on
    // full passes (frame thresholds, combo inputs, hit windows and sounds exactly as at 30 fps)
    {kGrpSword, 2, {
        0x41, 0x42, 0x43, 0x44,  // CUT_A CUT_F CUT_R CUT_L
        0x45, 0x46,              // CUT_EA CUT_EB (combo finishers)
        0x47, 0x48,              // CUT_EX_A CUT_EX_B (parry attacks)
        0x4A,                    // CUT_KESA
        0x55, 0x58, 0x59,        // CUT_TURN (spin attack), CUT_TURN_CHARGE, CUT_TURN_MOVE
        0x5A,                    // CUT_REVERSE (sword bounces off a wall)
    }},
    // items (the item actors themselves stay at 30 Hz): aiming, throwing, shooting, catching
    {kGrpItems, 2, {
        0x80, 0x81, 0x82,        // BOOMERANG_SUBJECT BOOMERANG_MOVE BOOMERANG_CATCH
        0x83, 0x84,              // HOOKSHOT_SUBJECT HOOKSHOT_MOVE (not HOOKSHOT_FLY: pulled by the hookshot)
        0x94, 0x95,              // BOW_SUBJECT BOW_MOVE
    }},
};
void init_link_procs() {
    static bool done = false;
    if (done) return;
    done = true;
    const char* e = getenv("WWHD_TRUE60_LINK");
    if (e && !strcmp(e, "audited")) {
        for (int v : kAudited) g_link_proc[v] = 1;
        for (auto& pg : kLinkGroups)
            for (int v : pg.procs) g_link_proc[v] = pg.mode;
    } else if (e && !strcmp(e, "all")) {
        for (auto& b : g_link_proc) b = 1;
    } else if (e && strcmp(e, "none")) {
        for (const char* p = e; *p;) {
            char* end;
            long v = strtol(p, &end, 0);
            if (end == p) break;
            uint8_t mode = 1;
            if (*end == 's') mode = 2, end++;
            if (v >= 0 && v < 256) g_link_proc[v] = mode;
            p = *end == ',' ? end + 1 : end;
        }
    } else if (!e) {
        for (auto& pg : kLinkGroups)
            if (g_groups & (1u << pg.g))
                for (int v : pg.procs) g_link_proc[v] = pg.mode;
    }
}

// The player camera (camera process; dCamera_c at +0x248) runs at 60 Hz while it uses the follow
// camera (dCamera_c::followCamera, whose smoothing is converted by tools/true60/sites_camera.txt);
// other camera modes stay at 30 Hz and are interpolated. WWHD_TRUE60_CAMERA=0 keeps it at 30 Hz.
constexpr uint32_t kCamMtd = 0x228;              // camera_class::mpMtd (GameCube 0x224)
const uint32_t kCameraExecute = GC(0x024FFA3C);  // camera_execute
constexpr uint32_t kDCamera = 0x248;             // camera_process_class::mCamera
bool g_follow_called = false;  // followCamera ran during the camera's execute
bool g_cam_follow = false;     // ... during its last execute
uint32_t g_camera = 0;
bool camera_enabled() {
    static const bool on = (!getenv("WWHD_TRUE60_CAMERA") || atoi(getenv("WWHD_TRUE60_CAMERA")) != 0) && (g_groups & (1u << kGrpCamera));
    return on && g_cam_fallback == 0;
}

// Safety net for the 60 Hz camera: a NaN in its state halts the game (cM3dGSph::SetC asserts on a
// NaN centre in dCamera_c::bumpCheck). Each 60 Hz camera step is snapshotted; if it produces a NaN
// the snapshot is restored, the camera runs at 30 Hz for a second and the case is logged to
// captures/true60-nan.log (for finding the cause).
// the camera process with its dCamera_c: the size in its profile (101D5640: name 0x1DC, size 0xB28;
// earlier 0x1248 was assumed here, which reached into the next heap block)
constexpr uint32_t kCamSnap = 0xB28;
bool g_cam_step = false;    // inside a 60 Hz camera execute
bool g_cam_nan = false;     // ... and a sphere got a NaN centre
uint32_t g_cam_nan_lr = 0;  // its caller
bool cam_has_nan(uint32_t proc) {
    for (uint32_t o = kDCamera; o < kCamSnap; o += 4) {
        uint32_t v = ld32(proc + o);
        if ((v & 0x7F800000) == 0x7F800000 && (v & 0x007FFFFF)) {
            // a NaN pattern: count it only when it was not there before the step (see caller)
            return true;
        }
    }
    return false;
}
bool is_camera(uint32_t proc) {
    uint32_t m = ld32(proc + kCamMtd);
    return m >= 0x10000000 && m < 0x50000000 && ld32(m + 8) == kCameraExecute;
}

// step length a process executes with on this pass
float classify(uint32_t proc) {
    static bool init = (init_link_procs(), true);
    (void)init;
    uint32_t fn = actor_execute_fn(proc);
    if (fn == kDaPyExecute) {
        if (g_link != proc) {
            g_link = proc;
            LOG("[true60] Link is process %08X", proc);
            if (false) {
                uint32_t of = ld32(proc + 0x65CC);
                uint32_t ti = ld32(of + 0x1C), q = ld32(of + 0x20);
                LOG("[t60dbg] old_fdata %08X trans %08X quat %08X (q-t %X); headers %08X %08X %08X %08X / %08X %08X %08X %08X", of, ti, q, q - ti,
                    ld32(ti - 16), ld32(ti - 12), ld32(ti - 8), ld32(ti - 4), ld32(q - 16), ld32(q - 12), ld32(q - 8), ld32(q - 4));
                LOG("[t60dbg] Link header %08X %08X %08X %08X", ld32(proc - 16), ld32(proc - 12), ld32(proc - 8), ld32(proc - 4));
            }
        }
        uint32_t p = ld32(proc + kCurProc);
        // standing on moving collision of a 30 Hz actor (platform, raft): the platform moves on full
        // passes only and is drawn interpolated, so Link moves with it at 30 Hz
        if (g_link_ride > 0) return 1.0f;
        return p < 256 && g_link_proc[p] ? 0.5f : 1.0f;  // (mode 2: see true60_link.cpp)
    }
    if (is_camera(proc)) {  // checked every time: each scene creates its own camera process
        if (g_camera != proc) LOG("[true60] camera is process %08X", proc);
        g_camera = proc;
        return camera_enabled() && g_cam_follow ? 0.5f : 1.0f;
    }
    return 1.0f;
}

// ---- trace (also without true60, to compare with the original) ----
// WWHD_LINK_TRACE=path: one line per Link execute: pass, real time, dt, proc, pos, speed, ...
FILE* g_trace = [] {
    const char* p = getenv("WWHD_LINK_TRACE");
    return p ? fopen(p, "w") : nullptr;
}();
void trace_link(uint32_t proc, float dt) {
    if (!g_trace) return;
    double t = (double)timebase::now() / timebase::kTicksPerSec;  // guest time (the test origin is logged as [test])
    fprintf(g_trace, "%llu %llu %.4f %d %.2f %d %.3f %.3f %.3f %.4f %.4f %.4f %.4f %d %d %.3f %.3f %.3f\n", (unsigned long long)g_pass, (unsigned long long)interp::logic_steps(), t,
            half_pass() ? 0 : 1, dt, (int)ld32(proc + kCurProc), ldf32(proc + kPos), ldf32(proc + kPos + 4), ldf32(proc + kPos + 8),
            ldf32(proc + kSpeedF), ldf32(proc + kSpeed), ldf32(proc + kSpeed + 4), ldf32(proc + kSpeed + 8),
            (int)(int16_t)ld16(proc + kShapeAngle + 2), (int)(int16_t)ld16(proc + kAngle + 2),
            ldf32(proc + kFrameCtrlUnder + 4), ldf32(proc + kFrameCtrlUnder), ldf32(proc + kNormalSpeed));
    static int n = 0;
    if (++n % 60 == 0) fflush(g_trace);
}
// WWHD_CAM_TRACE=path: one line per camera execute: pass, logic step, full pass, dt, follow camera
// used, eye xyz, center xyz, fovy
FILE* g_cam_trace = [] {
    const char* p = getenv("WWHD_CAM_TRACE");
    return p ? fopen(p, "w") : nullptr;
}();
void trace_camera(uint32_t proc, float dt) {
    if (!g_cam_trace) return;
    fprintf(g_cam_trace, "%llu %llu %d %.2f %d %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", (unsigned long long)g_pass,
            (unsigned long long)interp::logic_steps(), half_pass() ? 0 : 1, dt, g_follow_called ? 1 : 0, ldf32(proc + 0xDC),
            ldf32(proc + 0xE0), ldf32(proc + 0xE4), ldf32(proc + 0xE8), ldf32(proc + 0xEC), ldf32(proc + 0xF0), ldf32(proc + 0xD4));
    // counters and follow-camera work words (dCamera +0x100 m100, +0x108 m108, +0x36C..+0x388)
    uint32_t d = proc + kDCamera;
    fprintf(g_cam_trace, "    m100 %d m108 %d work", (int)ld32(d + 0x100), (int)ld32(d + 0x108));
    for (uint32_t o = 0x36C; o <= 0x388; o += 4) fprintf(g_cam_trace, " %08X", ld32(d + o));
    fprintf(g_cam_trace, "\n");
    static int n = 0;
    if (++n % 60 == 0) fflush(g_cam_trace);
}
}  // namespace

// Link's half-pass preview. What Link's execute changes in his own memory is put back at the start
// of the next full pass, which then runs the 30 Hz step from exactly the state the 30 fps game has:
//  - his process (profile size 0x8284), completely;
//  - his actor heap (fopAc_ac_c::heap +0xF4, a solid heap: start +0x8, size +0x20, ~590 KB): his
//    models' joint matrices (read by the next step before they are computed again: neck and head
//    aim, attachments), the old-frame pose blocks (m_old_fdata +0x65CC) and the other per-step
//    model state. Taken back word by word where his preview execute changed it: the heap also holds
//    the models' draw packets, which the draw (not the execute) links into the frame's draw lists;
//    taking the whole heap back broke the lists (sead assert in the J3D packet entry).
// Effects outside are fenced off during the preview (true60_link.cpp: procedure, decisions,
// actor/emitter creation, colliders, events) or done again identically by the full pass; sounds are
// not started on half passes at all (interp.cpp).
constexpr uint32_t kLinkSize = 0x8284;
constexpr uint32_t kActorHeap = 0xF4;
// Regions taken back word by word where Link's preview execute changed them (before/after the
// execute; changes made by anything else, like the draw linking packets, stay): his actor heap, the
// d_a_player statics holding his position (1046CD48, read back by the next execute) and the words of
// the dComIfGp play state (1046F0B0) his execute writes (10473FE8..10474050: player position and
// status, 10474C64: the do/A/R button statuses), found with WWHD_T60_REGIONLOG. Not whole static
// regions: other threads (the sound engine) write there while his execute runs, and taking their
// writes back crashed the sound thread (JASTrack). cM_rnd's seeds are put back separately.
struct Region { uint32_t base = 0; std::vector<uint32_t> before, after; };
struct LinkSnap {
    uint64_t pass = ~0ull;
    uint32_t proc = 0;
    std::vector<uint8_t> link;
    Region regions[4];
    uint32_t env_player[3];  // g_env_light +0xB2C: Link's position for the scene lighting, set by his draw
} g_snap;
const uint32_t kEnvPlayerPos = GD(0x10475A68) + 0xB2C;  // (found with WWHD_T60_ENVLOG)
void region_begin(Region& r, uint32_t base, uint32_t size) {
    r.base = base;
    r.before.resize(size / 4);
    memcpy(r.before.data(), ppc_ptr(base), size & ~3u);
}
void region_end(Region& r) {
    if (!r.base) return;
    r.after.resize(r.before.size());
    memcpy(r.after.data(), ppc_ptr(r.base), 4 * r.after.size());
}
void region_restore(Region& r) {
    if (!r.base || r.after.size() != r.before.size()) return;
    static const bool dbg = getenv("WWHD_T60_REGIONLOG") != nullptr;
    if (dbg && (&r == &g_snap.regions[3])) {
        static std::unordered_map<uint32_t, int> cnt;
        static int n = 0;
        for (size_t i = 0; i < r.before.size(); i++)
            if (r.after[i] != r.before[i]) cnt[r.base + 4 * (uint32_t)i]++;
        if (++n % 100 == 0) {
            std::vector<std::pair<uint32_t, int>> v(cnt.begin(), cnt.end());
            std::sort(v.begin(), v.end());
            std::string o;
            for (auto& [a, c] : v) { char t[32]; snprintf(t, sizeof t, " %08X:%d", a, c); o += t; }
            LOG("[regionlog]%s", o.c_str());
        }
    }
    if (dbg && &r == &g_snap.regions[3]) return;  // (log only)
    uint32_t* cur = (uint32_t*)ppc_ptr(r.base);
    const uint32_t* b = r.before.data();
    const uint32_t* a = r.after.data();
    for (size_t i = 0, n = r.before.size(); i < n; i++)
        if (a[i] != b[i]) cur[i] = b[i];  // raw words: no byte swap needed
}
// debug: WWHD_T60_MEMDIFF=step: memory changed by the half pass of that logic step and not taken back
std::vector<uint8_t> g_memdiff;
uint64_t g_memdiff_step = getenv("WWHD_T60_MEMDIFF") ? strtoull(getenv("WWHD_T60_MEMDIFF"), nullptr, 10) : 0;
const uint32_t kMemDiffLo = getenv("WWHD_T60_MEMDIFF_LO") ? (uint32_t)strtoul(getenv("WWHD_T60_MEMDIFF_LO"), nullptr, 16) : 0x10000000;
const uint32_t kMemDiffHi = getenv("WWHD_T60_MEMDIFF_HI") ? (uint32_t)strtoul(getenv("WWHD_T60_MEMDIFF_HI"), nullptr, 16) : 0x4A000000;
void link_preview_begin(uint32_t proc) {
    if (g_memdiff_step && true60_test::origin_step() && interp::logic_steps() == true60_test::origin_step() + g_memdiff_step && g_memdiff.empty()) {
        g_memdiff.assign(ppc_ptr(kMemDiffLo), ppc_ptr(kMemDiffHi));
        LOG("[memdiff] snapshot at step %llu", (unsigned long long)g_memdiff_step);
    }
    g_snap.pass = g_pass;
    g_snap.proc = proc;
    g_snap.link.assign(ppc_ptr(proc), ppc_ptr(proc) + kLinkSize);
    for (int i = 0; i < 3; i++) g_snap.env_player[i] = ld32(kEnvPlayerPos + 4 * i);
    for (auto& r : g_snap.regions) r.base = 0;
    region_begin(g_snap.regions[0], GD(0x1046CD10), 0x44);          // d_a_player statics (his kept position at +0x38)
    region_begin(g_snap.regions[1], GD(0x10473FE8), GD(0x10474C68) - GD(0x10473FE8));  // dComIfGp play: player position/status words, button statuses
    if (const char* e = getenv("WWHD_T60_EXTRA_REGION")) {  // debug: hunt for preview state (lo:size hex)
        uint32_t lo = (uint32_t)strtoul(e, nullptr, 16), sz = strchr(e, ':') ? (uint32_t)strtoul(strchr(e, ':') + 1, nullptr, 16) : 0;
        if (sz) region_begin(g_snap.regions[3], lo, sz);
    }
    uint32_t h = ld32(proc + kActorHeap);
    if (h >= mem::kMem2Start && h < mem::kMem2End) {
        uint32_t start = ld32(h + 0x8), size = ld32(h + 0x20) & ~3u;
        if (start >= mem::kMem2Start && (start & 3) == 0 && size > 0 && size <= 0x200000 && start + size <= mem::kMem2End)
            region_begin(g_snap.regions[2], h, start + size - h);  // (from the heap object: its allocation state too)
    }
    static bool warned = false;
    if (!g_snap.regions[2].base && !warned) {
        warned = true;
        LOG("[true60] Link's actor heap not found (%08X)", h);
    }
}
void link_preview_end() {
    for (auto& r : g_snap.regions) region_end(r);
}
void link_preview_restore() {
    if (g_snap.pass == ~0ull) return;
    g_snap.pass = ~0ull;
    if (g_snap.proc != g_link || g_snap.link.size() != kLinkSize) return;
    memcpy(ppc_ptr(g_snap.proc), g_snap.link.data(), kLinkSize);
    for (auto& r : g_snap.regions) region_restore(r);
    // his half-pass draw lit the scene from his preview position; the full pass's draws before his
    // own draw use the position of the last full pass, as at 30 fps
    for (int i = 0; i < 3; i++) st32(kEnvPlayerPos + 4 * i, g_snap.env_player[i]);
}

// The 60 Hz camera's full passes are exact 30 Hz steps: its half pass is a preview (drawn), and the
// next full pass starts again from the camera as it was before that half pass (pass_begin) and takes
// a whole step. So at full passes the camera is the 30 fps game's camera (Link reads it to steer;
// what is in view, and so the draw-time random numbers of the scene lighting, stays the same).
// WWHD_TRUE60_CAMERA_EXACT=0 keeps two half steps (debug).
const bool g_cam_exact = !getenv("WWHD_TRUE60_CAMERA_EXACT") || atoi(getenv("WWHD_TRUE60_CAMERA_EXACT")) != 0;
std::vector<uint8_t> g_cam_half;
// the camera's execute also writes the play state's camera data (dComIfGp: 1047E720.., 104846B0..,
// eye/center/up of the camera slots, read by the next pass's draws before its own camera_draw):
// taken back word by word where the preview execute changed them
const uint32_t kPlayCamLo = GD(0x1046F0B0), kPlayCamHi = GD(0x10485000);
Region g_cam_play;
uint64_t g_cam_half_pass = ~0ull, g_cam_restored = ~0ull;
uint32_t g_cam_half_proc = 0;
struct TimerField { uint32_t off; int bytes; };
// camera (dCamera_c offsets + kDCamera): step counters m07C, m080, m108, m118, m11C (Run),
// mForceLockTimer +0x138, and the follow camera's work counters (work at +0x37C in WWHD:
// +0x380 bezier counter (GameCube m388), +0x38A charge counter (m392); +0x384 see hook_025DF940)
const TimerField kCamCounters[] = {
    {kDCamera + 0x07C, 4}, {kDCamera + 0x080, 4}, {kDCamera + 0x108, 4}, {kDCamera + 0x118, 4}, {kDCamera + 0x11C, 4},
    {kDCamera + 0x138, 4}, {kDCamera + 0x380, 4}, {kDCamera + 0x38A, 2},
};
constexpr int kNumCamCounters = sizeof(kCamCounters) / sizeof(kCamCounters[0]);
constexpr int kNumCamStepCounters = 5;  // the first five (m07C .. m11C) only ever count up
int32_t read_field(uint32_t a, int bytes) { return bytes == 2 ? (int32_t)(int16_t)ld16(a) : (int32_t)ld32(a); }
void write_field(uint32_t a, int bytes, int32_t v) {
    if (bytes == 2) st16(a, (uint16_t)v);
    else st32(a, (uint32_t)v);
}

bool group_on(Group g) { return (g_groups >> g) & 1; }
int link_proc_mode(uint32_t p) {
    init_link_procs();
    return p < 256 ? g_link_proc[p] : 0;
}
float set_dt(float dt) {
    float o = t_dt;
    t_dt = dt;
    return o;
}

bool runs_60(uint32_t proc) {
    if (!enabled()) return false;
    auto it = g_procs.find(proc);
    return it != g_procs.end() && it->second.pass == g_pass && it->second.dt < 1.0f;
}
uint32_t link() { return g_link; }
uint64_t link_steps() { return g_link_steps; }
bool g_state_loaded = false;
bool state_loaded() { return g_state_loaded; }
// camera_draw on a half pass sets the view matrix for the preview camera (j3dSys view matrix at
// 104B45F8, found with WWHD_T60_CAMDRAWLOG). The next full pass draws its actors before its own
// camera_draw, as the 30 fps game does with the camera of the previous step: the matrix goes back to
// the full pass's. Only these words: the area around holds sound-engine state written by the sound
// thread meanwhile (taking a wider range back crashed it in JASTrack).
Region g_camdraw;
void camera_draw_preview(bool) {}  // (the view matrix is among the half-pass draw state, pass_begin)
// Draw state the half pass's previews changed and the next full pass would otherwise start from:
// what its draws decide before the full pass's own camera_draw and lighting are set up (which
// objects are in view, which are lit by a point light, with a random number each) must be the
// 30 fps game's. At the start of each half pass these are saved, at the next full pass put back:
//   the scene lighting (g_env_light 10475A68: light fades, flicker, the lighting position of the
//   player), the frustum clipper (mDoLib_clipper 1048CFF0, set up from the camera), the J3D view
//   matrix (104B45F8).
// WWHD_TRUE60_DRAWSTATE_KEEP=1 keeps them (debug).
struct HalfSave { uint32_t addr, size; std::vector<uint8_t> data; };
// (found with WWHD_T60_DRAWWRITE: the words half-pass draws change; not the sound engine's areas
// around 104B5000 and 104C1000, which the sound thread writes at the same time)
HalfSave g_half_saves[] = {
    {GD(0x10474DE0), 0x10, {}},    // play state: lighting position written by Link's draw
    {GD(0x1047E720), 0x90, {}},    // play state: camera slot 0 draw data (written by daBg's draw)
    {GD(0x104846B0), 0x90, {}},    // play state: camera slot 1 draw data (daBg)
    {GD(0x1048CFF0), 0x80, {}},    // mDoLib_clipper
    {GD(0x104B45F8), 0x30, {}},    // J3D view matrix
};
uint64_t g_half_saves_pass = ~0ull;
void pass_begin(bool full) {
    static const bool keep = getenv("WWHD_TRUE60_DRAWSTATE_KEEP") != nullptr;
    if (!full) {
        if (enabled() && !keep && (g_link || g_camera)) {
            for (auto& h : g_half_saves) h.data.assign(ppc_ptr(h.addr), ppc_ptr(h.addr) + h.size);
            g_half_saves_pass = g_pass;
        }
        return;
    }
    if (g_half_saves_pass + 1 == g_pass)
        for (auto& h : g_half_saves) memcpy(ppc_ptr(h.addr), h.data.data(), h.size);
    g_half_saves_pass = ~0ull;
    link_preview_restore();

    if (!g_memdiff.empty() && g_memdiff_step) {
        g_memdiff_step = 0;
        int n = 0;
        size_t i = 0, sz = g_memdiff.size();
        const uint8_t* cur = ppc_ptr(kMemDiffLo);
        while (i < sz && n < 400) {
            if (cur[i] == g_memdiff[i]) { i++; continue; }
            size_t j = i;
            size_t last = i;
            while (j < sz && j - last < 32) { if (cur[j] != g_memdiff[j]) last = j; j++; }
            LOG("[memdiff] %08X +%zX", (uint32_t)(kMemDiffLo + i), last + 1 - i);
            n++;
            i = last + 1;
        }
        g_memdiff.clear();
        g_memdiff.shrink_to_fit();
    }
    // the camera's half pass was a preview too: put it back before anything executes (Link reads it)
    if (g_cam_half_pass + 1 == g_pass && g_cam_half_proc == g_camera && g_cam_half.size() == kCamSnap) {
        memcpy(ppc_ptr(g_cam_half_proc), g_cam_half.data(), kCamSnap);
        region_restore(g_cam_play);
        g_cam_restored = g_pass;
    }
    g_cam_play.base = 0;
    g_cam_half_pass = ~0ull;
}
bool preview() { return t_dt < 1.0f && half_pass(); }
void ss_reset() {
    g_link_steps = 0;
    g_state_loaded = true;
    g_snap.pass = ~0ull;  // the loaded state has its own Link and camera
    g_cam_half_pass = ~0ull;
    g_procs.clear();
    g_link = 0;
    g_camera = 0;
    g_link_ride = 0;
    g_follow_called = g_cam_follow = false;
}

// statistics: executes per second by step length (logged every 10 s)
namespace {
struct Stats { uint32_t full = 0, half = 0, skipped = 0, link60 = 0, link30 = 0, particles = 0; } g_st;
void stats_tick() {
    static uint64_t t0 = timebase::now();
    uint64_t t = timebase::now();
    if (t - t0 < 10 * timebase::kTicksPerSec) return;
    double s = (double)(t - t0) / timebase::kTicksPerSec;
    LOG("[true60] per second: %.1f executes at dt=1, %.1f at dt=0.5, %.1f held back; Link %.1f steps at 60 Hz + %.1f at 30 Hz; "
        "particle updates %.1f",
        g_st.full / s, g_st.half / s, g_st.skipped / s, g_st.link60 / s, g_st.link30 / s, g_st.particles / s);
    g_st = Stats{};
    t0 = t;
}
}  // namespace

}  // namespace true60

using namespace true60;

// fpcM_Execute(process): per-process gate and step length
// debug: WWHD_WATCH_LINK=off: logs which process execute/draw changes the word at Link+off
static uint32_t watch_addr() {
    static const long abs_a = getenv("WWHD_WATCH_ADDR") ? strtol(getenv("WWHD_WATCH_ADDR"), nullptr, 16) : -1;
    if (abs_a >= 0) return (uint32_t)abs_a;
    static const long off = getenv("WWHD_WATCH_LINK") ? strtol(getenv("WWHD_WATCH_LINK"), nullptr, 16) : -1;
    return off >= 0 && true60::link() ? true60::link() + (uint32_t)off : 0;
}
static void watch_link(const char* what, uint32_t proc, uint32_t before) {
    uint32_t wa = watch_addr();
    if (!wa) return;
    uint32_t v = ld32(wa);
    if (v != before) LOG("[watch] pass %llu step %llu %s %s %08X (exec fn %08X): %08X -> %08X", (unsigned long long)true60::pass(), (unsigned long long)interp::logic_steps(), interp::hold_pass() ? "half" : "full", what, proc, actor_execute_fn(proc), before, v);
}
static uint32_t watch_val() {
    uint32_t wa = watch_addr();
    return wa ? ld32(wa) : 0;
}
static FILE* g_rx = getenv("WWHD_RND_EXEC") ? fopen(getenv("WWHD_RND_EXEC"), "w") : nullptr;  // debug
static void rx_log(uint32_t proc, uint32_t s0) {
    if (g_rx && ld32(kRndSeeds) != s0)
        fprintf(g_rx, "%llu %d %08X %08X %08X\n", (unsigned long long)interp::logic_steps(), (int)half_pass(), proc, actor_execute_fn(proc), s0);
}
extern "C" void hook_025DF940(Cpu* c) {
    uint32_t proc = c->r[3];
    uint32_t rx_s0 = ld32(kRndSeeds);
    struct WatchGuard { uint32_t p, v; ~WatchGuard() { watch_link("execute", p, v); } } watch_guard{proc, watch_val()};
    struct RxLog { uint32_t p, s; ~RxLog() { rx_log(p, s); } } rx_guard{proc, rx_s0};
    if (!enabled()) {
        bool is_link = g_trace && actor_execute_fn(proc) == kDaPyExecute;
        bool is_cam = g_cam_trace && is_camera(proc);
        if (is_cam) g_follow_called = false;
        uint32_t fn = true60_test::dumping() ? actor_execute_fn(proc) : 0;
        if (fn == kDaPyExecute) true60_test::before_execute_link(proc);
        f_025DF940_orig(c);
        if (fn) true60_test::after_execute(proc, fn, fn == kDaPyExecute, 1.0f);
        if (is_cam) trace_camera(proc, 1.0f);
        g_st.full++;
        if (g_trace || g_cam_trace) stats_tick();  // statistics also for the 30 fps reference runs
        if (is_link) {
            g_link = proc;
            trace_link(proc, 1.0f);
        }
        if (actor_execute_fn(proc) == kDaPyExecute) g_link_steps++;
        return;
    }
    float dt = classify(proc);
    if (half_pass() && dt >= 1.0f) {  // 30 Hz process on a half pass: waits for the full pass
        g_st.skipped++;
        c->r[3] = 0;
        return;
    }
    // Link in a converted procedure: the full pass is a whole 30 Hz step (exactly the 30 fps game's
    // step: the half-pass preview before it was taken back in pass_begin), the half pass a preview
    // made from a snapshot (link_preview_begin) with dt = 0.5. Drawn at 60 Hz on both.
    bool link_conv = proc == g_link && dt < 1.0f;  // (also set for the camera's exact step below)
    if (link_conv && !half_pass()) dt = 1.0f;
    bool link_preview = link_conv && half_pass();
    if (link_preview) link_preview_begin(proc);
    // The 60 Hz camera's full passes are exact 30 Hz steps: the half pass is a preview (drawn), and
    // the full pass starts again from the camera as it was before that half pass and takes a whole
    // step. So at full passes the camera is the 30 fps game's camera (what is in view, and so the
    // draw-time random numbers of the scene lighting, stay those of the 30 fps game).
    // WWHD_TRUE60_CAMERA_EXACT=0 keeps two half steps (debug).
    bool cam_preview = false;
    if (g_cam_exact && proc == g_camera && dt < 1.0f) {
        if (half_pass()) {
            g_cam_half.assign(ppc_ptr(proc), ppc_ptr(proc) + kCamSnap);
            g_cam_half_pass = g_pass;
            g_cam_half_proc = proc;
            cam_preview = true;
            region_begin(g_cam_play, kPlayCamLo, kPlayCamHi - kPlayCamLo);
        } else if (g_cam_restored == g_pass) {
            dt = 1.0f;  // pass_begin put the camera back: a whole step
            link_conv = true;  // (drawn as a 60 Hz process: not blended)
        }
    }
    float saved_dt = t_dt;
    uint32_t saved_proc = t_proc;
    t_dt = dt;
    t_proc = proc;
    int32_t cam[kNumCamCounters];
    bool hold_cam = dt < 1.0f && half_pass() && proc == g_camera;
    uint32_t cam_sum = 0;
    if (hold_cam) {
        for (int i = 0; i < kNumCamCounters; i++) cam[i] = read_field(proc + kCamCounters[i].off, kCamCounters[i].bytes);
        cam_sum = ld32(proc + kDCamera + 0x384);
    }
    if (proc == g_camera) g_follow_called = false;
    static std::vector<uint8_t> snap;
    bool cam60 = proc == g_camera;  // every camera step while true 60 is on (also its 30 Hz steps)
    bool nan_before = false;
    if (cam60) {
        snap.resize(kCamSnap);
        memcpy(snap.data(), ppc_ptr(proc), kCamSnap);
        nan_before = cam_has_nan(proc);  // NaN-like bit patterns that are plain data
        g_cam_step = true;
        g_cam_nan = false;
    }
    uint32_t dump_fn = true60_test::dumping() ? actor_execute_fn(proc) : 0;
    if (dump_fn == kDaPyExecute) true60_test::before_execute_link(proc);
    // random numbers (cM_rnd seeds): a half pass does not consume any, so the sequence the 30 Hz
    // processes and the full passes see is the one of the 30 fps game (drops, AI choices). A 60 Hz
    // process drawing on a half pass gets the values its next full step will draw.
    uint32_t rng[3];
    bool hold_rng = dt < 1.0f && half_pass();
    if (hold_rng)
        for (int i = 0; i < 3; i++) rng[i] = ld32(kRndSeeds + 4 * i);
    f_025DF940_orig(c);
    if (link_preview) link_preview_end();
    if (cam_preview) region_end(g_cam_play);
    if (hold_rng)
        for (int i = 0; i < 3; i++) st32(kRndSeeds + 4 * i, rng[i]);
    if (cam60) {
        g_cam_step = false;
        if (g_cam_nan || (!nan_before && cam_has_nan(proc))) {
            static int logged = 0;
            if (logged++ < 20) {
                std::error_code directory_error;
                std::filesystem::create_directory("captures", directory_error);
                if (FILE* f = fopen("captures/true60-nan.log", "a")) {
                    fprintf(f, "pass %llu: 60 Hz camera step produced NaN (sphere %d, caller %08X); restored, 30 Hz for 60 passes\n",
                            (unsigned long long)g_pass, (int)g_cam_nan, g_cam_nan_lr);
                    for (uint32_t o = kDCamera; o < kCamSnap; o += 4) {
                        uint32_t v = ld32(proc + o), b;
                        memcpy(&b, snap.data() + o, 4);
                        b = __builtin_bswap32(b);
                        if ((v & 0x7F800000) == 0x7F800000 && (v & 0x007FFFFF))
                            fprintf(f, "  dCamera+0x%03X: %08X (before %08X = %g)\n", o - kDCamera, v, b, (double)std::bit_cast<float>(b));
                    }
                    // what the camera reads: Link's NaN fields (computed NaNs: 7FC00000 / FFC00000)
                    if (uint32_t l = g_link) {
                        fprintf(f, "  dt %.2f, half pass %d, Link %08X proc %u at 60 Hz %d; Link NaN fields:", dt, (int)half_pass(), l,
                                ld32(l + kCurProc), (int)runs_60(l));
                        int n = 0;
                        for (uint32_t o = 0; o < 0x7000 && n < 40; o += 4) {
                            uint32_t v = ld32(l + o);
                            if (v == 0x7FC00000 || v == 0xFFC00000) fprintf(f, " +0x%X", o), n++;
                        }
                        fprintf(f, "\n");
                    }
                    fclose(f);
                }
                LOG("[true60] camera step produced NaN: restored, camera at 30 Hz for a second (captures/true60-nan.log)");
            }
            memcpy(ppc_ptr(proc), snap.data(), kCamSnap);
            g_cam_fallback = 60;
        }
    }
    if (proc == g_camera) {
        g_cam_follow = g_follow_called;
        trace_camera(proc, dt);
    }
    if (hold_cam) {  // counted on full passes only (also counts going down: the turn counter)
        // followCamera's catch-up move (work at +0x37C, tag 'FLLW'): +0x380 steps (GameCube m37C),
        // +0x384 the remaining sum of step weights, which WWHD decrements by the remaining steps each
        // step (the GameCube keeps the constant total m380). It is per step like m108, so it is held on
        // half passes too; otherwise it runs out while steps remain and the next ratio is x/0 -> NaN
        // (seen leaving Link's house). A restart of the move (m108 dropped) re-initialises it: kept.
        bool restarted = read_field(proc + kDCamera + 0x108, 4) < cam[2];
        if (!restarted && ld32(proc + kDCamera + 0x37C) == 0x464C4C57 && ld32(proc + kDCamera + 0x384) != cam_sum)
            st32(proc + kDCamera + 0x384, cam_sum);
        for (int i = 0; i < kNumCamCounters; i++) {
            uint32_t a = proc + kCamCounters[i].off;
            int32_t v = read_field(a, kCamCounters[i].bytes);
            // step counters only count up: a drop (e.g. m108 1 -> 0 when a camera mode restarts, which
            // makes followCamera initialise its step totals) is a reset and must not be undone, or the
            // follow camera divides by its never-initialised total (0/0 -> NaN after a scene change)
            bool up_only = i < kNumCamStepCounters;
            if (v == cam[i] + 1 || (!up_only && v == cam[i] - 1)) write_field(a, kCamCounters[i].bytes, cam[i]);
        }
    }
    t_dt = saved_dt;
    t_proc = saved_proc;
    if (dump_fn) true60_test::after_execute(proc, dump_fn, dump_fn == kDaPyExecute, dt);
    if (proc == g_link && !half_pass()) g_link_steps++;
    ProcInfo& pi = g_procs[proc];
    pi.pass = g_pass;
    pi.dt = link_conv ? 0.5f : dt;  // (drawn as a 60 Hz process)
    (dt < 1.0f ? g_st.half : g_st.full)++;
    if (proc == g_link) {
        (dt < 1.0f ? g_st.link60 : g_st.link30)++;
        trace_link(proc, dt);
    }
    stats_tick();
}

// ---------------------------------------------------------------------------------------------
// Shared primitives (SSystem/SComponent/c_lib.cpp). With dt = 1 the game's own code runs.
// Float versions: arguments rescaled, then the original runs. Integer versions: native
// re-implementations of the GameCube source with fractional smoothing and split steps.

// f32 cLib_addCalc(f32* v, f32 target, f32 scale, f32 maxStep, f32 minStep)
extern "C" void hook_0200ECD4(Cpu* c) {
    if (t_dt < 1.0f) {
        c->f[2].ps0 = (float)frac(c->f[2].ps0);
        c->f[3].ps0 = (float)(c->f[3].ps0 * t_dt);
        c->f[4].ps0 = (float)(c->f[4].ps0 * t_dt);
    }
    f_0200ECD4_orig(c);
}
// void cLib_addCalc2(f32* v, f32 target, f32 scale, f32 maxStep)
extern "C" void hook_0200ED84(Cpu* c) {
    if (t_dt < 1.0f) {
        c->f[2].ps0 = (float)frac(c->f[2].ps0);
        c->f[3].ps0 = (float)(c->f[3].ps0 * t_dt);
    }
    f_0200ED84_orig(c);
}
// void cLib_addCalc0(f32* v, f32 scale, f32 maxStep)
extern "C" void hook_0200EDC8(Cpu* c) {
    if (t_dt < 1.0f) {
        c->f[1].ps0 = (float)frac(c->f[1].ps0);
        c->f[2].ps0 = (float)(c->f[2].ps0 * t_dt);
    }
    f_0200EDC8_orig(c);
}
// f32 cLib_addCalcPos(cXyz* v, const cXyz& target, f32 scale, f32 maxStep, f32 minStep) (+ XZ)
extern "C" void hook_0200EE00(Cpu* c) {
    if (t_dt < 1.0f) {
        c->f[1].ps0 = (float)frac(c->f[1].ps0);
        c->f[2].ps0 = (float)(c->f[2].ps0 * t_dt);
        c->f[3].ps0 = (float)(c->f[3].ps0 * t_dt);
    }
    f_0200EE00_orig(c);
}
extern "C" void hook_0200EF78(Cpu* c) {
    if (t_dt < 1.0f) {
        c->f[1].ps0 = (float)frac(c->f[1].ps0);
        c->f[2].ps0 = (float)(c->f[2].ps0 * t_dt);
        c->f[3].ps0 = (float)(c->f[3].ps0 * t_dt);
    }
    f_0200EF78_orig(c);
}
// void cLib_addCalcPos2(cXyz* v, const cXyz& target, f32 scale, f32 maxStep) (+ XZ)
extern "C" void hook_0200F164(Cpu* c) {
    if (t_dt < 1.0f) {
        c->f[1].ps0 = (float)frac(c->f[1].ps0);
        c->f[2].ps0 = (float)(c->f[2].ps0 * t_dt);
    }
    f_0200F164_orig(c);
}
extern "C" void hook_0200F268(Cpu* c) {
    if (t_dt < 1.0f) {
        c->f[1].ps0 = (float)frac(c->f[1].ps0);
        c->f[2].ps0 = (float)(c->f[2].ps0 * t_dt);
    }
    f_0200F268_orig(c);
}

// integer smoothing: the original moves by diff/scale (truncated) per step; a half step moves by
// diff * (1-(1-1/scale)^dt), truncated the same way
static int32_t div_step(int32_t diff, int32_t scale) {
    if (scale == 0) return 0;
    if (t_dt >= 1.0f || scale == 1 || scale == -1) return diff / scale;
    double s = 1.0 / (double)scale;
    double f = s > 0 ? frac(s) : -frac(-s);
    return (int32_t)((double)diff * f);  // truncates towards zero like divw
}

// s16 cLib_addCalcAngleS(s16* v, s16 target, s16 scale, s16 maxStep, s16 minStep)
extern "C" void hook_0200F378(Cpu* c) {
    if (t_dt >= 1.0f) return f_0200F378_orig(c);
    uint32_t p = c->r[3];
    int16_t v = (int16_t)ld16(p), target = (int16_t)c->r[4], scale = (int16_t)c->r[5];
    int16_t maxStep = (int16_t)split((int16_t)c->r[6]), minStep = (int16_t)split((int16_t)c->r[7]);
    int16_t diff = (int16_t)(target - v);
    if (v != target) {
        int16_t step = (int16_t)div_step(diff, scale);
        if (step > minStep || step < -minStep) {
            if (step > maxStep) step = maxStep;
            if (step < -maxStep) step = (int16_t)-maxStep;
            v = (int16_t)(v + step);
        } else if (0 <= diff) {
            v = (int16_t)(v + minStep);
            if (0 >= (int16_t)(target - v)) v = target;
        } else {
            v = (int16_t)(v - minStep);
            if (0 <= (int16_t)(target - v)) v = target;
        }
        st16(p, (uint16_t)v);
    }
    c->r[3] = (uint32_t)(int32_t)(int16_t)(target - v);
}
// void cLib_addCalcAngleS2(s16* v, s16 target, s16 scale, s16 maxStep)
extern "C" void hook_0200F428(Cpu* c) {
    if (t_dt >= 1.0f) return f_0200F428_orig(c);
    uint32_t p = c->r[3];
    int16_t v = (int16_t)ld16(p), target = (int16_t)c->r[4], scale = (int16_t)c->r[5];
    int16_t maxStep = (int16_t)split((int16_t)c->r[6]);
    int16_t step = (int16_t)div_step((int16_t)(target - v), scale);
    if (step > maxStep) v = (int16_t)(v + maxStep);
    else if (step < -maxStep) v = (int16_t)(v - maxStep);
    else v = (int16_t)(v + step);
    st16(p, (uint16_t)v);
}
// s32 cLib_addCalcAngleL(s32* v, s32 target, s32 scale, s32 maxStep, s32 minStep)
extern "C" void hook_0200F474(Cpu* c) {
    if (t_dt >= 1.0f) return f_0200F474_orig(c);
    uint32_t p = c->r[3];
    int32_t v = (int32_t)ld32(p), target = (int32_t)c->r[4], scale = (int32_t)c->r[5];
    int32_t maxStep = split((int32_t)c->r[6]), minStep = split((int32_t)c->r[7]);
    int32_t diff = target - v;
    if (v != target) {
        int32_t step = div_step(diff, scale);
        if (step > minStep || step < -minStep) {
            if (step > maxStep) step = maxStep;
            if (step < -maxStep) step = -maxStep;
            v += step;
        } else if (0 <= diff) {
            v += minStep;
            if (0 >= target - v) v = target;
        } else {
            v -= minStep;
            if (0 <= target - v) v = target;
        }
        st32(p, (uint32_t)v);
    }
    c->r[3] = (uint32_t)diff;
}
// chase: a fixed step per step
extern "C" void hook_0200F4FC(Cpu* c) {  // int cLib_chaseUC(u8* v, u8 target, u8 step)
    if (t_dt < 1.0f && (c->r[5] & 0xFF)) {
        int32_t s = split((int32_t)(c->r[5] & 0xFF));
        if (s == 0) {  // nothing on this half step (step 1 on the full pass)
            c->r[3] = ld8(c->r[3]) == (c->r[4] & 0xFF);
            return;
        }
        c->r[5] = (uint32_t)s;
    }
    f_0200F4FC_orig(c);
}
extern "C" void hook_0200F564(Cpu* c) {  // int cLib_chaseS(s16* v, s16 target, s16 step)
    if (t_dt < 1.0f && (int16_t)c->r[5]) {
        int32_t s = split((int16_t)c->r[5]);
        if (s == 0) {
            c->r[3] = (int16_t)ld16(c->r[3]) == (int16_t)c->r[4];
            return;
        }
        c->r[5] = (uint32_t)s;
    }
    f_0200F564_orig(c);
}
extern "C" void hook_0200F5C8(Cpu* c) {  // int cLib_chaseF(f32* v, f32 target, f32 step)
    if (t_dt < 1.0f) c->f[2].ps0 = (float)(c->f[2].ps0 * t_dt);
    f_0200F5C8_orig(c);
}
extern "C" void hook_0200F62C(Cpu* c) {  // int cLib_chasePos(cXyz* v, const cXyz& target, f32 step)
    if (t_dt < 1.0f) c->f[1].ps0 = (float)(c->f[1].ps0 * t_dt);
    f_0200F62C_orig(c);
}
extern "C" void hook_0200F764(Cpu* c) {  // int cLib_chasePosXZ(cXyz* v, const cXyz& target, f32 step)
    if (t_dt < 1.0f) c->f[1].ps0 = (float)(c->f[1].ps0 * t_dt);
    f_0200F764_orig(c);
}
extern "C" void hook_0200F8D0(Cpu* c) {  // int cLib_chaseAngleS(s16* v, s16 target, s16 step)
    if (t_dt < 1.0f && (int16_t)c->r[5]) {
        int32_t s = split((int16_t)c->r[5]);
        if (s == 0) {
            c->r[3] = (int16_t)ld16(c->r[3]) == (int16_t)c->r[4];
            return;
        }
        c->r[5] = (uint32_t)s;
    }
    f_0200F8D0_orig(c);
}

// J3DFrameCtrl::update (WWHD layout, no vtable: rate +0x0, frame +0x4, start s16 +0x8,
// end s16 +0xA, loop s16 +0xC, attribute u8 +0xE, state u8 +0xF): frame += rate * dt. The update
// sets the rate to 0 when a one-shot animation ends; otherwise the real rate is put back.
// Full passes are exact: when the half pass before advanced this frame control and nothing else
// changed it since, the full pass repeats the 30 Hz step from the frame of the last full pass
// (frame + rate, with the original end/loop handling), so the game's frame comparisons (e.g.
// `frame > 16.0f` in procCutL, reached exactly at 30 fps) see the bit-identical 30 fps values.
namespace interp { bool fx_hold_anim(); }
namespace {
struct FcStep { uint64_t pass; uint32_t before, after, rate, range; };
std::unordered_map<uint32_t, FcStep> g_fc;  // frame controls advanced on the last half pass
}  // namespace
extern "C" void hook_027F2FC4(Cpu* c) {
    // drawing code advancing an animation on a hold/half pass: held back (interp_fx.cpp); inside
    // game logic (e.g. Link at 60 Hz) the step is scaled by dt below
    if (interp::fx_hold_anim()) return;
    if (t_dt >= 1.0f) return f_027F2FC4_orig(c);
    uint32_t fc = c->r[3];
    uint32_t rate = ld32(fc), frame = ld32(fc + 4), range = ld32(fc + 8);
    if (!half_pass()) {
        auto it = g_fc.find(fc);
        bool halved = false;  // the half pass before advanced it
        if (it != g_fc.end()) {
            FcStep st = it->second;
            g_fc.erase(it);
            if (st.pass + 1 == g_pass) {
                halved = true;
                if (st.after == frame && st.rate == rate && st.range == range) {
                    st32(fc + 4, st.before);
                    f_027F2FC4_orig(c);  // the whole 30 Hz step
                    return;
                }
            }
        }
        // not advanced on the half pass before (the process ran at 30 Hz there, e.g. Link entering a
        // 60 Hz procedure, or true 60 was just switched on): this pass is a whole step
        if (!halved) return f_027F2FC4_orig(c);
    }
    float scaled = u32_as_f32(rate) * t_dt;
    st32(fc, f32_as_u32(scaled));
    f_027F2FC4_orig(c);
    if (ld32(fc) == f32_as_u32(scaled)) st32(fc, rate);
    if (half_pass()) {
        if (g_fc.size() > 4096) g_fc.clear();
        g_fc[fc] = FcStep{g_pass, frame, ld32(fc + 4), ld32(fc), range};
    }
}

// J3DFrameCtrl::checkPass(f32 frame): true if `frame` lies in [frame, frame + rate), i.e. is passed
// by the next 30 Hz step (key frames for footsteps, voices, effects, item models). With dt < 1 it
// is answered on full passes only, where the frame is the exact 30 fps value (hook_027F2FC4): the
// windows of consecutive full passes tile the animation like at 30 fps, so every key frame is found
// once and at the same step as in the 30 fps game. Half passes find nothing.
extern "C" void f_027F2BF8_orig(Cpu* c);
extern "C" void hook_027F2BF8(Cpu* c) {
    if (t_dt >= 1.0f || !half_pass()) return f_027F2BF8_orig(c);
    c->r[3] = 0;
}

// mDoExt_MtxCalcOldFrame::decOldFrameMorfCounter: blend from the previous pose over a number of
// steps; the counter drops by dt (the blend stays linear in time)
// layout (GameCube = WWHD for this class, checked in the generated code): counter +0x4, 1/morf
// +0x8, rate +0xC, +0x10, +0x14
extern "C" void hook_025E3EC8(Cpu* c) {
    if (t_dt >= 1.0f) return f_025E3EC8_orig(c);
    uint32_t p = c->r[3];
    float cnt = u32_as_f32(ld32(p + 4));
    if (!(cnt > 0.0f)) return;
    cnt -= t_dt;
    if (cnt <= 0.0f) {
        cnt = 0.0f;
        st32(p + 8, f32_as_u32(0.0f));
        st32(p + 0xC, f32_as_u32(0.0f));
    }
    st32(p + 4, f32_as_u32(cnt));
    float f10 = u32_as_f32(ld32(p + 0x10));
    st32(p + 0x14, f32_as_u32(f10));
    float n10 = cnt * u32_as_f32(ld32(p + 8));
    st32(p + 0x10, f32_as_u32(n10));
    st32(p + 0xC, f32_as_u32(f10 > 0.0f ? 1.0f - (f10 - n10) / f10 : 0.0f));
}

// fopAcM_calcSpeed(actor): xz speed from speedF and the angle; speed.y += gravity (clamped).
// With dt < 1 gravity is applied for the shorter step. The change of speed.y is remembered for the
// actor's following fopAcM_posMove (exact-arc term).
static thread_local uint32_t t_dv_actor = 0;
static thread_local float t_dv = 0.0f;
extern "C" void hook_025D67A8(Cpu* c) {
    if (t_dt >= 1.0f) return f_025D67A8_orig(c);
    uint32_t a = c->r[3];
    float vy = u32_as_f32(ld32(a + kSpeed + 4));
    float g = u32_as_f32(ld32(a + kGravity));
    f_025D67A8_orig(c);  // runs with the full gravity, then corrected
    float full = u32_as_f32(ld32(a + kSpeed + 4));
    float maxf = u32_as_f32(ld32(a + kMaxFall));
    float half = vy + g * t_dt;
    if (half < maxf) half = maxf;
    if (full != vy + g && full != maxf) return;  // not the expected form: leave it
    st32(a + kSpeed + 4, f32_as_u32(half));
    t_dv_actor = a;
    t_dv = half - vy;
}

// fopAcM_posMove(actor, const cXyz* ccMove): pos += speed * dt (+ the collision push, unscaled).
// For a falling actor the exact discrete path of the 30 Hz game (v += g; p += v) at half steps needs
// p += v*dt + dv*(1-dt)/2 with dv the gravity just applied (docs/decomp-notes.md, "True 60 fps"),
// so jump arcs match the original at full steps.
extern "C" void hook_025D6800(Cpu* c) {
    if (t_dt >= 1.0f) return f_025D6800_orig(c);
    uint32_t a = c->r[3], mv = c->r[4];
    // exact-arc term only right after this actor's own fopAcM_calcSpeed (dv = gravity applied)
    float corr = t_dv_actor == a ? t_dv * (1.0f - t_dt) * 0.5f : 0.0f;
    t_dv_actor = 0;
    for (int i = 0; i < 3; i++) {
        float p = u32_as_f32(ld32(a + kPos + 4 * i)) + u32_as_f32(ld32(a + kSpeed + 4 * i)) * t_dt + (i == 1 ? corr : 0.0f);
        if (mv) p += u32_as_f32(ld32(mv + 4 * i));
        st32(a + kPos + 4 * i, f32_as_u32(p));
    }
}

// fpcM_Draw(process): remembers which process is drawing (nested: the play scene draws the actors),
// so interpolation leaves the models of 60 Hz processes alone
static uint32_t g_draw_stack[8];
static int g_draw_depth = 0;
static int g_force_draw_60 = 0;  // drawing that follows 60 Hz state although its process is 30 Hz
void true60::force_draw_60(bool on) { g_force_draw_60 += on ? 1 : -1; }
bool true60::drawing_60() {
    if (enabled() && g_force_draw_60 > 0) return true;
    return enabled() && g_draw_depth > 0 && g_draw_depth <= 8 && runs_60(g_draw_stack[g_draw_depth - 1]);
}
// An actor's draw on a half pass leaves no state behind in a 30 Hz actor: its draw also steps
// draw-side state in the actor (its tevStr: the environment light it is lit with, faded per draw,
// which decides e.g. whether point-light flicker is drawn for it, with a random number each time).
// At 60 fps that ran twice per step; with true 60 the words an actor's half-pass draw changed in
// its own memory (allocation size from its heap block header, proc-8) are put back right after it.
// WWHD_TRUE60_DRAW_KEEP=1 turns this off (debug).
extern "C" void hook_025DF904(Cpu* c) {
    uint32_t proc = c->r[3];
    if (g_draw_depth < 8) g_draw_stack[g_draw_depth] = proc;
    g_draw_depth++;
    uint32_t wv = watch_val();
    static const uint32_t dlp = getenv("WWHD_DRAWLOG_PROC") ? (uint32_t)strtoul(getenv("WWHD_DRAWLOG_PROC"), nullptr, 16) : 0;
    if (dlp && proc == dlp) {  // debug: draws of one process, with a word of it before (WWHD_DRAWLOG_OFF)
        static const uint32_t off = getenv("WWHD_DRAWLOG_OFF") ? (uint32_t)strtoul(getenv("WWHD_DRAWLOG_OFF"), nullptr, 16) : 0;
        LOG("[drawlog] step %llu %s depth %d word %08X exec %08X", (unsigned long long)interp::logic_steps(), half_pass() ? "half" : "full", g_draw_depth, ld32(proc + off), actor_execute_fn(proc));
        static FILE* df = getenv("WWHD_DRAWLOG_DUMP") ? fopen(getenv("WWHD_DRAWLOG_DUMP"), "wb") : nullptr;
        if (df && !half_pass()) {  // the process and g_env_light at its draw, as an ADMP record of 0x400 + 0x2000 bytes
            static uint32_t rlo = getenv("WWHD_DRAWLOG_RANGE") ? (uint32_t)strtoul(getenv("WWHD_DRAWLOG_RANGE"), nullptr, 16) : GD(0x10475A68);
            static uint32_t rsz = getenv("WWHD_DRAWLOG_RANGE") && strchr(getenv("WWHD_DRAWLOG_RANGE"), ':') ? (uint32_t)strtoul(strchr(getenv("WWHD_DRAWLOG_RANGE"), ':') + 1, nullptr, 16) : 0x2000;
            uint64_t step = interp::logic_steps(); uint32_t full = 1, size = 0x400 + rsz; float dtv = 1.0f;
            fwrite("ADMP", 1, 4, df); fwrite(&step, 8, 1, df); fwrite(&full, 4, 1, df); fwrite(&dtv, 4, 1, df);
            fwrite(&proc, 4, 1, df); fwrite(&size, 4, 1, df); fwrite(mem::ptr(proc), 1, 0x400, df); fwrite(mem::ptr(rlo), 1, rsz, df);
            fflush(df);
        }
    }
    static const bool keep = getenv("WWHD_TRUE60_DRAW_KEEP") != nullptr;
    static std::vector<uint32_t> before;
    uint32_t size = 0, cull_mtx = 0;
    uint8_t cull_before[0x30];
    if (enabled() && half_pass() && !keep && g_draw_depth >= 2 && !runs_60(proc) && actor_execute_fn(proc)) {
        size = ld32(proc - 8) & ~3u;
        if (size < 0x100 || size > 0x20000) size = 0;
        if (size) before.assign((uint32_t*)ppc_ptr(proc), (uint32_t*)ppc_ptr(proc + size));
        // its culling matrix (fopAc_ac_c +0x348, the model's base matrix: fopAcM_cullingCheck 025D6D08),
        // which some draws set (spinning rupees): the next full pass culls with the previous step's
        cull_mtx = ld32(proc + 0x348);
        if (cull_mtx >= mem::kMem2Start && cull_mtx < mem::kMem2End && (cull_mtx & 3) == 0)
            memcpy(cull_before, ppc_ptr(cull_mtx), 0x30);
        else
            cull_mtx = 0;
    }
    static const bool envlog = getenv("WWHD_T60_ENVLOG") != nullptr;  // debug: env-light words Link's half-pass draw/execute changes
    static std::vector<uint32_t> env_before;
    bool envdbg = envlog && proc == g_link && half_pass();
    if (envdbg) env_before.assign((uint32_t*)ppc_ptr(GD(0x10475A68)), (uint32_t*)ppc_ptr(GD(0x10475A68) + 0x2000));
    // debug: WWHD_T60_DRAWWRITE=1 logs static words (1046F0B0..104C3000) that half-pass actor draws change, with counts
    static const bool dwlog = getenv("WWHD_T60_DRAWWRITE") != nullptr;
    static std::vector<uint32_t> dw_before;
    bool dw = dwlog && half_pass() && g_draw_depth >= 2;
    if (dw) dw_before.assign((uint32_t*)ppc_ptr(GD(0x1046F0B0)), (uint32_t*)ppc_ptr(GD(0x104C3000)));
    f_025DF904_orig(c);
    if (dw) {
        static std::unordered_map<uint32_t, int> cnt;
        static std::unordered_map<uint32_t, uint32_t> who;
        static int n = 0;
        const uint32_t* cur = (const uint32_t*)ppc_ptr(GD(0x1046F0B0));
        for (size_t i = 0; i < dw_before.size(); i++)
            if (cur[i] != dw_before[i]) { uint32_t a = GD(0x1046F0B0) + 4 * (uint32_t)i; cnt[a]++; who[a] = actor_execute_fn(proc); }
        if (++n % 3000 == 0) {
            std::vector<std::pair<uint32_t, int>> v(cnt.begin(), cnt.end());
            std::sort(v.begin(), v.end());
            std::string o;
            for (auto& [a, k] : v) { char t[40]; snprintf(t, sizeof t, " %08X:%d:%08X", a, k, who[a]); o += t; }
            LOG("[drawwrite]%s", o.c_str());
        }
    }
    if (envdbg) {
        std::string o;
        const uint32_t* cur = (const uint32_t*)ppc_ptr(GD(0x10475A68));
        for (size_t i = 0; i < env_before.size(); i++)
            if (cur[i] != env_before[i]) { char t[16]; snprintf(t, sizeof t, " +%zX", 4 * i); o += t; }
        static int n = 0;
        if (n++ < 20) LOG("[envlog] Link draw changed g_env_light:%s", o.c_str());
    }
    if (size) {
        uint32_t* cur = (uint32_t*)ppc_ptr(proc);
        // J3D packets inside the actor that its draw entered into a draw buffer (J3DDrawBuffer::entry*:
        // packet +0x94 = the bucket slot, +0x10 = the next packet) stay entered: the buffer lists them
        // until its frameInit, which asserts that each listed packet points back at its slot
        auto entered_packet = [&](uint32_t i) {
            if (i * 4 < 0x94) return false;
            uint32_t slot = __builtin_bswap32(cur[i]);
            if (slot < mem::kMem2Start || slot >= mem::kMem2End || (slot & 3)) return false;
            uint32_t pkt = proc + 4 * i - 0x94;
            uint32_t q = ld32(slot);
            for (int n = 0; q && n < 4096; n++) {
                if (q == pkt) return true;
                if (q < mem::kMem2Start || q >= mem::kMem2End || (q & 3)) return false;
                q = ld32(q + 0x10);
            }
            return false;
        };
        static std::vector<uint32_t> keep;  // word indexes not taken back: entered packets' +0x94 and +0x10
        keep.clear();
        for (uint32_t i = 0x94 / 4; i < size / 4; i++)
            if (cur[i] != before[i] && entered_packet(i)) {
                keep.push_back(i);
                keep.push_back(i - (0x94 - 0x10) / 4);
            }
        for (uint32_t i = 0; i < size / 4; i++)
            if (cur[i] != before[i] && std::find(keep.begin(), keep.end(), i) == keep.end()) cur[i] = before[i];
        if (cull_mtx) memcpy(ppc_ptr(cull_mtx), cull_before, 0x30);
    }
    watch_link("draw", proc, wv);
    g_draw_depth--;
}

// dCamera_c::followCamera: marks the camera's execute as using the follow camera (true60 camera)
extern "C" void hook_025028B8(Cpu* c) {
    g_follow_called = true;
    f_025028B8_orig(c);
}

// JPAEmitterManager::calc (particle step). The play scene steps its particles from its draw
// (dScnPly_Draw -> dComIfGp_particle_calc3D/2D/Menu), which runs on every pass in both 60 fps
// modes: measured 762-765 updates/s instead of 390, so particles ran at double speed (also with
// frame interpolation). They step on full passes only (30 Hz particles, not converted yet).
// debug: WWHD_PARTICLES_EVERY_PASS=1 restores the old behaviour.
namespace interp { bool enabled(); }
// (hook_0282167C lives in interp_fx.cpp: held back on hold/half passes there, and blended)

// The play scene's draw (dScnPly_Draw 025AF8A0) also steps world systems, inside
// `if (!dMenu_flag() && pauseTimer == 0) { ... }`: vibration, ice, magma, grass/tree/wood/flower
// (sway and reactions), poison light, moving collision (dBgS::Move), snapshots, particles,
// cCt_execCounter (g_Counter.mTimer++). Draws run on every pass in both 60 fps modes, so these ran
// twice per step. On half passes the condition is made false (site before the `cmpwi r3, 0` that
// tests the pause check's result, 025B00B0), and the else branch's dVibration_c::Pause is skipped.
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

// dBgS::MoveBgCrrPos(poly, ground hit, cXyz* pos, csXyz* angle, csXyz* shape angle): carries an actor
// standing on moving collision. When it moves Link (from his execute, pos = &current.pos), Link
// counts as riding for the next passes (30 Hz with the platform; see classify).
extern "C" void hook_024EF968(Cpu* c) {
    uint32_t pos = c->r[6];
    bool link = g_link && pos == g_link + kPos;
    float before[3] = {0, 0, 0};
    if (link)
        for (int i = 0; i < 3; i++) before[i] = u32_as_f32(ld32(pos + 4 * i));
    f_024EF968_orig(c);
    if (link)
        for (int i = 0; i < 3; i++)
            if (u32_as_f32(ld32(pos + 4 * i)) != before[i]) {
                if (g_link_ride == 0 && enabled()) LOG("[true60] Link rides moving collision: 30 Hz");
                g_link_ride = 8;
                break;
            }
}

// cM3dGSph::SetC(this, const cXyz&): inside a 60 Hz camera step a NaN centre is skipped instead of
// halting the game; the step is then undone by the camera safety net (hook_025DF940)
extern "C" void f_02018D40_orig(Cpu* c);
extern "C" void hook_02018D40(Cpu* c) {
    if (g_cam_step) {
        uint32_t p = c->r[4];
        if (std::isnan(ldf32(p)) || std::isnan(ldf32(p + 4)) || std::isnan(ldf32(p + 8))) {
            if (!g_cam_nan) g_cam_nan_lr = c->lr;
            g_cam_nan = true;
            return;
        }
    }
    f_02018D40_orig(c);
}

// debug: WWHD_NAN_PROBE=1 (with WWHD_TRUE60_CAMERA=1): during each camera step, checks the camera's
// first state vectors (dCamera +0x10..+0x60) at every guest function entry and logs the functions
// entered just before the first NaN appears (the one that produced it is among the last entries)
void true60_nan_probe(uint32_t addr) {
    static uint32_t hist[64];
    static uint32_t n = 0;
    static int reported = 0;
    static uint64_t step_seen = ~0ull;
    if (!g_cam_step || reported >= 3) return;
    uint32_t cam = g_camera + kDCamera;
    if (step_seen != g_pass) step_seen = g_pass, n = 0;
    Cpu* c = threads::current();
    hist[n++ & 63] = addr;
    if (n < 64 || (n & 63) == 0) {}  // keep filling
    bool nan = false;
    for (uint32_t o = 0x10; o <= 0x60 && !nan; o += 4) nan = std::isnan(ldf32(cam + o));
    static bool was_nan = false;
    if (nan && !was_nan) {
        reported++;
        LOG("[nanprobe] pass %llu dt %.2f: camera state NaN at entry of %08X (lr %08X); last entries:", (unsigned long long)g_pass, t_dt, addr,
            c ? c->lr : 0);
        for (uint32_t i = n > 40 ? n - 40 : 0; i < n; i++) LOG("[nanprobe]   %08X", hist[i & 63]);
        for (uint32_t o = 0x10; o <= 0x60; o += 4) if (std::isnan(ldf32(cam + o))) LOG("[nanprobe]   NaN at dCamera+0x%02X", o);
    }
    was_nan = nan;
}

// The HUD (d_meter) updates its state from the play scene's draw (02593B10, the per-step part of
// the meter: button labels, gauges, the magic gauge sparkle with its random numbers), so at 60 fps
// it stepped twice per logic step. With true 60 the update runs on full passes only (half passes
// still draw it): HUD timing and its use of the shared random numbers are those of the 30 fps game.
extern "C" void f_02593B10_orig(Cpu* c);
extern "C" void hook_02593B10(Cpu* c) {
    static const bool every = getenv("WWHD_HUD_EVERY_PASS") != nullptr;  // debug: old behaviour
    if (enabled() && half_pass() && !every) return;
    f_02593B10_orig(c);
}
