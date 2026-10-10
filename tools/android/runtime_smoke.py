#!/usr/bin/env python3
"""Compile, activate and launch the full synthetic runtime; restore build selection."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shlex
import subprocess
import sys
import zipfile

PACKAGE = "org.wwhdrecomp.wwhd"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--require-extractor", action="store_true")
    parser.add_argument("--compiler-only", action="store_true", help="verify compile/load/activation only; leave rendering explicitly untested")
    parser.add_argument("--expected-api", type=int, help="reject a different emulator API before changing the app")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--require-isolated-device", action="store_true", help="verify the phone-linked library with independent secondary device presentation")
    parser.add_argument("--exercise-lifecycle", action="store_true", help="exercise folds, rotation, dismissal and surface loss on the phone-linked library")
    args = parser.parse_args()
    if args.compiler_only and (args.require_isolated_device or args.exercise_lifecycle):
        parser.error("display checks cannot be combined with --compiler-only")
    prefix = [args.adb, "-s", args.serial]

    def adb(*command, data=None, check=True):
        if command[0] == "shell": command = ("shell", shlex.join(command[1:]))
        return subprocess.run(prefix + list(command), input=data, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=40, check=check)

    if adb("shell", "getprop", "ro.kernel.qemu").stdout.strip() != b"1":
        parser.error("synthetic runtime activation requires an emulator")
    device = {name: adb("shell", "getprop", prop).stdout.decode().strip() for name, prop in (
        ("api", "ro.build.version.sdk"), ("abi", "ro.product.cpu.abi"),
        ("fingerprint", "ro.build.fingerprint"), ("model", "ro.product.model"))}
    device["api"] = int(device["api"])
    if args.expected_api is not None and device["api"] != args.expected_api:
        parser.error("emulator API differs from --expected-api")
    if args.apk: adb("install", "-r", str(args.apk))
    adb("shell", "am", "force-stop", PACKAGE)
    saved = {}
    for name in ("active", "previous"):
        for suffix in (".json", ".json.bak", ".json.new"):
            path = "no_backup/ondevice/" + name + suffix
            exists = adb("shell", "run-as", PACKAGE, "test", "-f", path, check=False)
            if exists.returncode not in (0, 1): raise RuntimeError("Cannot inspect existing runtime selection")
            saved[path] = adb("shell", "run-as", PACKAGE, "cat", path).stdout if exists.returncode == 0 else None
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = {"passed": False, "serial": args.serial, "device": device,
              "scope": "compiler/load/activation" if args.compiler_only else "compiler/load/activation/displays",
              "rendering": "not_tested" if args.compiler_only else "pending"}
    if args.apk:
        with args.apk.open("rb") as stream:
            checksum = hashlib.file_digest(stream, "sha256").hexdigest()
        paths = adb("shell", "pm", "path", PACKAGE).stdout.decode().splitlines()
        if len(paths) != 1 or not paths[0].startswith("package:"):
            raise AssertionError("Expected one installed APK")
        installed = adb("shell", "sha256sum", paths[0][8:]).stdout.decode().split()[0]
        if installed != checksum: raise AssertionError("Installed APK differs")
        report["apk_sha256"] = checksum
        with zipfile.ZipFile(args.apk) as apk:
            report["runtime_sdk"] = json.loads(apk.read("assets/runtime-sdk.json"))
    fixture = args.output.with_name(args.output.stem + "-python.json")
    display = args.output.with_name(args.output.stem + "-display.json")
    scripts = Path(__file__).resolve().parent
    try:
        subprocess.run([sys.executable, str(scripts / "python_smoke.py"), "--adb", args.adb,
                        "--serial", args.serial, "--require-runtime", "--activate-runtime",
                        "--output", str(fixture)] + (["--require-extractor"] if args.require_extractor else []), check=True)
        report["compiler_and_activation"] = json.loads(fixture.read_text())
        if "runtime_sdk" in report and report["compiler_and_activation"]["android"]["runtime_identity"] != report["runtime_sdk"]["identity"]:
            raise AssertionError("Phone-linked runtime differs from the APK SDK")
        if not args.compiler_only:
            display_options = []
            if args.require_isolated_device:
                display_options += ["--require-isolated-device", "--require-present-worker"]
            if args.exercise_lifecycle:
                display_options += ["--exercise-primary-rotation", "--exercise-folds",
                                    "--exercise-dismissal", "--exercise-surface-loss"]
            subprocess.run([sys.executable, str(scripts / "display_smoke.py"), "--adb", args.adb,
                            "--serial", args.serial, "--require-ondevice", "--output", str(display)]
                           + display_options, check=True)
            report["sdl_launch_and_displays"] = json.loads(display.read_text())
            report["rendering"] = "passed"
        report["checks_passed"] = True
    except Exception as failure:
        report["error"] = str(failure)
        raise
    finally:
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        adb("shell", "am", "force-stop", PACKAGE)
        for path, data in saved.items():
            if data is None:
                adb("shell", "run-as", PACKAGE, "rm", "-f", path)
            else:
                adb("shell", "run-as", PACKAGE, "sh", "-c", "cat > " + shlex.quote(path), data=data)
        for path, data in saved.items():
            restored = adb("shell", "run-as", PACKAGE, "cat", path, check=False)
            if data is None:
                if adb("shell", "run-as", PACKAGE, "test", "-e", path, check=False).returncode != 1:
                    raise AssertionError("Unexpected selection metadata after restore: " + path)
            elif restored.returncode != 0 or restored.stdout != data:
                raise AssertionError("Selection metadata restore differs: " + path)
        # The public fixture is disposable once its metrics and screenshots
        # have been captured and the original active build has been restored.
        if fixture.exists():
            generated = json.loads(fixture.read_text()).get("fixture_output", "")
            if re.fullmatch(r"/data/(?:user/0|data)/" + re.escape(PACKAGE) + r"/no_backup/ondevice/python-fixture-\d+", generated):
                adb("shell", "run-as", PACKAGE, "rm", "-rf", generated)
        report["selection_restored"] = True
        report["passed"] = report.get("checks_passed", False)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"passed": report["passed"], "report": str(args.output)}, indent=2))


if __name__ == "__main__":
    main()
