// Drawing converted processes (true 60 Hz logic, decomp build only).
//
// While converted processes (they run every logic step) and unconverted ones (they run on the tick30 step and are drawn
// interpolated) are drawn together, a converted process is drawn DELAYED to the phase the interpolated world is drawn
// at, so the picture is one consistent moment (no jitter between Link and the world he walks on).
//
// The interpolated world is drawn at world time W = S - 1 + pass_t() (in 30 Hz frames; S = the 30 Hz step clock with
// this frame's logic pass counted): a logic pass draws between the last two 30 Hz steps at 1/P, in-between pass k at
// (k+1)/P, the record pass the step S itself. A converted process has executed further: on the tick30 step it is at S,
// on the frame's earlier steps j = 1..N-1 at S + j/N. Its draw state (model joint matrices, the camera) is kept per
// step it executed, stamped with that time, and every pass draws the state at W: with P = N (frame interpolation at
// 30 N fps, what the step driver selects) W is always one of its steps (N - 1 steps back from its newest), so it is
// drawn exactly as it was; between two steps (P > N, or a 30 Hz fallback frame where it ran on tick30 only) it is
// blended like the world.
//
// ONE central switch: WWHD_T60_DRAW_ALIGN (default 1; 0 draws converted processes at their newest step). It goes off for
// good once everything is converted, and per scene earlier once everything visible there is converted (all_visible_converted(): the
// world systems of dScnPly_Draw are converted and no unconverted process drew a model since the scene started; false
// until the world systems are converted).
//
// Rule 14 (draw-side state once per logic step): a converted process's draw on one of its steps is that step's draw,
// not a repeat; frame interpolation's keep / put-back of actors drawn on passes without logic does not apply to it
// (stepped_this_pass).
#pragma once
#include <cstdint>

struct Cpu;
namespace interp::camera { struct CamState; struct Shake; }

namespace t60::draw {
bool switch_on();                // WWHD_T60_DRAW_ALIGN (default on)
bool align_on();                 // the switch, N > 1 selected, and not all_visible_converted()
bool all_visible_converted();    // the per-scene check (see above); false for now
bool converted(uint32_t proc);   // the process is converted (the decompiled rate gate's table, test override included)

// game_hooks.cpp fpcM_Execute: the process executed on this pass's step (its state is at the step's time now)
void note_execute(uint32_t proc);
bool stepped_this_pass(uint32_t proc);  // a converted process that executed on this pass's step
// Whole-form actors (decomp d/t60_whole.h through wwhd_t60_whole_picture): the execute of a whole-form actor on this pass's step
// (a step before the tick30 step) was a picture step, no step of its own. Its model's matrices of this step are not kept
// in its draw history (WWHD_T60_WHOLE_BLEND=0: kept, as before), so it is drawn between its frame ends: skeleton and base
// blended joint by joint like the interpolated world, with no per-actor picture hook. Draw only (no logic).
void note_picture(uint32_t proc);
bool picture_step(uint32_t proc);  // noted on this pass (same process id)
// Link carried (decomp d/actor/d_a_player_carry_t60.h through wwhd_t60_link_carry): a whole-form carrier moved Link on
// this pass's step by (dx, dy, dz) and dangle (Y, s16 units). model() draws his models with each frame's carry spread
// over the frame (frac(t) * the frame's carry - the part he already got), as the interpolated world shows the carrier.
// Draw only. WWHD_T60_LINK_CARRY=0: off.
void note_carry(uint32_t proc, float dx, float dy, float dz, int dangle);
// game_hooks.cpp fpcM_Draw: the process whose draw runs (0: none)
uint32_t draw_begin(uint32_t proc);   // returns the previous one, for draw_end
void draw_end(uint32_t previous);
uint32_t drawing();

// interp.cpp J3DModel::viewCalc: a model drawn by a converted process. Fills `out` (12 * n big-endian guest words) with
// its joint matrices at world time `w` and returns true; false when the model is not aligned (drawn as before).
bool model(uint32_t model, uint32_t jnt, uint32_t mtx, uint32_t n, double w, uint32_t* out);
// interp.cpp camera_draw: a converted camera process. Records `cur` (its newest step) and fills `out` with the camera
// at world time `w`; false when not aligned.
bool camera(uint32_t cam, const interp::camera::CamState& cur, const interp::camera::Shake& shake, double w,
            interp::camera::CamState& out);

// Draw words (decomp, through wwhd_t60_draw_words_begin / _end): a draw that is no J3DModel reads `n` guest words at
// `ea` of converted process `owner`; begin puts the words of its step at the world time in their place (kind 0: whole
// steps, 1: floats blended, 2: 3x4 matrices blended, 3: floats between frame ends) and returns true when it changed them, end puts the live words
// back; owner 0: the process whose draw runs (3D lines). WWHD_T60_DRAW_WORDS=0: off. DRAWLOG: 'p' lines (drawn first point / translation, then 'new': the newest step's)
bool words_begin(uint32_t owner, uint32_t ea, uint32_t n, int kind);
void words_end(uint32_t ea);
bool words_owns(uint32_t proc);  // a converted owner whose 3D lines the words history draws (interp_fx line_packet: no blend)

// debug: WWHD_T60_DRAWLOG=path: one line per aligned draw: pass, kind (m model / c camera), owner process, world time,
// the stamps of the two steps it was drawn between (equal: drawn exactly at a step), the newest stamp, the fraction, and
// the drawn root joint's translation (a camera: its eye)
void pass_start(uint64_t pass);  // t60.cpp pass_begin
}  // namespace t60::draw
