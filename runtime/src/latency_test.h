// Input-to-screen latency test (tools/gametest/latency/README.md). Off unless WWHD_LATENCY_TEST is set; every build.
//
// WWHD_LATENCY_TEST=bits:period_ms:hold_ms:count (e.g. 4000:1500:80:40, B on the GamePad): from
// WWHD_LATENCY_START_STEPS (default 150) of Link's executes after the last full state load on (or from logic step
// WWHD_LATENCY_START_AT since boot: portable-state runs), the button bits are pressed
// at host times t0 + k * period + jitter (jitter 0..40 ms, fixed sequence, so presses fall on every phase of the frame
// rhythm) and held hold_ms; a press waits (counted "busy") while Link is not in the procedure he was in before the
// previous press. Per press it records, on the host's steady clock:
//   press    when the bits became visible to the game's pad reads (the scheduled time; host input would arrive then)
//   read     the first VPADRead that returned them (not a repeated sample) and the logic step count then
//   react    the first execute of Link after that read whose procedure (daPy_lk_c::mCurProc) differs from the one at
//            the read: the game state shows the reaction (sword swing, roll, ...); its logic step
//   swap     the game's GX2SwapScanBuffers that ends the pass of that execute (its display list is called at the next
//            pass's start and scanned out by the swap after: the first frame with the reaction is swap + 1)
//   present  the TV present call of that frame returned (Vulkan vkQueuePresentKHR; a frame held for even delivery
//            counts when its present is issued); with hidden windows (no present at all) the end of the renderer's
//            swap on the render thread (the log's first line says which)
// WWHD_LATENCY_LOG=file: one line per press ("press k press_ns read_ns read_step react_ns react_step swap swap_ns
// present_ns proc_from proc_to"), a "miss" line when Link did not react within 45 logic steps; the log gets
// "[latency]" summary lines every 10 presses and "[latency] done" after `count` presses.
#pragma once
#include <cstdint>

namespace latency_test {
extern bool g_on;  // WWHD_LATENCY_TEST given (read once at start)
inline bool on() { return g_on; }
// VPADRead / KPADReadEx (game thread), a fresh sample: adds the press bits while a press is held, notes the first read of a press
void pad(uint32_t& buttons, float& ly);  // (WWHD_LATENCY_STICK=1: the left stick held up during a press too)
// Link's execute (game thread): his procedure before and after it
void link_execute(uint32_t proc_before, uint32_t proc_after);
// GX2SwapScanBuffers (game thread): the swap's number (1-based count of swaps)
void game_swap(uint64_t swap);
// render thread, OP_SWAP: the swap's number and the renderer's frame it presents
void render_swap(uint64_t swap, uint64_t frame);
// the renderer's TV present of `frame` was issued (any presenting thread)
void presented(uint64_t frame);
// render thread, after OP_SWAP's renderer swap: without any TV present (hidden windows) the frame counts as presented here
void render_swapped(uint64_t swap);
}  // namespace latency_test
