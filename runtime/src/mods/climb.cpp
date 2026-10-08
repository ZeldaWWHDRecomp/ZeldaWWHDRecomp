// "Climb any wall" (Gameplay menu, off by default; WWHD_CLIMB=1 starts with it on).
//
// Link climbs any steep wall with the game's own ivy climbing (d_a_player_climb.inc): grab, climb
// up/down/sideways, climb onto the ledge at the top (procHangClimb), let go with A. Nothing new is
// animated: the mod only makes the game's wall queries see an ordinary steep wall as ivy.
//
// How (GameCube source: tww/src/d/actor/d_a_player_main.cpp, d_a_player_climb.inc):
// - Ivy is wall code 1 (dBgS::GetWallCode, 024EF080). Link checks it in two places:
//   daPy_lk_c::setFrontWallType (023E3850; what Link does when he walks or falls into a wall) and
//   daPy_lk_c::setMoveBGCorrectClimb (02429408; every climbing step: still on climbable wall?).
// - Both also require an (almost) vertical wall: |normal.y| <= 0.05 (fcmpu at 023E39EC and
//   02429570). The site hooks below let walls with -0.1 <= normal.y < 0.5 (overhangs up to ~6°,
//   leaning back up to 30°) pass while the mod converts a query.
// - setFrontWallType runs unchanged first. Only when the game found nothing to do with the wall
//   (type 1: a plain wall too tall for the ledge actions; type 9: too tall for a jump-grab) is it
//   run again with plain walls reported as ivy. Small ledges keep their own actions (climb up,
//   jump-grab, hang), as do ladders (codes 4/5), push blocks (3), "no grab" walls (2) and real ivy.
// - Stamina (BotW-style wheel, HUD in climb_hud.mm) drains while Link climbs a converted wall
//   (a quarter as fast while he hangs still) and refills on solid ground. When it runs out, or
//   while B is held, setMoveBGCorrectClimb sees the wall as plain again, so the game's own code
//   lets go (procFall_init; A lets go as on ivy). After letting go of a converted wall Link can't
//   grab one again until he has landed or swims (and 0.5 s passed; 2 s after a hold that failed
//   at once), so he doesn't grab and drop in a loop. Out of stamina: no new grabs until it is full.
// - At the top, checkBgClimbMove looks for the ledge 25 units in; on converted walls (rounded or
//   leaning-back rock tops) the search continues further in (site_02429C04), so Link climbs onto
//   the ledge (procHangClimb) instead of dropping from the top.
// - Real ivy behaves exactly as before (no stamina). With the mod off every hook calls the
//   original code and nothing else.
// - True 60 (key 7): the climbing procedures (0x3D-0x40) are not in true60's 60 Hz list, so they
//   stay at 30 Hz; stamina is counted per original step (true60::dt()).
#include "guest_addr.h"
#include "mods/climb.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>

#include "input.h"
#include "runtime.h"
#include "true60.h"

extern "C" {
void f_023E3850_orig(Cpu* c);  // daPy_lk_c::setFrontWallType
void f_02429408_orig(Cpu* c);  // daPy_lk_c::setMoveBGCorrectClimb
void f_024EF080_orig(Cpu* c);  // dBgS::GetWallCode
void f_0240CDD0_orig(Cpu* c);  // daPy_lk_c::execute
}

