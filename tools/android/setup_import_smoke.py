#!/usr/bin/env python3
"""Exercise production SAF import with debug-only, authored pipe documents."""
import argparse
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", default="emulator-5554")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prefix = [args.adb, "-s", args.serial]

    def adb(*arguments, check=True):
        return subprocess.run(prefix + list(arguments), check=check, capture_output=True, text=True, timeout=30)

    if adb("shell", "getprop", "ro.kernel.qemu").stdout.strip() != "1":
        raise RuntimeError("This synthetic probe runs only on an emulator")
    package = "org.wwhdrecomp.wwhd"
    result_path = "files/setup-import-smoke.json"
    adb("shell", "run-as", package, "rm", "-f", result_path)
    adb("shell", "am", "start", "--activity-clear-top", "-n", package + "/.SetupImportSmokeActivity")
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        result = adb("shell", "run-as", package, "cat", result_path, check=False)
        if result.returncode == 0:
            value = json.loads(result.stdout)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(value, indent=2) + "\n")
            if not value.get("passed"):
                raise RuntimeError(value.get("error", "Import checks failed"))
            print("SAF import checks passed")
            return
        time.sleep(0.2)
    raise RuntimeError("Timed out waiting for SAF import checks")


if __name__ == "__main__":
    main()
