# Android device handoff for rhemfur

Status: **all physical-device checks below are pending**. Emulator results in
[android-implementation.md](android-implementation.md) do not establish these results.
The full implementation also has open acceptance items; this is a test checklist,
not a declaration that a release is ready.

## Record the build and device

- [ ] Record device/model, Android/API version, security patch, ABI, RAM, free
  internal storage, display resolutions/refresh rates, battery percentage,
  charging state and ambient temperature. Include Ayn Thor, a foldable and an
  external-display device when available.
- [ ] On an API 33 device, verify both compiler/library execution and actual
  gameplay rendering. Framework API support alone does not establish GPU support:
  record Vulkan version, dynamic-rendering availability and the startup result.
  The local API 33 emulator compiles/loads successfully, but its guest Vulkan driver
  does not expose the required dynamic-rendering support; its display check failed and remains unverified.
- [ ] Record the port commit, APK version/SHA-256, runtime SDK identity, embedded
  Python version, compiler identity and CI artifact/checksum. Use the project's
  pinned-source compiler CI artifact when it is available; remote CI success is
  still required. Never substitute another recompiler or an unrelated compiler.
- [ ] Identify measurements as a physical phone with a privately supplied dump.
  Record input format and size, without sending the dump, keys or generated code.
  Compilation currently uses one worker; record any later setting change.

Useful initial observations with the device connected over adb:

```sh
adb shell getprop ro.product.model
adb shell getprop ro.build.version.release
adb shell getprop ro.build.version.sdk
adb shell getprop ro.product.cpu.abi
adb shell df -k /data /sdcard
adb shell dumpsys battery
adb shell dumpsys thermalservice
adb shell dumpsys display
```

## Full phone setup and measurements

- [ ] Install the compiler-enabled APK. Complete selection, import, extraction,
  translation, compilation, activation and game launch entirely on the phone.
  Test a supported user-owned extracted folder, WUA, and WUD/WUX with privately
  selected disc/common key files. Include a dump on removable storage if supported.
- [ ] Check folder selection, each key picker, cancellation and rotation. Test a
  cloud document provider if available; confirm the app streams URIs rather than
  requiring a filesystem path. Move/revoke a selected document before import and
  confirm a useful error and a working reselection path.
- [ ] After importing a disc image with an invalid or incorrect key, choose the
  corrected key and retry in the same job. Close/reopen before retry, then pause
  and resume during the correction. Confirm the dump is retained without another
  provider copy, the other key is preserved and extraction succeeds with your
  correct keys. Share no key bytes or key digests.
- [ ] Measure wall time separately for import, extraction, translation,
  compilation, linking and activation. Export scalar stage metrics from the
  private job's completed checkpoints in a debug build; redact paths and exclude
  raw extractor/compiler logs and input inventories from shared reports.
- [ ] Record APK bytes, installed app/compiler/Python/runtime bytes, imported dump
  bytes, extracted bytes, generated/object bytes, final library bytes and minimum
  free space during the entire run. Distinguish retained intermediates from peak
  temporary storage. The 1 GiB import reserve is not a full-game size estimate.
- [ ] Sample memory throughout each stage, including the setup process and all
  extractor/compiler/linker child processes. Record sampling interval, peak
  aggregate PSS/RSS and any missed peaks. `adb shell dumpsys meminfo
  org.wwhdrecomp.wwhd` alone excludes native tool children; identify them with
  `adb shell ps -A -o PID,PPID,NAME` and collect `dumpsys meminfo` for their PIDs.
  Also record scalar `resources_before`/`resources_latest` from the private job's
  `state.json` and each completed `stages` entry. These kernel measurements report
  worker-process peak RSS and the largest reaped child's peak RSS separately,
  in bytes. They cover process lifetime, including earlier jobs in the same
  process; they are not additive and do not measure concurrent aggregate PSS/RSS.
  A killed worker may leave only its last persisted snapshot. Keep the aggregate
  sampling check pending until the full run is measured.
  A read-only capture command is available for a debuggable build:
  `python3 tools/android/resource_sampler.py --serial SERIAL --job-id JOB_ID
  --seconds 3600 --output phone-resources.json`. Get the job ID from the private
  `no_backup/ondevice/setup.json` pointer. Start capture before resuming setup;
  it stops when the job completes/fails or the capture window ends. It never
  installs, pauses or resumes the app. The report includes APK hash, device
  fingerprint, per-sample process-set changes, complete child-sample count,
  actual sampling gaps and sampled maxima. Check `capture_valid` and inspect
  missing/changing process samples; a valid capture still can miss short peaks.
  RSS sums double-count shared pages; PSS reads are sequential. App-private
  allocation excludes APK/native installation and external files; `/data` free
  space also reflects unrelated system activity. Keep kernel high-water values
  separate and do not add them to claim a simultaneous aggregate peak.
