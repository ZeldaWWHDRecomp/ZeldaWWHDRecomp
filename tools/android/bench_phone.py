#!/usr/bin/env python3
"""Debug-only phone benchmark: one scripted run of a DEBUG build on an Android device over adb.

The debug APK (android/app/src/debug/.../DebugLaunch.java) turns WWHD_* string extras of the start
intent into the game's environment, so a run is the desktop scripted run (tools/bench/run_bench.py)
on the phone: a portable save state loaded at a fixed TV frame, timed stick input in game seconds,
the render-thread profiler (WWHD_PROFILE), logic timing (WWHD_TICK_STATS) and per-frame intervals
(WWHD_VK_STUTTER_TRACE). The game quits itself (WWHD_EXIT_AT_FRAME); the files are pulled and
summarized: fps, 1% low (99th percentile frame interval), p99, render-thread CPU, game (logic) CPU,
and the app's memory (dumpsys meminfo TOTAL PSS / RSS, sampled during the measured part).

Build the debug APK with its own applicationId (-PwwhdDebugSuffix=.labtest) so it never replaces an
installed player build, and put the game into that app's files/game folder (after adb push:
chmod -R a+rwX files/game files/save, as adb creates them as the shell user). Release APKs ignore
the extras. Example (2 minutes on Windfall, 30 fps and interpolated 60):

  tools/android/bench_phone.py --package org.wwhdrecomp.wwhd.labtest --state windfall.wwstate \\
      --scene windfall --mode 30 --seconds 120 --out bench/phone/w30
"""
import argparse
import json
import math
import os
import re
import shlex
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "bench"))
import run_bench  # noqa: E402  (parse_prof, summarize, logic_cpu_samples, SCENES)

PLAYER_PACKAGE = "org.wwhdrecomp.wwhd"
ACTIVITY = "org.wwhdrecomp.wwhd.WwhdActivity"


def adb(args, serial, check=True, capture=True, timeout=120):
    command = ["adb"] + (["-s", serial] if serial else []) + args
    result = subprocess.run(command, capture_output=capture, text=True, timeout=timeout)
    if check and result.returncode != 0:
        raise SystemExit("adb failed: %s\n%s" % (shlex.join(command), (result.stderr or "").strip()))
    return result.stdout if capture else ""


def shell(serial, command, check=True, timeout=120):
    return adb(["shell", command], serial, check=check, timeout=timeout)


