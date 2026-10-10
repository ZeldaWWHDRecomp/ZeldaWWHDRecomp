#!/usr/bin/env python3
"""Test package-replacement recovery decisions with synthetic control records, on an emulator.

This tests automatic service dispatch, not successful compilation or activation after an update.
Use a minimal debug APK: its missing setup resources make dispatched work fail harmlessly.
"""
import argparse
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

    def adb(*command, data=None, check=True):
        if command[0] == "shell":
            command = ("shell", shlex.join(command[1:]))
        return subprocess.run(prefix + list(command), input=data, check=check,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)

    def shell(*command, **kwargs):
        return adb("shell", *command, **kwargs).stdout

    if shell("getprop", "ro.kernel.qemu").strip() != b"1":
        parser.error("requires an emulator")
    # A full setup APK could activate this intentionally fabricated completed-job record.
    import zipfile
    with zipfile.ZipFile(args.apk) as apk:
        if "assets/toolchain-apk.json" in apk.namelist():
            parser.error("requires a minimal debug APK without compiler resources")
    original = {}
    for path in ("setup.json", "active.json", "previous.json"):
        value = adb("shell", "run-as", PACKAGE, "cat", "no_backup/ondevice/" + path, check=False)
        if value.returncode not in (0, 1):
            raise RuntimeError("Cannot read existing setup metadata")
        original[path] = value.stdout if value.returncode == 0 else None
    report = {"passed": False, "scope": "synthetic package-update service dispatch", "observations": []}
    job = None

    def write(path, value):
        shell("run-as", PACKAGE, "sh", "-c", "cat > " + shlex.quote(path),
              data=json.dumps(value).encode())

    try:
        shell("am", "force-stop", PACKAGE)
        shell("am", "start", "-W", "-n", PACKAGE + "/.SetupServiceSmokeActivity", "--es", "mode", "create")
        created = json.loads(shell("run-as", PACKAGE, "cat", "files/setup-service-smoke.json"))
        if not created.get("passed"):
            raise AssertionError(created)
        identity = created["job"]
        if len(identity) != 32 or any(c not in "0123456789abcdef" for c in identity):
            raise AssertionError("Invalid synthetic job")
        job = "no_backup/ondevice/jobs/" + identity
        shell("am", "start", "-W", "--activity-clear-top", "-n",
              PACKAGE + "/.SetupServiceSmokeActivity", "--es", "mode", "stop")
        shell("input", "keyevent", "KEYCODE_HOME")
        time.sleep(1)
        for state, manual, dispatch in (("selected", False, False),
                                        ("complete", True, False), ("complete", False, True)):
            host = {"schema": 1, "state": state, "updated": 1}
            write(job + "/host.json", host)
            if manual:
                write(job + "/manual-pause.json", {"schema": 1})
            else:
                shell("run-as", PACKAGE, "rm", "-f", job + "/manual-pause.json", job + "/pause.json")
            adb("install", "-r", str(args.apk))  # Real system MY_PACKAGE_REPLACED broadcast.
            deadline = time.monotonic() + (15 if dispatch else 4)
            latest = None
            while time.monotonic() < deadline:
                latest = json.loads(shell("run-as", PACKAGE, "cat", job + "/host.json"))
                if dispatch and latest.get("updated", 0) > 1 and latest.get("state") == "failed":
                    break
                if not dispatch and latest != host:
                    raise AssertionError("Update started an unrequested or manually paused job: " + repr(latest))
                time.sleep(.2)
            if dispatch and (latest.get("state") != "failed" or latest.get("updated", 0) <= 1):
                raise AssertionError("Completed stale job was not dispatched: " + repr(latest))
            report["observations"].append({"prior_state": state, "manual_pause": manual,
                                           "dispatched": dispatch, "result": latest})
        for name in ("active.json", "previous.json"):
            value = adb("shell", "run-as", PACKAGE, "cat", "no_backup/ondevice/" + name, check=False)
            if (value.stdout if value.returncode == 0 else None) != original[name]:
                raise AssertionError("Update dispatch changed installed build metadata")
        report["passed"] = True
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.with_suffix(".log").write_bytes(shell("logcat", "-d", "-s", "wwhd-setup:V", "AndroidRuntime:E", "*:S"))
        shell("am", "force-stop", PACKAGE)
        for name, data in original.items():
            path = "no_backup/ondevice/" + name
            if data is None:
                shell("run-as", PACKAGE, "rm", "-f", path)
            else:
                shell("run-as", PACKAGE, "sh", "-c", "cat > " + shlex.quote(path), data=data)
        if job is not None:
            shell("run-as", PACKAGE, "rm", "-rf", job)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