- [ ] After repeated port/compiler updates and successful resource preparation,
  inspect the Python/compiler cache allocations. Obsolete hash-named generations
  should be removed, current cache markers should remain valid, and saves and
  the previously active game must still work. Interrupt/background during cleanup
  and retry; partially deleted obsolete caches must not damage the current cache.
- [ ] Verify compiler execution and runtime library loading on each supported
  Android version available, starting with API 33 and the current target/API 36.
  Check 16 KiB page-size hardware when available. Record failures without lowering
  the target SDK, using root or replacing the execution design.

## Pause, heat, battery and interrupted work

- [ ] During first-use Python and compiler resource unpacking, request a manual
  pause and trigger thermal/battery pauses. Confirm preparation stops, incomplete
  staging is removed, the setup wake lock releases, and resume finishes the same
  job. Record pause latency and time spent restarting the incomplete resource
  stage; repeat on slow storage. Emulator manual-pause coverage does not establish
  sensor behavior or phone latency.
- [ ] During full-game translation, request manual and normal thermal/battery
  pauses while analysis and C generation are active. Record latency and CPU drop;
  confirm incomplete output is removed, the wake lock is released, and resume
  restarts translation with identical C. Include a pause during RPX decompression:
  native Python operations may delay delivery until control returns to Python.
- [ ] Request pauses while a large compiler command and a sustained full-game
  link are already doing work. Confirm both tool process groups stop, incomplete
  compiler/linker temporaries disappear, verified objects and the previous game
  remain intact, and retry succeeds. Measure request-to-idle latency and confirm
  the wake lock is released. Synthetic linker coverage cancels just after spawn;
  it does not establish mid-link progress or physical-phone latency.
- [ ] Pause during import, extraction, translation, compilation and linking. Verify
  the screen accurately describes whether it waits for a file/stage boundary or
  restarts incomplete work. Confirm paused setup releases its wake lock.
- [ ] Close the screen and leave the app in the background during each stage.
  Confirm visible notification/progress and a usable pause/resume action, both
  with notification permission granted and denied.
- [ ] Test process termination while copying/writing and at stage boundaries.
  Reopen and confirm complete outputs are checked, partial output is not activated,
  and valid prior work is reused. Separately test task dismissal, Android force-stop
  followed by reopening, and reboot. Force-stop is a distinct Android stopped state;
  do not count an expected lack of background restart as ordinary process recovery.
- [ ] Run sustained setup under normal phone thermal management. Record thermal
  status, battery temperature, battery level and stage times. Current defaults pause
  at severe thermal status or 42°C battery temperature, and resume at light-or-lower
  status and at most 39°C. Do not deliberately overheat the phone.
- [ ] Observe unplugged battery pause at 20% or below and resume at charging or
  30% or above. Check hysteresis, and confirm manual pause stays paused when the
  phone cools or charging starts. Distinguish simulated thresholds from real readings.
- [ ] Reboot immediately after the last picker result as well as after a settled
  selection. The emulator has exposed loss of the newest URI grant in the immediate
  case. Before import completes, use Restore access with the original dump and
  reselect any key labelled as needing access. Confirm a different dump is rejected,
  the job identity and private copies are retained, and setup can resume. Test
  revocation separately from provider removal; a missing document cannot be
  restored by permission alone. Physical-device acceptance remains open.
