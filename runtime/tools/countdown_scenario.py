#!/usr/bin/env python3
"""Headless shared-countdown regression, using a caller's private local checkpoint.

usage: countdown_scenario.py BINARY GAME SAVE STATE_DIR WORK [--slot 2]
       [--renderer metal|vulkan|both] [--mode 30|60|120|true60|all]

STATE_DIR must contain a full checkpoint in the running letter-sorting game, with
at least 15 seconds remaining. Create it locally; never commit/share it. SAVE is
the matching Quest Log folder. Boot inputs leave file selection before loading,
so the checkpoint's completed network thread can be restored. Boot is uncapped;
WWHD_COUNTDOWN_REALTIME restores normal pacing as soon as the timer is present.
No saves/states/assets are included. WORK must be a fresh directory. Captures,
isolated caches/settings and copied saves stay there; only scalar results may
be used in reports. --check-trace FILE checks an existing trace without a game.
"""
import argparse
import json
from pathlib import Path
import sys

import portable_state_scenario as scenario
from medli_scenario import resource_gate


def measure(path):
    rows = [list(map(float, line.split())) for line in Path(path).read_text().splitlines()]
    # Discard pauses, startup/end delays, zero and the native unsigned underflow after expiry.
    rows = [r for r in rows if r[5] == 2 and r[6] == 0 and 0 < r[7] < 0x80000000]
    if len(rows) < 60:
        raise ValueError("not enough running countdown samples")
    pairs = [(a, b) for a, b in zip(rows, rows[1:]) if b[0] == a[0] + 1 and b[7] <= a[7]]
    holds = [(a, b) for a, b in pairs if b[2]]
    full = [(a, b) for a, b in pairs if not b[2]]
    # Use consecutive pairs, rather than subtracting endpoints across a pause/reset/load.
    ticks = sum(a[7] - b[7] for a, b in pairs)
    steps = sum(b[1] - a[1] for a, b in pairs)
    wall = sum(b[3] - a[3] for a, b in pairs)
    bad_holds = sum(a[7] != b[7] for a, b in holds)
    bad_full = sum(a[7] - b[7] != 1 for a, b in full)
    return {"samples": len(rows), "hold_samples": len(holds), "hold_changes": bad_holds,
            "full_samples": len(full), "full_bad": bad_full, "timer_ticks": ticks,
            "logic_steps": steps, "wall_seconds": wall, "ticks_per_second": ticks / wall,
            "pass": bool(full) and ticks == steps and bad_holds == 0 and bad_full == 0}


def measure_display(path, fps, paced):
    surfaces = {}
    for line in Path(path).read_text().splitlines():
        step, fraction, native, shown, surface = line.split()
        if 0 < int(shown) < 0x7fffffff:
            surfaces.setdefault(surface, []).append((int(step), float(fraction), int(shown)))
    result = {}
    for surface, rows in surfaces.items():
        phases = {}
        for step, fraction, shown in rows:
            phases.setdefault(step, set()).add(fraction)
        multi = sum(len(values) > 1 for values in phases.values())
        backwards = sum(b[2] > a[2] for a, b in zip(rows, rows[1:])
                        if 0 <= b[0] - a[0] <= 1 and b[:2] != a[:2])
        smooth = fps == 30 or (multi > 0 if paced else multi >= .95 * max(1, len(phases) - 2))
        result[surface] = {"steps": len(phases), "intermediate_refresh_steps": multi,
                           "backwards": backwards, "pass": smooth and backwards == 0}
    return {"surfaces": result, "pass": bool(result) and all(r["pass"] for r in result.values())}


def run_case(args, renderer, mode, paced):
    fps = 60 if mode == "true60" else int(mode)
    tag = f"{renderer}-{mode}-paced{paced}"
    resource_gate(args.work)
    directory = Path(scenario.prepare(str(args.work), tag, str(args.save)))
    env = {
        "WWHD_RENDERER_RUNTIME": renderer, "WWHD_STATE_DIR": str(args.state_dir),
        "WWHD_STATE_LOAD_AT": f"1800:{args.slot}",
        "WWHD_PRESS": ",".join(f"{f}-{f + 4}:8000" for f in range(600, 1500, 50)),
        "WWHD_INTERP": "0", "WWHD_TRUE60": "0", "WWHD_INTERP_FPS": str(max(60, fps)),
        "WWHD_INTERP_PACED": str(paced), "WWHD_DISPLAY_HZ": "0", "WWHD_UNCAPPED": "1",
        "WWHD_TEST_ORIGIN": "1", "WWHD_TEST_ORIGIN_LOAD": "0", "WWHD_TEST_END": "10",
        "WWHD_TEST_MODE": f"{2 if mode == 'true60' else int(fps != 30)}@0",
        "WWHD_COUNTDOWN_TRACE": str(directory / "countdown.txt"), "WWHD_COUNTDOWN_REALTIME": "1",
        "WWHD_COUNTDOWN_DISPLAY_TRACE": str(directory / "display.txt"),
        "WWHD_DISPLAY_SETTINGS": str(directory / "display.plist"),
        "WWHD_SETTINGS": str(directory / "settings.ini"), "WWHD_NO_CONTROLLERS": "1",
    }
    log = scenario.run_game(str(args.binary), str(args.game), str(directory), env,
                            lambda _: (directory / "test_done").exists(), 120)
    if f"Loaded slot {args.slot}" not in log or not (directory / "test_done").exists():
        raise RuntimeError(f"{tag}: checkpoint/scenario did not complete")
    result = {"case": tag, **measure(directory / "countdown.txt")}
    result["display"] = measure_display(directory / "display.txt", fps, paced)
    result["pass"] = result["pass"] and result["display"]["pass"]
    # Slow machines can run below 30 logic steps/s; exact tick/step equality is the invariant.
    print(json.dumps(result), flush=True)
    return result


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--check-trace":
        result = measure(sys.argv[2]); print(json.dumps(result, indent=2))
        return 0 if result["pass"] else 1
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("binary", "game", "save", "state_dir", "work"):
        parser.add_argument(name, type=lambda p: Path(p).resolve())
    parser.add_argument("--slot", type=int, choices=range(1, 6), default=2)
    parser.add_argument("--renderer", choices=("metal", "vulkan", "both"), default="both")
    parser.add_argument("--mode", choices=("30", "60", "120", "true60", "all"), default="all")
    args = parser.parse_args()
    if args.work.exists(): parser.error("WORK must be a fresh directory")
    args.work.mkdir(parents=True)
    renderers = ("metal", "vulkan") if args.renderer == "both" else (args.renderer,)
    modes = ("30", "60", "120", "true60") if args.mode == "all" else (args.mode,)
    results = [run_case(args, renderer, mode, paced)
               for renderer in renderers for mode in modes for paced in (0, 1)]
    (args.work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return 0 if all(r["pass"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
