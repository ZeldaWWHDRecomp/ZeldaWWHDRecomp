// SPDX-License-Identifier: MPL-2.0
// Jump anywhere (Gameplay mods, off by default): the jump button makes Link jump from where he
// stands or runs, with the game's own jump off a ledge (daPy_lk_c::procAutoJump_init: take-off
// animation, voice, his run speed carried on, landing as usual). Optionally once more in the air
// (double jump). Which procedures may jump and the per-step decision: jump_policy.h.
//
// No automatic ledge jumps (option): when the game itself starts its jump off a ledge (running off an
// edge; daPy_lk_c's ledge check, the only caller of procAutoJump_init), right after that step Link is
// put into the plain fall instead (procFall_init(0): keeps his forward speed; speed.y cleared), so he
// drops off the edge and the player jumps with the button (a short grace window counts as ground).
//
// procAutoJump_init (GameCube 80115EA4, USA 023F7958) clamps speedF to at least the auto jump's
// minimum (speed.y comes from it) and leaves the air step running Link up to full speed along his
// facing; ours then follows the stick instead, and goes straight up when the stick is released
// (jump_policy.h straight_up). His collision's ground flags are cleared as for the moon jump
// (cheats.cpp), or the jump would land at once.
// The jump has its own input (input_map kJump, the virtual input::kVirtJump): it never presses the
// game's own L, and any controller button, key or the on-screen Jump button can be it (Controls). The
// moon jump held at the same time wins. Off means identical: no guest reads or writes.
// The input is read on every pass (also the in-between passes of frame interpolation): a press that
// begins between two logic steps reaches the next step once (jump_policy::Latch).
//
// Idea/code from GreenNaugahyde/ZeldaWWHDRecompAndroid (MPL-2.0), mods/jump.cpp.
//
// Deku Leaf: wave to rise (its own switch, off by default; the rules in leaf_lift.h): after Link's step, a wave of
// the leaf while gliding raises speed.y to the lift and keeps the glide's gravity; held, the leaf's item button
// waves on. Off means identical: no guest reads or writes. From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60
// (MPL-2.0), mods/jump.cpp. WWHD_LEAF_LOG=1 logs each lift, WWHD_LEAF_TRACE=1 each glide step whose gravity
// changed.
// WWHD_JUMP_LOG=1 logs each jump.
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "guest_addr.h"
#include "input.h"
#include "input_map.h"
#include "jump_policy.h"
#include "leaf_lift.h"
#include "mods.h"
#include "runtime.h"

