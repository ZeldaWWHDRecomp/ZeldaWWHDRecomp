#!/usr/bin/env python3
"""Private-save boot/input smoke, not a combat acceptance test.

Usage: quick_use_smoke.py BINARY GAME SAVE WORK [--renderer vulkan|metal]
WORK must not exist. SAVE must contain a playable Quest Log 1 in user/cking.sav.
Runs the mod off/on in native 30, interpolation and true60 modes. All settings,
logs, caches and copied saves stay under WORK; no checkpoints/assets are shipped.
"""
import argparse
import hashlib
import json
from pathlib import Path

import portable_state_scenario as scenario


def hashes(root):
    return {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in root.rglob("*") if path.is_file()}


def run_case(args, enabled, mode):
    work = Path(scenario.prepare(str(args.work), f"enabled-{enabled}-mode-{mode}", str(args.save)))
    env = {
        "WWHD_RENDERER_RUNTIME": args.renderer, "WWHD_UNCAPPED": "1",
        "WWHD_INTERP": "0", "WWHD_TRUE60": "0", "WWHD_INTERP_FPS": "60",
        "WWHD_INTERP_PACED": "0", "WWHD_DISPLAY_HZ": "0",
        "WWHD_SETTINGS": str(work / "settings.ini"),
        "WWHD_DISPLAY_SETTINGS": str(work / "display.ini"),
        "WWHD_TEST_ORIGIN": "600", "WWHD_TEST_DEBUG": "1", "WWHD_TEST_END": "36",
        "WWHD_TEST_MODE": f"{mode}@25",
        "WWHD_TEST_PRESS": ",".join(f"{t}-{t + .15}:8000" for t in (5, 8, 11, 14, 17, 20))
                           + ",24-30:80,26-26.2:2000,28-28.2:1000,30-31:10",
        "WWHD_MOD_QUICK_USE": str(enabled), "WWHD_MODS_TRACE": str(work / "mods.trace"),
    }
    log = scenario.run_game(str(args.binary), str(args.game), str(work), env,
                            lambda _: (work / "test_done").exists(), 120)
    trace_path = work / "mods.trace"
    trace = trace_path.read_text() if trace_path.exists() else ""
    result = {"enabled": bool(enabled), "mode": mode,
              "completed": (work / "test_done").exists(),
              "mode_applied": f"60 fps mode {mode}" in log,
              "link_executed": "link proc" in trace,
              "fatal": any(text in log for text in ("[fatal]", "Segmentation fault", "guest trap"))}
    result["pass"] = result["completed"] and result["mode_applied"] and result["link_executed"] and not result["fatal"]
    print(json.dumps(result), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("binary", "game", "save", "work"):
        parser.add_argument(name, type=lambda value: Path(value).resolve())
    parser.add_argument("--renderer", choices=("vulkan", "metal"), default="vulkan")
    args = parser.parse_args()
    if args.work.exists():
        parser.error("WORK must be a fresh directory")
    if not (args.save / "user/cking.sav").is_file():
        parser.error("SAVE must contain user/cking.sav with a playable Quest Log 1")
    original = hashes(args.save)
    args.work.mkdir(parents=True)
    results = [run_case(args, enabled, mode) for enabled in (0, 1) for mode in (0, 1, 2)]
    unchanged = hashes(args.save) == original
    report = {"original_saves_unchanged": unchanged, "combat_validated": False, "cases": results}
    (args.work / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if unchanged and all(result["pass"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
