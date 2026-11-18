// Frame interpolation: which children of the per-frame function run on hold passes, without game state
// (interp.cpp hold_skip_child; runtime/tools/interp_hold_test.cpp, ctest interp_hold).
//
// A hold pass draws an in-between frame without a logic step. The per-frame function's children that
// keep per-frame state must not run there, or that state advances 2x/4x/8x per logic step at
// 60/120/240 fps. Bit i set: child i runs on hold passes too.
//   run:  0 (reads a system state; a non-zero result skips the whole frame), 2-3 (heap setup the drawing
//         needs), 6 (clears the HD light manager's light counts, which child 8 fills again), 8-10
//         (lighting, display, shadow cascades; child 8 restores its smoothed state on hold passes),
//         12, 13 (the display fader, gated in its hook: interp_fade.h), 14
//   held: 1, 4, 5 (HD menus and systems), 7 (hdSceneBoot_c::execute: the start-up loading screen,
//         issue #300), 11 (027199C0 -> dMesg_core_c::update 025F77B0: the message manager's frame end)
// Child 11 advances the message frame counter (+0x10, mod 10000) and the three cyclic timers of the
// animated button glyphs in message boxes (+0x994..+0x99C, periods 160/40/80 frames), and clears two
// per-frame flags of dComIfG_play_c (+0x5BD2, and the message BGM flag +0x5C20 unless the current
// message keeps it). It reads and writes nothing the drawing needs; run on every pass, the glyph
// animations played 2x/4x/8x too fast.
#pragma once
#include <cstdint>

namespace interp::hold {

constexpr int kMessageUpdateChild = 11;
constexpr uint32_t kDefaultRunMask = 0x774C;

// child i of the per-frame function runs on hold passes too (child 0 always does)
constexpr bool runs_on_hold(uint32_t run_mask, int i) { return ((run_mask | 1u) >> i) & 1u; }

}  // namespace interp::hold
