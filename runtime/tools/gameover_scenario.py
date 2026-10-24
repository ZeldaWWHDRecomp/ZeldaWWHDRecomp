#!/usr/bin/env python3
"""Headless Game Over -> Save -> Continue diagnostic (no game data included).

Usage: gameover_scenario.py BINARY GAME SAVE WORK [--renderer metal|vulkan]
       [--fps 30|40|60|120|240] [--paced] [--uncapped]
       [--checkpoint FULL_STATE.bin | --capture-opening] [--activate-after-step 1]
       [--interpolate-before-load]

BINARY must include WWHD_TEST_GAMEOVER_TRACE (this branch).
SAVE must contain user/cking.sav, with Quest Log 1 already in gameplay.
The tested public Forbidden Woods save works. The harness copies it, sets health
to zero using the existing test poke, then presses A through the save prompt
and Continue. Check the captures when using another save: dialogue timing can
differ. WORK must not exist. Settings, saves, caches and traces stay in WORK.
Run at most two game sessions at once. A Windows/Vulkan failure can be collected
with the same script, without changing the game's logic or sharing game data.

For the phase regression, supply a private full checkpoint during the Game Over
letter animation, before the save window opens, and --activate-after-step 1.
At 40 fps the old code can read the unopened window on an in-between draw and
commit the default To Title choice before any scripted button press. Checkpoint
runs use fixed seeds and press Save/Continue at 7/10 seconds after loading.
--capture-opening --fps 30 creates an exact handoff checkpoint in run/states;
replay that with --interpolate-before-load to exercise the pending request.
"""
import argparse
import json
from pathlib import Path

import portable_state_scenario as scenario
import builds


