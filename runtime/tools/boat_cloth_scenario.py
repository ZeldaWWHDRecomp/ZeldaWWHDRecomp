#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Real-game regression for #68's extra sail-edge movement at 60 fps.

Requires Pillow and private game/save files. --state is a full checkpoint aboard
the King of Red Lions with its sail furled and the camera looking forward from
behind Link. The scene must use the default 1280x720 output and show the upper
sail edge in --roi. No game data or reference images are distributed with this test.

Run: boat_cloth_scenario.py BINARY GAME SAVE WORK --state LOCAL_CHECKPOINT
Or check an existing consecutive capture: boat_cloth_scenario.py --check DIRECTORY

The checker tracks the top of the teal sail, requiring eight matching pixels on
a scanline to reject stars. It measures displacement from the two neighboring
frames' midpoint, rather than flagging ordinary 30 Hz cloth flutter. At 60 fps
the old exact cloth against an interpolated camera adds an approximately eight
pixel alternating error in this scene; the corrected draw stays below two.
Capture readbacks are deliberately unpaced so every intermediate pass is tested.
This is a rendering regression, not a benchmark or a physical-display FPS test.
"""
import argparse
import json
from pathlib import Path
import re
import statistics

import portable_state_scenario as scenario


def check(directory, roi=(430, 0, 690, 330), limit=2.0):
    from PIL import Image

    frames = sorted((int(m[1]), p) for p in Path(directory).glob("frame_*.png")
                    if (m := re.fullmatch(r"frame_(\d+)\.png", p.name)))
    if len(frames) != 24 or any(b[0] != a[0] + 1 for a, b in zip(frames, frames[1:])):
        raise ValueError("expected exactly 24 consecutive TV frames (including intermediate passes)")
    edge = []
    for _, path in frames:
        with Image.open(path) as image:
            if image.size != (1280, 720):
                raise ValueError("this scene's edge detector requires 1280x720 output")
            pixels = image.convert("RGB").crop(roi)
            width, height = pixels.size
            rgb = pixels.tobytes()
            data = list(zip(rgb[0::3], rgb[1::3], rgb[2::3]))
        for y in range(height):
            matching = sum(r > 40 and g > 65 and b > 65 and .48*g < r < .9*g and g > .80*b
                           for r, g, b in data[y*width:(y+1)*width])
            if matching >= 8:
                edge.append(y + roi[1])
                break
        else:
            raise ValueError(f"sail edge absent from ROI in {path.name}")
    residuals = [abs(b - (a + c)/2) for a, b, c in zip(edge, edge[1:], edge[2:])]
    error = statistics.median(residuals)
    result = {"frames": len(frames), "edge_y": edge, "median_midpoint_error_px": error,
              "limit_px": limit, "pass": error <= limit}
    print(json.dumps(result), flush=True)
    return result


def run(args, renderer):
    directory = scenario.prepare(args.work, f"cloth-{renderer}-60", args.save)
    Path(directory, "states/slot1.bin").symlink_to(Path(args.state).resolve())
    env = {
        "WWHD_RENDERER_RUNTIME": renderer, "WWHD_INTERP": "0", "WWHD_INTERP_FPS": "60",
        "WWHD_DISPLAY_HZ": "0", "WWHD_INTERP_PACED": "0",
        "WWHD_CONTROLS": directory + "/controls.json", "WWHD_DUMP_PRESENT": "1",
        "WWHD_UNCAPPED": "0", "WWHD_VK_UNCAPPED": "0",
        "WWHD_PRESS": ",".join(f"{f}-{f+8}:8000" for f in range(120, 400, 60)),
        "WWHD_STATE_LOAD_AT": "450:1", "WWHD_TEST_ORIGIN_LOAD": "60",
        "WWHD_TEST_MODE": "1@4", "WWHD_TEST_PRESS": "0.5-0.7:8000",
        "WWHD_TEST_STICK": "1-25:0:1", "WWHD_TEST_DEBUG": "1",
        "WWHD_DUMP_FRAMES": ",".join(str(f) for f in range(900, 924)),
    }
    scenario.run_game(args.binary, args.game, directory, env,
                      lambda _: Path(directory, "frame_923.png").exists(), 180)
    result = check(directory, args.roi, args.limit)
    Path(directory, "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result["pass"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "game", "save", "work"):
        parser.add_argument(name, nargs="?", type=lambda p: str(Path(p).resolve()))
    parser.add_argument("--state", help="local full checkpoint, never uploaded or committed")
    parser.add_argument("--renderer", choices=("metal", "vulkan", "both"), default="both")
    parser.add_argument("--check", help="check captures without running the game")
    parser.add_argument("--roi", nargs=4, type=int, default=(430, 0, 690, 330),
                        metavar=("LEFT", "TOP", "RIGHT", "BOTTOM"))
    parser.add_argument("--limit", type=float, default=2.0)
    args = parser.parse_args()
    if args.check:
        return 0 if check(args.check, args.roi, args.limit)["pass"] else 1
    if not all((args.binary, args.game, args.save, args.work, args.state)):
        parser.error("supply BINARY GAME SAVE WORK and --state, or --check DIRECTORY")
    if not Path(args.state).is_file():
        parser.error("checkpoint does not exist")
    renderers = ("metal", "vulkan") if args.renderer == "both" else (args.renderer,)
    results = [run(args, renderer) for renderer in renderers]
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
