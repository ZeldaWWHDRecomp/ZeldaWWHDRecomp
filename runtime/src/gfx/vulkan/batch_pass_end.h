// WWHD_VK_BATCH_AT_PASS_END=1 (decomp build, fast mode: cmake WWHD_NATIVE_DRAW; off by default).
// A draw-batch submission (WWHD_VK_DRAW_BATCH: 2048 draws, up to 3 per frame) made right after a draw ends the open
// render pass: the next draw begins it again with LOAD in the next command buffer (a re-open, ~20 us of
// vkCmdBeginRendering on Adreno) and every lowered packet kind is lowered again (L.submission / L.passCount).
// With the switch, such a submission is deferred while a pass is open and made at the next GX2 command boundary at
// which no pass is open and no packet batch is collected (gx2_core execute_one, after the batch flushes), or dropped
// when another submission came first (GX2Flush, the swap, a synchronous flush). It is never made inside a draw: a
// draw's descriptor sets and upload slices belong to the slot it records into. Only where command buffers are cut
// moves; the commands and their order on the one graphics queue are unchanged.
// Counter line "[vulkan batch at pass end]" (report).
#pragma once
#ifdef WWHD_NATIVE_DRAW
#include <cstdint>

namespace gfxvk::batch_pass_end {
bool on();
// a draw-batch submission is due (draw.cpp); true: deferred (a pass is open), the caller does not submit
bool defer();
// a command boundary (draw.cpp deferred_batch_point, called by gx2_core with a deferred submission pending):
// true when the caller submits now (no pass open and no other submission since); clears the pending flag when it
// decides (made or dropped)
bool take();
// the counter line, every stats period of FRAMES frames; SUBMISSIONS: draw-batch submissions in the period
void report(double frames, uint64_t submissions);
}  // namespace gfxvk::batch_pass_end
#endif
