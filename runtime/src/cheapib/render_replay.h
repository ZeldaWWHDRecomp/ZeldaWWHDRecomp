// Settings > Graphics > Frame rate "Faster rendering (replays unchanged draws)": the decomp build's fast mode turns on
// cheap in-between frames and the DrawDone wait off the critical path as one set (decomp build, fast mode only; the
// normal build and the decomp build's other modes have no such setting and keep their defaults):
//   WWHD_CHEAPIB=1, WWHD_CHEAPIB_S2=0xFFF (stages 1 and 2, every moving part), WWHD_CHEAPIB_B1=1 and
//   WWHD_CHEAPIB_B1_SKIP=1 (replayed Vulkan commands, the held slot), WWHD_DRAWOFF=2 (c2), WWHD_DRAWOFF_GPU_CAP=1 (a
//   step plans no more in-betweens than the GPU draws with its logic frame in the step). Their own defaults with
//   these on stay as measured: WWHD_CHEAPIB_B1_FAST 31, WWHD_DRAWOFF_FLIP_TIME 1, WWHD_DRAWOFF_SLOT_AHEAD 1,
//   WWHD_DRAWOFF_RENDER_COST 1, WWHD_DRAWOFF_GRID_KEEP 1.
// The set applies with the Vulkan renderer and second-thread recording on (b1 replays the worker's recorded commands;
// "Record graphics on a second thread", on by default on phones). Applies after a restart (read once).
// Measured (Xiaomi 14, Outset, 120 fps, 2026-11-19): 75.8 fps instead of 59.9, render thread 7.95 ms instead of
// 10.5 per frame; the picture can differ from a fully drawn in-between frame by a few thousand pixels in fine details
// (grass, palm leaves), depending on the render thread's speed.
// With the GPU cap (Xiaomi 14 at the 38 C gate, 2026-11-20, Outset / lava): 59.9 / 59.6 fps instead of 58.3-59.4 / 53.6,
// render thread 8.9 / 9.9 ms instead of 11.9 / 14.3, p99 frame 23.6-25.4 / 28.2 ms instead of 27.9-31.1 / 32.8.
//
// Precedence (each read once): a set environment variable of the list above wins for itself; else
// WWHD_RENDER_REPLAY=0/1 (tests: the whole set off or on, not saved); else the saved setting (hostui key
// "renderReplay", default on).
#pragma once

namespace cheapib::render_replay {

#ifdef WWHD_DECOMP_FAST
// the setting's saved value (default on); WWHD_RENDER_REPLAY when set
bool chosen();
// the set applies for this start: chosen(), the Vulkan renderer and second-thread recording (read once, logged)
bool active();
// getenv(name) when set; else the set's value for name while active(); else null (the switch's own default)
const char* env(const char* name);
// the settings page: what the set does for this start (null when active() or the setting is off)
const char* inactive_reason();
#else
inline const char* env(const char*) { return nullptr; }
#endif

}  // namespace cheapib::render_replay
