#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Native language, Outset gameplay, interpolation and true60 regression.

Uses copied saves and private output paths. Run once per renderer/language with
BINARY GAME SAVE WORK --language 3 (German), 4 (Italian), or 1 (English).
"""
import argparse
from pathlib import Path

import portable_state_scenario as scenario
import builds


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "game", "save", "work"):
        parser.add_argument(name, type=lambda p: str(Path(p).resolve()))
    parser.add_argument("--language", type=int, choices=(1, 3, 4), required=True)
    parser.add_argument("--renderer", choices=("metal", "vulkan"), default="metal")
    args = parser.parse_args()
    build = builds.identify(str(Path(args.game, "code/cking.rpx")))
    if build is None:
        raise ValueError("unsupported executable")
    directory = scenario.prepare(args.work, f"{build.name}-{args.language}-{args.renderer}", args.save)
    env = {
        "WWHD_LANGUAGE": str(args.language), "WWHD_RENDERER_RUNTIME": args.renderer,
        "WWHD_INTERP": "0", "WWHD_INTERP_FPS": "120", "WWHD_DISPLAY_HZ": "0",
        "WWHD_INTERP_PACED": "0", "WWHD_TEST_ORIGIN": "600",
        "WWHD_TEST_MODE": "1@28", "WWHD_TEST_MODES": "2@38,0@48",
        "WWHD_TEST_END": "52", "WWHD_TEST_DEBUG": "1",
        "WWHD_TEST_PRESS": ",".join(f"{t}-{t + .15}:8000" for t in (5, 8, 11, 14, 17, 20)),
        "WWHD_TEST_STICK": "29-31:0:1,39-41:0:1",
        # Boot 600 + 28*30 + 10*120 + 10*60 + 2*30 = 3300.
        "WWHD_PORTABLE_SAVE_AT": "3300:1", "WWHD_DUMP_FRAMES": "1200,2160,2940,3300",
    }
    done = lambda log: Path(directory, "test_done").exists()
    log = scenario.run_game(args.binary, args.game, directory, env, done, 300)
    state = Path(directory, "states/slot1.wwstate")
    checks = {
        "language": f"console language {args.language} (WWHD_LANGUAGE)" in log,
        "interpolation": "60 fps mode 1" in log,
        "true60": "60 fps mode 2" in log,
        "return_to_30": "60 fps mode 0" in log,
        "outset_state": state.exists() and scenario.parse_state(state)["stage"] == "sea",
        "finished": done(log),
    }
    print(build.name, args.language, args.renderer, checks, flush=True)
    print(scenario.show(log, ("savestate", "60 fps mode", "console language")), flush=True)
    print("RESULT: " + ("PASS" if all(checks.values()) else "FAIL"), flush=True)
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
