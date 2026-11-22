// The play scene's world block on a general step length (true 60 Hz logic, decomp build only; world-block port step).
//
// dScnPly_Draw (025AF8A0) runs world systems inside `if (!menu && !pause) { ... }`: vibration, magma, the mass
// collision prepare, grass / tree / wood / flower (sway and reactions), poison light, the background's move
// (dBgS::Move), snapshots, particles. The draw runs on every pass, so frame interpolation holds that block on its
// in-between passes (game_hooks.cpp hold_world: the pause check answers "paused" there). With true 60 the in-between
// passes that are the frame's earlier steps would need the block for the world systems the decompilation converted:
// they step on every logic step like a converted process.
//
// The decompilation names those systems (d_s_play_t60.h kT60WorldConverted, a bit per system, exported as
// wwhd_t60_world_converted). On a step before the tick30 step (t60::sub_step) with at least one converted system the
// hold is lifted, and the decompiled dScnPly_Draw runs only the converted systems there (each at the step's dt); the
// unconverted ones keep the tick30 step. Plain interpolation passes (no step) keep the hold. With no converted system
// (the mask 0: today) nothing changes: the hold applies as before.
//
// debug: WWHD_T60_WORLD=0 keeps the hold on every in-between pass (the switch; default on).
#pragma once
#include <cstdint>

namespace t60::world {
uint32_t converted_mask();  // the decompilation's converted world systems (0: none)
bool all_converted();       // every world system of the block is converted (t60_draw.cpp all_visible_converted)
// game_hooks.cpp hold_world: this pass is a step before the tick30 step and converted world systems step on it
bool lift_hold();
}  // namespace t60::world
