#!/usr/bin/env python3
"""Exercise setup foreground service controls/recovery on an emulator only."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import adb_util
import time

PACKAGE = "org.wwhdrecomp.wwhd"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prefix = [args.adb, "-s", args.serial]
    adb_util.wait_until_ready(prefix)

    def adb(*command, data=None, check=True):
        if command[0] == "shell": command = ("shell", shlex.join(command[1:]))
        return adb_util.run(prefix + list(command), input=data, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, check=check, timeout=30)

    def shell(*command):
        return adb("shell", *command).stdout.decode().strip()

    if shell("getprop", "ro.kernel.qemu") != "1":
        parser.error("service control tests require an emulator")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    original = {}
    for path in ("no_backup/ondevice/setup.json", "no_backup/ondevice/active.json", "no_backup/ondevice/previous.json"):
        exists = adb("shell", "run-as", PACKAGE, "test", "-f", path, check=False)
        if exists.returncode not in (0, 1): raise RuntimeError("Cannot inspect existing setup selection")
        original[path] = adb("shell", "run-as", PACKAGE, "cat", path).stdout if exists.returncode == 0 else None
    battery_dump = shell("dumpsys", "battery")
    battery_stopped = "UPDATES STOPPED" in battery_dump
    battery_fields = {name: shell("cmd", "battery", "get", name) for name in
                      ("ac", "usb", "wireless", "dock", "status", "level", "temp", "present")}
    thermal_dump = shell("dumpsys", "thermalservice")
    thermal_overridden = "IsStatusOverride: true" in thermal_dump
    thermal_original = re.search(r"Thermal Status: (\d+)", thermal_dump).group(1)
    report = {"passed": False, "serial": args.serial, "observations": []}
    job = None

    def control(mode, pid=None):
        shell("run-as", PACKAGE, "rm", "-f", "files/setup-service-smoke.json")
        shell("am", "start", "-W", "--activity-clear-top", "-n", PACKAGE + "/.SetupServiceSmokeActivity",
              "--es", "mode", mode, *(["--ei", "pid", str(pid)] if pid is not None else []))
        deadline = time.monotonic() + 10
        while True:
            try:
                result = json.loads(shell("run-as", PACKAGE, "cat", "files/setup-service-smoke.json"))
                break
            except (ValueError, subprocess.CalledProcessError):
                if time.monotonic() >= deadline: raise
                time.sleep(.1)
        if not result.get("passed"): raise AssertionError(result)
        return result

    def host():
        try: return json.loads(shell("run-as", PACKAGE, "cat", job + "/host.json"))
        except (ValueError, subprocess.CalledProcessError): return {}

    def until(label, predicate, timeout=45):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = host()
            if predicate(value):
                report["observations"].append({"check": label, **value})
                print(label + ": " + json.dumps(value), flush=True)
                return value
            time.sleep(.1)
        raise AssertionError(label + " timed out: " + repr(host()))

    def paused(reason):
        return lambda value: value.get("state") == "paused" and value.get("reason") == reason

    def wake_held():
        power = shell("dumpsys", "power")
        locks = power.split("Wake Locks:", 1)[-1].split("Suspend Blockers:", 1)[0]
        return "wwhd:setup" in locks

    def no_wake():
        if wake_held(): raise AssertionError("Paused/finished setup retains its wake lock")

    try:
        shell("am", "force-stop", PACKAGE)
        # Disposable tool data makes the preparing stage observable and tests
        # its safe pause boundary. No game input, saves or settings are removed.
        shell("run-as", PACKAGE, "rm", "-rf", "no_backup/toolchains")
        shell("cmd", "thermalservice", "override-status", "0")
        shell("cmd", "battery", "set", "-f", "temp", "250")
        created = control("create")
        if not re.fullmatch(r"[0-9a-f]{32}", created["job"]): raise AssertionError("Invalid test job")
        job = "no_backup/ondevice/jobs/" + created["job"]
        until("manual pause", paused("manual"))
        if "isForeground=true" not in shell("dumpsys", "activity", "services", PACKAGE):
            raise AssertionError("Paused setup service is not foreground")
        no_wake()
        control("open")
        deadline = time.monotonic() + 10
        while ".SetupActivity" not in shell("dumpsys", "activity", "top"):
            if time.monotonic() >= deadline: raise AssertionError("Setup progress screen did not open")
            time.sleep(.1)
        time.sleep(.2)
        args.output.with_suffix(".png").write_bytes(adb("exec-out", "screencap", "-p").stdout)
        shell("input", "keyevent", "KEYCODE_HOME")
        old_pid = shell("pidof", PACKAGE + ":setup")
        if not re.fullmatch(r"\d+", old_pid): raise AssertionError("Missing setup service process")
        prior_update = host().get("updated", 0)
        killed = control("kill-worker", int(old_pid))
        if killed.get("terminated") != int(old_pid): raise AssertionError(killed)
        until("sticky process recovery", lambda value: paused("manual")(value) and
              value.get("updated", 0) > prior_update and
              adb("shell", "pidof", PACKAGE + ":setup", check=False).stdout.decode().strip() not in ("", old_pid), 60)
        no_wake()
        # SEVERE counts only with a real temperature behind it (#254, #277): a warm battery (40.0 C) confirms it
        shell("cmd", "battery", "set", "-f", "temp", "400")
        shell("cmd", "thermalservice", "override-status", "3")
        shell("cmd", "battery", "unplug", "-f")
        shell("cmd", "battery", "set", "-f", "level", "10")
        control("resume")
        until("thermal pause", paused("heat"))
        no_wake()
        shell("cmd", "thermalservice", "override-status", "2")
        time.sleep(1.2)
        until("thermal hysteresis", paused("heat"))
        # #277: a heat pause never ends by itself; once cooled it offers Continue and keeps waiting
        if "heat-pause.json" not in shell("run-as", PACKAGE, "ls", job):
            raise AssertionError("Heat pause was not recorded in the job")
        shell("cmd", "thermalservice", "override-status", "0")
        shell("cmd", "battery", "set", "-f", "temp", "250")
        until("cooled heat pause offers Continue", lambda value: paused("heat")(value) and value.get("cooled") is True)
        time.sleep(8)
        until("cooled heat pause keeps waiting", lambda value: paused("heat")(value) and value.get("cooled") is True, 1)
        # the waiting pause survives a restart of the setup process (screen off, app in the background)
        old_pid = shell("pidof", PACKAGE + ":setup")
        prior_update = host().get("updated", 0)
        killed = control("kill-worker", int(old_pid))
        if killed.get("terminated") != int(old_pid): raise AssertionError(killed)
        until("heat pause restored after restart", lambda value: paused("heat")(value) and
              value.get("updated", 0) > prior_update and value.get("cooled") is True and
              adb("shell", "pidof", PACKAGE + ":setup", check=False).stdout.decode().strip() not in ("", old_pid), 60)
        no_wake()
        control("resume")
        until("Continue after cooling", paused("battery"))
        if "heat-pause.json" in shell("run-as", PACKAGE, "ls", job):
            raise AssertionError("Continue left the heat pause record behind")
        shell("cmd", "battery", "set", "-f", "temp", "400")
        shell("cmd", "thermalservice", "override-status", "3")
        until("later heat pauses again", lambda value: paused("heat")(value) and value.get("cooled") is False)
        shell("cmd", "thermalservice", "override-status", "4")
        until("critical thermal pause", paused("critical"))
        control("continue-heat")
        time.sleep(1.2)
        until("continue anyway keeps the critical stop", paused("critical"))
        shell("cmd", "thermalservice", "override-status", "3")
        until("continue anyway skips the heat pause", paused("battery"))
        if "continue-heat.json" not in shell("run-as", PACKAGE, "ls", job):
            raise AssertionError("Continue anyway was not persisted in the job")
        if "heat-pause.json" in shell("run-as", PACKAGE, "ls", job):
            raise AssertionError("Continue anyway left the heat pause record behind")
        shell("cmd", "battery", "set", "-f", "level", "25")
        time.sleep(1.2)
        until("battery hysteresis", paused("battery"))
        no_wake()
        shell("cmd", "thermalservice", "override-status", "0")
        shell("cmd", "battery", "set", "-f", "temp", "250")
        shell("cmd", "battery", "set", "-f", "level", "80")
        until("automatic resume", lambda value: value.get("state") == "preparing")
        if not wake_held(): raise AssertionError("Active setup did not acquire its wake lock")
        control("pause")
        until("pause after preparation", paused("manual"))
        no_wake()
        control("resume")
        failure = until("unsupported input error", lambda value: value.get("state") == "failed", 90)
        if "continue-heat.json" in shell("run-as", PACKAGE, "ls", job):
            raise AssertionError("Continue anyway outlived the finished setup run")
        if "not a file the port knows" not in failure.get("error", ""):
            raise AssertionError("Unsupported input lost its useful installer error")
        no_wake()
        if "isForeground=true" in shell("dumpsys", "activity", "services", PACKAGE):
            raise AssertionError("Failed setup retained its foreground service")
        for path in ("no_backup/ondevice/active.json", "no_backup/ondevice/previous.json"):
            present = adb("shell", "run-as", PACKAGE, "cat", path, check=False)
            if (present.stdout if present.returncode == 0 else None) != original[path]:
                raise AssertionError("Failed setup changed an installed build")
        report["checks_passed"] = True
    finally:
        report["logs"] = args.output.with_suffix(".log").name
        args.output.with_suffix(".log").write_bytes(adb("logcat", "-d", "-s", "wwhd-setup:V", "python.stderr:V", "AndroidRuntime:E", "*:S").stdout)
        shell("am", "force-stop", PACKAGE)
        shell("cmd", "thermalservice", "override-status", thermal_original) if thermal_overridden else shell("cmd", "thermalservice", "reset")
        shell("cmd", "battery", "reset", "-f")
        if battery_stopped:
            for name, value in battery_fields.items():
                shell("cmd", "battery", "set", "-f", name, {"true": "1", "false": "0"}.get(value, value))
        for path, data in original.items():
            if data is None: shell("run-as", PACKAGE, "rm", "-f", path)
            else: adb("shell", "run-as", PACKAGE, "sh", "-c", "cat > " + shlex.quote(path), data=data)
        if job is not None: shell("run-as", PACKAGE, "rm", "-rf", job)
        report["settings_restored"] = True
        report["passed"] = report.get("checks_passed", False)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"passed": report["passed"], "report": str(args.output)}))


if __name__ == "__main__":
    main()