def frame_stats(path, skip_ms=0.0):
    """Frame intervals (WWHD_VK_STUTTER_TRACE "frame" rows) after the first skip_ms of the trace."""
    import csv
    with open(path, newline="") as source:
        frames = [row for row in csv.DictReader(source) if row["kind"] == "frame"]
    if not frames:
        return {}
    start = int(frames[0]["start_ns"]) + skip_ms * 1e6
    ms = [(int(r["end_ns"]) - int(r["start_ns"])) / 1e6 for r in frames if int(r["start_ns"]) >= start]
    if not ms:
        return {}
    ordered = sorted(ms)
    def pct(p):
        return ordered[max(0, math.ceil(len(ordered) * p) - 1)]
    worst = ordered[-max(1, len(ordered) // 100):]
    total = sum(ms)
    return {"frames": len(ms), "seconds": total / 1000, "fps": len(ms) / (total / 1000),
            "median_ms": pct(.5), "p99_ms": pct(.99), "max_ms": ordered[-1],
            "low1_fps": 1000 / pct(.99), "low1_mean_fps": 1000 / statistics.fmean(worst)}


def meminfo(serial, package):
    """TOTAL PSS and TOTAL RSS (KiB) of the app from dumpsys meminfo."""
    text = shell(serial, "dumpsys meminfo " + package, check=False)
    pss = re.search(r"TOTAL PSS:\s+(\d+)", text)
    rss = re.search(r"TOTAL RSS:\s+(\d+)", text)
    if not pss:  # older layout: "TOTAL  <pss> ..."
        pss = re.search(r"^\s*TOTAL\s+(\d+)", text, re.M)
    return {"pss_kib": int(pss.group(1)) if pss else None, "rss_kib": int(rss.group(1)) if rss else None}


def process_cpu_s(serial, package):
    """utime + stime of the app process (/proc/<pid>/stat, clock ticks of 10 ms), or None."""
    pid = shell(serial, "pidof " + package, check=False).strip().split()
    if not pid:
        return None
    stat = shell(serial, "cat /proc/%s/stat" % pid[0], check=False)
    fields = stat.rsplit(")", 1)[-1].split()
    try:
        return (int(fields[11]) + int(fields[12])) / 100.0
    except (IndexError, ValueError):
        return None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--serial")
    p.add_argument("--package", required=True, help="the DEBUG build's applicationId (never the player build)")
    p.add_argument("--state", required=True, help="portable save state (.wwstate) to load")
    p.add_argument("--slot", type=int, default=4)
    p.add_argument("--scene", choices=sorted(run_bench.SCENES), default="still")
    p.add_argument("--mode", choices=["30", "60", "true60"], default="30")
    p.add_argument("--seconds", type=float, default=120, help="measured game seconds")
    p.add_argument("--warmup", type=float, default=10, help="seconds after the load excluded from the results")
    p.add_argument("--load-frame", type=int, default=3400, help="TV frame of the state load (A presses before it)")
    p.add_argument("--worker", choices=["0", "1"], help="WWHD_VK_RECORD_WORKER (builds that have it)")
    p.add_argument("--env", action="append", default=[], help="extra NAME=value for the game")
    p.add_argument("--label", default="run")
    p.add_argument("--out", required=True)
    p.add_argument("--timeout", type=float, default=900)
    p.add_argument("--force-foreground", action="store_true", help="start even when another app is in front")
    args = p.parse_args()

    if args.package == PLAYER_PACKAGE:
        raise SystemExit("refusing to drive the player build; install a debug build with -PwwhdDebugSuffix")
    flags = shell(args.serial, "dumpsys package " + args.package, check=False)
    if "DEBUGGABLE" not in flags:
        raise SystemExit(args.package + " is not an installed debug build")
    os.makedirs(args.out, exist_ok=True)
    files = "/sdcard/Android/data/%s/files" % args.package
    remote = "%s/bench/%s" % (files, args.label)
    rel = "{files}/bench/" + args.label

    if shell(args.serial, "pidof " + PLAYER_PACKAGE, check=False).strip():
        raise SystemExit("the player build is running; not starting a benchmark")
    top = shell(args.serial, "dumpsys activity activities | grep topResumedActivity", check=False)
    if args.package not in top and "launcher" not in top.lower() and not args.force_foreground:
        raise SystemExit("another app is in front (%s); not starting (--force-foreground overrides)" % top.strip())
    shell(args.serial, "am force-stop " + args.package)
    shell(args.serial, "rm -rf %s && mkdir -p %s/states" % (shlex.quote(remote), shlex.quote(remote)))
    adb(["push", args.state, "%s/states/slot%d.wwstate" % (remote, args.slot)], args.serial)
    # adb creates these as the shell user; the app reaches its Android/data folder through the
    # lower file system's permissions, so it needs them open (files pushed into game/ likewise)
    shell(args.serial, "chmod a+rwX %s/bench && chmod -R a+rwX %s" % (shlex.quote(files), shlex.quote(remote)))

    fps = {"30": 30, "60": 60, "true60": 60}[args.mode]
    origin = args.load_frame + 60  # input and the mode switch start a little after the load
    _, stick, rstick = run_bench.SCENES[args.scene]
    presses = ",".join("%d-%d:8000" % (f, f + 8) for f in range(max(0, args.load_frame - 2200), args.load_frame, 60))
    # the run ends on its own: TV frames until the end of warm-up + measured part, with headroom
    exit_frame = origin + int((args.warmup + args.seconds) * fps * 1.02) + fps
    env = {
        "WWHD_NO_AUDIO": "1", "WWHD_NO_HOST_INPUT": "1",
        "WWHD_STATE_DIR": rel + "/states", "WWHD_STATE_LOAD_AT": "%d:%d" % (args.load_frame, args.slot),
        "WWHD_PRESS": presses, "WWHD_TEST_ORIGIN": str(origin),
        "WWHD_PROFILE": "1", "WWHD_TICK_STATS": "1", "WWHD_INTERP_PASS_STATS": "1",
        # scenario end (game seconds after the origin): flushes the frame trace, writes test_done
        "WWHD_TEST_END": "%g" % (args.warmup + args.seconds),
        "WWHD_VK_STUTTER_TRACE": rel + "/frames.csv", "WWHD_LOG_FILE": rel + "/wwhd.log",
        "WWHD_EXIT_AT_FRAME": str(exit_frame), "WWHD_LAST_SESSION": "0",
    }
    if stick:
        env["WWHD_TEST_STICK"] = stick
    if rstick:
        env["WWHD_TEST_RSTICK"] = rstick
    if args.mode == "30":
        env["WWHD_INTERP"] = "0"
    elif args.mode == "60":
        env["WWHD_INTERP"] = "0"
        env["WWHD_INTERP_AT_STEP"] = str(origin)
    else:
        env["WWHD_INTERP"] = "0"
        env["WWHD_TRUE60_AT_STEP"] = str(origin)
    if args.worker is not None:
        env["WWHD_VK_RECORD_WORKER"] = args.worker
    for item in args.env:
        name, _, value = item.partition("=")
        env[name] = value
    extras = " ".join("--es %s %s" % (k, shlex.quote(v)) for k, v in sorted(env.items()))
    with open(os.path.join(args.out, "invocation.json"), "w") as f:
        json.dump({"package": args.package, "mode": args.mode, "scene": args.scene, "seconds": args.seconds,
                   "warmup": args.warmup, "env": env}, f, indent=2)
    shell(args.serial, "am start -S -W -n %s/%s %s" % (args.package, ACTIVITY, extras))

    started = time.monotonic()
    memory, temps = [], []
    measuring_after = None
    while time.monotonic() - started < args.timeout:
        time.sleep(5)
        if not shell(args.serial, "pidof " + args.package, check=False).strip():
            break
        log = shell(args.serial, "tail -c 4000 %s/wwhd.log 2>/dev/null" % remote, check=False)
        if measuring_after is None and "portable load: arrived" in log:
            measuring_after = time.monotonic() + args.warmup
        if measuring_after is not None and time.monotonic() >= measuring_after:
            sample = meminfo(args.serial, args.package)
            sample["cpu_s"] = process_cpu_s(args.serial, args.package)
            sample["t"] = round(time.monotonic() - started, 1)
            memory.append(sample)
    else:
        shell(args.serial, "am force-stop " + args.package)
        print("timeout: the run was stopped", file=sys.stderr)

    for name in ("wwhd.log", "frames.csv"):
        adb(["pull", "%s/%s" % (remote, name), os.path.join(args.out, name)], args.serial, check=False)
    lines = open(os.path.join(args.out, "wwhd.log"), errors="replace").read().splitlines() \
        if os.path.exists(os.path.join(args.out, "wwhd.log")) else []
    # measured windows: profiler reports after the load + warm-up (TV frames)
    first = origin + int(args.warmup * fps)
    windows = [w for w in run_bench.parse_prof(lines) if w["frame"] >= first]
    prof = run_bench.summarize(windows) if windows else {}
    logic = run_bench.logic_cpu_samples(lines)
    ticks = [float(m.group(1)) for l in lines for m in [re.match(r"\[tick\] CPU ([0-9.]+) ms/s", l)] if m]
    result = {"label": args.label, "mode": args.mode, "scene": args.scene,
              "loaded": any("portable load: arrived" in l for l in lines),
              "frames": {}, "render_cpu_ms": prof.get("render_cpu_ms"),
              "logic_cpu_ms": statistics.median(logic[len(logic) // 4:]) if logic else None,
              "game_thread_cpu_ms_per_s": statistics.median(ticks[len(ticks) // 3:]) if ticks else None,
              "render_wait_gpu_ms": prof.get("wait_gpu_ms"), "fps_profiler": prof.get("swaps_per_s"),
              "profiler_windows": len(windows), "memory": memory}
    trace = os.path.join(args.out, "frames.csv")
    if os.path.exists(trace):
        # the trace's first frames are boot and loading: skip by time up to the measured TV frame
        result["frames"] = frame_stats_from_frame(trace, first)
    cpu = [m for m in memory if m.get("cpu_s") is not None]
    if len(cpu) >= 2 and cpu[-1]["t"] > cpu[0]["t"]:
        # whole-process CPU over the measured part (all threads), per second and per presented frame
        per_s = (cpu[-1]["cpu_s"] - cpu[0]["cpu_s"]) / (cpu[-1]["t"] - cpu[0]["t"])
        result["total_cpu_ms_per_s"] = per_s * 1000
        if result["frames"].get("fps"):
            result["total_cpu_ms_per_frame"] = per_s * 1000 / result["frames"]["fps"]
    if memory:
        for key in ("pss_kib", "rss_kib"):
            values = [m[key] for m in memory if m[key]]
            if values:
                result[key.replace("_kib", "_mib_median")] = statistics.median(values) / 1024
                result[key.replace("_kib", "_mib_max")] = max(values) / 1024
    with open(os.path.join(args.out, "result.json"), "w") as f:
        json.dump(result, f, indent=2)
    print(json.dumps({k: v for k, v in result.items() if k != "memory"}, indent=2))


def frame_stats_from_frame(path, first_frame):
    """frame_stats over the frames from TV frame first_frame on."""
    import csv
    with open(path, newline="") as source:
        frames = [row for row in csv.DictReader(source) if row["kind"] == "frame"]
    start = next((int(r["start_ns"]) for r in frames if int(r["frame"]) >= first_frame), None)
    if start is None:
        return {}
    return frame_stats(path, (start - int(frames[0]["start_ns"])) / 1e6)


if __name__ == "__main__":
    main()