def measure(directory, control_at=28):
    rows = []
    for line in (directory / "gameover.txt").read_text().splitlines():
        fields = line.split()
        values = dict(field.split("=", 1) for field in fields[3:])
        rows.append((float(fields[0]), int(values["status"]), int(values["next"])))
    chosen = next((t for t, status, _ in rows if status == 2), None)
    requested = next((t for t, status, nxt in rows if status == 2 and nxt), None)
    cleared = next((t for t, _, nxt in rows if requested is not None and t > requested and not nxt), None)
    link = [row for line in (directory / "link.txt").read_text().splitlines()
            if len(row := line.split()) == 18]
    origin = int((directory / "gameover.txt").read_text().splitlines()[0].split()[1])
    late = [(float(row[6]), float(row[8])) for row in link if int(row[1]) >= origin + control_at * 30]
    moved = bool(late) and (max(x for x, _ in late) - min(x for x, _ in late) > 10 or
                            max(z for _, z in late) - min(z for _, z in late) > 10)
    return {"continue_seconds": chosen, "request_seconds": requested,
            "loading_seconds": cleared, "controls_after_continue": moved,
            "pass": chosen is not None and requested is not None and cleared is not None and moved}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("binary", "game", "save", "work"):
        parser.add_argument(name, type=lambda value: Path(value).resolve())
    parser.add_argument("--renderer", choices=("metal", "vulkan"), default="vulkan")
    parser.add_argument("--fps", type=int, choices=(30, 40, 60, 120, 240), default=60)
    parser.add_argument("--paced", action="store_true")
    parser.add_argument("--uncapped", action="store_true")
    state = parser.add_mutually_exclusive_group()
    state.add_argument("--checkpoint", type=lambda value: Path(value).resolve())
    state.add_argument("--capture-opening", action="store_true")
    parser.add_argument("--activate-after-step", type=int, choices=(0, 1, 2), default=0)
    parser.add_argument("--interpolate-before-load", action="store_true")
    args = parser.parse_args()
    if args.work.exists():
        parser.error("WORK must be a new directory")
    if args.checkpoint and (not args.checkpoint.is_file() or args.checkpoint.suffix != ".bin"):
        parser.error("--checkpoint must be a private full .bin checkpoint")
    if args.capture_opening and args.fps != 30:
        parser.error("--capture-opening requires --fps 30")
    if args.interpolate_before_load and (not args.checkpoint or args.activate_after_step):
        parser.error("--interpolate-before-load requires --checkpoint and no delayed activation")
    release = builds.identify(str(args.game / "code/cking.rpx"))
    if release is None:
        parser.error("unsupported game executable")
    health = release.data(0x101F84DC)
    if health is None:
        parser.error("save-info pointer has no release mapping")
    states = [str(args.checkpoint)] if args.checkpoint else None
    directory = Path(scenario.prepare(str(args.work), "run", str(args.save), states))
    if args.checkpoint and args.checkpoint.name != "slot1.bin":
        (directory / "states" / args.checkpoint.name).replace(directory / "states/slot1.bin")
    control_at, end = (16, 20) if args.checkpoint else (28, 35)
    presses = (7, 10, 13) if args.checkpoint else (10, 13, 16, 19)
    env = {
        "WWHD_USER_DIR": str(directory / "user"),
        "WWHD_SETTINGS": str(directory / "settings.ini"),
        "WWHD_DISPLAY_SETTINGS": str(directory / "display.plist"),
        "WWHD_SHADER_CACHE": str(directory / "shaders.bin"),
        "WWHD_VK_SHADER_CACHE": str(directory / "vk-shaders"),
        "WWHD_RENDERER_RUNTIME": args.renderer, "WWHD_NO_CONTROLLERS": "1",
        "WWHD_INTERP": "0", "WWHD_TRUE60": "0", "WWHD_DISPLAY_HZ": "0",
        "WWHD_INTERP_FPS": str(60 if args.fps == 30 else args.fps),
        "WWHD_INTERP_PACED": str(int(args.paced)), "WWHD_UNCAPPED": str(int(args.uncapped)),
        "WWHD_TEST_MODE": f"{int(args.fps != 30)}@{args.activate_after_step / 30:.9f}",
        "WWHD_PRESS": scenario.presses(), "WWHD_TEST_ORIGIN": "3300",
        "WWHD_TEST_POKE": f"2:*{health:08X}+22:0000",
        "WWHD_TEST_PRESS": ",".join(f"{t}-{t + .15}:8000" for t in presses),
        "WWHD_TEST_STICK": f"{control_at}-{control_at + 1}:0:1",
        "WWHD_TEST_END": str(end), "WWHD_TEST_DEBUG": "1",
        "WWHD_TEST_GAMEOVER_TRACE": str(directory / "gameover.txt"),
        "WWHD_LINK_TRACE": str(directory / "link.txt"),
        "WWHD_CAM_TRACE": str(directory / "camera.txt"),
        "WWHD_RNG_TRACE": str(directory / "rng.txt"),
        "WWHD_PROC_DIGEST": str(directory / "processes.txt"),
        "WWHD_SAVEINFO_DUMP": str(directory / "save-info.bin"),
        "WWHD_INTERP_FADER_TRACE": "3000", "WWHD_INTERP_FADE_PASSES": "5000",
        "WWHD_DUMP_FRAMES": ",".join(str(3300 + args.fps * t) for t in (15, 18, 22, 30)),
    }
    if args.checkpoint:
        env.update({"WWHD_STATE_LOAD_AT": "1800:1", "WWHD_TEST_ORIGIN_LOAD": "1",
                    "WWHD_TEST_FIXED_SEED": "1", "WWHD_TEST_POKE": "",
                    "WWHD_PRESS": ",".join(part for part in scenario.presses().split(",")
                                           if int(part.split("-", 1)[0]) < 1800),
                    "WWHD_DUMP_LOAD_FRAMES": ",".join(str(int(args.fps * t))
                                                     for t in (10.4, 10.6, 10.8, 11, 11.2, 11.4, 12, 18))})
    if args.capture_opening:
        env.update({"WWHD_TEST_GAMEOVER_SAVE": "1", "WWHD_FULL_SAVE_STATES": "1",
                    "WWHD_TEST_FIXED_SEED": "1"})
    if args.interpolate_before_load and args.fps != 30:
        # Boot/file select still run at 30 fps; enable before the checkpoint is
        # restored, so its first pending Game Over request gets hold draws too.
        env.pop("WWHD_TEST_MODE")
        env["WWHD_INTERP_AT_STEP"] = "1770"
    log = scenario.run_game(str(args.binary), str(args.game), str(directory), env,
                            lambda _: (directory / "test_done").exists(), 300)
    result = {"renderer": args.renderer, "fps": args.fps, "paced": args.paced,
              "finished": (directory / "test_done").exists()}
    try:
        result.update(measure(directory, control_at))
    except (OSError, ValueError, KeyError, IndexError) as error:
        result.update({"pass": False, "error": str(error)})
    result["pass"] = result["pass"] and result["finished"]
    (args.work / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    if not result["pass"]:
        print(scenario.show(log, ("test]", "savestate", "fatal", "crash")))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
