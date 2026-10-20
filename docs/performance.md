# Performance notes

Measurements and fixes from profiling the port on an Apple Silicon Mac, plus open leads.

## Native Windows checkpoint (2026-10-05)

Use `-DCMAKE_BUILD_TYPE=Release` with native LLVM/Ninja. The previous Windows
build cache selected Debug, leaving runtime/renderer code at `-O0`; Release now
uses `-O3 -DNDEBUG`. Exact guest FP settings remain unchanged.

Compared with the Windows port's performance notes, this renderer already has
asynchronous submission slots, mapped upload arenas,
grouped render passes, packed shader state, and descriptor reuse. Three missing
optimizations have been adapted:

- Windows guest deadline sleeps use SDL's high-resolution timer, including the
  sleep phase of precise vsync waits. Guest core release, freeze gates and
  completion callbacks retain their existing order.
- Index conversion retains thread-local scratch capacity. Immutable GPU uploads
  are copied before that scratch can be reused, including recursive AO replay.
- Full texture content checks use BSD-licensed xxHash v0.8.3. Every mip and byte
  is still checked; sparse-check cadence and disk cache formats are unchanged.
  (Since 2026-10-06 both renderers decide when to check by page write tracking,
  see "Texture change detection" below; the sparse check is only the fallback.)

Swapchains now prefer mailbox when advertised, with FIFO fallback. The existing
override in **this** checkout is `WWHD_VK_PRESENT_MODE` (the other checkout uses
`WWHD_VULKAN_PRESENT_MODE`). To test presentation throttling separately:

```powershell
$env:WWHD_VK_PRESENT_MODE = 'immediate'
./build/windows/wwhd.exe
```

Immediate mode may tear. Remove the variable to restore mailbox/FIFO selection.
This setting retains guest GX2 pacing and does not guarantee 60 fps.

The Release timer probe (`build/windows/sleep_test.exe`) compares 80 five-ms
deadlines with `timeBeginPeriod(1)`, matching the game. One local run measured
standard-library mean/p95 lateness of 0.639/1.011 ms and SDL mean/p95 lateness of
0.249/0.532 ms. These are timer measurements, not gameplay FPS results. All five
CTest checks and the GPU renderer smoke test with synchronization validation
passed on the RX 9070 XT, with no reported validation errors.

Steady 60 fps at 3x resolution and full-resolution AO remains
unverified. Compare the same scene, camera and settings with warm caches;
disable validation and detailed profiling for FPS comparisons. Frame
interpolation increases displayed frames but still needs each rendered frame
to fit its budget.

## Windows draw batching default (2026-10-05)

Windows now enables the existing bounded 2,048-draw asynchronous batching by
default, with a maximum of two mid-frame submissions. Explicit values of
`WWHD_VK_DRAW_BATCH` and `WWHD_VK_DRAW_BATCH_CAP` retain their existing behavior.

An isolated saved Outset scene at 3x resolution, AO mode 2, high-resolution AO
and anisotropy compared the same native Release executable with batching enabled
and disabled. VulkanProfiler and validation were disabled, implicit Vulkan
layers were disabled equally, presentation was immediate, and GamePad/input
were disabled. Each run restored the same state at renderer frame 1,200 and
measured frames 2,401–3,600 after warming. All runs averaged 6,210 draws/frame.

| Draw batch | FPS | Renderer CPU ms/frame |
| --- | ---: | ---: |
| 2,048 draws, trial A | 48.96 | 10.6511 |
| 2,048 draws, trial B | 51.09 | 10.3646 |
| Disabled (`0`), trial A | 33.39 | 10.5077 |

Keeping batching enabled was about 47–53% faster in this local fixture, with
similar renderer CPU time. That suggests improved overlap or pacing rather
than cheaper CPU draw preparation. There is only one unbatched sample, and
this does not establish a universal gain or sustained 60 FPS.

These comparisons used a local experimental build with bounded CPU caches and
pass-state preservation enabled equally in every run, and dynamic depth/raster
state disabled. They isolate the batching toggle in that build; they are not
a benchmark of an otherwise unmodified upstream checkout. Those experiments
are excluded from this change. A separate synchronization-validation game run
and the actual-device renderer smoke test passed in the local build.

## How to profile

Run a scripted session, then sample it with macOS `sample` (1 ms stacks for every thread):

```sh
# copy the save and shader cache so the test can't touch your real ones
cp -R save /tmp/wwhd-save; cp ~/Library/Caches/wwhd/shaders.bin /tmp/shaders.bin
WWHD_SHADER_CACHE=/tmp/shaders.bin \
WWHD_PRESS=600-610:8000,900-910:8000,1200-1210:8000,1500-1510:8000 \
WWHD_STATE_LOAD_AT=1800:2 WWHD_DUMP_FRAMES=2400 \
./build/cmake/wwhd --save /tmp/wwhd-save > run.log 2>&1 &
sleep 45; sample $! 20 -file sample.txt; kill $!
```

`WWHD_PRESS` presses A to get past the title screen, `WWHD_STATE_LOAD_AT=1800:2` loads save-state
slot 2 at TV frame 1800, and `WWHD_DUMP_FRAMES=2400` writes `frame_2400.png` (in the working
directory) to check that the run still renders correctly. The "Sort by top of stack" section of
`sample.txt` shows where the CPU time goes. `run.log` prints the shader and pipeline counts every
few seconds.

### Windows on ARM64: sampling the frame threads

macOS `sample` has no counterpart on Windows that works without administrator rights (ETW / WPR
need them). `WWHD_SAMPLE_THREADS=<prefix>` turns on a sampling profiler inside the game
(`runtime/src/platform/thread_sampler.cpp`, Windows on ARM64 only): about 1000 times a second the
render thread and the game's main thread are suspended, their stacks are copied (unwound with
`RtlVirtualUnwind`, then the frame-pointer chain) and they are resumed. `WWHD_SAMPLE_AFTER=<s>`
delays the start (skip boot and loading), `WWHD_SAMPLE_SECONDS=<s>` limits it. It writes
`<prefix>.samples` and `<prefix>.modules`; `tools/bench/thread_profile.py` reports the time by module
and function, and for time spent in the Vulkan driver or the kernel the game executable's call site
that led there:

```sh
tools/bench/run_bench.py --binary build/prof/wwhd.exe --state-dir my_states --scene outset --fps 60 \
    --runs 1 --out build/bench/prof --variant "prof:WWHD_SAMPLE_THREADS=prof,WWHD_SAMPLE_AFTER=35,WWHD_SAMPLE_SECONDS=40"
tools/bench/thread_profile.py build/bench/prof/prof_01/prof --exe build/prof/wwhd.exe --thread render
```

Build with debug information (`-DCMAKE_BUILD_TYPE=RelWithDebInfo`) so the executable's functions and
lines can be named (`llvm-symbolizer`, e.g. from llvm-mingw); functions of system and driver DLLs are
named after their nearest export, which names entry points but not internal functions. On a
Snapdragon X, return addresses saved on the stack carry a pointer-authentication code in their upper
bits; the sampler and the script strip it (user addresses fit in 48 bits).

First results (Snapdragon X Elite, Adreno X1-85 driver 31.0.152.1, `devel` of 2026-10-11, 60 fps,
llvm-mingw build): a hilltop view of Outset's village (about 3,970 draws/frame) ran 59.7 fps with
29.86 logic steps/s (116 fps uncapped); the render thread was idle about 20% of the time and spent
about 22% inside the Vulkan driver, spread over many calls: `vkCmdDrawIndexed` about 7-8%,
`vkCmdBeginRendering` about 3-4% (about 49 passes/frame), descriptor set allocation and updates
about 3%, the submission path about 4% (the driver destroys a kernel synchronization object in
`vkResetFences` and creates one in `vkQueueSubmit`, about 0.5% of the thread together). On this
scene, interleaved uncapped A/B runs (3 each) of `WWHD_VK_DRAW_BATCH=4096`, `WWHD_VK_DRAW_BATCH=0` and
`WWHD_VK_NARROW_BARRIERS=1` stayed within the runs' spread (+2.2%, +2.7%, -3.4% in median fps), so the
defaults are unchanged.

### Fixed-scene benchmark

`tools/bench/run_bench.py` runs the game headless (or with `--visible` windows, which presentation
and vsync pacing need) from a save state with scripted input, one game at a time, with a fresh copy
of the save and its own caches and settings files. A/B variants run interleaved:

```sh
tools/bench/run_bench.py --binary build/cmake/wwhd --state-dir my_states --scene outset \
    --fps 60 --renderer vulkan --runs 6 --out build/bench/sync \
    --variant old:WWHD_VK_LAZY_DRAW_DONE=0,WWHD_VK_ASYNC_PRESENT=0 --variant new:
```

`--state-dir` holds `slot<N>.bin` files made with the game's save-state keys (outset: slot 3,
windfall: slot 2, or `--slot`). `--fps 30|60|120|240|true60`, `--display-hz n` (the refresh rate
120/240 fps are capped to; 0: no cap, for hidden-window runs), `--uncapped` (throughput),
`--renderer metal`.
The state is loaded at TV frame 450 (A presses from frame 120 skip the intro), the input starts 200
frames later and is timed in game seconds, so 30 and 60 fps runs see the same input. Each run's
`[prof]` reports (render-thread profiler, `docs/vulkan.md`) after two warm-up windows are averaged;
`summary.json` / `summary.csv` hold the per-variant median, min, max and spread of frame time,
swaps/s, logic steps/s, render-thread CPU and per-phase ms, GPU waits, game-thread waits, uploads by
kind, unique guest bytes and draw classes. The load average before and after each run is recorded.

## 120 and 240 fps frame interpolation (2026-10-07)

Apple M3 Max, 120 Hz ProMotion display, Outset (slot 3, walking and turning), 40 s per run,
`tools/bench/run_bench.py` (one game at a time), Vulkan (MoltenVK) unless noted. Hidden runs present
nothing; visible runs present to the 120 Hz display. "In-between" is the share of the planned
in-between frames drawn (paced), frames per step is interpolation's own count.

| Mode | swaps/s | logic steps/s | in-between | frames/step |
|---|---|---|---|---|
| 60 fps, hidden (2 runs) | 59.95 | 29.99 | (not paced) | 2.00 |
| 120 fps, hidden (2 runs) | 119.28 | 29.94 | 99.3% | 3.98 |
| 240 fps, hidden, no display cap (2 runs) | 146.48 | 29.78 | 55.2% | 4.87 |
| 240 fps, hidden, capped to the 120 Hz display | 119.72 | 29.97 | 99.7% | 3.99 |
| 120 fps, hidden, `--display-hz 60` (fallback) | 59.94 | 29.99 | 100% | 2.00 |
| 60 fps, visible | 59.85 | 29.94 | (not paced) | 2.00 |
| 120 fps, visible (2 runs) | 114.72 | 29.67 | 94.2% | 3.83 |
| 240 fps, visible (drawn at 120) | 115.18 | 29.67 | 94.7% | 3.84 |
| 120 fps, visible, Metal | 108.07 | 29.13 | 88.7% | 3.66 |
| 240 fps, visible, Metal (drawn at 120) | 111.66 | 29.57 | 92.7% | 3.78 |
| 240 fps, uncapped, paced | 198.51 | 29.36 | 82.3% | 6.75 |
| 240 fps, uncapped, not paced | 206.64 | 25.84 | - | 8.00 |
| 30 fps, uncapped | 190.78 | 190.78 | - | 1 |

- 240 fps is limited by the render thread on this machine: it needs 4.4–4.7 ms of CPU per frame,
  more than one 4.17 ms tick, so an in-between pass takes two ticks and the pacer plans 3–5 of the
  7 in-between frames (the game thread needs 3.8 ms per logic pass and 1.3 ms per in-between pass,
  `WWHD_INTERP_PASS_STATS=1`). Uncapped, the same scene draws ~200 frames a second.
- Logic stays at 29.6–30.0 steps a second in every paced mode; unpaced 240 fps uncapped shows what
  pacing prevents (25.8).
- Visible runs present at ~115 of 120 Hz (as 60 fps runs present at 59.85 of 60).
- A camera trace (`WWHD_INTERP_CAM_TRACE`) at 240 fps shows the eye advancing in equal eighths of
  the step (t = 1/8 … 7/8, exact at t = 1) and steps re-planned with fewer in-between frames
  spaced at 1/7 etc.