namespace {
// daPy_lk_c fields (WWHD offsets; docs/decomp-notes.md)
constexpr uint32_t kCurProc = 0x65F0;         // mCurProc (GameCube 0x31D8)
constexpr uint32_t kFrontWallType = 0x68D5;   // mFrontWallType (GameCube 0x34B9)
constexpr uint32_t kModeFlg = 0x6A70;         // mModeFlg (GameCube 0x3618)
constexpr uint32_t kModeMidair = 0x2;         // ModeFlg_MIDAIR
constexpr uint32_t kModeSwim = 0x40000;       // ModeFlg_SWIM
constexpr uint32_t kFrameRate = 0x5898;       // mFrameCtrlUnder[0].mRate
constexpr uint32_t kPos = 0x314;              // current.pos

// daPy_lk_c::daPy_PROC
constexpr int kProcFall = 0x27, kProcHangStart = 0x2B, kProcHangWallCatch = 0x31, kProcHangClimb = 0x30;
constexpr int kProcClimbFirst = 0x3D, kProcClimbLast = 0x40;  // ClimbUpStart, ClimbDownStart, ClimbMoveUpDown, ClimbMoveSide
bool is_climb(int p) { return p >= kProcClimbFirst && p <= kProcClimbLast; }
bool is_hang(int p) { return p >= kProcHangStart && p <= kProcHangWallCatch; }

// accepted wall steepness while converting (normal.y; the game's own limit is |y| <= 0.05)
constexpr float kMinNy = -0.10f, kMaxNy = 0.50f;

std::atomic<bool> g_on{[] {
    const char* e = getenv("WWHD_CLIMB");
    return e && atoi(e) != 0;
}()};

// stamina: seconds of climbing on a full wheel (WWHD_CLIMB_STAMINA), per 30 Hz step
float climb_seconds() {
    static const float s = [] {
        const char* e = getenv("WWHD_CLIMB_STAMINA");
        float v = e ? (float)atof(e) : 0.0f;
        return v > 0 ? v : 12.0f;
    }();
    return s;
}
constexpr float kIdleDrain = 0.25f;        // hanging still drains at a quarter of the rate
constexpr float kRefillSeconds = 2.5f;     // empty to full on the ground
constexpr float kRefillDelay = 0.3f;       // seconds on the ground before refilling starts

// game-thread state (Link's execute; all hooks run on the game thread)
bool g_convert = false;     // inside a converting setFrontWallType / setMoveBGCorrectClimb
bool g_converted = false;   // ... and GetWallCode reported a plain wall as ivy
bool g_fake = false;        // Link climbs a converted (not real ivy) wall
bool g_block = false;       // let go of a converted wall: no new grab until he has landed ...
float g_block_time = 0;     // ... and this many seconds have passed
float g_climb_time = 0;     // seconds since the grab
int g_prev_proc = -1;
float g_ground_time = 0;    // seconds on the ground since the last climb
float g_hud_hold = 0;       // seconds the full wheel stays visible
float g_y0 = 0;             // height where the current climb started (log)

std::atomic<float> g_stamina{1.0f};
std::atomic<float> g_alpha{0.0f};
std::atomic<bool> g_exhausted{false};

// debug (WWHD_CLIMB_DEBUG=1): why a climbing step let go
int g_dbg_code = -1;
float g_dbg_ny = 99;
bool debug() {
    static const bool d = getenv("WWHD_CLIMB_DEBUG") != nullptr;
    return d;
}

bool b_held() { return (input::read().buttons & input::kB) != 0; }
}  // namespace

namespace mods {
bool climb_enabled() { return g_on.load(std::memory_order_relaxed); }
void set_climb_enabled(bool on) {
    g_on = on;
    LOG("[climb] climb any wall %s", on ? "on" : "off");
}
ClimbHud climb_hud() { return {g_stamina.load(), g_alpha.load(), g_exhausted.load()}; }
}  // namespace mods

// ---- dBgS::GetWallCode(cBgS_PolyInfo&) (024EF080): a plain wall reads as ivy (code 1) while
// one of the wrappers below converts. Ivy (1), "no grab" (2), push blocks (3) and ladders (4/5)
// keep their code.
extern "C" void hook_024EF080(Cpu* c) {
    f_024EF080_orig(c);
    if (!g_convert) return;
    uint32_t code = c->r[3];
    g_dbg_code = (int)code;
    if (code == 0 || code > 5) {
        c->r[3] = 1;
        g_converted = true;
    }
}

// the wall-steepness compares (f = |normal.y| against 0.05): a steep enough wall passes as vertical
static void relax(Cpu* c, int freg, uint32_t plane) {
    if (!g_convert) return;
    float ny = u32_as_f32(ld32(plane + 4));
    g_dbg_ny = ny;
    if (ny >= kMinNy && ny < kMaxNy) c->f[freg].ps0 = 0.0;
}
// setFrontWallType: `if (std::abs(wall_plane->GetNP()->y) > 0.05f) return;` f6 = |y|, r28 = plane
extern "C" void site_023E39EC(Cpu* c) { relax(c, 6, c->r[28]); }
// setMoveBGCorrectClimb: `std::fabsf(GetTriPla(mLinkLinChk)->GetNP()->y) > 0.05f` f8 = |y|, r3 = plane
extern "C" void site_02429570(Cpu* c) { relax(c, 8, c->r[3]); }

