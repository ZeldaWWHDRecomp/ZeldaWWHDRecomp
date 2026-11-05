#!/usr/bin/env python3
"""Boot/input smoke for optional lock-on assistance, using private copies of a local save.

Usage: lock_on_smoke.py BINARY GAME SAVE WORK [--renderer vulkan|metal]

Runs assistance off/on in native 30 and interpolated 60 modes. WORK must
not exist. Saves, settings, caches and logs stay in WORK; nothing is written to
SAVE. This is NOT a multi-enemy combat acceptance test: it verifies native-game
startup and input integration without claiming acquisition/release/cycling works
in combat. No game files, assets or checkpoints are included.
"""
import argparse
import hashlib
import json
from pathlib import Path

import portable_state_scenario as scenario


def run_case(args, enabled, mode):
    directory = Path(scenario.prepare(str(args.work), f"enabled-{enabled}-mode-{mode}", str(args.save)))
    env = {
        "WWHD_RENDERER_RUNTIME": args.renderer, "WWHD_UNCAPPED": "1",
        "WWHD_INTERP": "0", "WWHD_INTERP_FPS": "60",
        "WWHD_INTERP_PACED": "0", "WWHD_DISPLAY_HZ": "0",
        "WWHD_SETTINGS": str(directory / "settings.ini"),
        "WWHD_DISPLAY_SETTINGS": str(directory / "display.ini"),
        "WWHD_TEST_ORIGIN": "600", "WWHD_TEST_DEBUG": "1", "WWHD_TEST_END": "36",
        "WWHD_TEST_MODE": f"{mode}@25",
        "WWHD_TEST_PRESS": ",".join(f"{t}-{t + .15}:8000" for t in (5, 8, 11, 14, 17, 20))
                           + ",24-29:80,26-26.2:20000,28-28.2:20000",
        "WWHD_MOD_LOCK_ON": str(enabled), "WWHD_MOD_LOCK_ON_RANGE": "3",
        "WWHD_MOD_LOCK_ON_CYCLING": "1", "WWHD_MODS_TRACE": str(directory / "mods.trace"),
    }
    log = scenario.run_game(str(args.binary), str(args.game), str(directory), env,
                            lambda _: (directory / "test_done").exists(), 120)
    trace_path = directory / "mods.trace"
    trace = trace_path.read_text() if trace_path.exists() else ""
    result = {
        "enabled": bool(enabled), "mode": mode,
        "completed": (directory / "test_done").exists(),
        "mode_applied": f"60 fps mode {mode}" in log,
        "link_executed": "link proc" in trace,
        "fatal": any(marker in log for marker in ("[fatal]", "Segmentation fault", "guest trap")),
    }
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
    originals = {path: hashlib.sha256(path.read_bytes()).digest()
                 for path in (args.save / "user/cking.sav", args.save / "user/cking_playlog.sav") if path.exists()}
    args.work.mkdir(parents=True)
    results = [run_case(args, enabled, mode) for enabled in (0, 1) for mode in (0, 1, 2)]
    unchanged = all(hashlib.sha256(path.read_bytes()).digest() == digest for path, digest in originals.items())
    report = {"original_saves_unchanged": unchanged, "combat_validated": False, "cases": results}
    (args.work / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if unchanged and all(result["pass"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