- 60 fps output is unchanged: TV frames 700–703 (exact and halfway frames) are bit-identical to
  devel; later frames differ from devel by as much as two devel runs differ from each other.
- Before the per-kind averages and the spike limit, one 76 ms in-between pass kept all in-between
  frames off for 10 s at 120 fps (28.8 logic steps/s and 1% in-between frames in that window).

## Frames that miss their vsync (issue #125, 2026-10-11)

A computer that draws a little under 60 frames a second (Uncapped ~60) got about 30 at 60 fps and
40–50 at 120/240 fps. The cause was the virtual vsync (`gx2_core.cpp`): a flip executed only on a
tick of a fixed 59.94 Hz grid (2/4 times as fast at 120/240 fps), and the game waits for its flip
before it goes on. A frame ready 1 ms after its tick waited for the next one, so every frame took
two vsyncs (33.4 ms, double-buffered vsync). Now (`gx2/vsync_pacing.h`) a frame that is late flips
as soon as it is ready (GX2WaitForVsync waits for that frame in 0.25 ms steps instead of sleeping
to the next tick); lateness under one 59.94 Hz vsync keeps the grid, more moves the grid to the
flip. Frames that are on time flip exactly on the grid, as before. Two pacing changes in
`interp.cpp` go with it: a logic step that waited for its time starts at that time, not when the
sleep timer woke up (0.2–1 ms later every step), and at 120/240 fps a step that starts late keeps
the schedule (less than a step late), so the game catches up with fewer in-between frames.

Measurement aids (test only): `WWHD_TEST_RENDER_LOAD_MS=x` busy-waits x ms on the render thread per
frame, `WWHD_TEST_GAME_LOAD_MS=x` on the game thread at each swap; `WWHD_LOG_FLIP_PACING=1` logs the
flip intervals (p50/p95/p99/max, count over 25 and 34 ms) every 300 flips.

Apple M3 Max, Windfall (slot 2, walking), `tools/bench/run_bench.py`, hidden windows (nothing is
presented, so these are the game's own flips; the display's vsync and the Vulkan present modes are
not involved), 25 s per run, old and new interleaved in one binary, render thread +15 ms per frame
(17.7 ms in all, Uncapped 56 fps). fps = flips (swaps) per second; 120/240 fps uncapped by the display
(`--display-hz 0`):

| Limit | Renderer | old fps / logic steps/s | new fps / logic steps/s |
|---|---|---|---|
| 60 | Vulkan | 30.2 / 15.1 | 56.3 / 28.2 |
| 60 | Metal | 30.3 / 15.2 | 54.7 / 27.3 |
| 60, game thread +15 ms instead | Vulkan | 30.4 / 15.2 | 56.7 / 28.3 |
| 60, paced | Vulkan | 30.4 / 15.2 | 56.4 / 28.2 |
| 120 (paced) | Vulkan | 40.3 / 20.2 | 56.6 / 30.0 |
| 240 (paced) | Vulkan | 48.2 / 24.1 | 56.7 / 30.0 |
| 120 (paced) | Metal | 40.2 / 20.1 | 54.5 / 30.0 |
| 240 (paced) | Metal | 48.1 / 24.1 | 54.6 / 30.0 |
| true 60 | Vulkan | 30.3 / 15.1 | 56.2 / 28.1 |
| Uncapped | Vulkan | 56.0 / 28.0 | 55.7 / 27.9 |
| 30 | Vulkan | 30.0 / 30.0 | 30.0 / 30.0 |

At 60 fps with the load, the flip intervals were 33.4 ms (p50 and p95) before and 18.1 ms (p95)
after, with none over 34 ms. Without the load the rates are unchanged (60 fps: 59.9 flips/s, 30.0
logic steps/s, p95 16.7 ms on both renderers; Vulkan 120/240 fps 119.8/238.9; true 60 59.9), and the
worst flip interval at 60 fps fell from 33.4 to 17.7 ms. Metal at 240 fps without a display cap
(this computer draws ~150) now draws 149 instead of 119 frames a second at 30.0 instead of 29.8
logic steps/s.

## 240 fps micro-stutter: step slots (issue #164, 2026-10-12)

At 240 fps with "Keep game speed" on, v0.2.13 hitched every step for seconds at a time and skipped
an in-between frame every few seconds. Two clocks disagreed: paced interpolation scheduled logic
steps on a wall clock of its own (33.333 ms, 30.000 steps a second, a late step keeping the
schedule since #125), while the virtual vsync draws 8 frames a step on the 59.94 Hz grid (33.367
ms). The schedule drifted 0.034 ms a step against the grid. Where it ran ahead, a logic pass slept
past its frame's vsync, that frame flipped late ("flips when ready", #125, keeping the grid) and the
step's in-between frames bunched up behind it at ~3 ms to catch up: one 10-12 ms interval and
several short ones every step, until the drift had crossed a whole vsync (~8 s on and ~8 s off in a
cycle of ~16 s). Where it ran behind, the 2 ms planning slack ran out every few seconds and the rest
of a step's blended frames were skipped. 120 fps has the same cycle at a smaller scale.

