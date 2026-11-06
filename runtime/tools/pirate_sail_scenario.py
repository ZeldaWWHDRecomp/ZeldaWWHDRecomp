#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Real-game regression for #446: the pirate ship's sail drawn torn at 60/120/240 fps.

Requires Pillow and private game/save files. --state is a portable state (.wwstate) on Outset
Island early in the game, Link on the rock by the beach with the pirate ship anchored offshore
(the issue's report). The camera is turned with the right stick until the ship is in --roi at the
default 1280x720 output. No game data or reference images are distributed with this test.

Run: pirate_sail_scenario.py BINARY GAME SAVE WORK --state LOCAL_PORTABLE_STATE [--fps 60 120 240]
Or check an existing consecutive capture: pirate_sail_scenario.py --check DIRECTORY

Kept actors' memory was put back after every pass without logic, including the word that selects
the sail's GPU buffer set: one pass of every step refilled the buffers the GPU was still reading,
and that frame showed the sail's back faces through its front (dark blotches). The checker measures
each frame's distance from the midpoint of its two neighbours over the ship (mean absolute RGB
difference): the torn frames stand out by 1.7 to 4, the ship's ordinary motion stays below 0.25.
Capture readbacks are unpaced so every intermediate pass is tested.
"""
import argparse
import json
from pathlib import Path
import re

import portable_state_scenario as scenario


def check(directory, roi=(380, 170, 500, 310), limit=0.8):
    from PIL import Image

    frames = sorted((int(m[1]), p) for p in Path(directory).glob("frame_*.png")
                    if (m := re.fullmatch(r"frame_(\d+)\.png", p.name)))
    if len(frames) < 16 or any(b[0] != a[0] + 1 for a, b in zip(frames, frames[1:])):
        raise ValueError("expected at least 16 consecutive TV frames (including intermediate passes)")
    crops = []
    for _, path in frames:
        with Image.open(path) as image:
            if image.size != (1280, 720):
                raise ValueError("this scene's region requires 1280x720 output")
            crops.append(image.convert("RGB").crop(roi).tobytes())
    residuals = [sum(abs(b - (a + c) / 2) for a, b, c in zip(x, y, z)) / len(y)
                 for x, y, z in zip(crops, crops[1:], crops[2:])]
    worst = max(residuals)
    result = {"frames": len(frames), "midpoint_residuals": [round(r, 3) for r in residuals],
              "max_midpoint_residual": round(worst, 3), "limit": limit, "pass": worst <= limit}
    print(json.dumps(result), flush=True)
    return result


def run(args, fps):
    directory = scenario.prepare(args.work, f"pirate-sail-{fps}", args.save)
    first, count = 3800, 24
    env = {
        "WWHD_INTERP": "0", "WWHD_INTERP_FPS": str(fps), "WWHD_INTERP_AT_STEP": "3720",
        "WWHD_DISPLAY_HZ": "0", "WWHD_INTERP_PACED": "0", "WWHD_TEST_FIXED_SEED": "1",
        "WWHD_PRESS": scenario.presses(), "WWHD_PORTABLE_LOAD": str(Path(args.state).resolve()),
        "WWHD_RSTICK": "3600-3680:1:0",
        "WWHD_DUMP_FRAMES": ",".join(str(f) for f in range(first, first + count)),
    }
    scenario.run_game(args.binary, args.game, directory, env,
                      lambda _: Path(directory, f"frame_{first + count - 1}.png").exists(), 300)
    result = check(directory, args.roi, args.limit)
    Path(directory, "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result["pass"]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("binary", "game", "save", "work"):
        parser.add_argument(name, nargs="?", type=lambda p: str(Path(p).resolve()))
    parser.add_argument("--state", help="local portable state, never uploaded or committed")
    parser.add_argument("--fps", nargs="+", type=int, default=(60, 120, 240), choices=(60, 120, 240))
    parser.add_argument("--check", help="check captures without running the game")
    parser.add_argument("--roi", nargs=4, type=int, default=(380, 170, 500, 310),
                        metavar=("LEFT", "TOP", "RIGHT", "BOTTOM"))
    parser.add_argument("--limit", type=float, default=0.8)
    args = parser.parse_args()
    if args.check:
        return 0 if check(args.check, args.roi, args.limit)["pass"] else 1
    if not all((args.binary, args.game, args.save, args.work, args.state)):
        parser.error("supply BINARY GAME SAVE WORK and --state, or --check DIRECTORY")
    if not Path(args.state).is_file():
        parser.error("portable state does not exist")
    results = [run(args, fps) for fps in args.fps]
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
