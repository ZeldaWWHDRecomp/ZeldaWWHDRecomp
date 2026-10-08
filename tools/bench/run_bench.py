#!/usr/bin/env python3
"""Fixed-scene renderer benchmark: runs the game headless from a save state with scripted input and
summarizes the render-thread profiler's reports (runtime/src/render_prof.h) as JSON and CSV.

Every run uses a fresh COPY of the save, the given save-state folder (read only: the state file is
loaded, never written), its own shader caches and settings files, no audio, no gamepad and no host
input. One game process at a time; it is stopped with TERM, then KILL, and checked to be gone.

Example (two variants, interleaved A B B A ..., 6 runs each, 60 fps interpolation, Vulkan):

  tools/bench/run_bench.py --binary build/wwhd --state-dir my_states --scene outset \\
      --fps 60 --renderer vulkan --runs 6 --out bench/sync \\
      --variant base:WWHD_VK_LAZY_DRAW_DONE=0,WWHD_VK_ASYNC_PRESENT=0 --variant new:

Scenes (input is timed in game seconds from TV frame --origin, so 30 and 60 fps runs see the same
input): outset = walk and look around on Outset Island (state slot 3 by default), windfall = walk
through Windfall town (slot 2), still = no input (any slot). The state file must be
<state-dir>/slot<N>.bin, made with the game's own save-state keys.
"""
import argparse
import csv
import json
import os
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

SCENES = {
    # slot, left stick (from-to seconds : x : y), right stick
    "outset": (3, "0-13:0:1,13-23:1:0.5,23-36:0:-1,36-46:-1:0,46-63:0:1", "20-30:1:0,53-60:-1:0"),
    "windfall": (2, "0-10:0:1,10-20:1:0,20-33:0:-1,33-43:-1:0,43-56:0:1", "13-23:1:0,46-53:-1:0"),
    "still": (1, "", ""),
}


def other_games(own_pid=None):
    """Running game executables (wwhd*) other than own_pid: the executable name, not the arguments
    (this script itself is started with --game)."""
    try:
        out = subprocess.run(["ps", "-Ao", "pid=,comm="], capture_output=True, text=True).stdout
    except OSError:
        return []
    found = []
    for line in out.splitlines():
        pid, _, comm = line.strip().partition(" ")
        if os.path.basename(comm.strip()).startswith("wwhd") and (own_pid is None or int(pid) != own_pid):
            found.append(line.strip()[:160])
    return found


def load1():
    try:
        return os.getloadavg()[0]
    except OSError:
        return -1.0


def stop(proc):
    if proc.poll() is not None:
        return
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=10)


NUM = r"(-?[0-9]+(?:\.[0-9]+)?)"


