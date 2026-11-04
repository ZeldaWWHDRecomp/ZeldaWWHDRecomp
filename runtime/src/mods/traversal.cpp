// Traversal mods (Gameplay menu, all off by default): faster ladders and sidling (#95), sailing
// speed per sail and automatic wind with the normal sail (#96). Settings: traversal_settings.cpp;
// the maths: traversal.h (traversal_test). GameCube source: tww/src/d/actor/d_a_player_ladder.inc,
// d_a_player_whide.inc, d_a_ship.cpp; the Swift Sail is HD-only and read from the WWHD code.
//
// Ladders: the rung, mount and dismount clips move Link through their root motion (posMove with
//   m34C2 = 5), so playing them faster climbs faster with hands and feet in step. setFrameCtrl
//   (mods.cpp's hook) scales the rate of every clip started in a ladder procedure (0x38..0x3C);
//   procLadderMove (02427CA8) rewrites the rung clip's rate from the stick every step, so that
//   write is scaled here. Sounds come from the clips' frames (setLadderFootSe when a rung clip ends,
//   checkPass on the mount clips), so they keep their place in the motion. The procedures' own
//   end checks (|rate| < 0.01, LineCross for the ladder's end) are untouched. The stock procedure
//   rests one step between rungs (the footstep, then the next rung on the next step); with the mod
//   on the next rung starts in the footstep's step (the game's own changeLadderMoveProc, called
//   right after the procedure), so the climb speeds up by about the factor (traversal.h).
// Ivy and ledge hanging: their procedures ask getLadderMoveAnmSpeed (ivy up / down),
//   getClimbMoveAnmSpeed (ivy sideways) and getHangMoveAnmSpeed (hanging) for the clip rate on every
//   step and in their _init; the clips move Link, so the hooks scale the returned rate (traversal.h).
// Sidling: speed = mNormalSpeed * (1 - m3598) + foot motion * m3598 (posMoveFromFootPos). Both
//   parts grow by the factor: the walk clip plays faster (setFrameCtrl in PROC_WHIDE_MOVE) and the
//   speed target is scaled by raising mMaxNormalSpeed for the length of setNormalSpeedF and
//   setBlendWHideMoveAnime (whose blend reads mNormalSpeed / mMaxNormalSpeed, so it keeps the stock
//   blend). The field goes back right after each call, so leaving the wall (0.7 * mMaxNormalSpeed in
//   changeWHideEndProc) and every other procedure see the stock value.
// Ship: daShip_c::procSteerMove (0247E854) computes the target speed windPower with K = 100 when the
//   Swift Sail is in use (dComIfGp + 0x5D2C != 0, the HD check at 0247E97C) and K = 55 otherwise;
//   site 0247EB88 multiplies it by that sail's factor, site 0247ED24 puts the stock value back
//   before the jump code (jump height and the 20-unit jump threshold stay stock).
//   setMoveAngle's speed-dependent rudder response uses speedF / sail factor while sailing;
//   otherwise the boosted speed reaches its 0.1 turn-rate floor (at speed 130).
//   Automatic wind: on the Swift Sail branch the game calls dKyw_tact_wind_set(0, angle of the
//   boat's heading) every steering step (0247E9FC); site 0247E988 (the normal-sail branch) makes the
//   same call when the toggle is on. The Swift Sail's sound, sail swap and speed stay its own.
// Off means identical: every hook checks its switch first and then runs only the original code.
// 60 fps: all of this runs inside the game's own procedures, once per execute of Link or the ship,
//   and writes only rates and targets (never per-step amounts), so interpolation (60/120/240) and
//   true 60 (J3DFrameCtrl::update and cLib_addCalc scale the advance by dt) compose with it. In true
//   60 the ladder, sidle and ship procedures run at 30 Hz (not in true60.cpp's kLinkGroups; the
//   ship is not converted). The test setting WWHD_TRUE60_LINK=audited runs the ladder procedures on
//   every pass; that set is not checked with these mods.
#include "mods/mods.h"
#include "mods/traversal.h"

#include "guest_addr.h"
#include "runtime.h"

extern "C" {
void f_02417FF4_orig(Cpu* c);  // daPy_lk_c::getLadderMoveAnmSpeed (ladders, ivy up / down)
void f_024291E0_orig(Cpu* c);  // daPy_lk_c::getClimbMoveAnmSpeed (ivy sideways)
void f_02427D8C_orig(Cpu* c);  // daPy_lk_c::getHangMoveAnmSpeed (along a ledge while hanging)
void f_02427CA8_orig(Cpu* c);  // daPy_lk_c::procLadderMove
void f_0242A218_orig(Cpu* c);  // daPy_lk_c::setBlendWHideMoveAnime(f32)
}