- [ ] Exercise insufficient storage with disposable test inputs/storage, leaving
  an existing install and saves intact. Confirm useful retry errors, no activation
  of partial output and no unnecessary duplicate import on a normal retry.

## Port update, failure recovery and PC alternative

- [ ] Finish and launch a supported setup, create a save, and record a save hash
  privately. Install a newer same-signed port APK whose runtime/recompiler/toolchain
  identity changes. Confirm automatic rebuild from retained inputs, valid checkpoint
  reuse where applicable and atomic activation of the replacement library/assets.
- [ ] Reboot or terminate setup during that rebuild. Confirm the prior compatible
  build remains usable, the update resumes and saves/user settings survive.
- [ ] Exercise a failing rebuild using an intentionally failing development build.
  Confirm the previous valid selection and saves remain intact and a corrected
  build can retry. Check incompatible host ABI handling separately; an incompatible
  library must not be launched as a fallback.
- [ ] Install/use the optional PC-produced Android build and USB game-folder path.
  Confirm Play current build still works and save import/export remain usable.

## Dual screens

- [ ] Put TV output on the main display and GamePad output on the second display.
  Verify GamePad touch at all four corners, center, drags and held touches.
- [ ] Test display swap and every desktop-equivalent scaling mode, including
  letterboxing and touch coordinates after swapping. TV touches must not become
  GamePad touches when the second display shows TV.
- [ ] Disconnect/reconnect the external display while rendering and while holding
  touch. Fold/unfold repeatedly, rotate and background/reopen. Confirm no crash,
  stuck touch or frozen picture; fallback and restored output should be usable.
- [ ] In a debug build with an external display connected, hold GamePad touch
  and run `adb shell am broadcast -a org.wwhdrecomp.wwhd.SMOKE_FOLD -p
  org.wwhdrecomp.wwhd --ez dismiss_secondary true`. Confirm touch cancels, the
  main picture keeps updating, and the secondary picture returns automatically
  on the same display without unplugging. Repeat around background/resume.
- [ ] Connect an external display during startup and resume while its window
  service is still becoming ready. A transient window rejection should recover
  automatically; persistent rejection should leave the main picture updating.
  Reconnect after failure and confirm the second picture returns. Emulator-only
  authored InvalidDisplayException probes cover the retry budget; actual device
  window-service rejection remains pending.
- [ ] Exercise real secondary Vulkan surface loss where the device/driver allows
  it, for external output and fold panes. Confirm touch cancels, the main-screen
  fallback updates and the secondary host returns without unplugging. Repeat
  around pause/resume and display replacement; an old failure must not replace
  the new host. Emulator acquisition/presentation fault injection passes, but
  actual driver-generated errors and persistent-failure behavior remain pending.
  Include loss during surface capability/format/presentation-mode queries and
  swapchain creation, not just acquisition and presentation.
- [ ] Trigger Activity recreation during gameplay with normal/swapped external
  output and fold panes. Verify game state, audio and controllers continue,
  held touch cancels, new touches work and both pictures resume. Separately test
  process death/relaunch; native-session retention only covers a live process.
