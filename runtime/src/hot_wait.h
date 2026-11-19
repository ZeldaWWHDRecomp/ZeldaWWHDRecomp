// Hot vsync waits (WWHD_VSYNC_HOT_WAIT_US=n; default 8000 on phones and the Steam Deck with second-thread recording
// and frame interpolation on, smooth_defaults.h, 0 at 30 fps and everywhere else; the variable wins; the default
// follows the frame rate mode while the game runs): GX2WaitForVsync's waits (gx2_core.cpp: the
// virtual vsync's deadline and the late-flip polling before it) that are at most n microseconds long spin with the
// guest core released instead of sleeping. Why: on a Xiaomi 14 (normal build, lava, 60 fps, 2026-11-14) a pass that
// followed a game-thread sleep of 4 ms or more ran 40-50 % slower on both the game thread (in-between pass 14.6
// instead of 10.2 ms) and the render thread (+6-9 ms), in every renderer configuration: the cores clock down while
// both threads wait. Such sleeps come when the game is ahead of its step slots, which the faster render thread of the
// record worker makes three times as frequent there (98 instead of 26 a minute), so the frames after them became the
// slowest 1 %. Spinning the short waits keeps the cores busy; waits longer than n (30 fps, menus) sleep as before.
// Phone check (Xiaomi 14, 60 fps, n=2, against the pacing half without the worker; the spin probe
// WWHD_VSYNC_PRECISE=1 WWHD_VSYNC_SPIN_US=8000): worker + spin lava +5.3 fps, p99 -0.1 ms, 1 % low +0.2 fps (worker
// alone: +4.9 fps, p99 +2.9 ms, 1 % low -3.9 fps); cliff +7.9 fps, p99 -2.2 ms, 1 % low +3.9 fps. At 30 fps (no
// interpolation, Xiaomi 14, 5-minute runs) spinning gave no pacing gain (lava 1 % low 20.7 instead of 21.3 fps, p99
// 48.3 instead of 47.0 ms) and the phone warmed faster (+0.04 C/min lava, about +0.1 C/min cliff): off there.
#pragma once
#include <chrono>
#include <cstdint>

namespace threads {
// park_sleep_until (runtime.h), spinning instead of sleeping (std::this_thread::yield between clock reads)
void park_spin_until(std::chrono::steady_clock::time_point tp, void (*before_resume)() = nullptr);
}  // namespace threads
