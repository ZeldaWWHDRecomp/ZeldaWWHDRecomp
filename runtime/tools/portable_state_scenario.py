#!/usr/bin/env python3
"""Game test of portable save states (runtime/src/portable_state.h), headless.

usage: portable_state_scenario.py WWHD_BINARY GAME_DIR SAVE_DIR WORK_DIR [--boat-save DIR] [--event-save DIR] [--skip-full]

SAVE_DIR is a save folder (with user/cking.sav whose Quest Log 1 is in play, e.g. on Outset); only a
copy of cking.sav and cking_playlog.sav is used. Three runs, one game at a time:

  make:  boot, file select (A presses), gameplay; for the "house" case a stage change to Link's
         house (LinkRM, the stage-change request at 104741F0 poked), for the "outset" case none
         (same stage as the cold boot, another place); walk a little, save a portable state to slot 1
  load:  cold boot of the same save, gameplay on Outset, change the rupees (so the load has to
         restore them) and walk elsewhere, load slot 1; when Link has arrived save slot 2.
         Checks: stage, room and Link's position (within 30 units) and angle match, the save data
         (inventory, flags, progress: the cking.sav block) and the HD sections are equal apart from
         the time of day (it runs on)
  questlog: the house state loaded while Quest Log 2 is played (a cking.sav whose files 1 and 2
         are SAVE_DIR's file 1): the notice is logged and the data goes into Quest Log 2
  boat:  (--boat-save, e.g. gametest/saves/ghost) swim to the boat, climb aboard, set sail, sail;
         save; cold boot (Link on land), load: Link is on the boat, the boat at its place/heading
  event: (--event-save, e.g. gametest/saves/helm) a portable save while the King of Red Lions
         talks is refused and writes nothing, a full save at the same moment works, and a portable
         save once Link is under control again works
  full:  full save states (WWHD_STATE_SAVE_AT / WWHD_STATE_LOAD_AT) still save and load

A game is only started when no performance benchmark (run_bench.py) is running; the script starts
at most one game and stops it itself (TERM, then KILL).
"""
import os
import re
import shutil
import signal
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "savegame"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "recomp"))
import builds

release_map = None


def data_address(usa):
    native = release_map.data(usa) if release_map else usa
    assert native is not None, "scenario address has no release mapping: %08X" % usa
    return "%08X" % native


def wait_for_quiet_machine():
    while True:
        out = subprocess.run(["ps", "-axo", "command"], capture_output=True, text=True).stdout
        # a Python process running the benchmark (not shells that merely mention its name)
        if not re.search(r"^\S*python[\d.]*\s+(\S*/)?run_bench\.py", out, re.M):
            return
        print("  a benchmark is running; waiting", flush=True)
        time.sleep(30)


def run_game(binary, game, run_dir, env_extra, until, timeout):
    """starts the game in run_dir, stops it when `until(log)` is true or after `timeout` s"""
    wait_for_quiet_machine()
    env = dict(os.environ)
    env.update({"WWHD_HIDDEN_WINDOWS": "1", "WWHD_NO_AUDIO": "1", "WWHD_NO_HOST_INPUT": "1", "WWHD_NO_GAMEPAD": "1",
                "WWHD_RENDERER_RUNTIME": "metal", "WWHD_STATE_DIR": "states", "WWHD_SHADER_CACHE": "../shader_cache.bin",
                "WWHD_VK_SHADER_CACHE": "../vk_shader_cache"})  # private caches, never the player's
    env.update(env_extra)
    log_path = os.path.join(run_dir, "log")
    with open(log_path, "w") as log:
        p = subprocess.Popen([binary, "--game", game, "--save", "save"], cwd=run_dir, env=env, stdout=log,
                             stderr=subprocess.STDOUT)
    t0 = time.time()
    try:
        while p.poll() is None and time.time() - t0 < timeout:
            time.sleep(1)
            if until(open(log_path, errors="replace").read()):
                time.sleep(1)
                break
    finally:
        if p.poll() is None:
            p.send_signal(signal.SIGTERM)
            try:
                p.wait(5)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait(5)
    assert p.poll() is not None, "game still running"
    return open(log_path, errors="replace").read()


