#!/usr/bin/env python3
"""Exercise bounded Android second-window startup retries on an emulator."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import time

PACKAGE = "org.wwhdrecomp.wwhd"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prefix = [args.adb, "-s", args.serial]

    def adb(*command, check=True):
        if command[0] == "shell": command = ("shell", shlex.join(command[1:]))
        return subprocess.run(prefix + list(command), capture_output=True, text=True, check=check, timeout=25).stdout.strip()

    if adb("shell", "getprop", "ro.kernel.qemu") != "1": parser.error("requires an emulator")
    adb("install", "-r", str(args.apk))
    paths = adb("shell", "pm", "path", PACKAGE).splitlines()
    if len(paths) != 1 or not paths[0].startswith("package:"): parser.error("requires one installed debug APK")
    with args.apk.open("rb") as stream: checksum = hashlib.file_digest(stream, "sha256").hexdigest()
    if adb("shell", "sha256sum", paths[0][8:]).split()[0] != checksum: parser.error("installed APK differs")
    original = adb("shell", "settings", "get", "global", "overlay_display_devices")
    report = {"passed": False, "apk_sha256": checksum, "observations": [],
              "scope": "authored InvalidDisplayException at window creation; bounded retries, fallback, main progress and topology recovery",
              "environment": {key: adb("shell", "getprop", prop) for key, prop in
                              (("api", "ro.build.version.sdk"), ("abi", "ro.product.cpu.abi"), ("fingerprint", "ro.build.fingerprint"))}}
    args.output.parent.mkdir(parents=True, exist_ok=True)

    def overlay(value):
        if value == "null": adb("shell", "settings", "delete", "global", "overlay_display_devices")
        else: adb("shell", "settings", "put", "global", "overlay_display_devices", value)
        # Wait for the actual device topology, so rapid remove/add cannot coalesce.
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            displays = adb("shell", "dumpsys", "display")
            present = any('DisplayDeviceInfo{' in line and 'uniqueId="overlay:' in line
                          for line in displays.splitlines())
            if present == (value != "null"): return
            time.sleep(.1)
        raise AssertionError("overlay topology did not settle: " + value)
    def metrics(): return json.loads(adb("shell", "run-as", PACKAGE, "cat", "files/display-smoke.json", check=False))
    def logs(): return adb("logcat", "-d", "-s", "wwhd-display:W", "*:S")
    def rejections(): return logs().count("secondary host rejected display=")
    def until(label, predicate, timeout=25):
        deadline = time.monotonic() + timeout
        latest = None
        while time.monotonic() < deadline:
            try:
                latest = metrics()
                if predicate(latest):
                    report["observations"].append({"phase": label, **latest})
                    return latest
            except (ValueError, KeyError): pass
            time.sleep(.1)
        raise AssertionError(label + " timed out; last=" + repr(latest))
    def launch(count):
        adb("shell", "am", "force-stop", PACKAGE)
        adb("shell", "run-as", PACKAGE, "rm", "-f", "files/display-smoke.json")
        adb("logcat", "-c")
        adb("shell", "am", "start", "-W", "-n", PACKAGE + "/.DisplaySmokeActivity",
            "--ez", "isolated_secondary", "true", "--ei", "invalid_display_shows", str(count))
    try:
        adb("shell", "input", "keyevent", "82")
        adb("shell", "wm", "dismiss-keyguard")
        overlay("800x480/160")
        for count in (0, 1, 3):
            launch(count)
            ready = until("connected after " + str(count) + " rejected shows",
                          lambda m: m["dual"] and m["secondary_presented"] >= 10 and m["primary_presented"] >= 30)
            if rejections() != count: raise AssertionError("unexpected transient retry count")
            if not ready["secondary_isolated_device"]: raise AssertionError("isolated worker was not selected")
        launch(5)
        fallback = until("four rejected shows fall back", lambda m: rejections() >= 4 and not m["dual"] and m["primary_presented"] >= 30)
        until("persistent rejection keeps main updating", lambda m: not m["dual"] and m["primary_presented"] >= fallback["primary_presented"] + 120)
        if rejections() != 4: raise AssertionError("persistent failure exceeded retry budget")
        overlay("null")
        removed = until("unplug after exhausted budget", lambda m: not m["dual"] and m["primary_presented"] >= fallback["primary_presented"] + 140)
        overlay("800x480/160")
        until("new display event recovers", lambda m: m["dual"] and m["secondary_presented"] >= 10 and m["primary_presented"] > removed["primary_presented"])
        if rejections() != 5: raise AssertionError("new display event did not reset retry budget")
        report["passed"] = True
    except Exception as failure:
        report["error"] = str(failure)
        raise
    finally:
        try:
            args.output.with_suffix(".log").write_text(adb("logcat", "-d", "-s", "wwhd:I", "wwhd-display:I", "AndroidRuntime:E", "*:S") + "\n")
            adb("shell", "am", "force-stop", PACKAGE)
            overlay(original)
        except Exception as failure:
            report.update(passed=False, cleanup_error=str(failure))
            raise
        finally: args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"passed": report["passed"], "observations": len(report["observations"])}))


if __name__ == "__main__": main()