Now the schedule is the grid (`gx2/vsync_pacing.h`, step slots): a step's logic frame is due two
vsyncs after the previous step's (the game's swap interval, 29.97 steps a second as on the
console), and the logic pass starts one drawn frame before that slot (`interp_pacing.h`
`slot_step_start`). A logic frame less than a step late keeps the schedule and the next step plans
fewer in-between frames (the #125 behaviour); later than a step starts a new schedule. Two smaller
changes go with it: a logic pass that ran late keeps the in-between frames that still fit instead
of skipping all blended frames of the step (`skip_to`), and an in-between pass probe that
hits a hitch (at the start the first in-between pass is a probe, and it measured 75-112 ms in four
of six boots at 240 fps here) counts twice the old estimate and is retried after a second instead of
keeping in-between frames out for 10 s. `WWHD_TRACE_FLIPS=file` and `WWHD_TRACE_STEPS=file` write
per-flip and per-step traces.

Measurements (Apple M3 Max, Windfall, Vulkan, hidden windows, `WWHD_DISPLAY_HZ=240`, three runs per build, v0.2.12 /
v0.2.13 / the fix interleaved; 1-minute load 30-45 from other work, so the rates are noisy): at 240 fps, uneven flip
intervals (over 1.4 or under 0.6 frames) per second 13.5 / 21.0 / 8.3; at 120 fps 6.9 / 11.2 / 6.8; with 15 ms of
extra render work per frame (the #125 case) 44 / 51 / 51 fps; with 2.5 ms of extra game work 99 / 147 / 155 fps;
"Keep game speed" off unchanged. At boot, v0.2.13 measured its first in-between pass at ~85 ms and drew no in-between
frames; the fix retries that probe and draws ~90 % of them a second later. The late-step rule is `skip_to` (it
skips as few blended passes as make the rest of the step fit). Full table (three runs per build, interleaved, median; Windfall, Vulkan unless noted, hidden windows,
`WWHD_DISPLAY_HZ=240`, 36 s per run; 1-minute load 30-46 for every row, so absolute rates are depressed, the
comparison between builds is fair). Cells: fps, frame-time p50 / p99, uneven intervals per second, logic steps/s.

| Case | v0.2.12 | v0.2.13 | fix |
|---|---|---|---|
| 240 fps | 229.1, 4.17 / 8.34 ms, 7.99/s, 29.61 | 239.2, 4.17 / 10.61 ms, 19.05/s, 30.00 | 238.8, 4.17 / 5.24 ms, 1.85/s, 29.97 |
| 120 fps | 119.9, 8.34 / 8.34 ms, 0.08/s, 29.96 | 119.7, 8.34 / 14.30 ms, 6.56/s, 29.97 | 119.8, 8.34 / 8.34 ms, 0.08/s, 29.97 |
| 240, render +15 ms (#125 case) | 47.9, 20.85 / 20.86 ms, 23.95 | 56.6, 17.60 / 19.36 ms, 30.00 | 56.6, 17.62 / 18.99 ms, 29.97 |
| 240, game +2.5 ms | 119.6, 8.34 / 8.34 ms, 29.93 | 180.7, 5.01 / 23.50 ms, 10.7/s, 30.00 | 191.4, 5.08 / 8.15 ms, 11.8/s, 29.97 |
| Metal 240 | 109.3, 8.35 / 16.70 ms, 29.09 | 127.0, 7.66 / 13.36 ms, 30.00 | 126.3, 7.76 / 11.77 ms, 29.97 |
| 60 paced, render +15 ms | 29.8, 33.37 / 33.37 ms, 15.27 | 51.3, 17.44 / 33.37 ms, 28.65 | 51.4, 17.40 / 33.37 ms, 28.72 |
| 240, "Keep game speed" off | 235.2, 4.17 / 8.34 ms, 4.68/s, 29.41 | 239.4, 4.17 / 5.07 ms, 1.37/s, 29.93 | 239.5, 4.17 / 5.37 ms, 1.79/s, 29.94 |
| Boot at 240: in-between frames in the first 10 s | 2 / 1 / 1 % | 0 / 0 / 0 % | 91 / 85 / 81 % |

Uneven/s is left out where every interval exceeds 1.4 frames. v0.2.12 is tag 5c39d489 and v0.2.13 devel d7d96991,
each with only the trace and load aids added.


## Keep game speed at 60 fps (issue #178, 2026-10-19)

With 60 fps frame interpolation and "Keep game speed" on, a computer whose render thread cannot draw
60 frames a second still ran the game slower than real time: a Steam Deck reported 45 fps and 22.8
logic steps/s (render thread ~21 ms a frame). Two causes, both at 60 fps only:

- The in-between decision is taken when the logic pass has issued its frame, before the pass waits
  for its flip. With the render thread behind, that wait is most of the pass, so every in-between
  frame seemed to fit and every step took two render frames (here 24-25.5 steps/s at ~50 fps).
- 60 fps had no step schedule: a step that ran late (by up to the 2 ms planning slack, or more) was
  never made up for. 120/240 fps have used the step slots on the vsync grid since issue #164.

Now 60 fps uses the same step slots and late-step rule as 120/240 fps (`gx2/vsync_pacing.h`; a logic
pass less than a step late keeps the schedule), and the in-between decision of a 60 fps step counts
the logic pass at least at its recent duration and lets the next step start at most one frame late
(`interp_pacing.h` `logic_pass_elapsed`): that step then skips its own in-between frame if needed,
so the game keeps 29.97 steps a second and the computer draws as many frames as it can. Without the
load nothing changes.

Apple M3 Max, Outset (the pier, standing), Vulkan, hidden windows (`WWHD_DISPLAY_HZ=0` at 120/240
fps), "Keep game speed" on, three runs per build interleaved, ~60 s each after interpolation is
switched on, 1-minute load 4-9. Cells: logic steps/s, fps (flips/s), flip interval p50 / p99,
in-between frames drawn (median of the runs).

| Case | before | after |
|---|---|---|
| 60 fps, render +15 ms | 25.52, 51.0, 19.58 / 20.23 ms, 100% | 29.96, 50.0, 19.86 / 22.09 ms, 67% |
| 60 fps, render +19 ms (one run each; a Steam Deck's frame time) | 21.12, 42.3, 23.64 / 24.37 ms, 100% | 29.95, 42.0, 23.64 / 26.75 ms, 40% |
| 60 fps, no load | 30.00, 59.9, 16.69 / 17.63 ms, 100% | 30.00, 59.9, 16.68 / 18.60 ms, 100% |
| 120 fps, render +15 ms | 30.00, 50.7, 19.70 / 20.34 ms, 23% | 29.98, 50.7, 19.71 / 20.39 ms, 23% |
| 240 fps, no load (~110 fps) | 30.00, 109.1, 8.93 / 15.38 ms, 38% | 30.00, 112.9, 8.29 / 14.02 ms, 39% |

Logic steps/s here are the game's own 300-step averages (30.0 = 29.97 within their rounding). The
60 fps p99 without load varies between runs of the same build (17.6-18.6 ms before, 17.6-20.6 ms
after; one run of each build had a single interval over 25 ms). The simulation in
`runtime/tools/interp_pacing_test.cpp` covers render loads of 15, 18 and 21 ms a frame and game
passes a little over a vsync (before: 21-29 steps/s; after: 29.97, with the frames the renderer
draws).

## Keep game speed at 60 fps: skips at the deficit rate (issue #254, 2026-10-20)

A phone whose passes take a little longer than a vsync (Xiaomi 14, ~17.2 ms) still lost game time at
60 fps with Keep game speed on: 29.84-29.85 logic steps/s while it skipped fewer in-between frames
than its frame time needs. Two causes, at 60 fps only:

- After a skipped in-between frame the next logic pass slept until its step's time, so a skip that
  came early saved less than a frame (waits after 3-4 % of the steps).
- A step that one slow pass (a 20-40 ms hitch) left more than a step late started a new step schedule,
  and that time was lost (5 restarts a minute).

Now the next logic pass starts at once after a skip and keeps what the skip saved as a lead on the
step schedule (`interp_pacing.h` `wait_after_drop`): the step lateness is the accumulated deficit,
and every skip takes a whole in-between pass off it. A step up to two steps late keeps the schedule
and catches up by skipping (`keep_window`; a scene load still restarts it). 120/240 fps and Keep
game speed off are unchanged.

Xiaomi 14 (adb), Windfall waters (portable state, scripted stick), 60 fps, Keep game speed on, warm
shader cache, cool start (battery < 33 C), 60 s measured, runs interleaved C B B C A. A = before
issue #178's fix, B = with it, C = this change. Steps/s: bench (frame trace) and the step trace;
skipped: in-between frames skipped in the step trace, and the share that keeps the speed at the
run's own mean frame time; restarts / waits: step trace N / W per run.

| Run | steps/s | trace steps/s | fps | skipped (needed) | restarts / waits |
|---|---|---|---|---|---|
| A | 29.20 | 29.20 | 58.1 | 0 % (5.2 %) | (no schedule) |
| B 1 | 29.90 | 29.84 | 57.0 | 8.4 % (9.3 %) | 5 / 69 |
| B 2 | 29.90 | 29.85 | 57.9 | 5.9 % (6.7 %) | 5 / 53 |
| C 1 | 30.00 | 29.97 | 57.6 | 7.4 % (7.4 %) | 0 / 0 |
| C 2 | 30.00 | 29.94 | 57.9 | 6.4 % (6.6 %) | 1 / 0 |

The skips still come in groups (a median of 5 steps apart, p90 25-31): the scene's cost changes from
second to second, and the skips follow it.

## 40 fps (2026-10-20)

A frame-rate choice between 30 and 60 fps for 40, 80 and 120 Hz screens (Steam Deck LCD at 40 Hz,
Steam Deck OLED at 80 Hz, phones with 120 Hz panels): 3 logic steps over 4 frames, a frame every 1.5
vsyncs of 59.94 Hz (25.03 ms, 39.96 fps; the game keeps 29.97 steps a second, as at every rate).

- The virtual vsync ticks every 6 quarter vsyncs, and a logic step's frame is due on its step slot
  (every 8 quarter vsyncs) as at 60 fps. So one step in three has two frames before the next step's
  slot and gets an in-between frame (logic pass halfway, record pass exact, as at 60 fps); the other
  two are drawn once, their logic pass drawn exactly and recorded as the next step's "before" (a
  record logic pass). Without slots (Keep game speed off) the first step of every three.
- Keep game speed (the 60 fps setting) works as at 60 fps: a step with an in-between frame draws it
  when the next step starts at most a frame late, a skip is followed at once by the next step (a
  lead), and a step up to two steps late keeps the schedule. A device whose logic passes alone take
  a step or longer draws no in-between frames (each restart of the schedule would otherwise let one
  through).
- On a display whose rate is not a multiple of 40 (60, 90, 144 Hz) the frames still go out every
  25 ms, and the screen shows them for 1 and 2 refreshes in turn. Settings and the log say so; the
  choice is left to the player (30 or 60 fps are even there). Android asks for 120 Hz and prefers the lowest
  display mode above 60 Hz that is a multiple of 40 (80, else 120; 40 Hz only without one): asked
  for 40 Hz, with its 40 Hz mode preferred, a Xiaomi 14 stayed at 60 Hz.
- Android's automatic frame interpolation (Keep game speed off) pauses below 33.3 frames a second
  at 40 fps (5/6 of the rate) instead of 50, and resumes at 21 ms of render thread per 30 fps
  frame instead of 14. 60 fps and up are unchanged.
- Motion: the in-between frame is drawn halfway, so the picture moves 1/2, 1/2, 1, 1 steps per frame
  over a group (frames evenly spaced, motion not quite). Even motion (3/4 of a step per frame) would
  need every frame blended and recorded.

The simulation in `runtime/tools/interp_pacing_test.cpp` (`test_steps_40`): fast passes 39.96
flips/s, 29.97 steps/s, every interval one frame; on 40/80/120 Hz (multiples of 39.96) every frame
1/2/3 refreshes, on 60 Hz half 1 and half 2; 26/28/31 ms passes skip 17/44/79 % of the in-between
frames (as many as keep the speed) at 29.97 steps/s with no wait after a skip; hitches of 25/40 ms
every 300 passes are made up for.

Xiaomi 14 (adb), Windfall waters (portable state, scripted stick), 40 fps, Keep game speed on, warm
shader cache, 60 s measured after 10 s. The panel rate is HyperOS's choice (the display service
ignores the app's preferred rate): 60 Hz in run 1 (frames shown for 1 and 2 refreshes in turn, the
Settings note shown), 120 Hz in run 2. Flips: the virtual vsync trace over the whole run; steps/s:
the game's 300-step log (and the logic frames in the trace, which include two scene-load restarts).

| Run | panel | steps/s | fps | flip interval p1 / p50 / p99, sd | in-between drawn |
|---|---|---|---|---|---|
| 1 | 60 Hz | 30.00 (29.86) | 39.8 | 21.0 / 25.03 / 30.2 ms, 3.4 ms | 99.5 % |
| 2 | 120 Hz | 30.00 (29.93) | 40.0 | 21.0 / 25.02 / 30.4 ms, 2.4 ms | 99.8 % |

## Even frame pacing (2026-10-18)

With frame interpolation the passes of a logic step take different times: the logic pass runs the
game, an in-between pass only draws. Where the game thread is the limit (Xiaomi 14 with "Record
graphics on a second thread": logic pass 19-20 ms, in-between pass 12-14 ms) frames were ready 20
and 13 ms apart, and on the 120 Hz screen a third of the 60 fps frames stayed for one or three
refreshes instead of two. Asking the compositor for present times did not help (the times were
already past; a feedback on the measured interval fell to 24 fps; holds landed on wrong refreshes).

Now each frame's swap carries its intended interval (the step's length over its planned frames).
The Vulkan renderer submits the frame's work at once but holds its present call until one interval
after the previous frame, less 1/128 of an interval, so the slower kind of frame sets the rhythm and
a schedule pushed late by a hitch comes back within about a hundred frames. A late frame goes out at
once, no frame waits longer than one interval, and the game never waits for a held frame. Paced
steps plan only in-between counts that divide the display's refreshes per step (on a 120 Hz screen
one or three, never two: 90 fps would stay 1, 1 and 2 refreshes). On by default on Android, off
elsewhere; `WWHD_INTERP_EVEN_PACING=0/1` overrides, `WWHD_VK_PRESENT_TRACE=file` writes ready,
planned and actual present times per frame, `WWHD_TEST_GAME_LOAD_MS=x:y` loads logic and in-between
passes unevenly to reproduce the case on a computer.

Xiaomi 14, Windfall, 120 fps setting paced to 60, frames shown for 1 / 2 / 3 refreshes
(SurfaceFlinger): record worker on 11.5-16.8 / 66.5-75.8 / 12.2-16.0 % without, 0.9-2.1 / 92.0-95.6
/ 2.0-5.0 % with it (five runs; in-between frames held about 4-5 ms, logic frames 1-2 ms); 60 fps
setting 82-84 % -> 87-94 % two-refresh frames. Record worker off (render thread is the limit, frames
already even): nothing is held, no change. fps unchanged; frame-time p99 about 1-2 ms higher. Mac
(MoltenVK, ProMotion, `WWHD_TEST_GAME_LOAD_MS=12:5.5`): 49 % -> 88-97 % two-refresh frames.

## Desktop CPU/GPU overlap and CPU path defaults (2026-10-07)

Apple M3 Max, MoltenVK, Vulkan renderer, `tools/bench/run_bench.py`, Outset (slot 3) and Windfall
(slot 2) saved scenes, 40 s of scripted walking, variants interleaved, medians [min..max] over 4-6
runs per variant. "old" = `WWHD_VK_LAZY_DRAW_DONE=0 WWHD_VK_ASYNC_PRESENT=0` (the previous desktop
behaviour: vkDeviceWaitIdle at every GX2DrawDone, a drained queue at every present).

**Lazy DrawDone and asynchronous presentation** (visible windows):

| Scene | old | new |
|---|---|---|
| Outset 60 fps (vsync) | 59.35 swaps/s, GPU wait 3.36 ms/frame, game waits 6.22 ms at DrawDone | 59.49, 0.37 ms, 3.20 ms |
| Windfall 60 fps (vsync) | 59.72, GPU wait 4.22 ms, render thread in ops 11.06 ms | 59.74, 0.00 ms, 6.78 ms |
| Outset uncapped, immediate present | 102.5 [96.6..105.2] swaps/s | 115.7 [115.0..143.5] |
| Windfall uncapped, immediate present | 103.2 [101.9..103.6] swaps/s | 157.5 [143.5..158.5] |
| Outset paced 60 fps, render thread +7 ms | 40.3 swaps/s, 48.7% in-between frames, 27.4 steps/s | 59.2, 99.7%, 29.6 |
| Outset paced 60 fps, render thread +8 ms | 35.3 swaps/s, 33.2% in-between frames, 27.4 steps/s | 59.05, 99.7%, 29.55 |

Render-thread CPU time is unchanged; the GPU drain was idle time on both sides. The +7/+8 ms rows
add a busy-wait to the render thread per frame (a local evaluation aid, not in the code), the regime
of slower machines (upstream issue #7): there the old path dropped most 60 fps frames and the game
ran 9% slow. The SDL host (Vulkan-only build, the Windows/Linux window host) shows the same:
uncapped Outset 105.5 -> 144.1 swaps/s, GPU wait 2.96 -> 0.43 ms.

**CPU paths** (`docs/vulkan.md`, all 15 off vs all on, new sync defaults, headless):

| Scene | render-thread CPU off -> on | uploads MiB/frame |
|---|---|---|
| Outset 60 fps | 6.23 -> 5.56 ms (-10.8%) | 22.4 -> 18.6 |
| Windfall 60 fps | 5.83 -> 5.36 ms (-8.0%) | 28.2 -> 26.3 |
| Outset 30 fps | 7.56 -> 6.76 ms (-10.5%) | 22.5 -> 18.7 |
| Outset uncapped | 6.37 -> 5.46 ms (-14.3%), 150 -> 166 swaps/s | 21.9 -> 18.4 |

Leaving one path out (Outset 60, 2 runs each, run spread about 0.4 ms) costs: specialized index
conversion 0.31 ms, shader state memo 0.28, uniform snapshot reuse 0.25, sampler memo 0.09, sparse
hash memo 0.09, vertex bind skipping 0.08, redundant bind skipping 0.06, feedback image reuse 0.06,
shader address memo 0.05, vertex snapshot reuse 0.03 (and 3.3 MiB/frame of uploads); descriptor
ranks, vertex history reuse, fetch memo, pipeline lookaside and the shader-key dirty classifier
are neutral within the noise (-0.06 to +0.01 ms). None costs measurably, so all 15 are defaults.

Correctness: frame dumps at six points of a scripted walk (TV and GamePad) are byte-identical
between old and new and with the CPU paths, except a 20-pixel band at the sea horizon that also
differs between two runs of the same configuration (run-to-run nondeterminism); save state save and
reload in-game (slot 3 -> save slot 5 -> load slot 5) works with both; Khronos validation over the
gameplay run reports no errors (one known warning: a fragment output without attachment, also with
the old settings); `--renderer-smoke` passes with validation in both builds, with and without the
CPU paths.

**Where the render-thread time goes** (all defaults on, Outset 60 fps, per frame; `[prof]` lines):
draws 5.3 ms of 6.5 ms in ops (3,230 draws, 1.74 us each), register writes 0.9 ms (39,000 writes).
Per draw phase: submit/draw command 1.03 ms (includes the batched queue submissions), shader lookup
0.62, uniforms 0.63, vertex snapshots 0.62, textures 0.44, pass 0.40, pipeline 0.38, descriptors
0.37, record 0.31, indices 0.23, targets 0.20. Uploads are 18.6 MiB per frame, 15.7 of them vertex
copies, but the copied guest ranges hold only 2.2 MiB of unique bytes; an interpolation hold frame
copies the same 18.6 MiB again. 55% of Outset draws (12% on Windfall) change only buffer pointers
or ALU constants since the previous draw.

## Where the time goes

Busy (non-waiting) samples in one 20 s in-game run with 60 fps interpolation on, after all the fixes
below (Apple M4):

| | Metal | Vulkan |
|---|---|---|
| Recompiled game code (`f_XXXXXXXX`) | ~3,600 | ~3,400 |
| Renderer (`gfx::`/`gfxvk::`, `gx2::`, driver) | ~2,300 | ~3,300 |
| Other (memcpy, clocks, malloc, objc, …) | ~2,900 | ~3,100 |

The recompiled code is about 17% of one core, and the GX2 render thread is about 30% busy, so the
CPU translation isn't the bottleneck on this machine; the renderer work matters more on slower CPUs
(upstream issue #7).

## Fixes

### Both-renderer build crashed at the first shader with Homebrew boost installed

With Metal and Vulkan in one app, `CMakeLists.txt` added Vulkan's and glslang's include directory
(`/opt/homebrew/include`) to `wwhd` as a normal `-I` path, ahead of the vendored Cemu headers. With
Homebrew's `boost` installed, `metal_draw.mm` compiled against Homebrew's `static_vector` while
`cemu_latte` used the vendored one, so `LatteDecompilerShader` was 968 bytes on one side and 920 on
the other, and the first translated shader read a garbage `strBuf_shaderSource` (both renderers
crashed at boot). Those directories are now added as `SYSTEM`, which is searched after the project's
own paths. A Metal-only build never had the problem. With the fix, the 2,241 shaders of a test run
are byte-identical to the Metal-only build's.

### Pipeline queue scanned every frame (`build_pending_pipelines`)

At startup the shader cache and the head start queue ~250–280k pipeline recipes. A recipe is built
in the background once both of its shaders have compiled. `cache_warm_step` runs once per frame
inside `gfx::swap()` with a budget of 16 builds, but the budget only limited builds, not checks. So
every frame walked the whole queue, doing three hash lookups per recipe against a table of ~260k
shaders.

Each per-frame call now checks at most 2,048 recipes and resumes from a cursor on the next frame.
`--warm-shaders` passes an unlimited budget and still scans the whole queue.

| same 20 s run | before | after |
|---|---|---|
| `build_pending_pipelines` self time (render thread) | 6,086 samples (~40%) | 29 |
| whole-process CPU | 94% | 62–67% |
| pipelines built by the end of the run | 20,241 | 28,909 |
| draws skipped while a pipeline compiles | 373 | 247 |

### Pipeline recipes appended to the cache again

Pipelines built from the queue aren't recorded (they're already in the file). But when the game
needed a pipeline before its queued recipe was built, the normal path recorded it again. About 20% of
the pipeline records in a long-used `shaders.bin` were exact duplicates (33k of 170k), and the file
grew every session.

`g_known_pipelines` holds a hash of every recipe loaded from the cache file and the head start.
Recipes already in it aren't queued twice or written again. In the test run the cache grew 0 bytes
(it grew ~157 KB per run before), and startup queues 136k pipelines instead of 165k.

### Texture-coordinate snapping rewrote each shader 32 times (`snap_texcoords`)

Before compiling, every shader source is rewritten to snap 2D texture coordinates to the 1/256 texel
grid. The old code searched the whole source once per texture slot (32 slots) and inserted text in
place. Background compiles from `cache_warm_step` ran this on the render thread. It now makes one
pass over the declarations and one over the sample calls, then builds the output once.

The output must not change: the system Metal cache is keyed by the source, so any byte difference
would recompile every shader. Old and new produce identical output for 4,292 shaders dumped from a
play session (`WWHD_DUMP_SHADERS=1`), plus hand-written edge cases (nested calls, two-digit slots,
non-2D textures, unbalanced parentheses). The new version is 25× faster (301 ms → 12 ms for that
set). In the 20 s run, memchr/memcmp under `compile_msl` dropped from ~670 samples to ~160.

### Render thread polled for compiles (`wait_compiled`)

When a draw needs a shader or pipeline that's still compiling, the render thread waits up to 25 ms
per frame (`WWHD_COMPILE_WAIT_MS`) before skipping the draw. It used to poll: `usleep(100)` plus a
clock read in a loop. Metal's completion handlers now publish the result through `compile_done`,
which signals a condition variable that `wait_compiled` sleeps on until the result arrives or the
time runs out.

| same 20 s run | before | after |
|---|---|---|
| clock reads (`mach_continuous_time`) | 423 samples | 24 |
| draws skipped while a pipeline compiles | 247 | 0 |

The render thread now wakes as soon as a compile finishes instead of up to 0.1 ms (plus timer
slack) later, so fewer waits run out of time. Busy samples for the whole process went from ~10,400
to ~9,900 with both of these changes.

### Vulkan: fixed 2 ms spin before every vsync (`park_sleep_until`)

With the Vulkan renderer on macOS, the game thread's vsync wait slept until 2 ms before the vsync and
busy-waited the rest, because sleep timers wake late. Measured in game, they wake 0.25–1 ms late (rarely
1.75 ms), so most of the 2 ms was spent spinning: ~1,700 samples, 15% of Vulkan's busy CPU. The window
now follows the measured lateness: the largest of the last 120 wakes plus 250 µs, kept within
0.5–2 ms; a wake past the vsync puts it straight back at 2 ms. `WWHD_VSYNC_SPIN_US=n` fixes the window.

| Vulkan, same 20 s run | fixed 2 ms | adaptive |
|---|---|---|
| clock reads in the vsync wait | 1,844 samples | 768 |
| busy samples, whole process | 11,393 | 9,806 |

Over a 60 s run, 20 of ~3,200 waits woke after the vsync (none with the fixed window), all by under a
millisecond of a 33 ms frame. Swaps slower than 36 ms were the same in both: 2.

### Metal: shader key rehashed on every state change (`stage_state_hash`)

A draw re-derives its shaders whenever a shader-relevant register changed (`g_shader_state_gen`):
~2,950 lookups per frame with 60 fps interpolation, ~60% of which gathered exactly the same ~400 words
as the previous lookup for that stage (the generation also moves for registers that aren't in the key,
and vertex and pixel shaders share it). Each lookup hashed all of them in a serial multiply chain. The
last gathered words and result are now kept per stage (two buffers, swapped on a change); an identical
gather returns the previous result after a `memcmp`. Same input, same key, by construction.

### Metal: narrow shader keys (2026-10-20)

The Metal renderer translated a shader once per `stage_state_hash`: ~380 register words per stage,
including all 18 texture units and 54 samplers, every render-target word and, for vertex shaders, the
pixel-shader state. It now uses the two-level key of the Vulkan renderer (docs/vulkan.md, "Shader
keys"; the linkage words are shared, `gfx/shader_linkage.h`) plus what the MSL translation reads
beyond the GLSL one: for vertex shaders the whole PS input table (MSL declares every PS input in the
vertex output, written or not), whether rasterization is enabled and rect emulation; for pixel
shaders whether the depth buffer is active, the data type of each colour target the shader writes
and, per sampled unit, the colour target it reads through framebuffer fetch. A shader keeps its wide
key as its name in the recipe files (user cache, head start, template); replaying a record maps the
wide key it names to the shared shader, so existing caches and templates keep resolving their
pipelines. `WWHD_MTL_SHADER_KEY_VERIFY=1` translates again once per new wide key that the narrow key
gives an existing shader and compares the MSL and the binding metadata (`[gfx shader key] VIOLATION`);
`WWHD_MTL_WIDE_SHADER_KEYS=1` brings back the wide key for comparisons.

Outset route (60 s from save state slot 3, empty caches, `WWHD_SYNC_SHADERS=1`, fixed seed):

| Metal | wide key | narrow key |
|---|---|---|
| translations | 5,782 | 572 |
| pipelines | 3,050 | 337 |
| decompile time (ms) | 1,051 | 108 |
| `shaders.bin` | 6.8 MB | 0.69 MB |

The verify mode checked 5,210 wide keys on that route: no violations. Captured frames at Outset
+400 and +1,500 logic steps and Windfall +600 (TV, present and GamePad pictures and the target of
every draw of the frame: 441, 395 and 127 images) are byte-identical to the wide key's.

### Metal: index conversion (`build_indices`)

Indices were converted with a switch on the index format for every index and `insert` calls for every
triangle, into a vector allocated per draw. The format is now resolved once per draw (a template over
the reader), output is written into a presized buffer, and the draw reuses one vector (render thread
only; the one nested `draw` call, the full-size occlusion redraw, comes after the indices are
submitted). Old and new produce identical output for 12,675 cases (every primitive type, index format,
with and without an index buffer, counts 0–64); the new one is ~5× faster on large lists.

| Metal, same 20 s run | before | after |
|---|---|---|
| `get_shader_uncached` self time | 626 samples | 336 |
| `build_indices` self time | 291 | 57 |
| renderer samples | 2,701 | 2,262 |
| busy samples, whole process | 9,242 | 8,812 |

## Open leads

Ranked by expected payoff. Sample counts are from the 20 s run above.

1. **Boot time: shader cache replay (~6.2 s + 1.6 s head start).** `cache_load` decompiles all
   ~260k cached shaders on the render thread before the first draw. They're only 2,344 distinct
   programs: the key (`stage_state_hash`) includes render-target and texture state, so each program
   averages ~111 translations, and `shaders.bin` reaches ~290 MB. Options:
   - spread the decompiles across worker threads (the decompiler would need to be checked for
     shared state first);
   - store the translated MSL, so replay skips the decompile;
   - narrow the key: done (2026-10-20, "Metal: narrow shader keys" above). A replayed record whose
     narrow key is known is mapped to that shader without a decompile, and new caches hold one
     record per narrow key.
2. **`OSSendMessage` (~300 samples, measured, not worth changing).** One job queue carries ~26,000
   messages a second (sender `0276035C`, one worker receiving). The `notify_all` almost always wakes
   a worker that is really waiting, which is necessary; macOS already skips the kernel call when no
   thread waits, so waking only on waiters would save next to nothing.
3. **Recompiled code (small).** `PPC_ENTER` checks the trace flag and the preemption flag on every
   function call; compiling the trace check out of release builds saves a little. Keep
   `-ffp-contract=off` and the exact rounding: they are what makes the results correct.

## Texture change detection (2026-10-06)

The renderers used to re-hash a CPU texture in full only when it was new, invalidated by an exact
GX2Invalidate range, or every 64 frames (phased by frame and address); in between they compared 256
sampled words (Metal: of the base level only). The game changes textures in place without announcing
it, so a change between the samples showed up to 63 frames late, and two runs reaching the same scene
at different frame counts could render it differently (Mirror Shield in the Wind Temple: 668 pixels).

Now both renderers use page write tracking (`runtime/src/write_watch.cpp`): each full check
write-protects the host pages of all the texture's levels and records a stamp; the first CPU write to
such a page faults once, the handler stamps the page and makes it writable. A lookup re-hashes (every
byte, every level, xxHash) only when a page carries a newer stamp, or when the game signalled a change
(GX2Invalidate on the range, GX2CopySurface into it, a loaded save state). Unchanged textures cost a
stamp comparison per page and no hashing. Kernel writes into guest memory (FSReadFile's `fread`)
are bracketed with `wwatch::HostWrite`, since a protected page would make the read fail with EFAULT
instead of faulting. The sampled check remains only for hosts where page protection is unavailable.
The 300-frame `[gfx]` report (Metal) and the `[vulkan textures]` line (`WWHD_VK_CPU_ONLY_STATS=1`) show
full checks, hashed bytes (Metal), uploads, page write faults and pages protected;
`WWHD_LOG_TEXCHECK=1` (Metal) logs which textures were re-checked because of a write.

Cost, Outset beach (states slot 4, 45 s explore walk), render-thread CPU ms/frame, two runs each,
devel vs this change. Indicative only: measured while the machine ran other long jobs (load 5-9 on
16 cores). Metal 5.50 / 5.53 before, 5.31 / 5.69 after; Vulkan (AppKit) 6.73 / 7.02 before,
6.79 / 6.52 after: within run-to-run noise. About 2 write faults and 2 full checks of small
textures per frame in steady play (0.1-1.5 MiB hashed per 300 frames); an idle re-measure is open.