def prepare(work, tag, save_dir, states=None):
    """a run folder with a COPY of the save (save_dir: a save folder, or a cking.sav file)"""
    d = os.path.join(work, tag)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(d, "save", "user"))
    if save_dir.endswith(".sav"):
        shutil.copy(save_dir, os.path.join(d, "save", "user", "cking.sav"))
    for f in ("cking.sav", "cking_playlog.sav") if not save_dir.endswith(".sav") else ():
        src = os.path.join(save_dir, "user", f)
        if os.path.exists(src):
            shutil.copy(src, os.path.join(d, "save", "user", f))
    os.makedirs(os.path.join(d, "states"))
    for f in states or []:
        shutil.copy(f, os.path.join(d, "states"))
    return d


def presses(extra=(), quest_log=1):
    """A every 60 frames through the title and file select into gameplay (Down to pick Quest Log 2/3), plus extra A presses"""
    a = list(range(1200, 3200, 60))
    out = []
    if quest_log > 1:
        a = [f for f in a if f < 1300 or f >= 1390]
        out += ["%d-%d:0100" % (1310 + 30 * i, 1314 + 30 * i) for i in range(quest_log - 1)]
    return ",".join(["%d-%d:8000" % (f, f + 8) for f in a + list(extra)] + out)


def parse_state(path):
    kv = {}
    for line in open(path):
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            kv[k.strip()] = v.strip()
    return kv


def show(log, words=("savestate", "test]")):
    return "\n".join(l for l in log.splitlines() if any(w in l for w in words))[-3000:]


def make_state(binary, game, save, work, name, env_extra, slot=1, origin=3300):
    """boots a copy of `save`, runs env_extra (presses, stick, pokes), saves a portable state (WWHD_PORTABLE_SAVE_AT in env_extra)"""
    d = prepare(work, name + "-make", save)
    env = {"WWHD_PRESS": presses(), "WWHD_TEST_ORIGIN": str(origin)}
    env.update(env_extra)
    log = run_game(binary, game, d, env, lambda l: "slot %d: portable state written" % slot in l or "slot %d: not saved" % slot in l, 300)
    path = os.path.join(d, "states", "slot%d.wwstate" % slot)
    m = re.search(r"slot %d: portable state written \((\d+) bytes; (\S+) room (-?\d+) at" % slot, log)
    if not m or not os.path.exists(path):
        print(name + ": make FAILED (no portable state)\n" + show(log))
        return None, log
    print(name + ": slot %d written, %s bytes, stage %s room %s" % ((slot,) + m.groups()))
    if os.path.getsize(path) > 64 * 1024:
        print(name + ": FAILED: file is %d bytes" % os.path.getsize(path))
        return None, log
    return path, log


def load_state(binary, game, save, work, name, state, quest_log=1, origin=3300):
    """cold boot of `save`, rupees changed and Link moved, load `state`, save slot 2 after the arrival"""
    d2 = prepare(work, name + "-load", save, [state])
    shutil.move(os.path.join(d2, "states", os.path.basename(state)), os.path.join(d2, "states", "slot1.wwstate"))
    env = {"WWHD_PRESS": presses(quest_log=quest_log), "WWHD_TEST_ORIGIN": str(origin),
           "WWHD_TEST_POKE": "1:*%s+24:0063" % data_address(0x101F84DC),  # 99 rupees before the load
           "WWHD_STICK": "%d-%d:1:0" % (origin + 30, origin + 80),
           "WWHD_PORTABLE_LOAD_AT": "%d:1" % (origin + 90), "WWHD_PORTABLE_SAVE_AT": "%d:2" % (origin + 600)}
    log = run_game(binary, game, d2, env, lambda l: "slot 2: portable state written" in l or "slot 2: not saved" in l, 300)
    arrived = re.search(r"portable load: arrived in (\S+) room (-?\d+) at (\S+) (\S+) (\S+) \(distance (\S+)", log)
    print(name + ": " + (arrived.group(0) if arrived else "Link did not arrive"))
    state2 = os.path.join(d2, "states", "slot2.wwstate")
    if not arrived or not os.path.exists(state2):
        print(show(log))
        return None, log
    return state2, log