def parse_prof(lines):
    """[prof] report blocks (render_prof.cpp) -> list of dicts, one per 120-frame window."""
    windows, cur = [], None
    for line in lines:
        if not line.startswith("[prof] "):
            continue
        body = line[7:]
        m = re.match(r"frame (\d+): " + NUM + r" frames \(" + NUM + r" hold\), " + NUM + r" ms/frame, " + NUM +
                     r" swaps/s, " + NUM + r" logic steps/s; render thread CPU " + NUM + r" ms/frame, in ops " + NUM +
                     r" ms/frame, idle \(waiting for commands\) " + NUM + " ms/frame", body)
        if m:
            cur = {"frame": int(m.group(1)), "frames": float(m.group(2)), "hold_frames": float(m.group(3)),
                   "frame_ms": float(m.group(4)), "swaps_per_s": float(m.group(5)), "logic_steps_per_s": float(m.group(6)),
                   "render_cpu_ms": float(m.group(7)), "render_ops_ms": float(m.group(8)), "render_idle_ms": float(m.group(9))}
            windows.append(cur)
            continue
        if cur is None:
            continue
        if body.startswith("ops ms/frame:"):
            for name, ms, n in re.findall(r" ([a-z]+) " + NUM + r" \(" + NUM + r"/frame\)", body):
                cur["op_" + name + "_ms"] = float(ms)
                cur["op_" + name + "_per_frame"] = float(n)
        elif body.startswith("draw phases"):
            head, _, tail = body.partition(":")
            phases, _, rest = tail.partition(";")
            for name, ms in re.findall(r" ([a-z]+) " + NUM, phases):
                cur["phase_" + name + "_ms"] = float(ms)
            m = re.search(r"untracked " + NUM + r"; " + NUM + " us/draw", rest)
            if m:
                cur["phase_untracked_ms"], cur["us_per_draw"] = float(m.group(1)), float(m.group(2))
        elif body.startswith("render thread waits"):
            m = re.search(r"GPU " + NUM + r" \(" + NUM + r"/frame\) acquire " + NUM + " present " + NUM, body)
            if m:
                cur["wait_gpu_ms"], cur["wait_gpu_per_frame"] = float(m.group(1)), float(m.group(2))
                cur["wait_acquire_ms"], cur["wait_present_ms"] = float(m.group(3)), float(m.group(4))
            for name, ms, n in re.findall(r" (DrawDone|CopySurface|flip|other) " + NUM + r" ms \(" + NUM + r"/frame\)", body):
                cur["game_wait_" + name + "_ms"] = float(ms)
                cur["game_wait_" + name + "_per_frame"] = float(n)
        elif body.startswith("uploads MiB/frame"):
            m = re.match(r"uploads MiB/frame " + NUM, body)
            cur["upload_mib"] = float(m.group(1))
            logic = body.split("logic/30 fps frames:")[1].split(";")[0]
            for name, v in re.findall(r" ([a-z]+) " + NUM, logic):
                cur["upload_" + name + "_mib"] = float(v)
            if "hold frames:" in body:
                for name, v in re.findall(r" ([a-z]+) " + NUM, body.split("hold frames:")[1]):
                    cur["upload_hold_" + name + "_mib"] = float(v)
        elif body.startswith("unique guest"):
            m = re.search(r"all " + NUM, body)
            cur["unique_all_mib"] = float(m.group(1))
            for name, u, c in re.findall(r" ([a-z]+) " + NUM + " of " + NUM + " copied", body):
                cur["unique_" + name + "_mib"] = float(u)
                cur["copied_" + name + "_mib"] = float(c)
        elif body.startswith("draw classes"):
            m = re.match(r"draw classes: " + NUM + r"% same registers, " + NUM + r"% only buffer pointers/ALU constants, " +
                         NUM + r"% other \(" + NUM + " draws/frame\)", body)
            if m:
                cur["draws_same_pct"], cur["draws_fast_pct"] = float(m.group(1)), float(m.group(2))
                cur["draws_other_pct"], cur["draws_per_frame"] = float(m.group(3)), float(m.group(4))
                cur["top_other_regs"] = body.split("top other writes/frame:")[1].strip()
        elif body.startswith("shader translations"):
            cur["shader_report"] = body
    return windows


def summarize(windows):
    """Mean of every numeric field over the measured windows."""
    keys = sorted({k for w in windows for k, v in w.items() if isinstance(v, float)})
    return {k: statistics.fmean([w[k] for w in windows if k in w]) for k in keys}


