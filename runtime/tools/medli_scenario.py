#!/usr/bin/env python3
"""Headless regression for #85, on copies of an early Dragon Roost save only.

Travel uses the existing stage/restart warp test aids. Quill's conversation and the
Chieftain's introduction/Delivery Bag run through native scripted A presses; no
inventory or event flags are poked. Checkpoints must be outside cutscenes before
travelling on. Finally enter the upper room, walk to Medli and check her actor.
The optional sword cheat exercises the production cheat service before this route.
Player saves, states, actor dumps and screenshots stay in WORK, never in Git.

usage: medli_scenario.py BINARY GAME SAVE WORK --cache-dir DIR
       [--renderer metal|vulkan] [--mode 30|60|120|true60] [--cheat]
       [--expect absent|present] [--portable STATE] [--turbo]
SAVE is cking.sav or a folder with user/cking.sav. Its Quest Log 1 must be early,
before Quill's Dragon Roost conversation. WORK must not already exist.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time

import portable_state_scenario as scenario

ORIGIN = 1800
# Leave generous native dialogue time, and a quiet checkpoint before each warp.
QUILL_CHECK, CHIEF_ENTER, CHIEF_CHECK, UPPER_ENTER, UPPER_CHECK, END = 80, 85, 230, 235, 249, 260


def resource_gate(work):
    while True:
        commands = subprocess.run(["ps", "-axo", "comm"], capture_output=True, text=True).stdout
        benchmark = subprocess.run(["ps", "-axo", "command"], capture_output=True, text=True).stdout
        busy = any(Path(line.strip()).name == "wwhd" for line in commands.splitlines())
        bench = re.search(r"^\S*python[\d.]*\s+(?:-\S+\s+)*(\S*/)?run_bench\.py(?:\s|$)", benchmark, re.M | re.I)
        if os.getloadavg()[0] <= 30 and shutil.disk_usage(work).free > 15 * 1024**3 and not busy and not bench:
            return
        print("Waiting for load <=30, >15 GiB disk and no game/benchmark", flush=True)
        time.sleep(15)


def actor_records(path, after_step):
    count = 0
    with path.open("rb") as f:
        while header := f.read(28):
            if len(header) != 28:
                break  # the game is stopped with TERM; its last buffered record may be partial
            if header[:4] != b"ADMP":
                raise ValueError("invalid actor dump header")
            step, full, dt, proc, size = struct.unpack("<QIfII", header[4:])
            if size > 0x10000:
                raise ValueError("invalid actor dump size")
            if len(f.read(size)) != size:
                break  # count only complete records
            if step >= after_step and full:
                count += 1
    return count


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("binary", "game", "save", "work"):
        ap.add_argument(name, type=Path)
    ap.add_argument("--cache-dir", required=True, type=Path)
    ap.add_argument("--renderer", choices=("metal", "vulkan"), default="metal")
    ap.add_argument("--mode", choices=("30", "60", "120", "true60"), default="30")
    ap.add_argument("--expect", choices=("absent", "present"), default="present")
    ap.add_argument("--cheat", action="store_true")
    ap.add_argument("--turbo", action="store_true")
    ap.add_argument("--portable", type=Path)
    a = ap.parse_args()
    a.binary, a.game, a.save, a.work, a.cache_dir = [x.resolve() for x in (a.binary, a.game, a.save, a.work, a.cache_dir)]
    if a.work.exists():
        ap.error("WORK must not already exist (use a new run folder)")
    a.work.parent.mkdir(parents=True, exist_ok=True)
    resource_gate(a.work.parent)
    d = Path(scenario.prepare(str(a.work.parent), a.work.name, str(a.save)))
    a.cache_dir.mkdir(parents=True, exist_ok=True)
    mode = 2 if a.mode == "true60" else 0 if a.mode == "30" else 1
    fps = 60 if a.mode == "true60" else int(a.mode)
    checkpoints = [QUILL_CHECK, CHIEF_CHECK, UPPER_CHECK, END - 2]
    pulse_times = list(range(12, 75)) + list(range(CHIEF_ENTER + 8, CHIEF_CHECK - 4))
    warp = lambda point, room, name: name.encode().hex().ljust(16, "0") + f"{point:04x}{room:02x}ff0100"
    def restart(t, name, room, pos, angle):
        # Same dSv_restart_c fields used by portable-state loading (save base +0x1148).
        return [f"{t}:*101F84DC+1148:{room:02x}",
                f"{t}:*101F84DC+115E:{angle & 0xffff:04x}",
                f"{t}:*101F84DC+1160:" + struct.pack(">fff", *pos).hex(),
                f"{t}:*101F84DC+116C:ff0000{room:02x}0000000000000000",
                f"{t}:104741F0:" + warp(0xffff, room, name)]
    pokes = restart(1, "sea", 13, (199400,1130,-196847), 8192)
    pokes += [f"{CHIEF_ENTER}:104741F0:" + warp(0, 0, "Atorizk")]
    pokes += restart(UPPER_ENTER, "Atorizk", 0, (1350,700,-1146), -16384)
    env = {
        "WWHD_PRESS": scenario.presses(), "WWHD_TEST_ORIGIN": str(ORIGIN),
        "WWHD_INTERP": "0", "WWHD_INTERP_FPS": str(max(60, fps)), "WWHD_DISPLAY_HZ": "0",
        "WWHD_TEST_MODE": f"{mode}@0", "WWHD_TEST_END": str(END),
        "WWHD_TEST_PRESS": ",".join(f"{t}-{t+.15}:8000" for t in pulse_times),
        "WWHD_TEST_POKE": ",".join(pokes),
        "WWHD_TEST_STICK": f"{CHIEF_ENTER+7}-{CHIEF_ENTER+11}:0:1",
        "WWHD_TEST_GOTO": f"{ORIGIN/30+UPPER_ENTER+7}:1290:-1146",
        "WWHD_ACTOR_DUMP": "medli.bin:02289E28:100", "WWHD_LINK_TRACE": "link.txt",
        "WWHD_SAVEINFO_DUMP": "saveinfo.bin", "WWHD_SAVEINFO_AT": ",".join(map(str, checkpoints)),
        "WWHD_PORTABLE_SAVE_AT": ",".join(f"{ORIGIN+fps*t}:{i+1}" for i,t in enumerate(checkpoints)),
        "WWHD_DUMP_FRAMES": ",".join(str(ORIGIN+fps*t) for t in checkpoints),
        "WWHD_RENDERER_RUNTIME": a.renderer,
        "WWHD_SHADER_CACHE": str(a.cache_dir / "medli-metal.bin"),
        "WWHD_VK_SHADER_CACHE": str(a.cache_dir / "medli-vulkan"),
        "WWHD_MOD_QUICK_DOORS": "1" if a.turbo else "0",
        "WWHD_MOD_FAST_SCENES": "1" if a.turbo else "0",
    }
    if a.cheat:
        env["WWHD_CHEAT"] = "sword"
    if a.portable:
        env["WWHD_PORTABLE_LOAD"] = str(a.portable.resolve())
    log = scenario.run_game(str(a.binary), str(a.game), str(d), env,
                            lambda text: (d / "test_done").exists(), 600)
    sys_path = Path(__file__).resolve().parents[2] / "tools/savegame"
    import sys
    sys.path.insert(0, str(sys_path))
    import wwsave as w
    import wwstate
    states = {}
    checks = {"finished": (d / "test_done").exists()}
    for i,t in enumerate(checkpoints, 1):
        p = d / f"states/slot{i}.wwstate"
        checks[f"checkpoint_{t}"] = p.exists()
        if p.exists():
            states[t] = wwstate.read_state(p)
    before = states.get(QUILL_CHECK)
    chief = states.get(CHIEF_CHECK)
    upper = states.get(UPPER_CHECK)
    if before and chief:
        bg = before["savedata"][:w.GAMEDATA_SIZE]; cg = chief["savedata"][:w.GAMEDATA_SIZE]
        checks["quill_finished_before_chief"] = before["stage"] == "sea" and bool(w.get(bg,w.HD_FIELDS,"event.flags")[0x1F] & 0x40)
        checks["delivery_bag_received"] = w.get(bg,w.HD_FIELDS,"item.slot18")[0] == 0xFF and w.get(cg,w.HD_FIELDS,"item.slot18")[0] == 0x30
        checks["chief_stage"] = chief["stage"] == "Atorizk"
    else:
        checks["delivery_bag_received"] = False
    checks["upper_room"] = bool(upper and upper["stage"] == "Atorizk" and upper["room"] == "0" and upper["start_point"] == "-1")
    if upper:
        x,y,z = map(float,upper["link_pos"].split())
        checks["walked_to_medli"] = x < 1330 and abs(y-700) < 30 and ((x-1211)**2+(z+1146)**2)**.5 < 180
    else:
        checks["walked_to_medli"] = False
    count = actor_records(d / "medli.bin", ORIGIN + 30 * UPPER_ENTER) if (d / "medli.bin").exists() else 0
    checks["medli_expected"] = count >= 30 if a.expect == "present" else count == 0
    result = {"checks": checks, "medli_records_after_upper_entry": count, "mode": a.mode,
              "renderer": a.renderer, "cheat": a.cheat, "turbo": a.turbo, "expected": a.expect,
              "pass": all(checks.values())}
    (d / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2), flush=True)
    print(scenario.show(log), flush=True)
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
