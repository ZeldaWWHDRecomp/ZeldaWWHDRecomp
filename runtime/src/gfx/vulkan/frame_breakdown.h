// Per-frame breakdown of the render thread and of the frame's way to the screen (WWHD_VK_FRAME_BREAKDOWN=1, a
// diagnostic, off by default; needs WWHD_VK_STUTTER_TRACE: the rows go into that trace, next to its "frame" rows).
// Every build with cmake WWHD_VK_SMOOTH (cmake/VkSmooth.cmake, on by default).
//
// Rows (kind, frame, key, start_ns, end_ns, thread, result, cpu_ns; steady clock ns):
//   fb_idle, fb_swap, fb_acquire, fb_present, fb_worker_wait, fb_fence_wait: the render thread's time in that part
//     during frame FRAME (R.frame at its swap): key = how many times, start/end = first begin / last end, cpu = the
//     summed time. idle = waiting for GX2 commands (gx2_core.cpp), swap = the whole swap (backend.cpp swap()),
//     acquire / present = vkAcquireNextImageKHR / vkQueuePresentKHR of the TV, worker_wait = waits for the record
//     worker (record_worker.cpp), fence_wait = submission slot fence waits.
//   fb_hold: the TV present of frame FRAME under even pacing: start = ready, key = due, end = issued,
//     result = 1 held, 0 presented at once.
//   fb_flip: a game flip (gx2_core.cpp update_flips): frame = the swap it belongs to, start = when the game swapped
//     it, key = its vsync tick's time, end = when it executed, result = 1 an in-between frame.
//   fb_display (Android, VK_GOOGLE_display_timing when the driver has it; display_timing.h): frame = the frame
//     presented, start = its present call, end = actualPresentTime (on screen), key = earliestPresentTime,
//     cpu = presentMargin, result = 1 when display pacing asked for a time.
//   fb_gwait (key = the wait's site), fb_gvsync: the game thread's waits for the render thread and in
//     GX2WaitForVsync; fb_gpass: a pass started (result 1 = in-between, key = phase); fb_gdrop: an in-between frame
//     dropped (key = the step's elapsed time, cpu = the in-between pass estimate, start = the budget).
#pragma once
#ifdef WWHD_VK_SMOOTH
#include "loader.h"
#include <cstdint>

namespace gfxvk::frame_breakdown {
bool enabled();
enum Part : int { Idle, Swap, Acquire, Present, WorkerWait, FenceWait, kParts };
uint64_t now_ns();
// begin() returns 0 when off; end() with 0 does nothing
inline uint64_t begin() { return enabled() ? now_ns() : 0; }
void end(Part part, uint64_t started);
struct Scope {
    Part part; uint64_t started;
    explicit Scope(Part p) : part(p), started(begin()) {}
    ~Scope() { end(part, started); }
};
// render thread: frame FRAME's swap ended (after the stutter trace's frame row); writes this frame's rows
void frame_done(uint64_t frame);
// even pacing: the TV frame's plan (ready, due) and its present call
void hold_planned(uint64_t frame, int64_t ready, int64_t due, bool held);
void hold_issued(uint64_t frame, int64_t at);
// game flips (any thread)
void flip(uint64_t swap, int64_t swappedAt, int64_t dueAt, int64_t executedAt, bool inBetween);
// game thread (gx2_core.cpp, interp.cpp): its waits for the render thread (SITE: gx2_core.cpp SyncSite), its
// GX2WaitForVsync, the start of each pass (HOLD: an in-between pass) and each dropped in-between frame
void game_wait(int site, int64_t start, int64_t end);
void game_vsync(int64_t start, int64_t end);
void game_pass(bool hold, int phase);
void game_drop(int64_t elapsed, int64_t pass, int64_t budget);
}  // namespace gfxvk::frame_breakdown
#endif