namespace {
using namespace mods::traversal;
// daPy_lk_c (WWHD offsets; docs/decomp-notes.md)
constexpr uint32_t kCurProc = 0x65F0;          // mCurProc
constexpr uint32_t kFrameCtrlUnder0 = 0x5898;  // mFrameCtrlUnder[0]: rate at +0
constexpr uint32_t kMaxNormalSpeed = 0x3C4;    // daPy_py_c::mMaxNormalSpeed (GameCube 0x2A8)
constexpr uint32_t kProcTimer = 0x6916;        // mProcVar0.m34D0 (s16; GameCube 0x34D0)
constexpr uint32_t kLadderSide = 0x69C0;       // mProcVar6.m3570 (GameCube 0x3570): which hand is up
const uint32_t kChangeLadderMoveProc = GC(0x0242757C);  // daPy_lk_c::changeLadderMoveProc(int)
// daShip_c
constexpr uint32_t kShapeAngleY = 0x32A;       // shape_angle.y
constexpr uint32_t kShipMode = 0x635;          // mCurMode; MODE_STEER_MOVE = 1
constexpr uint32_t kShipState = 0x644;         // mStateFlag; SAIL_ON = 0x200
const uint32_t kPlay = GD(0x1046F0B0);          // dComIfGp (the play info; what 025200D4 returns)
constexpr uint32_t kSwiftSail = 0x5D2C;        // dComIfGp: the Swift Sail is in use (HD)
const uint32_t kTactWindSet = GC(0x0257E490);  // dKyw_tact_wind_set(s16 ang_x, s16 ang_y)

float ladder_factor() { return active_factor(mods::ladder_speed(), mods::ladder_speed_factor()); }
float sidle_factor() { return active_factor(mods::sidle_speed(), mods::sidle_speed_factor()); }

// procSteerMove: the stock target while it is scaled (game thread only)
bool g_sail_scaled = false;
uint32_t g_sail_stock = 0, g_sail_written = 0;
}  // namespace

namespace mods {
SidleScope sidle_scope_begin(Cpu* c, bool speed_steps) {
    if (!sidle_speed()) return {};
    const uint32_t link = c->r[3];
    if (ld32(link + kCurProc) != kProcWHideMove) return {};
    const float k = sidle_factor();
    if (k == 1.f) return {};
    const uint32_t stock = ld32(link + kMaxNormalSpeed);
    const uint32_t scaled = f32_as_u32(u32_as_f32(stock) * k);
    st32(link + kMaxNormalSpeed, scaled);
    // setNormalSpeedF(accel f1, ratio f2, max step f3, min step f4): the steps grow with the target,
    // so reaching and losing the faster speed takes as long as the stock one
    if (speed_steps)
        for (int r : {1, 3, 4}) c->f[r].ps0 = (float)c->f[r].ps0 * k;
    return {link, stock, scaled};
}
void sidle_scope_end(const SidleScope& s) {
    // put the stock value back unless the game set its own meanwhile (checkNextMode does)
    if (s.link && ld32(s.link + kMaxNormalSpeed) == s.scaled) st32(s.link + kMaxNormalSpeed, s.stock);
}
}  // namespace mods

// ---- daPy_lk_c::procLadderMove (02427CA8): the rung clip's rate, rewritten from the stick ----
extern "C" void hook_02427CA8(Cpu* c) {
    if (!mods::ladder_speed()) {
        f_02427CA8_orig(c);
        return;
    }
    const uint32_t link = c->r[3];
    const uint32_t before = ld32(link + kCurProc);
    const float entry = u32_as_f32(ld32(link + kFrameCtrlUnder0));
    const int16_t timer = (int16_t)ld16(link + kProcTimer);
    f_02427CA8_orig(c);
    const float k = ladder_factor();
    if (k == 1.f) return;
    const uint32_t after = ld32(link + kCurProc);
    if (ladder_rate_rewritten(entry, before, after)) {
        st32(link + kFrameCtrlUnder0, f32_as_u32(u32_as_f32(ld32(link + kFrameCtrlUnder0)) * k));
    } else if (ladder_rest_step(entry, timer, (int16_t)ld16(link + kProcTimer), before, after)) {
        // the footstep was just played: start the next rung now (or the dismount at the ladder's end)
        const Cpu saved = *c;
        guest_call(c, kChangeLadderMoveProc, {link, ld32(link + kLadderSide)});
        *c = saved;
    }
}

