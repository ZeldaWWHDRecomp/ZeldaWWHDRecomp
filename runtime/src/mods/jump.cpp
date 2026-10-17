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
// minimum, so from standing he hops forward a little; speed.y comes from speedF. His collision's
// ground flags are cleared as for the moon jump (cheats.cpp), or the jump would land at once.
// The moon jump cheat on the same button wins. Off means identical: no guest reads or writes.
// True 60: decisions and guest calls happen on full steps only; previews are rolled back by true60.
//
// Idea/code from GreenNaugahyde/ZeldaWWHDRecompAndroid (MPL-2.0), mods/jump.cpp.
// WWHD_JUMP_LOG=1 logs each jump.
#include <atomic>
#include <cstdlib>
#include <cstring>

#include "guest_addr.h"
#include "input.h"
#include "jump_policy.h"
#include "mods.h"
#include "runtime.h"
#include "true60.h"

namespace mods {
namespace {
using namespace jump_policy;
constexpr uint32_t kCurProc = 0x65F0;  // daPy_lk_c::mCurProc
constexpr uint32_t kModeFlg = 0x6A70, kModeSwim = 0x40000;
constexpr uint32_t kAcchFlags = 0x80C + 0x28, kAcchGround = 0x20 | 0x40 | 0x80;  // as in cheats.cpp
constexpr uint32_t kSpeedF = 0x370, kSpeedY = 0x340;
const uint32_t kAutoJumpInit = GC(0x023F7958);  // daPy_lk_c::procAutoJump_init()
const uint32_t kFallInit = GC(0x023F6564);      // daPy_lk_c::procFall_init(int, f32)
const uint32_t kPlay = GD(0x1046F0B0);
constexpr uint32_t kStage = 0x5134, kEventMode = 0x5292;  // as in fast_forward.cpp

std::atomic<uint32_t> g_buttons{0};
State g_state;
uint32_t g_link = 0;

bool log_on() {
    static const bool on = getenv("WWHD_JUMP_LOG") != nullptr;
    return on;
}

bool blocked(uint32_t link) {
    const char* stage = reinterpret_cast<const char*>(mem::ptr(kPlay + kStage));
    if (!*stage || !strncmp(stage, "sea_T", 8) || !strncmp(stage, "Name", 8)) return true;  // title, file select
    if (ld8(kPlay + kEventMode) != 0) return true;  // dialogue, cutscenes
    if (ld32(link + kModeFlg) & kModeSwim) return true;
    return infinite(kMoonJump) && jump_button() == input::kL;  // the moon jump owns L
}

void jump(Cpu* c, uint32_t link, Action a) {
    const Cpu saved = *c;
    guest_call(c, kAutoJumpInit, {link});
    *c = saved;
    if (a != Action::kDoubleJump) stf32(link + kSpeedY, ground_speed_y(ldf32(link + kSpeedY)));
    st32(link + kAcchFlags, ld32(link + kAcchFlags) & ~kAcchGround);  // off the ground: no landing at once
    if (log_on())
        LOG("[jump] %s: speedF %.1f, speed.y %.1f", a == Action::kJump ? "jump" : a == Action::kGraceJump ? "grace jump" : "double jump",
            ldf32(link + kSpeedF), ldf32(link + kSpeedY));
}
}  // namespace

void jump_input(uint32_t buttons) {
    if (jump_anywhere()) g_buttons.store(buttons, std::memory_order_relaxed);
}

void jump_reset() {
    g_state = {};
    g_link = 0;
}

void jump_step(Cpu* c, uint32_t link, bool before) {
    if (!jump_anywhere()) {
        if (g_link) jump_reset();
        return;
    }
    if (link < 0x10000 || link > 0xFFFF9000) return;
    const bool preview = true60::dt() < 1.f;
    if (link != g_link) {
        if (preview) return;
        jump_reset();
        g_link = link;
    }
    const uint32_t proc = ld32(link + kCurProc);
    if (before) {
        if (preview) return;
        const bool button = (g_buttons.load(std::memory_order_relaxed) & jump_button()) != 0;
        const Action a = step(g_state, {proc, button, blocked(link), double_jump()});
        if (a != Action::kNone) jump(c, link, a);
        return;
    }
    // after the execute: the game's own ledge jump began in this step: a plain fall instead
    const bool convert = preview ? proc == kProcAutoJump && !g_state.own && no_auto_jump()
                                 : after_step(g_state, proc, no_auto_jump());
    if (!convert) return;
    stf32(link + kSpeedY, 0.f);
    const Cpu saved = *c;
    c->f[1].ps0 = c->f[1].ps1 = 5.;  // animation blend
    guest_call(c, kFallInit, {link, 0});
    *c = saved;
    if (log_on() && !preview) LOG("[jump] the game's ledge jump made a fall (speedF %.1f)", ldf32(link + kSpeedF));
}

}  // namespace mods