namespace mods {
namespace {
using namespace jump_policy;
constexpr uint32_t kCurProc = 0x65F0;  // daPy_lk_c::mCurProc
constexpr uint32_t kModeFlg = 0x6A70, kModeSwim = 0x40000;
constexpr uint32_t kAcchFlags = 0x80C + 0x28, kAcchGround = 0x20 | 0x40 | 0x80;  // as in cheats.cpp
constexpr uint32_t kSpeedF = 0x370, kSpeedY = 0x340;
constexpr uint32_t kNormalSpeed = 0x6A14, kStickDistance = 0x6A08;  // daPy_lk_c: mNormalSpeed, mStickDistance
constexpr uint32_t kProcTimer = 0x6916;                             // mProcVar0 (s16; GameCube 0x34D0)
constexpr uint32_t kPosX = 0x314, kFacing = 0x32A;  // current.pos (x, +8 z), shape_angle.y
constexpr uint32_t kRoot = 0x7308, kRootMode = 0x68DE;              // m3700 (GameCube 0x3700), m34C2 (u8)
const uint32_t kHeldPos = GD(0x1046CD48);  // the HD's retained copy of his position (0240D130; editor_game.cpp)
const uint32_t kAutoJumpInit = GC(0x023F7958);  // daPy_lk_c::procAutoJump_init()
const uint32_t kFallInit = GC(0x023F6564);      // daPy_lk_c::procFall_init(int, f32)
const uint32_t kPlay = GD(0x1046F0B0);
constexpr uint32_t kStage = 0x5134, kEventMode = 0x5292;  // as in fast_forward.cpp

std::mutex g_input_mu;
Latch g_latch;  // input::kVirt* bits between full steps
State g_state;
uint32_t g_link = 0;
bool g_straight = false;  // our jump goes straight up: posMove's take-off root motion is taken back
float g_root_x = 0, g_root_z = 0;  // m3700 before Link's execute (every pass, previews too)

bool log_on() {
    static const bool on = getenv("WWHD_JUMP_LOG") != nullptr;
    return on;
}

bool blocked(uint32_t link, uint32_t held) {
    const char* stage = reinterpret_cast<const char*>(mem::ptr(kPlay + kStage));
    if (!*stage || !strncmp(stage, "sea_T", 8) || !strncmp(stage, "Name", 8)) return true;  // title, file select
    if (ld8(kPlay + kEventMode) != 0) return true;  // dialogue, cutscenes
    if (ld32(link + kModeFlg) & kModeSwim) return true;
    return infinite(kMoonJump) && (held & input::kVirtMoonJump);  // the moon jump on the same input wins
}

void jump(Cpu* c, uint32_t link, Action a) {
    const Cpu saved = *c;
    guest_call(c, kAutoJumpInit, {link});
    *c = saved;
    if (a != Action::kDoubleJump) stf32(link + kSpeedY, ground_speed_y(ldf32(link + kSpeedY)));
    st16(link + kProcTimer, uint16_t(kAirStick));  // the air step follows the stick
    g_straight = straight_up(ldf32(link + kStickDistance));
    if (g_straight) {
        stf32(link + kSpeedF, 0.f);
        stf32(link + kNormalSpeed, 0.f);
        g_root_x = ldf32(link + kRoot), g_root_z = ldf32(link + kRoot + 8);  // (the init zeroed it)
    }
    st32(link + kAcchFlags, ld32(link + kAcchFlags) & ~kAcchGround);  // off the ground: no landing at once
    if (log_on())
        LOG("[jump] %s: speedF %.1f, speed.y %.1f, stick %.2f", a == Action::kJump ? "jump" : a == Action::kGraceJump ? "grace jump" : "double jump",
            ldf32(link + kSpeedF), ldf32(link + kSpeedY), ldf32(link + kStickDistance));
}
}  // namespace

void jump_input(uint32_t virt) {
    if (!jump_anywhere()) return;
    std::lock_guard<std::mutex> lk(g_input_mu);
    g_latch.see(virt);
}

void leaf_reset();

void jump_reset() {
    leaf_reset();
    g_state = {};
    g_link = 0;
    g_straight = false;
    std::lock_guard<std::mutex> lk(g_input_mu);
    g_latch = {};
}

void jump_step(Cpu* c, uint32_t link, bool before) {
    if (!jump_anywhere()) {
        if (g_link) jump_reset();
        return;
    }
    if (link < 0x10000 || link > 0xFFFF9000) return;
    if (link != g_link) {
        jump_reset();
        g_link = link;
    }
    const uint32_t proc = ld32(link + kCurProc);
    if (before) {
        if (g_straight) g_root_x = ldf32(link + kRoot), g_root_z = ldf32(link + kRoot + 8);
        // the old Jump anywhere button choice becomes the Jump binding of an old controls.json (once)
        static const bool adopted = (input_map::adopt_legacy_jump_button(jump_button()), true);
        (void)adopted;
        uint32_t held, pressed;
        {
            std::lock_guard<std::mutex> lk(g_input_mu);
            held = g_latch.last;
            pressed = g_latch.take();
        }
        const bool button = (held & input::kVirtJump) != 0;
        const Action a = step(g_state, {proc, button, blocked(link, held), double_jump(), (pressed & input::kVirtJump) ? 1 : 0});
        if (a != Action::kNone) jump(c, link, a);
        return;
    }
    // after the execute: a straight-up jump takes back what the take-off animation moved him this pass
    if (g_straight) {
        if (proc != kProcAutoJump) {
            g_straight = false;
        } else if (ld8(link + kRootMode) == 1) {
            const Xz d = root_motion(ldf32(link + kRoot) - g_root_x, ldf32(link + kRoot + 8) - g_root_z, int16_t(ld16(link + kFacing)));
            for (uint32_t x : {link + kPosX, kHeldPos}) {  // (and the HD's retained copy the next execute reads)
                stf32(x, ldf32(x) - d.x);
                stf32(x + 8, ldf32(x + 8) - d.z);
            }
        }
    }
    // the game's own ledge jump began in this step: a plain fall instead
    const bool convert = after_step(g_state, proc, no_auto_jump());
    if (!convert) return;
    stf32(link + kSpeedY, 0.f);
    const Cpu saved = *c;
    c->f[1].ps0 = c->f[1].ps1 = 5.;  // animation blend
    guest_call(c, kFallInit, {link, 0});
    *c = saved;
    if (log_on()) LOG("[jump] the game's ledge jump made a fall (speedF %.1f)", ldf32(link + kSpeedF));
}

// ---- Deku Leaf: wave to rise ----
namespace {
constexpr uint32_t kGravity = 0x374;  // fopAc_ac_c::gravity
leaf_lift::Glide g_glide;
leaf_lift::Repeat g_leaf_repeat;
std::atomic<uint64_t> g_leaf_waves{0};
std::atomic<uint32_t> g_leaf_link{0};
std::atomic<bool> g_leaf_gliding{false};
}  // namespace

void leaf_reset() {
    g_glide = {};
    g_leaf_repeat = {};
    g_leaf_gliding = false;
}

// test aid: WWHD_TEST_LEAF_GLIDE=step: from that logic step on, Link falling (procFall) opens the Deku Leaf into
// a glide (procFanGlide_init 0241A3C4), as its button does in the air; for scripted runs that put him in the air
// with WWHD_LINK_WARP (the tests need no ledge). Off by default; not a player option.
void leaf_test_glide(Cpu* c, uint32_t link) {
    static const uint64_t from = [] { const char* e = getenv("WWHD_TEST_LEAF_GLIDE"); return e ? strtoull(e, nullptr, 10) : ~0ull; }();
    if (step() < from || link < 0x10000 || link > 0xFFFF9000) return;
    static uint64_t done_at = ~0ull;
    if (ld32(link + kCurProc) != 0x27 || step() - done_at < 60) return;  // procFall; once per fall
    done_at = step();
    const Cpu saved = *c;
    guest_call(c, GC(0x0241A3C4), {link, 0});
    *c = saved;
    LOG("[leaf] test: the fall became a glide (proc 0x%X)", ld32(link + kCurProc));
}

void leaf_step(Cpu* c, uint32_t link) {
    leaf_test_glide(c, link);
    if (!leaf_rise()) {
        g_glide = {};
        g_leaf_gliding = false;
        return;
    }
    if (link < 0x10000 || link > 0xFFFF9000) return;
    if (link != g_leaf_link.exchange(link)) g_glide = {};
    const uint32_t proc = ld32(link + kCurProc);
    static const bool trace = getenv("WWHD_LEAF_TRACE") != nullptr;  // each glide step whose gravity changed
    if (trace) {
        static uint32_t last_proc = ~0u;
        static float last_g = 99;
        const float g = ldf32(link + kGravity);
        if (proc != last_proc || (proc == leaf_lift::kProcFanGlide && g != last_g))
            LOG("[leaf] step %llu: proc 0x%X, gravity %.2f, speed.y %.2f", (unsigned long long)step(), proc, g,
                ldf32(link + kSpeedY));
        last_proc = proc, last_g = g;
    }
    g_leaf_gliding = proc == leaf_lift::kProcFanGlide;
    const float vy = ldf32(link + kSpeedY);
    const leaf_lift::Lifted l =
        leaf_lift::after_step(g_glide, true, proc, ldf32(link + kGravity), vy, int(leaf_rise_height()));
    if (!l.wave) return;
    stf32(link + kSpeedY, l.speed_y);
    stf32(link + kGravity, l.gravity);
    g_leaf_waves = g_glide.waves;
    static const bool log = getenv("WWHD_LEAF_LOG") != nullptr;
    if (log) LOG("[leaf] wave %llu: speed.y %.1f -> %.1f, gravity %.2f", (unsigned long long)g_glide.waves, vy, l.speed_y, l.gravity);
}

void leaf_filter_pad(input::PadState& s) {
    constexpr uint32_t kItems = input::kX | input::kY | input::kR;
    const bool on = leaf_rise();
    if (!on && g_leaf_repeat.seen_waves == ~0ull) return;
    if (leaf_lift::drop(g_leaf_repeat, on, on && g_leaf_gliding.load(), (s.buttons & kItems) != 0, g_leaf_waves.load(),
                        step()))
        s.buttons &= ~kItems;  // (while gliding the other item buttons have no use)
}

}  // namespace mods
