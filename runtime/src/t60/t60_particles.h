// Particles on a general step length (true 60 Hz logic, decomp build only).
//
// The decompiled JParticle library steps with dt (emitter ages and rate-step crossings, particle ages,
// motion with air^dt and the exact-arc term, fields, rotation split). The play scene's world block runs the particle
// calc (dScnPly_Draw: calculateParticles 025A81A0 + drawParticleDirect 025A8148) as world system kT60W_Particles
// (decomp d/d_s_play_t60.h): once it is in the decompilation's converted mask, the calc runs on every logic step at
// dt = 1/N (the earlier steps lift the world hold, t60_world.h). Frame interpolation's particle part (interp_fx.cpp)
// then does not apply on those passes:
//   - hold back (hook_0282167C / hook_025A8148 return on hold passes, and undo g_Counter.mTimer++): a step pass runs the
//     calc; mTimer (decomp l_particleFrame) counts 30 Hz frames in the decompilation itself (tick30 only);
//   - the halfway blend and the born shift (the step's before/after pairs at the pass's fraction): a step pass draws the
//     particles at the world time W the picture shows (t60_draw.h: converted processes are drawn N - 1 steps back from
//     their newest step while unconverted ones are interpolated): each particle's state of the step W shows (a short
//     per-step history), put back to the exact state at the next pass start; a particle born after W is drawn at its
//     newest state (it shows one step early). W at the newest step (draw alignment off): the exact state.
// Plain in-between passes (more passes than steps) keep frame interpolation's rules.
//
// debug: WWHD_T60_PTCL=0 runs the particle calc as before (frame interpolation's holds and blends) even when the
// decompilation converts it (the switch; default on). WWHD_T60_PTCL_STATS=1: per 5 s, step passes / particles drawn
// from the history / born after W.
#pragma once
#include <cstdint>

namespace t60::particles {
// the first menu group (decomp dPa_control_c dPtclGroup_2DmenuFore_e): groups 9-12 are calculated by calcMenu 025A81F8
// outside the world block (every pass, dt 1), so they keep frame interpolation's 30 Hz rules (interp_fx.cpp)
constexpr uint32_t kMenuGroupFirst = 9;
// this pass is a true 60 step and the decompilation runs the particle calc on the steps (kT60W_Particles converted)
bool steps_here();
// steps W lies behind the newest step on this pass (0: draw the exact state)
int draw_lag();
// the logic-step stamp of this pass (one per true 60 step)
uint64_t step_stamp();
void count(int what);  // WWHD_T60_PTCL_STATS: 0 a step pass, 1 a particle drawn from history, 2 born after W
}  // namespace t60::particles