def compare(name, state1, state2, same_slot=True, pos_tolerance=30, xz_only=False):
    ok = True
    s1, s2 = parse_state(state1), parse_state(state2)
    for k in ("stage", "room", "player_name") + (("file_slot",) if same_slot else ()):
        if s1[k] != s2[k]:
            print(name + ": %s differs: %s vs %s" % (k, s1[k], s2[k]))
            ok = False
    p1 = [float(x) for x in s1["link_pos"].split()]
    p2 = [float(x) for x in s2["link_pos"].split()]
    dist = sum((a - b) ** 2 for i, (a, b) in enumerate(zip(p1, p2)) if not (xz_only and i == 1)) ** 0.5
    da = (int(s1["link_angle_y"]) - int(s2["link_angle_y"])) & 0xFFFF
    da = min(da, 0x10000 - da)
    print(name + ": Link %s -> %s (%sdistance %.1f), angle difference %d" % (p1, p2, "horizontal " if xz_only else "", dist, da))
    if dist > pos_tolerance or da > 0x400:
        ok = False
    # save data: equal apart from the time of day (it runs on)
    a, b = bytes.fromhex(s1["savedata"]), bytes.fromhex(s2["savedata"])
    import wwsave as W
    diff = [n for n, (off, size) in W.HD_FIELDS.items() if a[off:off + size] != b[off:off + size]]
    print(name + ": save data fields that differ: %s (time of day %s -> %s)" % (diff or "none", s1["time_of_day"], s2["time_of_day"]))
    if set(diff) - {"status_b.time"}:
        ok = False
    rupee_off = W.HD_FIELDS["status_a.rupee"][0]
    print(name + ": rupees %d (state) / %d (after the load; 99 were poked before it)" %
          (int.from_bytes(a[rupee_off:rupee_off + 2], "big"), int.from_bytes(b[rupee_off:rupee_off + 2], "big")))
    for k in ("hd_player", "hd_status", "hd_event") + (("hd_map",) if s1["stage"] != "sea" or s1["on_ship"] == "0" else ()):
        if s1[k] != s2[k]:
            print(name + ": %s differs" % k)
            ok = False
    return ok, s1, s2


def portable_case(binary, game, save_dir, work, name, warp, expect_stage, origin=3300):
    """make a portable state (after an optional stage-change poke and a walk), load it in a cold boot"""
    env = {"WWHD_STICK": "%d-%d:0:1" % (origin + 420, origin + 450), "WWHD_PORTABLE_SAVE_AT": "%d:1" % (origin + 540)}
    if warp:
        env["WWHD_TEST_POKE"] = "1:%s:" % data_address(0x104741F0) + warp
    state1, _ = make_state(binary, game, save_dir, work, name, env)
    if not state1:
        return False, None
    ok = parse_state(state1)["stage"] == expect_stage
    if not ok:
        print(name + ": expected stage " + expect_stage)
    state2, _ = load_state(binary, game, save_dir, work, name, state1)
    if not state2:
        return False, state1
    return compare(name, state1, state2)[0] and ok, state1


def questlog_case(binary, game, save_dir, work, state1):
    """a state from Quest Log 1 loaded while Quest Log 2 is played"""
    import wwsave as W
    slots, extras, _ = W.read_hd(open(os.path.join(save_dir, "user", "cking.sav"), "rb").read())
    slots = [slots[0], slots[0], slots[2]]
    extras = [extras[0], dict(extras[0]), extras[2]]
    two = os.path.join(work, "two_quest_logs.sav")
    open(two, "wb").write(W.write_hd(slots, extras))
    state2, log = load_state(binary, game, two, work, "questlog", state1, quest_log=2)
    if not state2:
        return False
    notice = re.search(r"this state is from Quest Log (\d); it is loaded into Quest Log (\d)", log)
    print("questlog: " + (notice.group(0) if notice else "NO notice logged"))
    ok, s1, s2 = compare("questlog", state1, state2, same_slot=False)
    print("questlog: state from Quest Log %d, saved again from Quest Log %d" % (int(s1["file_slot"]) + 1, int(s2["file_slot"]) + 1))
    return ok and bool(notice) and notice.groups() == ("1", "2") and s2["file_slot"] == "1"