- [ ] Record primary/secondary queue-family IDs from the Vulkan startup log (or
  the debug display fixture's `primary_queue_family` and `secondary_queue_family`).
  Where hardware exposes another graphics-and-present family, exercise that path
  with touch, scaling, swapped roles, captures, resize, unplug and blocked output.
  Check image/swapchain sharing and command-buffer family validation with Vulkan
  validation enabled where available. Emulator family-0 testing does not cover
  cross-family GPU execution. Devices with only one usable queue or without
  present-fence retirement now attempt a separate-logical-device worker with one
  shared hardware-buffer snapshot. Record `secondary_isolated_device`, primary
  requested queue count, application swapchain/snapshot counts and local queue
  waits. Validate fence-only acquisition, zero-wait-semaphore presentation,
  external ownership/FD hand-offs and teardown. The emulator's unchanged timing
  gate failed in earlier runs; the latest default-path local combined run passes.
  Physical performance and remote CI acceptance remain pending.
- [ ] Run the debug fixture command `shared_image_probe` with the exact installed
  debug APK; `shared_image_smoke.py` is an emulator-only automation wrapper. Record support for RGBA8 Android hardware-buffer imports and sync-FD
  semaphore hand-offs between separate logical devices, each using queue zero.
  Check external ownership transitions with validation where available. This
  probe checks consumer shader-rendered GPU image bytes and primary rendering
  afterward; it does not
  exercise an isolated secondary swapchain or prove independent frame timing.
- [ ] Run the debug fixture command `external_memory_capabilities` with the exact
  installed debug APK to record external-memory image and buffer support. The
  `shared_image_smoke.py --external-memory-capabilities` wrapper is emulator-only. This
  read-only diagnostic reports handle types, usage flags, format-query results,
  and import/export feature masks. A successful diagnostic means queries completed,
  even when every feature mask is zero; it proves no allocation or rendering.
  API 36.1 arm64 GFXStream/SwiftShader rejected RGBA8 optimal images for opaque FD
  and DMA-BUF with transfer-only, sampled-transfer, and color-attachment usages;
  storage/transfer buffers reported no features for those handles or Android
  hardware buffers. Extension advertisement alone did not establish usable memory.
- [ ] For the separate-device path, collect the fixture's opt-in named
  `secondary_stage_cpu` counters and timing-profile deltas. Compare acquisition,
  submission and FD hand-off costs with main cadence under a controlled host load.
  These CPU wall times include driver waits and scheduling, and do not establish
  GPU busy time or physical scanout behavior.
- [ ] Measure main-display frame times before, during and after secondary rendering,
  including a slower/blocked secondary display and repeated replacement. Report
  median, p95/p99 and worst frame times with sample duration and refresh rates.
  Record whether the log enables secondary fence retirement or reports the idle
  compatibility path. Fence-capable drivers now retire secondary generations
  without a device-wide wait; the emulator resource-bound stress test is not a
  real stalled-compositor measurement. Record whether the dedicated secondary
  queue/worker is available. Its emulator timing gate passes with batched scan
  snapshots; shared GPU capacity and physical compositor behavior still require
  these measurements. A visible picture alone does not prove independent performance.

## Return evidence

For each checkbox, record pass/fail/pending, exact device/build, reproduction steps
and scalar metrics. Share redacted setup state/error and selected application log
messages only. Do not send keys, game files, generated C, complete private manifests,
file inventories or raw logs that may contain private paths/content. Keep failures
and untested device/version combinations explicit.

For a foldable exposing one logical display, verify Jetpack reports a separating
FoldingFeature while partially folded or spanning a physical hinge. TV should
occupy the left/top pane and GamePad the right/bottom pane, with the hinge gap
excluded. A fully flat flexible display returns to the existing single-display
layout. Plugging in an external display should restore the full main pane and
move the secondary picture there; unplugging should restore hinge panes when
still separating. Synthetic emulator hinge injection does not complete these
hardware checks.

## Vulkan barrier capture

Android performance gain is **pending device results**. Emulator display checks cannot establish
Mali/Adreno GPU cost. Use the same native library, game, copied save/checkpoint, scripted input,
resolution, frame mode and driver for both variants. Record device/OS/GPU/driver and temperatures.

The API 36.1 SwiftShader emulator passes swapchain presentation and PNG readback in both modes.
Its full renderer smoke fails `volume render wrote outside its slice` in both modes; keep this
baseline limitation separate and do not report the full emulator smoke as passing.

1. In a private test APK, use SDL's manifest environment metadata under `<application>`:
   `<meta-data android:name="SDL_ENV.WWHD_VK_NARROW_BARRIERS" android:value="0" />` for the old
   path, `1` for precise barriers (also the default). Reinstall/restart between variants; keep the native library identical.
   Also set `SDL_ENV.WWHD_PROFILE=1`, `SDL_ENV.WWHD_NO_AUDIO=1` and the existing `WWHD_TEST_*`
   scripted input/state-load switches. Use private shader caches, copied saves and a verified state
   load. Do not distribute an APK containing game code.
2. First enable Khronos synchronization validation (`WWHD_VK_VALIDATION=1`, with the Android
   validation layer packaged/enabled) and capture the six desktop scenes at 30 and interpolated
   60 fps. Set `WWHD_TEST_ORIGIN_LOAD=n` for inputs relative to the completed load. For 60 fps,
   start with `WWHD_INTERP=0` and use `WWHD_TEST_MODE=1@0` so interpolation switches at that same
   origin; avoid an absolute startup-step mode switch. Use
   `WWHD_TEST_CAPTURE_LOAD_COUNTER=n` for a queued full-pass TV/GamePad/present capture at the
   restored game counter plus n. Paused menus freeze that counter: use
   `WWHD_TEST_CAPTURE_LOAD_STEP=n` there. Keep offsets identical between modes. Require zero sync
   hazards/errors and byte-identical decoded frames for stable baseline scenes. Where the old path
   varies, use five captures per path: same pixels, no larger value ranges or difference counts.
   Keep validation and frame dumping disabled during performance captures.
3. Warm both variants. Let the device cool between runs, keep power/refresh/brightness fixed and
   close other GPU applications. Run five interleaved old/new pairs of the same 60-second gameplay
   route; alternate pair order. Extend by five pairs only if frame time is within ±2% and IQRs
   overlap. Keep startup, checkpoint restore and shader compilation out of the measured window.
4. Save the profiler log (`adb logcat -v threadtime`) for each run. Report median and Q1–Q3 across
   run medians for `ms/frame` and `render thread CPU ... ms/frame`. Reject missing state loads,
   changed scene/input, thermal throttling and incomplete profiler windows, retaining the reason.
5. Capture each measured window with Perfetto or Android GPU Inspector System Profiler.
   A minimal Perfetto text config (save as `barriers.pbtxt`) is:

   ```text
   buffers { size_kb: 65536 fill_policy: RING_BUFFER }
   duration_ms: 60000
   data_sources { config { name: "linux.ftrace" ftrace_config {
     ftrace_events: "sched/sched_switch"
     ftrace_events: "sched/sched_wakeup"
     ftrace_events: "power/cpu_frequency"
     atrace_categories: "gfx"
     atrace_apps: "org.wwhdrecomp.wwhd"
   } } }
   data_sources { config { name: "android.surfaceflinger.frametimeline" } }
   data_sources { config { name: "gpu.renderstages" } }
   ```

   ```sh
   adb push barriers.pbtxt /data/local/tmp/barriers.pbtxt
   adb shell perfetto --txt -c /data/local/tmp/barriers.pbtxt -o /data/misc/perfetto-traces/barriers.perfetto-trace
   adb pull /data/misc/perfetto-traces/barriers.perfetto-trace
   ```

   Start the trace at the same script checkpoint in every run and rename it with variant/pair.
   Inspect SurfaceFlinger frame intervals (when available for the app's SurfaceView) and
   scheduled CPU time for the `GX2 render` thread. Use the runtime profiler for frame time if
   FrameTimeline does not expose the SDL surface.
   GPU render stages require driver support: report unavailable GPU time as unavailable. If AGI
   exposes GPU duration/counters, export those with the trace; do not substitute CPU GPU-wait time
   or emulator timing. Vulkan timestamp intervals (`WWHD_VK_GPU_TIMESTAMPS=1`) are submission
   intervals, not exclusive GPU busy time; calibrate their overhead before using them in a comparison.

References: [FrameTimeline](https://perfetto.dev/docs/data-sources/frametimeline),
[AGI GPU render stages](https://developer.android.com/agi/sys-trace/gpu-render-stages).
