#!/usr/bin/env python3
"""Start ShaderPrepareActivity and verify gameplay routing did not finish it."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import time
import adb_util


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--package", default="org.wwhdrecomp.wwhd")
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prefix = [args.adb, "-s", args.serial]
    adb_util.wait_until_ready(prefix)

    def adb(*command, check=True):
        if command[0] == "shell":
            command = ("shell", shlex.join(command[1:]))
        return adb_util.run(prefix + list(command), check=check, capture_output=True,
                            text=True, timeout=30).stdout.strip()

    if args.apk:
        adb("install", "-r", str(args.apk))
    report = {"passed": False}
    try:
        adb("shell", "am", "force-stop", args.package)
        adb("shell", "run-as", args.package, "rm", "-f", "files/shader-routing-smoke.json")
        adb("shell", "am", "start", "-W", "-n",
            args.package + "/org.wwhdrecomp.wwhd.ActivityRoutingSmokeActivity")
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            try:
                report = json.loads(adb("shell", "run-as", args.package, "cat",
                                        "files/shader-routing-smoke.json"))
                if not report.get("pending", True):
                    assert report["activity"] == "org.wwhdrecomp.wwhd.ShaderPrepareActivity"
                    assert not report["finishing"], "ShaderPrepareActivity was redirected and finished"
                    report["passed"] = True
                    break
            except (ValueError, subprocess.CalledProcessError):
                pass
            time.sleep(.2)
        else:
            raise AssertionError("ShaderPrepareActivity did not complete onCreate: " + repr(report))
    finally:
        adb("shell", "am", "force-stop", args.package, check=False)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