def run_once(args, variant, env_extra, index, out_dir):
    binary = args.variant_binaries.get(variant, args.binary)
    run_dir = os.path.join(out_dir, "%s_%02d" % (variant, index))
    shutil.rmtree(run_dir, ignore_errors=True)
    os.makedirs(run_dir)
    shutil.copytree(args.save, os.path.join(run_dir, "save"))
    os.chmod(os.path.join(run_dir, "save"), 0o755)
    for root, dirs, files in os.walk(os.path.join(run_dir, "save")):  # the copy must be writable
        for n in dirs + files:
            os.chmod(os.path.join(root, n), 0o755 if n in dirs else 0o644)
    slot, stick, rstick = SCENES[args.scene]
    slot = args.slot or slot
    load_at, origin = args.load_frame, args.origin
    cache = os.path.abspath(args.cache_dir or os.path.join(out_dir, "cache"))
    os.makedirs(cache, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith("WWHD_")}
    press_from = args.press_from if args.press_from >= 0 else max(0, load_at - 300)
    presses = ",".join("%d-%d:8000" % (f, f + 8) for f in range(press_from, load_at, args.press_every))
    env.update({
        "WWHD_NO_AUDIO": "1", "WWHD_NO_GAMEPAD": "1", "WWHD_NO_HOST_INPUT": "1",
        "WWHD_SHADER_CACHE": os.path.join(cache, "shaders.bin"), "WWHD_VK_SHADER_CACHE": os.path.join(cache, "vkshaders"),
        "WWHD_VK_PIPELINE_CACHE": os.path.join(cache, "vkpipelines.bin"),
        "WWHD_DISPLAY_SETTINGS": os.path.join(run_dir, "display.plist"), "WWHD_SETTINGS": os.path.join(run_dir, "settings.ini"),
        "XDG_CONFIG_HOME": os.path.join(run_dir, "config"),
        "WWHD_PRESS": presses, "WWHD_STATE_DIR": os.path.abspath(args.state_dir), "WWHD_STATE_LOAD_AT": "%d:%d" % (load_at, slot),
        "WWHD_TEST_ORIGIN": str(origin), "WWHD_TEST_END": str(args.seconds),
        "WWHD_RENDERER_RUNTIME": args.renderer, "WWHD_PROFILE": "1", "WWHD_TICK_STATS": "1",
        "WWHD_INTERP_PASS_STATS": "1",
    })
    if not args.visible:
        env["WWHD_HIDDEN_WINDOWS"] = "1"  # nothing pops up, but nothing is presented either
    if stick:
        env["WWHD_TEST_STICK"] = stick
    if rstick:
        env["WWHD_TEST_RSTICK"] = rstick
    if args.renderer == "vulkan":
        env["WWHD_VK_CPU_ONLY_STATS"] = "1"
    if args.fps in ("60", "120", "240"):
        env["WWHD_INTERP_AT_STEP"] = str(load_at + 60)
        if args.fps != "60":  # the rate, without switching interpolation on before the step above
            env["WWHD_INTERP"], env["WWHD_INTERP_FPS"] = "0", args.fps
    if args.display_hz is not None:
        env["WWHD_DISPLAY_HZ"] = str(args.display_hz)
    elif args.fps == "true60":
        env["WWHD_TRUE60_AT_STEP"] = str(load_at + 60)
    if args.uncapped:
        env["WWHD_VK_UNCAPPED"] = "1"
    env.update(env_extra)
    if args.gate:
        subprocess.run(args.gate, shell=True, check=False)  # e.g. wait for another benchmark to finish
    while args.wait_for_others and other_games():
        print("  waiting: another game process is running", file=sys.stderr)
        time.sleep(30)
    while args.max_load and load1() >= args.max_load:
        print("  waiting: 1-minute load %.2f >= %.2f" % (load1(), args.max_load), file=sys.stderr, flush=True)
        time.sleep(30)
    load_before = load1()
    started = time.time()
    with open(os.path.join(run_dir, "log"), "w") as log:
        proc = subprocess.Popen([os.path.abspath(binary), "--game", os.path.abspath(args.game), "--save", "save"],
                                cwd=run_dir, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        status = "ok"
        try:
            while proc.poll() is None:
                if os.path.exists(os.path.join(run_dir, "test_done")):
                    time.sleep(1)
                    break
                if time.time() - started > args.timeout:
                    status = "timeout"
                    break
                if args.watch_others and other_games(proc.pid):
                    status = "disturbed"  # another game started meanwhile: the timings are not usable
                    break
                time.sleep(1)
        finally:
            stop(proc)
    if proc.poll() is None:
        raise RuntimeError("game process %d is still running" % proc.pid)
    load_after = load1()
    shutil.rmtree(os.path.join(run_dir, "save"), ignore_errors=True)
    with open(os.path.join(run_dir, "log"), errors="replace") as f:
        lines = f.read().splitlines()
    loaded = next((i for i, l in enumerate(lines) if "Loaded slot" in l), None)
    if loaded is None and status == "ok":
        status = "no state load"
    windows = parse_prof(lines[loaded:] if loaded is not None else [])
    windows = windows[args.skip_windows:]
    paced = [float(m.group(1)) for l in lines[loaded or 0:] for m in [re.search(r"\[interp\] paced: " + NUM + "% of in-between", l)] if m]
    steps = [(float(m.group(1)), float(m.group(2))) for l in lines[loaded or 0:]
             for m in [re.search(r"\[interp\] " + NUM + r" logic steps/s \([^,]*, " + NUM + " frames per step", l)] if m]
    pacing = [float(m.group(1)) for l in lines[loaded or 0:] for m in [re.search(r"\[vulkan pacing\].*p95 " + NUM, l)] if m]
    result = {"variant": variant, "run": index, "status": status, "env": env_extra, "load_before": load_before,
              "load_after": load_after, "seconds": round(time.time() - started, 1), "windows": len(windows),
              "summary": summarize(windows) if windows else {}}
    tick = [float(m.group(1)) for l in lines[loaded or 0:] for m in [re.search(r"\[tick\] CPU " + NUM, l)] if m]
    logic = [float(m.group(1)) for l in lines[loaded or 0:] for m in [re.search(r"main thread CPU per pass: logic " + NUM, l)] if m]
    if tick:
        result["summary"]["tick_cpu_ms_per_s"] = statistics.fmean(tick[1:] or tick)
    if logic:
        result["summary"]["logic_cpu_ms"] = statistics.fmean(logic[1:] or logic)
    if pacing:
        result["summary"]["vulkan_pacing_p95_ms"] = statistics.fmean(pacing[args.skip_windows:] or pacing)
    if paced:  # paced interpolation: share of in-between frames drawn, per 300 steps
        result["summary"]["paced_drawn_pct"] = statistics.fmean(paced[1:] or paced)
    if steps:  # frame interpolation's own count: logic steps/s and frames per step, per 300 steps
        result["summary"]["interp_frames_per_step"] = statistics.fmean(f for _, f in (steps[1:] or steps))
    with open(os.path.join(run_dir, "result.json"), "w") as f:
        json.dump({"result": result, "windows": windows}, f, indent=1)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--binary", required=True, help="game executable (wwhd)")
    p.add_argument("--variant-binary", action="append", default=[], metavar="NAME=PATH",
                   help="executable for a named variant (keeps cross-build A/B runs interleaved)")
    p.add_argument("--game", default=os.path.join(REPO, "game"), help="extracted game folder (default: <repo>/game)")
    p.add_argument("--save", default=os.path.join(REPO, "save"), help="save folder, copied for every run (default: <repo>/save)")
    p.add_argument("--state-dir", required=True, help="folder with slot<N>.bin save states (read only)")
    p.add_argument("--scene", choices=sorted(SCENES), default="outset")
    p.add_argument("--slot", type=int, default=0, help="state slot (default: the scene's)")
    p.add_argument("--fps", choices=["30", "60", "120", "240", "true60"], default="30",
                   help="60, 120, 240 = frame interpolation (120/240: paced unless WWHD_INTERP_PACED=0)")
    p.add_argument("--display-hz", type=int, help="WWHD_DISPLAY_HZ: the display refresh rate frame interpolation is "
                   "capped to (0: no cap; default: the detected one, e.g. 120 on a ProMotion Mac)")
    p.add_argument("--renderer", choices=["vulkan", "metal"], default="vulkan")
    p.add_argument("--visible", action="store_true",
                   help="show the game windows (presentation, swapchain and vsync pacing are only exercised then)")
    p.add_argument("--uncapped", action="store_true", help="WWHD_VK_UNCAPPED=1: throughput, not gameplay pacing")
    p.add_argument("--seconds", type=float, default=60, help="scenario length in game seconds after --origin")
    # the state load restores the whole game state, so it only needs the boot to have finished; A
    # presses from frame 120 skip the intro and title (validated 2026-10-07 at loads 360 and 600)
    p.add_argument("--load-frame", type=int, default=450, help="TV frame of the state load")
    p.add_argument("--origin", type=int, default=650, help="TV frame where the scripted input starts")
    p.add_argument("--press-from", type=int, default=120,
                   help="first TV frame of the A presses through the intro and title (-1: load frame - 300)")
    p.add_argument("--press-every", type=int, default=30, help="frames between A presses")
    p.add_argument("--skip-windows", type=int, default=2, help="120-frame reports after the load to ignore (warm-up)")
    p.add_argument("--variant", action="append", default=[], metavar="NAME:K=V,K=V",
                   help="a configuration (environment); repeat for A/B runs, run interleaved")
    p.add_argument("--runs", type=int, default=1, help="runs per variant")
    p.add_argument("--warmup", action="store_true", help="one discarded run first (warms the shader caches)")
    p.add_argument("--cache-dir", help="shader/pipeline caches shared by the runs (default: <out>/cache)")
    p.add_argument("--timeout", type=float, default=600)
    p.add_argument("--max-load", type=float, default=0, help="wait until the 1-minute load is below this (0 disables)")
    p.add_argument("--gate", help="shell command run (and waited for) before every run")
    p.add_argument("--out", default=os.path.join(REPO, "build", "bench"))
    p.add_argument("--no-wait", dest="wait_for_others", action="store_false", help="don't wait for other game processes")
    p.add_argument("--no-watch", dest="watch_others", action="store_false", help="don't discard runs disturbed by another game")
    args = p.parse_args()
    if not os.path.exists(os.path.join(args.state_dir, "slot%d.bin" % (args.slot or SCENES[args.scene][0]))):
        p.error("no slot%d.bin in %s" % (args.slot or SCENES[args.scene][0], args.state_dir))
    args.variant_binaries = dict(v.split("=", 1) for v in args.variant_binary)
    variants = []
    for v in args.variant or ["default:"]:
        name, _, envs = v.partition(":")
        env = dict(kv.split("=", 1) for kv in envs.split(",") if kv)
        variants.append((name, env))
    os.makedirs(args.out, exist_ok=True)
    results = []
    if args.warmup:
        run_once(args, "warmup", variants[0][1], 0, args.out)
    for i in range(args.runs):
        order = variants if i % 2 == 0 else list(reversed(variants))  # A B, B A, ...
        for name, env in order:
            for attempt in range(3):
                r = run_once(args, name, env, i + 1, args.out)
                print("%s run %d: %s, %d windows, load %.1f -> %.1f, %s" % (
                    name, i + 1, r["status"], r["windows"], r["load_before"], r["load_after"],
                    ", ".join("%s %.2f" % (k, r["summary"][k]) for k in ("frame_ms", "swaps_per_s", "render_cpu_ms",
                                                                          "wait_gpu_ms") if k in r["summary"])), flush=True)
                if r["status"] == "ok":
                    break
            results.append(r)
    # per-variant statistics over the runs
    table = {}
    for name, _ in variants:
        runs = [r["summary"] for r in results if r["variant"] == name and r["status"] == "ok" and r["summary"]]
        keys = sorted({k for s in runs for k in s})
        stats = {}
        for k in keys:
            vals = [s[k] for s in runs if k in s]
            stats[k] = {"median": statistics.median(vals), "mean": statistics.fmean(vals), "min": min(vals), "max": max(vals),
                        "q1": statistics.quantiles(vals, n=4)[0] if len(vals) > 1 else vals[0],
                        "q3": statistics.quantiles(vals, n=4)[2] if len(vals) > 1 else vals[0],
                        "iqr": (statistics.quantiles(vals, n=4)[2] - statistics.quantiles(vals, n=4)[0]) if len(vals) > 1 else 0.0,
                        "stdev": statistics.stdev(vals) if len(vals) > 1 else 0.0, "n": len(vals)}
        table[name] = stats
    meta = {k: getattr(args, k) for k in ("scene", "fps", "renderer", "uncapped", "visible", "seconds", "runs", "display_hz")}
    meta["binary"] = os.path.basename(args.binary)
    with open(os.path.join(args.out, "summary.json"), "w") as f:
        json.dump({"meta": meta, "variants": table, "runs": results}, f, indent=1)
    keys = sorted({k for t in table.values() for k in t})
    with open(os.path.join(args.out, "summary.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["metric"] + ["%s %s" % (n, s) for n, _ in variants for s in ("median", "min", "max", "n")])
        for k in keys:
            row = [k]
            for n, _ in variants:
                s = table[n].get(k)
                row += [("%.4g" % s[x]) if s else "" for x in ("median", "min", "max", "n")] if s else ["", "", "", ""]
            w.writerow(row)
    for k in ("frame_ms", "swaps_per_s", "logic_steps_per_s", "render_cpu_ms", "render_idle_ms", "wait_gpu_ms",
              "game_wait_DrawDone_ms", "paced_drawn_pct", "interp_frames_per_step", "upload_mib", "draws_per_frame"):
        if any(k in t for t in table.values()):
            print("%-24s %s" % (k, "  ".join("%s %.2f [%.2f..%.2f]" % (n, table[n][k]["median"], table[n][k]["min"], table[n][k]["max"])
                                            for n, _ in variants if k in table[n])))
    print("summary: %s" % os.path.join(args.out, "summary.json"))


if __name__ == "__main__":
    main()
