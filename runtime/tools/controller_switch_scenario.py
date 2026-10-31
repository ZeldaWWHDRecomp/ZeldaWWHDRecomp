#!/usr/bin/env python3
"""Hidden check of a live controller switch (Controls: "Keyboard and controllers act as").

BINARY GAME SAVE WORK --case CASE [--state SLOT.bin]

Issue #330: switched from the GamePad to the Pro Controller in a menu (pause menu, file select), the
game must keep hearing the player until its own controller mode has followed, then play with the Pro
Controller. Each case boots a copy of SAVE (only user/cking.sav is copied) into WORK/<case>, starts
on the GamePad (the title's automatic choice) or the Pro Controller, switches with
WWHD_TEST_CONTROLLER at a scripted moment and presses buttons afterwards:

  pause             pause menu open, switch, B closes it, then Plus reaches the game (Pro Controller)
  pause-back        pause menu open, switch to the Pro Controller and straight back: B closes the menu
                    on the GamePad, the game's mode never changes
  file-select       file select, switch, A opens the save; the mode follows in play
  file-select-late  as file-select, but A only after more than the re-apply's give-up time
  free-play         switch while Link stands on the pier; A and Plus reach the game afterwards
  dialog            switch in a dialog (needs --state: a full save state in a dialog, slot 2 here)
  reverse           starts on the Pro Controller, switches to the GamePad in play (holds_pro_controller)
  reverse-pause     as reverse, switched in the pause menu: B closes it (the held Pro Controller stays
                    live while the game still reads it), then the game plays with the GamePad

Muted, hidden windows, no host input, own user folder; the shader cache lives in WORK. The log's
[controller] / [test] controller trace lines are printed; the exit status is 0 when the case passed.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

BOOT = "1200-1208:8000,1260-1268:8000,1320-1328:8000,1380-1388:8000,1440-1448:8000"  # title -> play (TV frames)
# the direct switch, or (WWHD_TEST_CONTROLLER_MENU=1) the pause menu's controller change
TO_PRO = ("it plays with the Pro Controller now (mode 0)", "the game plays with the Pro Controller (mode 0)")
TO_GAMEPAD = ("it plays with the GamePad now (mode 1)", "the game plays with the GamePad (mode 1)")

# per case: boot presses (TV frames), scenario presses (game seconds), switches (t:1 GamePad, t:2 Pro),
# the line that must appear, the end (game seconds) and whether the run starts on the Pro Controller
CASES = {
    "pause": dict(boot=BOOT, press="70-70.3:0008,76-76.3:4000,78-78.3:4000,84-84.3:0008", switch="73:2",
                  expect=TO_PRO, end=90),
    "pause-back": dict(boot=BOOT, press="70-70.3:0008,78-78.3:4000,80-80.3:4000", switch="73:2,75:1",
                       expect=None, end=86),
    "file-select": dict(boot="1200-1208:8000", press=",".join(f"{t}-{t}.3:8000" for t in range(50, 62, 2)) + ",72-72.3:0008",
                        switch="46:2", expect=TO_PRO, end=78),
    "file-select-late": dict(boot="1200-1208:8000",
                             press=",".join(f"{t}-{t}.3:8000" for t in range(186, 198, 2)) + ",208-208.3:0008",
                             switch="46:2", expect=TO_PRO, end=214),
    "free-play": dict(boot=BOOT, press="68-68.3:8000,72-72.3:0008", switch="65:2", expect=TO_PRO, end=78),
    "dialog": dict(boot=BOOT, press="68-68.3:8000,72-72.3:8000", stick="66-66.3:0:-1", switch="62:2",
                   expect=TO_PRO, end=78, state="1700:2"),
    "reverse": dict(boot=BOOT, press="68-68.3:8000,72-72.3:0008", switch="65:1", expect=TO_GAMEPAD, end=78, pro=True),
    "reverse-pause": dict(boot=BOOT, press="70-70.3:0008,76-76.3:4000,78-78.3:4000,84-84.3:0008", switch="73:1",
                          expect=TO_GAMEPAD, end=90, pro=True),
}


def switch_done(line, expect):
    # a finished switch ("..., as the player": nothing had to change)
    return "[controller]" in line and "as the player" not in line and any(e in line for e in expect)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("binary", "game", "save", "work"):
        parser.add_argument(name, type=lambda p: Path(p).resolve())
    parser.add_argument("--case", choices=sorted(CASES), required=True)
    parser.add_argument("--state", type=lambda p: Path(p).resolve(), help="save state for the dialog case (copied)")
    parser.add_argument("--timeout", type=int, default=420, help="wall-clock seconds before the run is stopped")
    args = parser.parse_args()
    case = CASES[args.case]
    run = args.work / args.case
    if run.exists():
        shutil.rmtree(run)
    user = run / "user"
    (run / "save" / "user").mkdir(parents=True)
    (user / "states").mkdir(parents=True)
    shutil.copy2(args.save / "user" / "cking.sav", run / "save" / "user" / "cking.sav")
    env = dict(os.environ, WWHD_USER_DIR=str(user), WWHD_CONTROLS=str(user / "controls.json"),
               WWHD_STATE_DIR=str(user / "states"), WWHD_DISPLAY_SETTINGS=str(user / "display.json"),
               WWHD_HIDDEN_WINDOWS="1", WWHD_NO_AUDIO="1", WWHD_NO_HOST_INPUT="1", WWHD_NO_GAMEPAD="1",
               WWHD_RENDERER_RUNTIME="metal", WWHD_INTERP="0", WWHD_LANGUAGE="1",
               WWHD_SHADER_CACHE=str(args.work / "shader-cache.bin"),
               WWHD_VK_SHADER_CACHE=str(args.work / "vk-cache.bin"),
               WWHD_TEST_ORIGIN="1", WWHD_TEST_END=str(case["end"]), WWHD_TEST_CONTROLLER_TRACE="1",
               WWHD_PRESS=case["boot"], WWHD_TEST_PRESS=case["press"], WWHD_TEST_CONTROLLER=case["switch"],
               WWHD_TEST_STICK=case.get("stick", ""))
    env.pop("WWHD_PRO_CONTROLLER", None)
    if case.get("pro"):
        env["WWHD_PRO_CONTROLLER"] = "1"
    if "state" in case:
        if not args.state:
            parser.error("the dialog case needs --state (a full save state in a dialog)")
        at, slot = case["state"].split(":")
        shutil.copy2(args.state, user / "states" / f"slot{slot}.bin")
        env["WWHD_STATE_LOAD_AT"] = case["state"]
    log_path = run / "log.txt"
    with log_path.open("w") as log:
        process = subprocess.Popen([str(args.binary), "--game", str(args.game), "--save", str(run / "save")],
                                   cwd=run, env=env, stdout=log, stderr=subprocess.STDOUT)
        started = time.monotonic()
        try:
            while process.poll() is None and time.monotonic() - started < args.timeout:
                if (run / "test_done").exists():
                    break
                time.sleep(0.5)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    text = log_path.read_text(errors="replace")
    lines = text.splitlines()
    for line in lines:
        if "[controller]" in line and "game input" not in line or "controller trace" in line or "controller set" in line:
            print(line)
    # after the last switch: does the game hear a press (its pointer controller's trigger)?
    switched = max((i for i, l in enumerate(lines) if "[test] controller set" in l), default=-1)
    followed = next((i for i, l in enumerate(lines)
                     if case["expect"] and switch_done(l, case["expect"])), None)
    after = followed if followed is not None else switched
    heard = [l for l in lines[after + 1:] if re.search(r"game input hold \w+ trigger (?!00000000)", l)] if after >= 0 else []
    for line in heard[:4]:
        print(line)
    modes = re.findall(r"controller trace host (\d) game mode (-?\d+) at ([\d.]+)", text)
    result = dict(case=args.case, done=(run / "test_done").exists(), switched=switched >= 0,
                  expected_line=case["expect"], expected_found=followed is not None,
                  game_hears_after=len(heard) > 0, final_host=modes[-1][0] if modes else None,
                  final_game_mode=modes[-1][1] if modes else None)
    if args.case == "file-select-late":
        # the trace ends 20 s after the switch; the game reaching play from the file select is the press heard
        result["game_hears_after"] = result["expected_found"]
    if args.case == "pause-back":
        # back on the GamePad before the game moved: no mode change, and B still closes the menu
        result["expected_found"] = not any(switch_done(l, TO_PRO + TO_GAMEPAD) for l in lines)
    passed = result["done"] and result["switched"] and result["expected_found"] and result["game_hears_after"]
    result["passed"] = passed
    (run / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2), flush=True)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