// ---- daPy_lk_c::checkBgClimbMove (02429950), top of the wall: the line 125 units above Link's feet
// found no more wall, so the game looks for ground 25 units further in, between 95 and 155 units
// above his feet (procHangClimb_init: climb onto the ledge), else he lets go (procFall_init). Rock
// tops are often rounded or leaning back, and there the ground 25 units in is too low and Link
// would drop from the very top. On a converted wall the search continues further in (40, 60 and
// 80 units) and accepts ground from 40 units above his feet. Runs right after the game's own
// GroundCross (f1 = ground height; mGndChk at +0xB14; the probe point on the stack at sp+8/sp+0x10,
// sp+0x18 = feet + 125).
namespace {
constexpr uint32_t kGndChk = 0xB14;         // daPy_lk_c::mGndChk (dBgS_GndChk): position at +0x24
constexpr uint32_t kGndPoly = 0xB28;        // ... its poly info: bg index u16 +0, poly index u16 +2
const uint32_t kBgsAccess = GC(0x025200D4); // returns the game info; + 0x12A0 = dComIfG_Bgsp()
const uint32_t kGroundCross = GC(0x02008974), kGetTriPla = GC(0x020084C8);

bool ground_ok(Cpu* c, uint32_t self, uint32_t bgs, float g, float min_y) {
    if (!(g > -1e30f) || g < min_y) return false;
    uint32_t pla = guest_call(c, kGetTriPla, {bgs, ld16(self + kGndPoly + 2), ld16(self + kGndPoly)});
    return pla && u32_as_f32(ld32(pla + 4)) >= 0.5f;  // cBgW_CheckBGround
}
}  // namespace

extern "C" void site_02429C04(Cpu* c) {
    if (!g_on || !g_fake) return;
    const uint32_t self = c->r[30], sp = c->r[1];
    const float feet = u32_as_f32(ld32(self + kPos + 4));
    const float top = u32_as_f32(ld32(sp + 0x18));  // feet + 125
    const float s = (float)c->f[28].ps0, co = (float)c->f[29].ps0;  // sin / cos of shape_angle.y
    const Cpu saved = *c;
    uint32_t bgs = guest_call(c, kBgsAccess) + 0x12A0;
    if (ground_ok(c, self, bgs, (float)saved.f[1].ps0, top - 30.0f)) {  // the game's own check passes
        *c = saved;
        return;
    }
    const float px = u32_as_f32(ld32(self + kPos)), pz = u32_as_f32(ld32(self + kPos + 8));
    for (float d : {40.0f, 60.0f, 80.0f}) {
        float x = px + d * s, z = pz + d * co, y = top + 30.0f;
        st32(self + kGndChk + 0x24, f32_as_u32(x));
        st32(self + kGndChk + 0x28, f32_as_u32(y));
        st32(self + kGndChk + 0x2C, f32_as_u32(z));
        guest_call(c, kGroundCross, {bgs, self + kGndChk});
        float g = (float)c->f[1].ps0;
        if (ground_ok(c, self, bgs, g, feet + 40.0f)) {
            *c = saved;
            st32(sp + 0x8, f32_as_u32(x));
            st32(sp + 0x10, f32_as_u32(z));
            if (g < top - 30.0f) st32(sp + 0x18, f32_as_u32(g + 30.0f));  // passes the game's height check
            c->f[1].ps0 = c->f[1].ps1 = g;
            if (debug()) LOG("[climb] debug: ledge found %.0f units in, %.1f above the feet", d, g - feet);
            return;
        }
    }
    if (debug()) LOG("[climb] debug: no ledge at the top (game's probe: %.1f above the feet)", (float)saved.f[1].ps0 - feet);
    *c = saved;  // the game lets go
}

// ---- daPy_lk_c::setFrontWallType (023E3850): the game's pass first; plain walls as ivy only if
// it found nothing to do with the wall ----
extern "C" void hook_023E3850(Cpu* c) {
    uint32_t self = c->r[3];
    f_023E3850_orig(c);
    if (!g_on || g_block || g_exhausted) return;
    uint8_t type = ld8(self + kFrontWallType);
    if (type != 1 && type != 9) return;
    st8(self + kFrontWallType, 0);  // the function returns at once unless this is 0
    c->r[3] = self;
    g_convert = true;
    g_converted = false;
    f_023E3850_orig(c);
    g_convert = false;
    uint8_t now = ld8(self + kFrontWallType);
    // 3: climb (from the ground or mid-air); 7 from mid-air: hang from the ledge just above.
    // Anything else: the game's own result stands.
    if (!g_converted || (now != 3 && now != 7)) st8(self + kFrontWallType, type);
    else if (now == 3) g_fake = true;  // the climbing procedure checks the wall from its next step on
}

