#!/usr/bin/env python3
"""Replay a one-tap item switch from a LOCAL full checkpoint, not bundled game data.

Usage: quick_use_checkpoint_replay.py BINARY GAME SAVE CHECKPOINT WORK [options]
The checkpoint should put Link near a lockable enemy, already holding another item.
WORK must be new. All saves/settings/states/caches are isolated; no checkpoint is
written back. Passing observes a single mod release through the native checker,
not enemy damage, item effectiveness, or every possible combat interaction.
"""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

import portable_state_scenario as scenario
from quick_use_smoke import hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "game", "save", "checkpoint", "work"):
        parser.add_argument(name, type=lambda v: Path(v).resolve())
    parser.add_argument("--enabled", type=int, choices=(0, 1), default=1)
    parser.add_argument("--speed", type=float, choices=(1.0, 1.25, 1.5), default=1.0)
    parser.add_argument("--expect-native-launches", type=int, help="exact native launch count for the requested item")
    parser.add_argument("--expect-ammo-used", type=int, help="exact arrow/bomb inventory consumption")
    parser.add_argument("--expect-acceleration", type=int, choices=(0, 1), help="override whether a boosted native advance is expected")
    parser.add_argument("--mode", type=int, choices=(0, 1, 2), default=0)
    parser.add_argument("--item", type=lambda v: int(v, 0), default=0x25)
    parser.add_argument("--button", type=lambda v: int(v, 0), default=0x10)
    parser.add_argument("--hold", type=float, default=0.1)
    parser.add_argument("--expect-proc", type=lambda v: int(v, 0))
    parser.add_argument("--native-only", action="store_true", help="assert an unchanged native one-press path, with no mod request/release")
    parser.add_argument("--presses", help="override the timed button sequence (advanced local fixtures)")
    args = parser.parse_args()
    if args.work.exists():
        parser.error("WORK must be a fresh directory")
    procedures = {0x25: 0x7D, 0x27: 0x95, 0x2D: 0x81, 0x2F: 0x84,
                  0x31: 0x71, 0x33: 0x52, 0x34: 0x92, 0x35: 0x95, 0x36: 0x95}
    if args.item not in procedures or args.button not in (0x10, 0x1000, 0x2000):
        parser.error("use a supported item ID and its assigned X/Y/R button")
    instrumented = args.item in (0x27, 0x2D, 0x31, 0x33, 0x35, 0x36)
    if args.expect_native_launches is not None and (not instrumented or args.expect_native_launches < 0):
        parser.error("native launch counts require an instrumented bow/boomerang/bomb/hammer item and a nonnegative count")
    ammo_offset = 0x20 + (0x6A if args.item == 0x31 else 0x69)
    if args.expect_ammo_used is not None and (args.item not in (0x27, 0x35, 0x36, 0x31) or args.expect_ammo_used < 0):
        parser.error("ammo consumption requires a bow/bomb item and a nonnegative count")
    if not 0 < args.hold <= 2:
        parser.error("hold must be greater than zero and at most 2 seconds")
    if not args.checkpoint.is_file() or not (args.save / "user/cking.sav").is_file():
        parser.error("a local full checkpoint and playable save directory are required")
    binary_sha256 = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    original = hashes(args.save)
    args.work.mkdir()
    shutil.copytree(args.save, args.work / "save")
    states = args.work / "states"
    states.mkdir()
    shutil.copyfile(args.checkpoint, states / "slot2.bin")
    env = {
        "WWHD_RENDERER_RUNTIME": "vulkan", "WWHD_UNCAPPED": "1",
        "WWHD_INTERP": "0", "WWHD_TRUE60": "0", "WWHD_INTERP_FPS": "60",
        "WWHD_INTERP_PACED": "0", "WWHD_DISPLAY_HZ": "0",
        "WWHD_SETTINGS": str(args.work / "settings.ini"),
        "WWHD_DISPLAY_SETTINGS": str(args.work / "display.ini"),
        "WWHD_CONTROLS": str(args.work / "controls.json"),
        "WWHD_STATE_DIR": str(states), "WWHD_MOD_MANAGER_DIR": str(args.work / "ModManager"),
        "WWHD_MOD_QUICK_USE": str(args.enabled), "WWHD_MOD_QUICK_USE_SPEED": str(args.speed),
        "WWHD_MODS_TRACE": str(args.work / "mods.trace"),
        "WWHD_PRESS": ",".join(f"{f}-{f+4}:8000" for f in range(120, 1540, 60)),
        "WWHD_STATE_LOAD_AT": "1600:2", "WWHD_STATE_SAVE_AT": "",
        "WWHD_PORTABLE_SAVE_AT": "", "WWHD_PORTABLE_LOAD_AT": "",
        "WWHD_TEST_ORIGIN_LOAD": "5", "WWHD_TEST_MODE": f"{args.mode}@0",
        "WWHD_TEST_PRESS": args.presses or f"0-4:80,1-{1+args.hold}:{args.button:X}",
        "WWHD_TEST_END": "4", "WWHD_TEST_DEBUG": "1",
        "WWHD_SAVEINFO_DUMP": str(args.work / "saveinfo.bin"), "WWHD_SAVEINFO_AT": "0.5",
    }
    log = scenario.run_game(str(args.binary), str(args.game), str(args.work), env,
                            lambda l: (args.work / "test_done").exists() or "[fatal]" in l or "slot 2: cannot" in l, 150)
    trace_path = args.work / "mods.trace"
    trace = trace_path.read_text() if trace_path.exists() else ""
    releases = len(re.findall(rf"quick-use: release item {args.item:02X} ", trace))
    accepted = bool(re.search(rf"quick-use: (equip|draw) item {args.item:02X} ", trace))
    modes = (0x51, 0x52, 0x53) if args.item == 0x33 else (procedures[args.item],)
    mode_pattern = "|".join(str(mode) for mode in modes)
    reached_mode = bool(re.search(rf"link proc \d+ -> (?:{mode_pattern})\b", trace))
    followups = None if args.expect_proc is None else len(re.findall(rf"link proc \d+ -> {args.expect_proc}\b", trace))
    followup = followups is None or followups == 1
    advances = re.findall(r"quick-use: animation advance frame ([\d.]+) -> ([\d.]+) rate ([\d.]+) speed ([\d.]+)", trace)
    launches = len(re.findall(rf"quick-use: native launch item {args.item:02X}\b", trace))
    accelerated = any(float(a[3]) > 1 for a in advances)
    expected_releases = int(bool(args.enabled) and not args.native_only)
    expected_mode = args.item not in (0x31, 0x33) or bool(expected_releases) or args.native_only
    expected_acceleration = (True if args.item in (0x25, 0x2D, 0x2F) else None) if expected_releases and args.speed > 1 else False
    if args.expect_acceleration is not None:
        expected_acceleration = bool(args.expect_acceleration)
    ammo_before = ammo_after = ammo_used = None
    if args.item in (0x27, 0x35, 0x36, 0x31):
        samples = [args.work / "saveinfo.bin.0.5", args.work / "saveinfo.bin"]
        if all(p.is_file() and p.stat().st_size == 0x12A0 for p in samples):
            ammo_before, ammo_after = (p.read_bytes()[ammo_offset] for p in samples)
            ammo_used = ammo_before - ammo_after
    result = {"binary_sha256": binary_sha256, "native_only": args.native_only, "expected_automatic_releases": expected_releases,
              "expected_native_item_mode": expected_mode, "expected_acceleration": expected_acceleration,
              "ammo_before": ammo_before, "ammo_after": ammo_after, "ammo_used": ammo_used,
              "animation_speed": args.speed, "accelerated_advances": len(advances),
              "acceleration_observed": accelerated, "native_launch_instrumented": instrumented,
              "native_item_launches": launches if instrumented else None,
              "native_boomerang_launches": len(re.findall(r"quick-use: native launch item 2D\b", trace)),
              "state_loaded": "Loaded slot 2" in log, "completed": (args.work / "test_done").exists(),
              "mode_applied": f"60 fps mode {args.mode}" in log,
              "enabled": bool(args.enabled), "hold_seconds": args.hold if not args.presses else None,
              "presses": env["WWHD_TEST_PRESS"], "item": args.item,
              "native_item_mode": reached_mode, "accepted": accepted, "automatic_releases": releases,
              "expected_native_followup": followup, "native_followup_count": followups,
              "original_saves_unchanged": original == hashes(args.save),
              "fatal": any(t in log for t in ("[fatal]", "Segmentation fault", "guest trap"))}
    result["pass"] = (result["state_loaded"] and result["completed"] and result["mode_applied"] and reached_mode == expected_mode
                      and followup and result["original_saves_unchanged"] and not result["fatal"]
                      and releases == expected_releases and accepted == bool(expected_releases)
                      and (expected_acceleration is None or accelerated == expected_acceleration)
                      and (args.expect_native_launches is None or launches == args.expect_native_launches)
                      and (args.expect_ammo_used is None or ammo_used == args.expect_ammo_used))
    (args.work / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result), flush=True)
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
