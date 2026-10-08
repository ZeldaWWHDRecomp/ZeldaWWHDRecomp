#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Real-game pause-menu / screenshot regression at 120 and 240 interpolation passes.

usage: interp_menu_scenario.py BINARY GAME SAVE WORK [--renderer metal|vulkan|both]

Uses copies of SAVE, private state/cache/screenshot paths and posted F10 events
through the normal input handler. Portable save refusal while paused, followed by
successful saves after each close, checks that #64 does not reopen the menu.
WWHD_DISPLAY_HZ=0 and WWHD_INTERP_PACED=0 force every requested pass; this
checks behavior, not physical 240 Hz display delivery or performance.
"""
import argparse
import os
from pathlib import Path
import sys

import portable_state_scenario as scenario


def run_case(args, renderer, fps):
    tag = f"{renderer}-{fps}"
    directory = scenario.prepare(args.work, tag, args.save)

    def frame(t):
        # Origin at frame 600; boot at 30 Hz, then switch at scenario time 28 s.
        return 1440 + (t - 28) * fps

    presses = [f"{t}-{t + .15}:8000" for t in (5, 8, 11, 14, 17, 20)]
    presses += [f"{t}-{t + .15}:0008" for t in (30, 38, 42, 46)]
    env = {
        "WWHD_LANGUAGE": "3", "WWHD_RENDERER_RUNTIME": renderer,
        "WWHD_INTERP": "0", "WWHD_INTERP_FPS": str(fps), "WWHD_DISPLAY_HZ": "0",
        "WWHD_INTERP_PACED": "0", "WWHD_DRC_MODE": "pip",
        "WWHD_CONTROLS": directory + "/controls.json",
        "WWHD_TEST_ORIGIN": "600", "WWHD_TEST_MODE": "1@28", "WWHD_TEST_END": "50",
        "WWHD_TEST_PRESS": ",".join(presses),
        "WWHD_PORTABLE_SAVE_AT": ",".join(f"{frame(t)}:{s}" for t, s in ((34, 1), (40, 2), (44, 3), (48, 4))),
        "WWHD_TEST_POST_KEYS": f"{frame(34)}:F10,{frame(48)}:F10",
        "WWHD_SCREENSHOT_DIR": directory + "/shots", "WWHD_SCREENSHOT_GAMEPAD": "1",
        "WWHD_DUMP_FRAMES": ",".join(str(frame(t)) for t in (34, 40, 44, 48)),
        "WWHD_TEST_DEBUG": "1",
    }
    print("START", tag, flush=True)
    done = lambda log: os.path.exists(directory + "/test_done")
    log = scenario.run_game(args.binary, args.game, directory, env, done, 300)
    shots = list(Path(directory, "shots").glob("*.png"))
    checks = {
        "first_pause_refused": "slot 1: portable state refused: a game menu is open" in log,
        "first_close_saved": "slot 2: portable state written" in log,
        "second_pause_refused": "slot 3: portable state refused: a game menu is open" in log,
        "second_close_saved": "slot 4: portable state written" in log,
        "F10_tv_gamepad": len(shots) == 4,
        "scenario_done": done(log),
    }
    print(tag, checks, flush=True)
    print(scenario.show(log, ("savestate", "screenshot", "test] t=")), flush=True)
    return all(checks.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "game", "save", "work"):
        parser.add_argument(name, type=lambda p: str(Path(p).resolve()))
    parser.add_argument("--renderer", choices=("metal", "vulkan", "both"), default="both")
    args = parser.parse_args()
    Path(args.work).mkdir(parents=True, exist_ok=True)
    renderers = ("metal", "vulkan") if args.renderer == "both" else (args.renderer,)
    results = [run_case(args, renderer, fps) for renderer in renderers for fps in (120, 240)]
    print("RESULT: " + ("PASS" if all(results) else "FAIL"), flush=True)
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
