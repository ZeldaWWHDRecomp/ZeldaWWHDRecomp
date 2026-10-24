#!/usr/bin/env python3
"""Hidden file-select Quit proof. All mutable data and captures stay in WORK.

BINARY GAME SAVE WORK --case on|off|confirm|quit|mouse|cancel|copy|erase
SAVE is an existing save directory; only cking.sav is copied. Captures are
composed presents (including the host UI), rather than player screenshots.
Uses the macOS hidden presentation path (Metal or Vulkan). Other aspect ratios exercise
letterboxing; mouse input clicks near the visible button edges.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "game", "save", "work"):
        parser.add_argument(name, type=lambda p: Path(p).resolve())
    parser.add_argument("--case", choices=("on", "off", "confirm", "quit", "mouse", "cancel", "copy", "erase"), default="on")
    parser.add_argument("--renderer", choices=("metal", "vulkan"), default="metal")
    parser.add_argument("--size", choices=("1920x1080", "1280x720", "1280x960", "2560x1080", "1080x1920"), default="1920x1080")
    parser.add_argument("--prompts", choices=("original", "xbox", "playstation"), default="original")
    parser.add_argument("--layout", choices=("position", "labels"), default="position")
    parser.add_argument("--slot-timeout", type=int, default=180, help="seconds to wait for a game-session slot")
    args = parser.parse_args()
    deadline = time.monotonic() + args.slot_timeout
    announced = False
    while True:
        commands = subprocess.check_output(["ps", "-axo", "command"], text=True).splitlines()
        games = [line for line in commands
                 if line.split() and line.split()[0].endswith("/wwhd") and " --game " in line]
        if len(games) < 2:
            break
        if not announced:
            print("Waiting for a game-session slot (limit: two)", flush=True)
            announced = True
        if time.monotonic() >= deadline:
            raise RuntimeError("Two other game sessions are still running")
        time.sleep(.1)
    tag = args.case + "-" + args.size
    if args.renderer != "metal":
        tag += "-" + args.renderer
    if args.prompts != "original":
        tag += "-" + args.prompts
    if args.layout != "position":
        tag += "-" + args.layout
    run = args.work / tag
    run.mkdir(parents=True, exist_ok=False)
    user = run / "user"
    (run / "save" / "user").mkdir(parents=True)
    user.mkdir()
    source = args.save / "user" / "cking.sav"
    if source.exists():
        shutil.copy2(source, run / "save" / "user" / "cking.sav")
    controls = user / "controls.json"
    mapping = {"options": {"file_select_quit": args.case != "off", "button_prompts": args.prompts}}
    if args.layout == "labels":
        mapping["controller"] = {name: name for name in ("A", "B", "X", "Y")}
    controls.write_text(json.dumps(mapping))
    presses = "180-185:8000"
    captures = "150,230,400,500" if args.case == "on" else "400,500"
    if args.case == "off":
        # With the host button disabled, X reaches the native file selector.
        # The extra capture verifies that this face input has no native action.
        presses += ",550-555:2000"
        captures += ",610"
    if args.case in ("confirm", "quit", "cancel"):
        presses += ",550-555:2000"
        captures += ",610"
    if args.case == "cancel":
        presses += ",590-595:8000,650-655:2000,690-695:4000"
        captures += ",710"
    if args.case == "mouse":
        captures += ",610"
    if args.case == "quit":
        presses += ",640-645:0400,680-685:8000"
    sticks = ""
    if args.case in ("copy", "erase"):
        presses += ",550-555:8000"
        sticks = "600-605:-1:0"
        if args.case == "copy":
            presses += ",650-655:8000,750-755:4000,850-855:4000"
            captures += ",610,710,910"
        else:
            sticks += ",650-655:-1:0"
            presses += ",700-705:8000,800-805:4000,900-905:4000"
            captures += ",660,760,960"
    env = dict(os.environ, WWHD_USER_DIR=str(user), WWHD_CONTROLS=str(controls),
               WWHD_STATE_DIR=str(user / "states"), WWHD_DISPLAY_SETTINGS=str(user / "display.json"),
               WWHD_HIDDEN_WINDOWS="1", WWHD_NO_AUDIO="1", WWHD_NO_HOST_INPUT="1", WWHD_NO_GAMEPAD="1",
               WWHD_RENDERER_RUNTIME=args.renderer, WWHD_INTERP="0", WWHD_LANGUAGE="1",
               WWHD_ASPECT="16:9",
               WWHD_INTERP_PACED="0", WWHD_DISPLAY_HZ="0", WWHD_DRC_MODE="off",
               WWHD_SHADER_CACHE=str(args.work / "shader-cache.bin"),
               WWHD_VK_SHADER_CACHE=str(args.work / "vk-cache"),
               WWHD_SIM_SCREEN=args.size, WWHD_DUMP_FRAMES=captures, WWHD_DUMP_PRESENT="1",
               WWHD_PRESS=presses, WWHD_STICK=sticks, WWHD_LOG_BUTTONS="1", WWHD_TEST_FILE_QUIT_TRACE="1")
    if args.case == "mouse":
        width, height = map(int, args.size.split("x"))
        scale = min(width / 1280, height / 720)
        origin_x, origin_y = (width - 1280 * scale) / 2, (height - 720 * scale) / 2
        def point(x, y):
            return f"{(origin_x + x * scale) / width:.6f}:{(origin_y + y * scale) / height:.6f}"
        quit_point = point(525, 610)  # just inside the native plate's top-left edge
        yes_point = point(890, 450)   # just inside the confirmation's bottom-right edge
        env["WWHD_TEST_POST_MOUSE"] = f"540:move:{quit_point},550:down:{quit_point},555:up:{quit_point},640:move:{yes_point},650:down:{yes_point},655:up:{yes_point}"
    log_path = run / "log.txt"
    clean = False
    stop_frame = {"on": 500, "off": 610, "confirm": 610, "cancel": 710, "copy": 910, "erase": 960}.get(args.case)
    with log_path.open("w") as log:
        process = subprocess.Popen([str(args.binary), "--game", str(args.game), "--save", str(run / "save")],
                                   cwd=run, env=env, stdout=log, stderr=subprocess.STDOUT)
        started = time.monotonic()
        try:
            while process.poll() is None and time.monotonic() - started < 240:
                time.sleep(1)
                if stop_frame and (run / f"frame_{stop_frame}_present.png").exists():
                    break
            clean = process.poll() == 0
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    text = log_path.read_text(errors="replace")
    expected = {
        "quit": "[file-select-quit] answer: Yes",
        "mouse": "[file-select-quit] mouse: asking",
        "cancel": "[file-select-quit] answer: No",
        "confirm": "[file-select-quit] asking",
        "on": "[file-select-quit] loaded runtime menu font",
        "copy": "caption=5 wait=0 step=1 state=1049a79c",
        "erase": "caption=2 wait=0 step=1 state=1049a718",
    }.get(args.case)
    shutdown = clean and "[quit] Settings > Quit Game: quitting" in text
    default_no_and_back = text.count("[file-select-quit] answer: No") == 2 and "answer: Yes" not in text
    hidden = "[file-select-quit] loaded runtime menu font" not in text
    result = dict(case=args.case, size=args.size, exit_code=process.returncode,
                  clean_quit=shutdown, expected_log=expected,
                  expected_log_found=expected is None or expected in text,
                  default_no_and_back=default_no_and_back if args.case == "cancel" else None,
                  setting_hidden=hidden if args.case == "off" else None,
                  captures=[p.name for p in sorted(run.glob("*_present.png"))])
    (run / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2), flush=True)
    required = {f"frame_{frame}_present.png" for frame in captures.split(",")}
    passed = result["expected_log_found"] and required.issubset(result["captures"])
    if args.case in ("quit", "mouse"):
        passed &= shutdown
    if args.case == "cancel":
        passed &= default_no_and_back
    if args.case == "off":
        passed &= hidden
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
