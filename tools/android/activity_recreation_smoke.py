#!/usr/bin/env python3
"""Verify a live native session survives replacement Android Activities, on an emulator."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import time

PACKAGE = "org.wwhdrecomp.wwhd"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prefix = [args.adb, "-s", args.serial]

    def adb(*command, check=True):
        if command[0] == "shell": command = ("shell", shlex.join(command[1:]))
        return subprocess.run(prefix + list(command), check=check, capture_output=True,
                              text=True, timeout=30).stdout.strip()

    if adb("shell", "getprop", "ro.kernel.qemu") != "1": parser.error("requires an emulator")
    if args.apk: adb("install", "-r", str(args.apk))
    original = adb("shell", "settings", "get", "global", "overlay_display_devices")
    settings = "/sdcard/Android/data/" + PACKAGE + "/files/config/wwhd/settings.ini"
    existing = subprocess.run(prefix + ["exec-out", "cat", settings], capture_output=True, timeout=20)
    if existing.returncode not in (0, 1): raise RuntimeError("Cannot inspect settings")
    saved = existing.stdout if existing.returncode == 0 else None
    report = {"passed": False, "observations": []}

    def read(name):
        return json.loads(adb("shell", "run-as", PACKAGE, "cat", "files/" + name))

    def until(label, predicate, timeout=20):
        deadline = time.monotonic() + timeout
        latest = {}
        while time.monotonic() < deadline:
            try:
                latest = read("display-smoke.json")
                if predicate(latest):
                    report["observations"].append({"check": label, **latest})
                    return latest
            except (ValueError, subprocess.CalledProcessError): pass
            time.sleep(.2)
        raise AssertionError(label + " timed out: " + repr(latest))

    def command(value):
        script = "printf '%s' " + shlex.quote(value) + " > files/display-smoke-command.tmp && mv files/display-smoke-command.tmp files/display-smoke-command"
        adb("shell", "run-as", PACKAGE, "sh", "-c", script)

    def broadcast(*extras):
        adb("shell", "am", "broadcast", "-a", PACKAGE + ".SMOKE_FOLD", "-p", PACKAGE, *extras)

    try:
        adb("shell", "am", "force-stop", PACKAGE)
        adb("shell", "run-as", PACKAGE, "rm", "-f", "files/display-smoke.json", "files/display-smoke-command")
        adb("shell", "input", "-d", "0", "motionevent", "UP", "0", "0")
        adb("shell", "settings", "put", "global", "overlay_display_devices", "800x480/160")
        adb("logcat", "-c")
        adb("shell", "am", "start", "-W", "-n", PACKAGE + "/.DisplaySmokeActivity")
        initial = until("initial presentation", lambda m: m["dual"] and m["secondary_presented"] >= 10)
        initial_pid = adb("shell", "pidof", PACKAGE)
        for mode in ("external-normal", "external-swapped", "fold-horizontal"):
            command("swap1" if mode == "external-swapped" else "swap0")
            until(mode + " roles", lambda m: m["dual"] and m["swap_active"] == (mode == "external-swapped"))
            if mode == "fold-horizontal":
                adb("shell", "settings", "delete", "global", "overlay_display_devices")
                until("external removed", lambda m: not m["dual"])
                broadcast("--es", "synthetic_fold", "horizontal")
                until("fold panes ready", lambda m: m["dual"] and m["primary_height"] < initial["primary_height"])
            before = read("display-smoke.json")
            if mode == "external-normal":
                ids = re.findall(r"secondary display=(\d+)", adb("logcat", "-d", "-s", "wwhd-display:I", "*:S"))
                if not ids: raise AssertionError("Missing secondary display ID")
                display, x, y = ids[-1], 400, 240
            elif mode == "external-swapped":
                display, x, y = "0", before["primary_width"] // 2, before["primary_height"] // 2
                adb("shell", "cmd", "statusbar", "collapse", check=False)
            else:
                display, x, y = "0", initial["primary_width"] // 2, initial["primary_height"] - before["secondary_height"] // 2
            def touch(action):
                adb("shell", "input", "-d", display, "motionevent", action, str(x), str(y))
            def centered(m):
                return m["touch"] and abs(m["tx"] - .5) < .08 and abs(m["ty"] - .5) < .08
            touch("DOWN")
            until(mode + " held touch before recreation", centered)
            activity = read("display-smoke-activity.json")
            broadcast("--ez", "recreate", "true")

            def restored(m):
                replacement = read("display-smoke-activity.json")
                return (replacement["instance"] != activity["instance"] and replacement["reused"] and
                        replacement["expected_thread"] != 0 and
                        replacement["expected_thread"] == replacement["native_thread"] and
                        str(replacement["pid"]) == initial_pid and m["dual"] and not m["touch"] and
                        m["primary_presented"] > before["primary_presented"] + 20 and
                        m["secondary_presented"] > before["secondary_presented"] + 20)

            after = until(mode + " Activity replaced with native thread retained", restored)
            touch("UP")
            touch("DOWN")
            until(mode + " touch after recreation", centered)
            touch("UP")
            until(mode + " touch released", lambda m: not m["touch"])
            replacement = read("display-smoke-activity.json")
            report["observations"].append({"check": mode + " retained identity", **replacement})
            if (after["primary_width"], after["primary_height"]) != (before["primary_width"], before["primary_height"]):
                raise AssertionError("Recreation did not restore pane extent")
        logs = adb("logcat", "-d", "-s", "SDL:V", "*:S")
        if len(re.findall(r"nativeInitSDLThread\(\)", logs)) != 1:
            raise AssertionError("Recreation restarted SDL_main")
        report["passed"] = True
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.with_suffix(".log").write_text(adb("logcat", "-d", "-s", "SDL:V", "wwhd:I", "wwhd-display:I", "AndroidRuntime:E", "*:S") + "\n")
        adb("shell", "input", "-d", "0", "motionevent", "UP", "0", "0", check=False)
        adb("shell", "am", "force-stop", PACKAGE, check=False)
        if saved is None: adb("shell", "rm", "-f", settings)
        else:
            subprocess.run(prefix + ["shell", "-T", "cat > " + shlex.quote(settings)], input=saved,
                           check=True, capture_output=True, timeout=20)
        if original == "null": adb("shell", "settings", "delete", "global", "overlay_display_devices")
        else: adb("shell", "settings", "put", "global", "overlay_display_devices", original)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"passed": report["passed"], "observations": len(report["observations"])}))


if __name__ == "__main__":
    main()
