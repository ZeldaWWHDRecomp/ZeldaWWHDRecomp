#!/usr/bin/env python3
"""Exercise the debug APK's real Vulkan secondary surface on an Android emulator.

Pass --apk to install a placeholder-code debug APK. Existing emulator overlay
display settings are restored even if assertions fail. Never run on a phone.
"""
import argparse
import hashlib
import os
import json
import platform
from pathlib import Path
import re
import shlex
import subprocess
import time

PACKAGE = "org.wwhdrecomp.wwhd"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--require-ondevice", action="store_true", help="require the locally compiled active runtime")
    parser.add_argument("--require-fence-retirement", action="store_true", help="fail if the driver lacks secondary present-fence retirement")
    parser.add_argument("--require-isolated-device", action="store_true", help="force separate queue-zero logical devices and require that path")
    parser.add_argument("--require-present-worker", action="store_true", help="fail if the driver lacks the dedicated secondary queue path")
    parser.add_argument("--measure-timing", action="store_true", help="measure 180 primary intervals with secondary disabled, enabled, held and disconnected")
    parser.add_argument("--exercise-folds", action="store_true", help="inject synthetic window hinges; exercise real pane surfaces, not hardware posture detection")
    parser.add_argument("--exercise-primary-rotation", action="store_true", help="rotate the primary Activity to portrait and back while secondary presents")
    parser.add_argument("--exercise-dismissal", action="store_true", help="dismiss the secondary window without removing its display; require automatic recovery")
    parser.add_argument("--exercise-surface-loss", action="store_true", help="inject secondary acquisition/presentation/query loss and require host recovery")
    parser.add_argument("--disable-gpu-timestamps", action="store_true", help="measure CPU cadence without optional GPU query instrumentation")
    parser.add_argument("--shared-general", action="store_true", help="compare GENERAL shared-image layout on the forced isolated-device fixture")
    args = parser.parse_args()
    if args.shared_general and not args.require_isolated_device: parser.error("--shared-general requires --require-isolated-device")
    prefix = [args.adb, "-s", args.serial]

    def adb(*command, check=True):
        if command and command[0] == "shell":
            # adb joins shell arguments into a remote command. Quote them to
            # preserve empty settings and multi-display values containing ';'.
            command = ("shell", shlex.join(command[1:]))
        return subprocess.run(prefix + list(command), check=check, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20).stdout.strip()

    if adb("shell", "getprop", "ro.kernel.qemu") != "1":
        parser.error("this test changes display settings and requires an emulator")
    if args.apk: adb("install", "-r", str(args.apk))
    package_paths = adb("shell", "pm", "path", PACKAGE).splitlines()
    if len(package_paths) != 1 or not package_paths[0].startswith("package:"):
        parser.error("requires one installed fixture APK")
    apk_sha256 = adb("shell", "sha256sum", package_paths[0][8:]).split()[0]
    if args.apk:
        with args.apk.open("rb") as stream:
            supplied_sha256 = hashlib.file_digest(stream, "sha256").hexdigest()
        if supplied_sha256 != apk_sha256: parser.error("installed APK differs from supplied APK")
    original = adb("shell", "settings", "get", "global", "overlay_display_devices")
    immersive = adb("shell", "settings", "get", "secure", "immersive_mode_confirmations")
    observations = []
    timing_profiles = []
    timing_thresholds = {}
    timing_context = {}
    passed = False
    settings_path = "/sdcard/Android/data/" + PACKAGE + "/files/config/wwhd/settings.ini"
    probe = subprocess.run(prefix + ["shell", shlex.join(["test", "-f", settings_path])],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
    if probe.returncode not in (0, 1):
        raise RuntimeError("cannot inspect emulator app settings")
    saved_settings = subprocess.check_output(prefix + ["exec-out", "cat", settings_path], timeout=20) if probe.returncode == 0 else None

    def command(value):
        # Atomic publication prevents the native loop reading a partial command.
        script = "printf '%s' " + shlex.quote(value) + " > files/display-smoke-command.tmp && mv files/display-smoke-command.tmp files/display-smoke-command"
        adb("shell", "run-as", PACKAGE, "sh", "-c", script)

    def screenshot(suffix):
        target = args.output.with_name(args.output.stem + suffix + ".png")
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("wb") as output:
            subprocess.run(prefix + ["exec-out", "screencap", "-p"], stdout=output,
                           stderr=subprocess.PIPE, check=True, timeout=20)

    def metrics():
        path = "files/display-smoke.json"
        return json.loads(adb("shell", "run-as", PACKAGE, "cat", path))

    def until(label, predicate, timeout=20):
        deadline = time.monotonic() + timeout
        latest = None
        while time.monotonic() < deadline:
            try:
                latest = metrics()
                if predicate(latest):
                    observations.append({"phase": label, **latest})
                    print(label + ": " + json.dumps(latest), flush=True)
                    return latest
            except (ValueError, subprocess.CalledProcessError):
                pass
            time.sleep(.2)
        raise AssertionError(label + " timed out; last metrics=" + repr(latest))

    def overlay(value):
        if value == "null": adb("shell", "settings", "delete", "global", "overlay_display_devices")
        else: adb("shell", "settings", "put", "global", "overlay_display_devices", value)

    try:
        adb("shell", "am", "force-stop", PACKAGE)
        adb("shell", "run-as", PACKAGE, "rm", "-f",
            "files/display-smoke.json", "files/display-smoke-command", check=False)
        adb("logcat", "-c")
        adb("shell", "input", "keyevent", "82")
        adb("shell", "wm", "dismiss-keyguard")
        # Force-stop does not release InputDispatcher's injected pointer state.
        # End a DOWN left behind by an interrupted previous test before injecting again.
        adb("shell", "input", "-d", "0", "motionevent", "UP", "0", "0")
        adb("shell", "settings", "put", "secure", "immersive_mode_confirmations", "confirmed")
        overlay("800x480/160")
        adb("shell", "am", "start", "-W", "-n", PACKAGE + "/.DisplaySmokeActivity",
            "--ez", "isolated_secondary", "true" if args.require_isolated_device else "false",
            "--ez", "shared_general", "true" if args.shared_general else "false")
        until("connected", lambda m: m["dual"] and m["secondary_presented"] >= 10)
        if metrics()["secondary_shared_general"] != args.shared_general:
            raise AssertionError("shared image layout does not match requested fixture")
        if args.require_isolated_device and (not metrics().get("secondary_isolated_device") or metrics().get("primary_requested_queues")!=1):
            raise AssertionError("Driver did not enable isolated secondary logical device")
        if args.require_fence_retirement and not metrics().get("secondary_fence_retirement"):
            raise AssertionError("Driver did not enable secondary present-fence retirement")
        if args.require_present_worker and not metrics().get("secondary_present_worker"):
            raise AssertionError("Driver did not enable the secondary presentation worker")
        command("swap0")
        connected = until("normal roles", lambda m: m["dual"] and not m["swap_requested"] and not m["swap_active"])
        args.output.parent.mkdir(parents=True, exist_ok=True)
        # The emulator's simulated display is composited in an overlay window
        # on the primary screen, so this captures both authored color patterns.
        screenshot("")
        logs = adb("logcat", "-d", "-s", "wwhd-display:I", "*:S")
        ids = re.findall(r"secondary display=(\d+)", logs)
        if not ids: raise AssertionError("secondary logical display ID was not logged")
        display = ids[-1]
        adb("shell", "input", "-d", display, "motionevent", "DOWN", "400", "240")
        until("secondary touch", lambda m: m["touch"] and abs(m["tx"] - .5) < .08 and abs(m["ty"] - .5) < .08)
        if args.exercise_dismissal:
            held = metrics()
            adb("shell", "am", "broadcast", "-a", PACKAGE + ".SMOKE_FOLD", "-p", PACKAGE,
                "--ez", "dismiss_secondary", "true")
            dismissed = until("independent dismissal falls back and cancels touch", lambda m:
                              not m["dual"] and not m["touch"] and
                              m["primary_presented"] > held["primary_presented"])
            until("independent dismissal automatically recovers", lambda m:
                  m["dual"] and not m["touch"] and
                  m["primary_presented"] >= dismissed["primary_presented"] + 10 and
                  m["secondary_presented"] >= held["secondary_presented"] + 10)
            ids_after = re.findall(r"secondary display=(\d+)", adb("logcat", "-d", "-s", "wwhd-display:I", "*:S"))
            if len(ids_after) <= len(ids) or ids_after[-1] != display:
                raise AssertionError("Dismissal did not recreate the host on the same logical display")
            adb("shell", "input", "-d", display, "motionevent", "UP", "400", "240")
            adb("shell", "input", "-d", display, "motionevent", "DOWN", "400", "240")
            until("recovered secondary touch", lambda m:
                  m["touch"] and abs(m["tx"] - .5) < .08 and abs(m["ty"] - .5) < .08)
        if args.exercise_surface_loss:
            for fault in ("lose_acquire", "lose_present", "lose_query"):
                ids = re.findall(r"secondary display=(\d+)", adb("logcat", "-d", "-s", "wwhd-display:I", "*:S"))
                held = metrics()
                command(fault)
                lost = until(fault + " falls back and cancels touch", lambda m:
                             not m["dual"] and not m["touch"] and
                             m["secondary_surface_losses"] == held["secondary_surface_losses"] + 1 and
                             m["primary_presented"] > held["primary_presented"])
                until(fault + " automatically recovers", lambda m:
                      m["dual"] and not m["touch"] and
                      m["primary_presented"] >= lost["primary_presented"] + 10 and
                      m["secondary_presented"] >= held["secondary_presented"] + 10)
                latest_ids = re.findall(r"secondary display=(\d+)", adb("logcat", "-d", "-s", "wwhd-display:I", "*:S"))
                if len(latest_ids) <= len(ids) or latest_ids[-1] != display:
                    raise AssertionError("WSI loss did not recreate the host on the same display")
                ids = latest_ids
                adb("shell", "input", "-d", display, "motionevent", "UP", "400", "240")
                adb("shell", "input", "-d", display, "motionevent", "DOWN", "400", "240")
                restored = until(fault + " recovered secondary touch", lambda m:
                                 m["touch"] and abs(m["tx"] - .5) < .08 and abs(m["ty"] - .5) < .08)
                adb("shell", "am", "broadcast", "-a", PACKAGE + ".SMOKE_FOLD", "-p", PACKAGE,
                    "--ez", "stale_surface_recovery", "true")
                until(fault + " ignores obsolete recovery", lambda m:
                      m["dual"] and m["touch"] and
                      m["primary_presented"] >= restored["primary_presented"] + 10 and
                      m["secondary_presented"] >= restored["secondary_presented"] + 10)
                if re.findall(r"secondary display=(\d+)", adb("logcat", "-d", "-s", "wwhd-display:I", "*:S")) != ids:
                    raise AssertionError("Obsolete WSI recovery replaced the current host")
        command("swap1")
        swapped = until("swapped roles cancel touch", lambda m: m["swap_requested"] and m["swap_active"] and not m["touch"])
        until("swapped frames presented", lambda m: m["swap_active"] and
              m["primary_presented"] >= swapped["primary_presented"] + 10 and
              m["secondary_presented"] >= swapped["secondary_presented"] + 10)
        screenshot("-swapped")
        adb("shell", "input", "-d", display, "motionevent", "UP", "400", "240")
        adb("shell", "input", "-d", display, "motionevent", "DOWN", "400", "240")
        until("TV rejects GamePad touch", lambda m: not m["touch"] and m["primary_presented"] >= swapped["primary_presented"] + 10)
        adb("shell", "input", "-d", display, "motionevent", "UP", "400", "240")
        for scaling in range(3):
            command("filter" + str(scaling))
            until("scaling " + str(scaling), lambda m: m["scale_filter"] == scaling)
        # Primary input must target the resumed app rather than an expanded SystemUI panel.
        adb("shell", "cmd", "statusbar", "collapse")
        adb("shell", "input", "-d", "0", "motionevent", "DOWN",
            str(swapped["primary_width"] // 2), str(swapped["primary_height"] // 2))
        until("primary GamePad touch", lambda m: m["touch"] and abs(m["tx"] - .5) < .08 and abs(m["ty"] - .5) < .08)
        adb("shell", "input", "-d", "0", "motionevent", "UP",
            str(swapped["primary_width"] // 2), str(swapped["primary_height"] // 2))
        until("primary touch released", lambda m: not m["touch"])
        adb("shell", "input", "-d", "0", "motionevent", "DOWN",
            str(swapped["primary_width"] // 2), str(swapped["primary_height"] // 2))
        until("primary touch held for removal", lambda m: m["touch"])
        # Removing a surface while a finger is down must release the GamePad.
        overlay("null")
        removed = until("removed", lambda m: not m["dual"] and not m["swap_active"] and m["swap_requested"] and not m["touch"] and
                        m["primary_presented"] >= connected["primary_presented"] + 10)
        resumed = until("primary continues", lambda m: m["primary_presented"] >= removed["primary_presented"] + 10)
        adb("shell", "input", "-d", "0", "motionevent", "UP",
            str(swapped["primary_width"] // 2), str(swapped["primary_height"] // 2))
        overlay("800x480/160")
        reconnected = until("reconnected", lambda m: m["dual"] and m["swap_active"] and m["secondary_presented"] >= removed["secondary_presented"] + 10 and
              m["primary_presented"] > resumed["primary_presented"])
        # Change orientation and density of the secondary logical display.
        # This replaces its SurfaceView and exercises stale callback rejection
        # while both physical presentation counters must keep advancing.
        overlay("480x800/240")
        resized = until("portrait secondary replacement", lambda m: m["dual"] and m["swap_active"] and
                        m["primary_presented"] >= reconnected["primary_presented"] + 20 and
                        m["secondary_presented"] >= reconnected["secondary_presented"] + 20)
        overlay("800x480/160")
        landscape = until("landscape secondary replacement", lambda m: m["dual"] and m["swap_active"] and
              m["primary_presented"] >= resized["primary_presented"] + 20 and
              m["secondary_presented"] >= resized["secondary_presented"] + 20)
        if landscape.get("secondary_present_worker"):
            command("worker1")
            blocked = until("secondary present worker held", lambda m:
                            m["secondary_present_held"] and m["secondary_present_pending"] and not m["secondary_acquire_pending"])
            until("main progresses during blocked secondary present", lambda m:
                  m["primary_presented"] >= blocked["primary_presented"] + 60 and
                  m["secondary_presented"] == blocked["secondary_presented"] and
                  m["secondary_present_pending"] and m["secondary_present_skipped"] > 0)
            before_resize = metrics()
            command("resize_primary")
            until("primary swapchain recreates during blocked secondary present", lambda m:
                  m["primary_swapchains"] > before_resize["primary_swapchains"] and
                  m["primary_presented"] >= before_resize["primary_presented"] + 10 and
                  m["secondary_present_pending"] and
                  m["secondary_presented"] == blocked["secondary_presented"])
            overlay("null")
            disconnected = until("disconnect during blocked secondary present", lambda m:
                                 not m["dual"] and not m["touch"] and
                                 m["secondary_present_pending"] and
                                 m["primary_presented"] >= blocked["primary_presented"] + 70)
            overlay("800x480/160")
            until("new surface deferred while worker owns old surface", lambda m:
                  not m["dual"] and m["secondary_present_pending"] and
                  m["primary_presented"] >= disconnected["primary_presented"] + 10)
            command("worker0")
            until("worker releases old surface and newest resumes", lambda m:
                  not m["secondary_present_held"] and m["dual"] and m["swap_active"] and
                  m["secondary_presented"] >= blocked["secondary_presented"] + 10)
            command("acquire1")
            acquiring = until("secondary acquisition held", lambda m:
                              m["secondary_acquire_held"] and m["secondary_acquire_pending"])
            until("main progresses during blocked secondary acquisition", lambda m:
                  m["primary_presented"] >= acquiring["primary_presented"] + 60 and
                  m["secondary_presented"] == acquiring["secondary_presented"] and m["secondary_acquire_pending"])
            overlay("null")
            until("disconnect during blocked secondary acquisition", lambda m:
                  not m["dual"] and not m["touch"] and m["secondary_acquire_pending"])
            overlay("800x480/160")
            deferred = metrics()
            until("replacement deferred during blocked acquisition", lambda m:
                  not m["dual"] and m["secondary_acquire_pending"] and
                  m["primary_presented"] >= deferred["primary_presented"] + 10)
            command("acquire0")
            until("acquired old image consumed and replacement resumes", lambda m:
                  not m["secondary_acquire_held"] and m["dual"] and m["swap_active"] and
                  m["secondary_presented"] >= acquiring["secondary_presented"] + 10)
        if landscape.get("secondary_fence_retirement"):
            until("secondary generations retire without idle", lambda m:
                  m["secondary_retired"] >= 3 and m["secondary_retirement_pending"] == 0)
            command("retirement1")
            held = until("hold secondary retirement", lambda m: m["secondary_retirement_held"])
            for count, size in enumerate(("640x360/160", "720x480/160", "640x400/160", "800x600/160"), 1):
                overlay(size)
                width, height = map(int, size.split("/")[0].split("x"))
                held = until("bounded secondary generation " + str(count), lambda m:
                             m["secondary_retirement_pending"] == count and
                             m["primary_presented"] >= held["primary_presented"] + 5 and
                             (count == 4 or (m["dual"] and m["secondary_width"] == width and
                              m["secondary_height"] == height and
                              m["secondary_presented"] > held["secondary_presented"])))
            overlay("800x480/160")
            full = until("full retirement queue keeps primary progressing", lambda m:
                         not m["dual"] and m["secondary_retirement_pending"] == 4 and
                         m["primary_presented"] >= held["primary_presented"] + 10)
            command("retirement0")
            until("retirement resumes newest secondary surface", lambda m:
                  not m["secondary_retirement_held"] and m["dual"] and
                  m["secondary_retirement_pending"] == 0 and
                  m["secondary_retired"] >= held["secondary_retired"] + 4 and
                  m["primary_presented"] > full["primary_presented"] and
                  m["secondary_presented"] > full["secondary_presented"])
            if any(m.get("secondary_idle_waits", 0) != 0 or m.get("secondary_retirement_pending", 0) > 4
                   for m in observations):
                raise AssertionError("Secondary retirement drained the device or exceeded its resource bound")
        if args.require_isolated_device:
            previous=metrics()
            for count, size in enumerate(("640x360/160", "720x480/160", "640x400/160", "800x600/160", "800x480/160"), 1):
                overlay(size)
                width,height=map(int,size.split("/")[0].split("x"))
                previous=until("isolated secondary replacement " + str(count), lambda m:
                    m["dual"] and m["secondary_width"]==width and m["secondary_height"]==height and
                    m["primary_presented"]>=previous["primary_presented"]+10 and
                    m["secondary_presented"]>=previous["secondary_presented"]+5)
            if any(m.get("secondary_device_swapchains",0)>1 or m.get("secondary_shared_snapshots",0)>1 or
                   m.get("secondary_idle_waits",0)!=0 for m in observations):
                raise AssertionError("Isolated secondary exceeded its resource bound or drained the main device")
        if args.measure_timing:
            timing_context = {"host": platform.platform(), "api": adb("shell", "getprop", "ro.build.version.sdk"),
                              "model": adb("shell", "getprop", "ro.product.model"),
                              "build_fingerprint": adb("shell", "getprop", "ro.build.fingerprint"),
                              "loop_delay_ms": 16, "pattern_dimensions": [64, 36],
                              "gpu_timestamp_scope": "primary graphics queue submission intervals; excludes independent secondary worker drawing",
                              "scope": "synthetic CPU presentation cadence and GPU submission timestamp intervals; not scanout latency or GPU busy time"}
            gpu = json.loads(adb("shell", "cmd", "gpu", "vkjson"))
            timing_context["gpu"] = [{"properties": d.get("properties", {}), "queues": d.get("queues", [])}
                                     for d in gpu.get("devices", [])]
            if not landscape.get("secondary_present_worker"):
                raise AssertionError("Timing stress profile requires a dedicated secondary presentation worker")
            command("secondary_profile1")
            def profile(label):
                before = metrics()
                host_load_before = list(os.getloadavg()) if hasattr(os, "getloadavg") else None
                command("timing_reset")
                measured = until("timing " + label, lambda m:
                                 m["timing_epoch"] > before["timing_epoch"] and
                                 m["primary_interval_samples"] >= 180, timeout=25)
                if (measured["gpu_submission_intervals_ns"] < before["gpu_submission_intervals_ns"] or
                        measured["gpu_submission_samples"] < before["gpu_submission_samples"]):
                    raise AssertionError("GPU lifetime timing counters regressed during " + label)
                measured["gpu_submission_interval_ms_per_primary"] = (
                    (measured["gpu_submission_intervals_ns"] - before["gpu_submission_intervals_ns"]) /
                    max(1, measured["primary_presented"] - before["primary_presented"]) / 1e6
                    if measured["gpu_timestamps_enabled"] and before["gpu_timestamps_enabled"] else None)
                previous_stages = {stage["stage"]: stage for stage in before["secondary_stage_cpu"]}
                current_stages = {stage["stage"]: stage for stage in measured["secondary_stage_cpu"]}
                if previous_stages.keys() != current_stages.keys() or len(current_stages) != 9:
                    raise AssertionError("Incomplete secondary CPU stage counters")
                measured["secondary_stage_cpu_delta"] = {
                    name: {"calls": after["calls"] - previous_stages[name]["calls"],
                           "ms": (after["ns"] - previous_stages[name]["ns"]) / 1e6}
                    for name, after in current_stages.items()}
                measured["host_load"] = {"cpu_count": os.cpu_count(), "load_before": host_load_before,
                                         "load_after": list(os.getloadavg()) if hasattr(os, "getloadavg") else None}
                timing_profiles.append({"configuration": label, **measured})
                return measured
            if args.disable_gpu_timestamps:
                command("timestamps0")
                until("GPU timestamp instrumentation disabled", lambda m: not m["gpu_timestamps_enabled"])
            command("swap0")
            until("normal roles for timing", lambda m: not m["swap_requested"] and not m["swap_active"])
            command("mode3")  # TV-only while secondary is absent; no fallback PIP cost.
            until("TV-only baseline layout", lambda m: m["drc_mode"] == 3)
            overlay("null")
            until("secondary disabled for timing", lambda m: not m["dual"] and not m["secondary_present_pending"])
            baseline = profile("disabled")
            overlay("800x480/160")
            until("secondary enabled for timing", lambda m: m["dual"] and
                  m["secondary_presented"] >= baseline["secondary_presented"] + 10)
            display_dump = args.output.with_suffix(".displays.txt")
            display_dump.write_text(adb("shell", "dumpsys", "display") + "\n")
            timing_context["display_dump"] = str(display_dump)
            enabled = profile("enabled")
            command("worker1")
            held_profile = until("worker held for timing", lambda m: m["secondary_present_held"] and m["secondary_present_pending"])
            blocked_profile = profile("held")
            if blocked_profile["secondary_presented"] != held_profile["secondary_presented"]:
                raise AssertionError("Held worker continued secondary presentation")
            overlay("null")
            until("secondary disconnected for timing", lambda m: not m["dual"] and m["secondary_present_pending"])
            unavailable = profile("disconnected")
            # Predefined emulator regression limits. These are CPU submission
            # cadence measurements (the loop includes a fixed 16 ms delay), not
            # physical display scanout latency or a full game's GPU budget.
            timing_thresholds = {"primary_p50_ms": baseline["primary_intervals_ms"]["p50"] * 1.25 + 2,
                                 "primary_p99_ms": baseline["primary_intervals_ms"]["p99"] * 1.5 + 10,
                                 "primary_worst_ms": 250, "minimum_samples": 180}
            for label, measured in (("enabled", enabled), ("held", blocked_profile), ("disconnected", unavailable)):
                if (measured["primary_intervals_ms"]["p50"] > timing_thresholds["primary_p50_ms"] or
                        measured["primary_intervals_ms"]["p99"] > timing_thresholds["primary_p99_ms"] or
                        measured["primary_intervals_ms"]["max"] > timing_thresholds["primary_worst_ms"]):
                    raise AssertionError("Primary timing regression with secondary " + label)
            command("worker0")
            until("timing worker drained", lambda m: not m["secondary_present_pending"] and not m["dual"])
            overlay("800x480/160")
            until("timing secondary restored", lambda m: m["dual"] and m["secondary_presented"] > unavailable["secondary_presented"])
            command("mode" + str(connected["drc_mode"]))
            until("timing layout restored", lambda m: m["drc_mode"] == connected["drc_mode"])
            command("swap1")
            until("timing swapped roles restored", lambda m: m["swap_requested"] and m["swap_active"])
        resize_logs = adb("logcat", "-d", "-s", "wwhd:I", "*:S")
        portrait_position = resize_logs.find("swapchain 480x800")
        if portrait_position < 0 or "swapchain 800x480" not in resize_logs[portrait_position:]:
            raise AssertionError("secondary swapchains did not adopt portrait and restored landscape dimensions")
        if args.exercise_primary_rotation:
            command("swap1")
            baseline = until("primary rotation baseline", lambda m: m["dual"] and m["swap_active"])
            for orientation, label in ((1, "portrait"), (0, "landscape")):
                before = metrics()
                adb("shell", "am", "broadcast", "-a", PACKAGE + ".SMOKE_FOLD", "-p", PACKAGE,
                    "--ei", "orientation", str(orientation))
                rotated = until("primary " + label + " rotation", lambda m:
                                (m["primary_height"] > m["primary_width"] if orientation == 1 else
                                 m["primary_width"] > m["primary_height"]) and
                                m["primary_swapchains"] > before["primary_swapchains"] and
                                m["primary_presented"] > before["primary_presented"] + 10 and
                                m["secondary_presented"] > before["secondary_presented"] + 10)
                x, y = rotated["primary_width"] // 2, rotated["primary_height"] // 2
                adb("shell", "input", "motionevent", "DOWN", str(x), str(y))
                until("rotated primary GamePad touch", lambda m:
                      m["touch"] and abs(m["tx"] - .5) < .08 and abs(m["ty"] - .5) < .08)
                adb("shell", "input", "motionevent", "UP", str(x), str(y))
                until("rotated primary touch released", lambda m: not m["touch"])
            if (rotated["primary_width"], rotated["primary_height"]) != (baseline["primary_width"], baseline["primary_height"]):
                raise AssertionError("Primary rotation did not restore original extent")
        if args.exercise_folds:
            command("swap0")
            overlay("null")
            full = until("fold baseline single display", lambda m: not m["dual"] and not m["swap_requested"])

            def fold(posture):
                adb("shell", "am", "broadcast", "-a", PACKAGE + ".SMOKE_FOLD", "-p",
                    PACKAGE, "--es", "synthetic_fold", posture)

            for posture in ("horizontal", "vertical", "horizontal"):
                before = metrics()
                fold(posture)
                pane = until("synthetic " + posture + " fold", lambda m:
                             m["dual"] and m["primary_presented"] > before["primary_presented"] + 10 and
                             m["secondary_presented"] > before["secondary_presented"] + 10 and
                             (m["primary_height"] < full["primary_height"] if posture == "horizontal"
                              else m["primary_width"] < full["primary_width"]))
                if args.require_isolated_device:
                    pane = until("fold " + posture + " stable swapchain", lambda m:
                        m["dual"] and m["secondary_local_queue_waits"]==pane["secondary_local_queue_waits"] and
                        m["primary_presented"]>=pane["primary_presented"]+60 and
                        m["secondary_presented"]>=pane["secondary_presented"]+5)
                x = (full["primary_width"] / 2 if posture == "horizontal" else
                     full["primary_width"] - pane["secondary_width"] / 2)
                y = (full["primary_height"] - pane["secondary_height"] / 2 if posture == "horizontal"
                     else full["primary_height"] / 2)
                adb("shell", "input", "motionevent", "DOWN", str(int(x)), str(int(y)))
                until("fold pane GamePad touch", lambda m:
                      m["touch"] and abs(m["tx"] - .5) < .08 and abs(m["ty"] - .5) < .08)
                if args.exercise_surface_loss and posture == "vertical":
                    for fault in ("lose_acquire", "lose_present", "lose_query"):
                        before_loss = metrics()
                        pane_logs = re.findall(r"fold panes=.*", adb("logcat", "-d", "-s", "wwhd-display:I", "*:S"))
                        command(fault)
                        lost = until("fold " + fault + " cancels touch", lambda m:
                                     not m["touch"] and
                                     m["secondary_surface_losses"] == before_loss["secondary_surface_losses"] + 1 and
                                     m["primary_presented"] > before_loss["primary_presented"])
                        pane = until("fold " + fault + " restores pane surfaces", lambda m:
                                     m["dual"] and not m["touch"] and
                                     m["primary_width"] == before_loss["primary_width"] and
                                     m["primary_height"] == before_loss["primary_height"] and
                                     m["secondary_width"] == before_loss["secondary_width"] and
                                     m["secondary_height"] == before_loss["secondary_height"] and
                                     m["primary_presented"] >= lost["primary_presented"] + 10 and
                                     m["secondary_presented"] >= before_loss["secondary_presented"] + 10)
                        recovered_logs = re.findall(r"fold panes=.*", adb("logcat", "-d", "-s", "wwhd-display:I", "*:S"))
                        if len(recovered_logs) <= len(pane_logs):
                            raise AssertionError("Fold WSI loss did not replace its embedded host")
                        adb("shell", "input", "motionevent", "UP", str(int(x)), str(int(y)))
                        adb("shell", "input", "motionevent", "DOWN", str(int(x)), str(int(y)))
                        touching = until("fold " + fault + " recovered touch", lambda m:
                                         m["touch"] and abs(m["tx"] - .5) < .08 and abs(m["ty"] - .5) < .08)
                        adb("shell", "am", "broadcast", "-a", PACKAGE + ".SMOKE_FOLD", "-p", PACKAGE,
                            "--ez", "stale_surface_recovery", "true")
                        until("fold " + fault + " ignores obsolete recovery", lambda m:
                              m["dual"] and m["touch"] and
                              m["primary_presented"] >= touching["primary_presented"] + 10 and
                              m["secondary_presented"] >= touching["secondary_presented"] + 10)
                        if re.findall(r"fold panes=.*", adb("logcat", "-d", "-s", "wwhd-display:I", "*:S")) != recovered_logs:
                            raise AssertionError("Obsolete recovery replaced the current fold host")
                screenshot("-fold-" + posture)
                if posture == "vertical":
                    # Moving a held touch off the pane must cancel it immediately.
                    overlay("800x480/160")
                    external = until("external display takes priority over hinge", lambda m:
                                     m["dual"] and not m["touch"] and
                                     m["primary_width"] == full["primary_width"] and
                                     m["primary_height"] == full["primary_height"] and
                                     m["secondary_width"] == 800 and m["secondary_height"] == 480)
                    overlay("null")
                    pane = until("unplug restores fold panes", lambda m:
                                 m["dual"] and m["primary_width"] < full["primary_width"] and
                                 m["secondary_width"] == pane["secondary_width"] and
                                 m["primary_presented"] > external["primary_presented"] + 10 and
                                 m["secondary_presented"] > external["secondary_presented"] + 10)
                    adb("shell", "input", "motionevent", "UP", str(int(x)), str(int(y)))
                    adb("shell", "input", "motionevent", "DOWN", str(int(x)), str(int(y)))
                    until("restored fold pane touch", lambda m: m["touch"])
                command("swap1")
                swapped_pane = until("fold role swap cancels held touch", lambda m:
                                     m["swap_active"] and not m["touch"])
                adb("shell", "input", "motionevent", "UP", str(int(x)), str(int(y)))
                px, py = swapped_pane["primary_width"] // 2, swapped_pane["primary_height"] // 2
                adb("shell", "input", "motionevent", "DOWN", str(px), str(py))
                until("fold primary GamePad touch", lambda m:
                      m["touch"] and abs(m["tx"] - .5) < .08 and abs(m["ty"] - .5) < .08)
                command("swap0")
                normal_pane = until("fold normal roles restored", lambda m:
                                    not m["swap_active"] and not m["touch"])
                adb("shell", "input", "motionevent", "UP", str(px), str(py))
                hx, hy = full["primary_width"] // 2, full["primary_height"] // 2
                adb("shell", "input", "motionevent", "DOWN", str(hx), str(hy))
                until("hinge gap rejects GamePad touch", lambda m:
                      not m["touch"] and m["primary_presented"] > normal_pane["primary_presented"] + 5)
                adb("shell", "input", "motionevent", "UP", str(hx), str(hy))
                adb("shell", "input", "motionevent", "DOWN", str(int(x)), str(int(y)))
                until("fold touch held for live unfold", lambda m: m["touch"])
                fold("none")
                until("unfold cancels touch and restores full surface", lambda m:
                      not m["dual"] and not m["touch"] and
                      m["primary_width"] == full["primary_width"] and
                      m["primary_height"] == full["primary_height"] and
                      m["primary_presented"] > pane["primary_presented"] + 10)
                adb("shell", "input", "motionevent", "UP", str(int(x)), str(int(y)))
            overlay("800x480/160")
            command("swap1")
            until("external display after folds", lambda m: m["dual"] and m["swap_active"])
        adb("shell", "am", "force-stop", PACKAGE)
        adb("shell", "run-as", PACKAGE, "rm", "-f", "files/display-smoke.json")
        adb("shell", "am", "start", "-W", "-n", PACKAGE + "/.DisplaySmokeActivity",
            "--ez", "isolated_secondary", "true" if args.require_isolated_device else "false",
            "--ez", "shared_general", "true" if args.shared_general else "false")
        until("settings survive restart", lambda m: m["dual"] and m["swap_active"] and m["swap_requested"] and m["scale_filter"] == 2 and m["secondary_presented"] >= 10)
        if args.require_ondevice:
            active = json.loads(adb("shell", "run-as", PACKAGE, "cat", "no_backup/ondevice/active.json"))
            loaded = adb("logcat", "-d", "-s", "wwhd-game:I", "*:S")
            if sum("Loaded on-device game " in line and line.endswith("/" + active["library"]) for line in loaded.splitlines()) < 2:
                raise AssertionError("Both SDL launches must select the compiled on-device runtime")
            if "game" not in active or sum("Selected game assets " in line and line.endswith("/" + active["game"])
                                            for line in loaded.splitlines()) < 2:
                raise AssertionError("Both SDL launches must select the runtime's paired asset tree")
        command("exit_smoke")
        shutdown_deadline=time.monotonic()+20
        while time.monotonic()<shutdown_deadline:
            shutdown_logs=adb("logcat", "-d", "-s", "wwhd:I", "*:S")
            if "[vulkan] secondary worker resources released" in shutdown_logs:
                break
            time.sleep(.2)
        else:
            raise AssertionError("Orderly secondary worker teardown did not finish")
        passed = True
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({"serial": args.serial, "apk_sha256": apk_sha256, "passed": False, "checks_passed": passed,
                                          "observations": observations, "timing_profiles": timing_profiles,
                                          "timing_thresholds": timing_thresholds, "timing_context": timing_context}, indent=2) + "\n")
        try:
            logs = adb("logcat", "-d", "-s", "wwhd:I", "wwhd-display:I", "wwhd-game:I", "SDL:V", "AndroidRuntime:E", "*:S")
            args.output.with_suffix(".log").write_text(logs + "\n")
        finally:
            try:
                try:
                    adb("shell", "input", "-d", "0", "motionevent", "UP", "0", "0", check=False)
                finally:
                    adb("shell", "am", "force-stop", PACKAGE, check=False)
            finally:
                try:
                    if saved_settings is None:
                        adb("shell", "rm", "-f", settings_path)
                    else:
                        subprocess.run(prefix + ["shell", "-T", "cat > " + shlex.quote(settings_path)],
                                       input=saved_settings, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       check=True, timeout=20)
                finally:
                    try:
                        overlay(original)
                    finally:
                        if immersive == "null":
                            adb("shell", "settings", "delete", "secure", "immersive_mode_confirmations")
                        else:
                            adb("shell", "settings", "put", "secure", "immersive_mode_confirmations", immersive)
        args.output.write_text(json.dumps({"serial": args.serial, "apk_sha256": apk_sha256, "passed": passed,
                                          "observations": observations, "timing_profiles": timing_profiles,
                                          "timing_thresholds": timing_thresholds, "timing_context": timing_context}, indent=2) + "\n")


if __name__ == "__main__":
    main()
