# Android device checks (rhemfur)

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
