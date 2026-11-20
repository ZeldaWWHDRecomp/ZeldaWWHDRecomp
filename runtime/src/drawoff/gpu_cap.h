// drawoff GPU cap (decomp build, fast mode only): the paced in-between plan counts the GPU's time per frame.
//
// With "Faster rendering (replays unchanged draws)" (cheapib + c2) an in-between pass costs ~6 ms on the CPU while its
// frame still costs the GPU what a logic frame does (phone, hot: ~12.5 ms). interp's plan (pacing::plan_in_between)
// sees only the CPU passes, so a step got 3 in-betweens (4 frames, ~50 ms of GPU in a 33 ms step); the GPU fell behind,
// and the next logic frame waited ~10 ms for a submission fence and came a vsync or two late (phone r1/r2: 45 % / 22 %
// of the long frames; the s2c2 tail, cheapib/stage2/HANDOVER.md "s2c2 tail").
//
// WWHD_DRAWOFF_GPU_CAP=1 (c2 only; default 0 = nothing changes, no timing added): every command buffer gets two GPU
// timestamps (the dynamic-resolution timing path), the render thread keeps the GPU's busy time per drawn frame, and a
// step plans at most as many in-betweens as the GPU can draw with its logic frame in the step:
// (n + 1) x GPU frame <= fill x step, WWHD_DRAWOFF_GPU_FILL (percent, default 100). The even-pacing rule comes after it.
#pragma once
#include <cstdint>

namespace drawoff::gpu_cap {
bool on();                 // the switch (read once; c2 only)
bool timing_wanted();      // backend: give every command buffer the two timestamps (on())
// backend, render thread: one command buffer's GPU interval (ns)
void gpu_interval(double ns);
// interp (game thread): the in-betweens a step may plan, n the CPU plan, step the step length (ns)
int cap(int n, int64_t step);
}  // namespace drawoff::gpu_cap
