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
   - narrow the key. That is a correctness risk: every register in it changes the generated code
     somewhere.
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

