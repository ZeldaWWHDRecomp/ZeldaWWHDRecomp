#!/usr/bin/env python3
"""Verify embedded Android translation against exact desktop C/header bytes."""
import argparse
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import time

PACKAGE = "org.wwhdrecomp.wwhd"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--require-compiler", action="store_true")
    parser.add_argument("--require-extractor", action="store_true")
    parser.add_argument("--require-runtime", action="store_true", help="require relinking/loading the complete runtime")
    parser.add_argument("--activate-runtime", action="store_true", help="activate this synthetic runtime on the test emulator")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prefix = [args.adb, "-s", args.serial]

    def adb(*command):
        if command[0] == "shell": command = ("shell", shlex.join(command[1:]))
        return subprocess.check_output(prefix + list(command), timeout=30).decode().strip()

    if adb("shell", "getprop", "ro.kernel.qemu") != "1":
        parser.error("synthetic smoke launcher requires an emulator")
    if args.apk: adb("install", "-r", str(args.apk))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = {"passed": False, "serial": args.serial}
    try:
        adb("shell", "am", "force-stop", PACKAGE)
        adb("shell", "run-as", PACKAGE, "rm", "-f", "files/python-smoke.json")
        adb("logcat", "-c")
        started = time.monotonic()
        adb("shell", "am", "start", "-W", "-n", PACKAGE + "/.PythonSmokeActivity",
            "--ez", "compiler", "true" if args.require_compiler or args.require_runtime or args.activate_runtime else "false",
            "--ez", "extractor", "true" if args.require_extractor else "false",
            "--ez", "activate", "true" if args.activate_runtime else "false")
        deadline = time.monotonic() + 180
        result = None
        while time.monotonic() < deadline:
            try:
                result = json.loads(adb("shell", "run-as", PACKAGE, "cat", "files/python-smoke.json"))
                break
            except (ValueError, subprocess.CalledProcessError): time.sleep(.2)
        if not result or not result.get("passed"):
            raise AssertionError("embedded fixture failed: " + repr(result))
        if args.activate_runtime and not result.get("activated"):
            raise AssertionError("Full runtime activation was not verified")
        report["startup_and_translation_seconds"] = time.monotonic() - started
        metrics = json.loads(adb("shell", "run-as", PACKAGE, "cat", result["output"] + "/metrics.json"))
        report["android"] = metrics
        report["fixture_output"] = result["output"]
        report["activated"] = result.get("activated", False)
        if args.activate_runtime:
            if result.get("game_assets") != metrics.get("game_path"):
                raise AssertionError("Activated runtime did not retain the paired game assets")
            report["game_assets"] = result["game_assets"]
        if args.require_extractor and not metrics.get("extraction_verified"):
            raise AssertionError("Android extractor checkpoint was not verified")
        if not metrics.get("translation_pause_verified"):
            raise AssertionError("embedded in-translator pause/retry was not verified")
        if not metrics.get("installer_checkpoint_verified"):
            raise AssertionError("embedded shared installer checkpoint was not verified")
        if not metrics.get("native_process_verified"):
            raise AssertionError("embedded native process bridge was not verified")
        if (args.require_compiler or args.require_runtime or args.activate_runtime) and not metrics.get("compiler_load_verified"):
            raise AssertionError("Android-hosted compile/link/load was not verified")
        if (args.require_compiler or args.require_runtime or args.activate_runtime) and not (
                metrics.get("active_compile_pause_verified") and metrics.get("active_link_pause_verified")):
            raise AssertionError("Active native compiler/linker pause and retry were not verified")
        if args.require_runtime and not metrics.get("durable_job_verified"):
            raise AssertionError("Durable setup job restart and pause were not verified")
        if (args.require_runtime or args.activate_runtime) and not metrics.get("full_runtime_load_verified"):
            raise AssertionError("Full Android runtime link/load was not verified")
        fixture_path = Path(__file__).resolve().parents[1] / "recomp/android_fixture.py"
        spec = importlib.util.spec_from_file_location("fixture", fixture_path)
        fixture = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fixture)
        with tempfile.TemporaryDirectory() as temporary:
            desktop = Path(temporary) / "desktop"
            report["desktop"] = fixture.main(["--output", str(desktop)])
            expected = fixture.inventory(desktop / "gen")
            names = adb("shell", "run-as", PACKAGE, "ls", result["output"] + "/gen").splitlines()
            actual_names = {name for name in names if Path(name).suffix in (".c", ".h")}
            if actual_names != set(expected): raise AssertionError("Android C/header inventory differs")
            for name, content in expected.items():
                actual = subprocess.check_output(prefix + ["exec-out", "run-as", PACKAGE,
                    "cat", result["output"] + "/gen/" + name], timeout=30)
                if actual != content: raise AssertionError("Android C/header bytes differ: " + name)
        pid = adb("shell", "pidof", PACKAGE + ":setup")
        status = adb("shell", "run-as", PACKAGE, "cat", "/proc/" + pid + "/status")
        report["process_memory"] = {line.split(":", 1)[0]: line.split(":", 1)[1].strip()
                                    for line in status.splitlines() if line.startswith(("VmRSS:", "VmHWM:"))}
        report["passed"] = True
    finally:
        try:
            args.output.with_suffix(".log").write_text(adb("logcat", "-d", "-s", "wwhd-python:V", "python.stdout:V", "python.stderr:V", "AndroidRuntime:E", "*:S") + "\n")
        finally:
            args.output.write_text(json.dumps(report, indent=2) + "\n")
            adb("shell", "am", "force-stop", PACKAGE)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