// ---- daPy_lk_c::setBlendWHideMoveAnime(f32) (0242A218): the sidle blend reads
// mNormalSpeed / mMaxNormalSpeed; with the scaled maximum it keeps the stock blend ----
extern "C" void hook_0242A218(Cpu* c) {
    const mods::SidleScope scope = mods::sidle_scope_begin(c, false);
    f_0242A218_orig(c);
    mods::sidle_scope_end(scope);
}

// ---- daShip_c::procSteerMove (0247E854) ----
// setMoveAngle, immediately before fabs(f10 = speedF). Normalize only this register:
// movement, acceleration, control-angle smoothing, forced movement and jumps keep real speed.
extern "C" void site_0247D4E8(Cpu* c) {
    if (!mods::sail_speed()) return;
    const uint32_t ship = c->r[31];
    if (ld8(ship + kShipMode) != 1 || !(ld32(ship + kShipState) & 0x200)) return;
    const bool swift = ld8(kPlay + kSwiftSail) != 0;
    const float factor = active_factor(true, swift ? mods::sail_speed_swift_factor() : mods::sail_speed_normal_factor());
    if (factor == 1.f) return;
    c->f[10].ps0 = (float)c->f[10].ps0 / factor;
}

// 0247E988: the normal-sail branch (`lbz r9, 0x63D(r31)`), right after the HD Swift Sail check.
// Automatic wind: the Swift Sail branch's dKyw_tact_wind_set call (0247E9EC..0247E9FC).
extern "C" void site_0247E988(Cpu* c) {
    if (!mods::auto_wind()) return;
    const uint32_t ship = c->r[31];
    const Cpu saved = *c;
    guest_call(c, kTactWindSet, {0, (uint32_t)(int32_t)follow_wind_angle((int16_t)ld16(ship + kShapeAngleY))});
    *c = saved;
}
// 0247EB88: f29 = windPower, the target speed (`fmuls f29, f9, f29` just before)
extern "C" void site_0247EB88(Cpu* c) {
    g_sail_scaled = false;
    if (!mods::sail_speed()) return;
    const bool swift = ld8(kPlay + kSwiftSail) != 0;
    const float stock = (float)c->f[29].ps0;
    const float target = sail_target(stock, swift, mods::sail_speed_normal_factor(), mods::sail_speed_swift_factor());
    if (target == stock) return;
    c->f[29].ps0 = c->f[29].ps1 = target;
    g_sail_stock = f32_as_u32(stock);
    g_sail_written = f32_as_u32(target);
    g_sail_scaled = true;
}
// 0247ED24: the jump code (windPower >= 20 and speed.y = clamp(windPower, 15, 40)) reads f29
extern "C" void site_0247ED24(Cpu* c) {
    if (!g_sail_scaled) return;
    g_sail_scaled = false;
    if (f32_as_u32((float)c->f[29].ps0) == g_sail_written) c->f[29].ps0 = c->f[29].ps1 = u32_as_f32(g_sail_stock);
}

// ---- ivy and ledge hanging: the clip rate getters (f1 = rate; r3 = Link on entry) ----
namespace {
void scale_getter(Cpu* c, uint32_t link, AnmGetter g) {
    const float k = mods::traversal_getter_factor(int(g), ld32(link + kCurProc));
    if (k == 1.f) return;
    c->f[1].ps0 = c->f[1].ps1 = (float)c->f[1].ps0 * k;
}
}  // namespace
extern "C" void hook_02417FF4(Cpu* c) {
    const uint32_t link = c->r[3];
    f_02417FF4_orig(c);
    if (mods::ivy_speed()) scale_getter(c, link, AnmGetter::kLadder);
}
extern "C" void hook_024291E0(Cpu* c) {
    const uint32_t link = c->r[3];
    f_024291E0_orig(c);
    if (mods::ivy_speed()) scale_getter(c, link, AnmGetter::kClimb);
}
extern "C" void hook_02427D8C(Cpu* c) {
    const uint32_t link = c->r[3];
    f_02427D8C_orig(c);
    if (mods::hang_speed()) scale_getter(c, link, AnmGetter::kHang);
}