def boat_case(binary, game, boat_save, work, origin=3300):
    """swim to the boat, climb aboard (A), set sail (A), sail; save; cold boot; load: Link on the boat"""
    env = {"WWHD_PRESS": presses(extra=(3430, 3560)), "WWHD_STICK": "3300-3400:0:-1,3600-3800:0:1",
           "WWHD_PORTABLE_SAVE_AT": "3820:1"}
    state1, _ = make_state(binary, game, boat_save, work, "boat", env, origin=origin)
    if not state1:
        return False
    s1 = parse_state(state1)
    print("boat: on_ship %s, boat at %s angle %s" % (s1["on_ship"], s1["ship_pos"], s1["ship_angle_y"]))
    if s1["on_ship"] != "1":
        print("boat: Link was not on the boat when saving")
        return False
    state2, log = load_state(binary, game, boat_save, work, "boat", state1, origin=origin)
    if not state2:
        return False
    m = re.search(r"portable load: boat at (\S+) (\S+) (\S+) angle (-?\d+) \(distance (\S+)\), Link (on|not on) the boat", log)
    print("boat: " + (m.group(0) if m else "no boat line"))
    ok = bool(m) and m.group(6) == "on"
    if m:
        # horizontal: the boat rides the waves. The game puts the boat where Link was (daPy_lk_c create:
        # ship->initStartPos(&current.pos)), which is a few tens of units from where it was.
        q = [float(m.group(i)) for i in (1, 2, 3)]
        p = [float(x) for x in s1["ship_pos"].split()]
        dxz = ((q[0] - p[0]) ** 2 + (q[2] - p[2]) ** 2) ** 0.5
        print("boat: boat %.1f units (horizontal) from its saved position" % dxz)
        ok = ok and dxz < 150
        da = (int(m.group(4)) - int(s1["ship_angle_y"])) & 0xFFFF
        da = min(da, 0x10000 - da)
        print("boat: heading difference %d" % da)
        ok = ok and da <= 0x400
    s2 = parse_state(state2)
    print("boat: after the load on_ship %s, boat at %s" % (s2["on_ship"], s2["ship_pos"]))
    ok2, _, _ = compare("boat", state1, state2, pos_tolerance=150, xz_only=True)
    return ok and ok2 and s2["on_ship"] == "1"


def event_case(binary, game, event_save, work):
    """the King of Red Lions talks after Link walks into the water: no portable state then, a full one yes"""
    d = prepare(work, "event", event_save)
    env = {"WWHD_PRESS": presses(extra=range(3500, 3700, 40)), "WWHD_STICK": "3300-3360:0.54:-0.84",
           "WWHD_PORTABLE_SAVE_AT": "3485:1,3820:2", "WWHD_STATE_SAVE_AT": "3487:3", "WWHD_DUMP_FRAMES": "3485"}
    log = run_game(binary, game, d, env, lambda l: "slot 2: portable state written" in l or "slot 2: not saved" in l
                   or "slot 2: portable state refused" in l, 300)
    refused = re.search(r"slot 1: portable state refused: (.*)", log)
    no_file = not os.path.exists(os.path.join(d, "states", "slot1.wwstate"))
    full = re.search(r"slot 3: written \(([\d.]+) MB", log)
    later = "slot 2: portable state written" in log
    print("event: mid-dialogue portable save %s%s; file %s; full save at the same moment %s; portable save after the dialogue %s" %
          ("refused (" + refused.group(1) + ")" if refused else "NOT refused", "", "absent" if no_file else "WRITTEN",
           "written (%s MB)" % full.group(1) if full else "NOT written", "written" if later else "NOT written"))
    if not later:
        print(show(log))
    try:
        os.remove(os.path.join(d, "states", "slot3.bin"))  # this run's own ~300 MB file
    except OSError:
        pass
    return bool(refused) and no_file and bool(full) and later


