// Hot vsync waits (WWHD_VSYNC_HOT_WAIT_US=n, a switch, off by default): GX2WaitForVsync's waits (gx2_core.cpp: the
// virtual vsync's deadline and the late-flip polling before it) that are at most n microseconds long spin with the
// guest core released instead of sleeping. Why: on a Xiaomi 14 (normal build, lava, 60 fps, 2026-11-14) a pass that
// followed a game-thread sleep of 4 ms or more ran 40-50 % slower on both the game thread (in-between pass 14.6
// instead of 10.2 ms) and the render thread (+6-9 ms), in every renderer configuration: the cores clock down while
// both threads wait. Such sleeps come when the game is ahead of its step slots, which the faster render thread of the
// record worker makes three times as frequent there (98 instead of 26 a minute), so the frames after them became the
// slowest 1 %. Spinning the short waits keeps the cores busy; waits longer than n (30 fps, menus) sleep as before.
#pragma once
#include <chrono>
#include <cstdint>

namespace threads {
// the switch's value in microseconds (0: off), read once
int64_t hot_wait_us();
// park_sleep_until (runtime.h), spinning instead of sleeping (std::this_thread::yield between clock reads)
void park_spin_until(std::chrono::steady_clock::time_point tp, void (*before_resume)() = nullptr);
}  // namespace threads