// ---- daPy_lk_c::setMoveBGCorrectClimb (02429408): every climbing step; keeps Link on the wall
// or lets go (procFall_init) when the wall is no longer climbable ----
extern "C" void hook_02429408(Cpu* c) {
    if (!g_on) {
        g_fake = false;
        f_02429408_orig(c);
        return;
    }
    // out of stamina or B held: the wall reads as plain again and the game lets go
    bool convert = !g_exhausted && !b_held();
    uint32_t self = c->r[3];
    g_convert = convert;
    g_converted = false;
    g_dbg_code = -1;
    g_dbg_ny = 99;
    f_02429408_orig(c);
    g_convert = false;
    if (convert) g_fake = g_converted;
    if (debug() && (int)ld32(self + kCurProc) == kProcFall)
        LOG("[climb] debug: let go: wall code %d, normal.y %.3f, y %.1f, ground %.1f, angle %d", g_dbg_code, g_dbg_ny,
            u32_as_f32(ld32(self + kPos + 4)), u32_as_f32(ld32(self + 0x8A0)), (int)(int16_t)ld16(self + 0x32A));
}

// ---- daPy_lk_c::execute (0240CDD0): stamina, re-grab block, HUD state ----
extern "C" void hook_0240CDD0(Cpu* c) {
    uint32_t self = c->r[3];
    f_0240CDD0_orig(c);
    if (!g_on) {
        g_stamina = 1.0f;
        g_alpha = 0.0f;
        g_exhausted = false;
        g_fake = g_block = false;
        g_prev_proc = -1;
        return;
    }
    const float dt = true60::dt();  // 1 = one 30 Hz step (climbing always runs at 30 Hz)
    const float step = dt / 30.0f;  // seconds
    int proc = (int)ld32(self + kCurProc);
    uint32_t mode = ld32(self + kModeFlg);
    bool midair = (mode & kModeMidair) != 0, swim = (mode & kModeSwim) != 0;
    bool climbing = is_climb(proc);
    float y = u32_as_f32(ld32(self + kPos + 4));
    float st = g_stamina;

    if (climbing && !is_climb(g_prev_proc)) {
        g_y0 = y;
        g_climb_time = 0;
        LOG("[climb] grab (proc %#x, %s wall) at y=%.1f, stamina %.2f", proc, g_fake ? "plain" : "ivy", y, st);
    }
    if (climbing) g_climb_time += step;
    if (climbing && g_fake) {
        bool moving = std::fabs(u32_as_f32(ld32(self + kFrameRate))) > 0.01f;
        st -= step / climb_seconds() * (moving ? 1.0f : kIdleDrain);
        if (st <= 0) {
            st = 0;
            if (!g_exhausted) LOG("[climb] out of stamina at y=%.1f (+%.1f)", y, y - g_y0);
            g_exhausted = true;
        }
        g_ground_time = 0;
        g_hud_hold = 1.0f;
    }
    if (!climbing && is_climb(g_prev_proc)) {
        const char* how = proc == kProcHangClimb ? "climbed onto the ledge"
                          : proc == kProcFall      ? (g_exhausted ? "let go (no stamina)" : "let go")
                                                   : "left the wall";
        LOG("[climb] %s (proc %#x) at y=%.1f (+%.1f), stamina %.2f", how, proc, y, y - g_y0, st);
        // no instant re-grab after letting go of a plain wall: not before landing (or swimming), and
        // after a hold that failed at once (the wall below the grab point is not climbable, e.g. a
        // rock base under water) not for 2 s, so Link doesn't grab and drop in a loop
        if (g_fake && proc != kProcHangClimb) {
            g_block = true;
            g_block_time = g_climb_time < 0.2f ? 2.0f : 0.5f;
        }
        g_fake = false;
    }
    if (g_block_time > 0) g_block_time -= step;
    if (!climbing && !midair && !is_hang(proc)) {  // landed (or swimming): may grab again
        if (g_block_time <= 0) g_block = false;
    }
    if (!climbing && !midair && !swim && !is_hang(proc)) {  // on solid ground: refill
        g_ground_time += step;
        if (g_ground_time >= kRefillDelay && st < 1.0f) {
            st += step / kRefillSeconds;
            if (st >= 1.0f) {
                st = 1.0f;
                g_exhausted = false;
            }
        }
    }
    g_stamina = st;

    // HUD: shown while climbing a plain wall or refilling, fades out a second after it is full
    float a = g_alpha;
    bool show = (climbing && g_fake) || st < 1.0f;
    if (show) g_hud_hold = 1.0f;
    else if (g_hud_hold > 0) g_hud_hold -= step;
    float target = (show || g_hud_hold > 0) ? 1.0f : 0.0f;
    a += (target - a) * std::min(1.0f, step * 8.0f);
    if (target == 0 && a < 0.01f) a = 0;
    g_alpha = a;
    g_prev_proc = proc;
}