def main():
    global release_map
    if len(sys.argv) < 5:
        print(__doc__)
        return 2
    binary, game, save_dir, work = (os.path.abspath(a) for a in sys.argv[1:5])
    release_map = builds.identify(os.path.join(game, "code", "cking.rpx"))
    if release_map is None:
        raise ValueError("unsupported executable for the scenario")
    print("%s scenario: game addresses mapped through the build registry" % release_map.name, flush=True)
    opt = lambda k: os.path.abspath(sys.argv[sys.argv.index(k) + 1]) if k in sys.argv else None  # noqa: E731
    skip_full = "--skip-full" in sys.argv
    only = opt("--only") and os.path.basename(opt("--only"))
    os.makedirs(work, exist_ok=True)
    results = {}
    run = lambda n: only is None or n in only.split(",")  # noqa: E731
    origin = 3300  # TV frame: well inside gameplay

    # Link's house (another stage than the cold boot's Outset), and Outset itself (same stage, other place)
    warp = "4C696E6B524D0000" + "0000" + "00" + "FF" + "01" + "00"  # "LinkRM", point 0, room 0, layer -1, enabled, wipe 0
    house_state = None
    if run("house") or run("questlog"):
        results["house"], house_state = portable_case(binary, game, save_dir, work, "house", warp, "LinkRM")
    if run("outset"):
        results["outset"] = portable_case(binary, game, save_dir, work, "outset", None, "sea")[0]
    if run("questlog") and house_state:
        results["questlog"] = questlog_case(binary, game, save_dir, work, house_state)
    if run("boat") and opt("--boat-save"):
        results["boat"] = boat_case(binary, game, opt("--boat-save"), work)
    if run("event") and opt("--event-save"):
        results["event"] = event_case(binary, game, opt("--event-save"), work)

    # ---- full states keep working
    if not skip_full and run("full"):
        d3 = prepare(work, "full", save_dir)
        # then a portable state into the same slot: the full one is kept, the log and the Saves tab say so
        env = {"WWHD_PRESS": presses(), "WWHD_STATE_SAVE_AT": "%d:3" % origin, "WWHD_STATE_LOAD_AT": "%d:3" % (origin + 300),
               "WWHD_PORTABLE_SAVE_AT": "%d:3" % (origin + 450), "WWHD_TEST_OVERLAY": "open:saves@%d" % (origin + 500),
               "WWHD_DUMP_FRAMES": str(origin + 560), "WWHD_DUMP_PRESENT": "1"}
        done = "frame_%d_present.png" % (origin + 560)
        log = run_game(binary, game, d3, env, lambda l: os.path.exists(os.path.join(d3, done)) or "slot 3: cannot" in l or "slot 3: not" in l, 300)
        written = re.search(r"slot 3: written \(([\d.]+) MB on disk", log)
        loaded = "Loaded slot 3" in log
        kept = re.search(r"slot 3 also holds an older full state \((.*)\); it is kept", log)
        print("full: %s, %s" % ("written (%s MB)" % written.group(1) if written else "NOT written", "loaded" if loaded else "NOT loaded"))
        print("full: portable state into the same slot: %s; slot3.bin %s; Saves tab picture %s" %
              ("warned (" + kept.group(1) + ")" if kept else "NO warning",
               "kept" if os.path.exists(os.path.join(d3, "states", "slot3.bin")) else "GONE", os.path.join(d3, done)))
        results["full"] = bool(written and loaded and kept and os.path.exists(os.path.join(d3, "states", "slot3.bin")))
        try:
            os.remove(os.path.join(d3, "states", "slot3.bin"))  # this run's own ~300 MB file
        except OSError:
            pass
    for k, v in results.items():
        print("%-9s %s" % (k, "PASS" if v else "FAIL"))
    ok = all(results.values())
    print("RESULT: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
