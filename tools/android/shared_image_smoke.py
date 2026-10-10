#!/usr/bin/env python3
"""Check shared-image shader rendering between two queue-zero Vulkan devices.

This is a prerequisite probe for isolated secondary presentation. It does not
render through a second-device swapchain or establish independent frame timing.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shlex
import subprocess
import time

PACKAGE = "org.wwhdrecomp.wwhd"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--external-memory-capabilities", action="store_true", help="query external image/buffer support only; does not test allocation or GPU transfer")
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--general-layout", action="store_true", help="test shared GPU images kept in GENERAL layout")
    args = parser.parse_args()
    if args.general_layout and args.external_memory_capabilities: parser.error("choose GPU rendering or capability queries")
    prefix = [args.adb, "-s", args.serial]
    command = "external_memory_capabilities" if args.external_memory_capabilities else "shared_image_general_probe" if args.general_layout else "shared_image_probe"
    marker = "external memory capability probe" if args.external_memory_capabilities else "two-device shared image general shader probe" if args.general_layout else "two-device shared image shader probe"

    def adb(*command, check=True):
        if command[0] == "shell": command = ("shell", shlex.join(command[1:]))
        return subprocess.run(prefix + list(command), check=check, capture_output=True, text=True, timeout=20).stdout.strip()

    if adb("shell", "getprop", "ro.kernel.qemu") != "1": parser.error("requires an emulator")
    paths = adb("shell", "pm", "path", PACKAGE).splitlines()
    if len(paths) != 1 or not paths[0].startswith("package:"): parser.error("requires one installed debug APK")
    with args.apk.open("rb") as stream: checksum = hashlib.file_digest(stream, "sha256").hexdigest()
    if adb("shell", "sha256sum", paths[0][8:]).split()[0] != checksum:
        parser.error("installed APK differs from supplied APK")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = {"passed": False, "apk_sha256": checksum, "serial": args.serial,
              "scope": "shared GPU allocation, semaphore hand-off and consumer shader rendering; no isolated swapchain or frame-time proof"}
    report["environment"] = {key: adb("shell", "getprop", prop) for key, prop in
                             (("api", "ro.build.version.sdk"), ("abi", "ro.product.cpu.abi"),
                              ("fingerprint", "ro.build.fingerprint"))}
    try:
        adb("shell", "am", "force-stop", PACKAGE)
        adb("shell", "run-as", PACKAGE, "rm", "-f", "files/display-smoke.json", "files/display-smoke-command")
        adb("logcat", "-c")
        adb("shell", "am", "start", "-W", "-n", PACKAGE + "/.DisplaySmokeActivity")
        def metrics():
            return json.loads(adb("shell", "run-as", PACKAGE, "cat", "files/display-smoke.json", check=False))
        deadline = time.monotonic() + 45
        while True:
            try:
                before = metrics()
                if before["primary_presented"] >= 10: break
            except (ValueError, KeyError): pass
            if time.monotonic() >= deadline: raise AssertionError("primary fixture did not start")
            time.sleep(.1)
        started = time.monotonic()
        adb("shell", "run-as", PACKAGE, "sh", "-c",
            "printf " + command + " > files/display-smoke-command.tmp && mv files/display-smoke-command.tmp files/display-smoke-command")
        deadline = time.monotonic() + 35
        while True:
            logs = adb("logcat", "-d", "-s", "wwhd:I", "*:S")
            if marker + " failed:" in logs: raise AssertionError(logs.split(marker + " failed:")[-1])
            if marker + " passed" in logs: break
            if time.monotonic() >= deadline: raise AssertionError("shared image probe timed out")
            time.sleep(.1)
        report["probe_seconds"] = time.monotonic() - started
        deadline = time.monotonic() + 15
        while True:
            after = metrics()
            if after["primary_presented"] >= before["primary_presented"] + 5: break
            if time.monotonic() >= deadline: raise AssertionError("primary rendering did not resume after probe")
            time.sleep(.1)
        if args.external_memory_capabilities:
            report["buffers"] = [dict(zip(("handle", "features", "compatible"), map(int, match))) for match in
                                 re.findall(r"external buffer capability handle=(\d+) features=(\d+) compatible=(\d+)", logs)]
            report["images"] = [dict(zip(("handle", "usage", "result", "features", "compatible"), map(int, match))) for match in
                                re.findall(r"external image capability handle=(\d+) usage=(\d+) result=(-?\d+) features=(\d+) compatible=(\d+)", logs)]
            if len(report["buffers"]) != 3 or len(report["images"]) != 6:
                raise AssertionError("incomplete external-memory capability results")
            report.update(passed=True, scope="read-only external image/buffer capability queries; no allocation, import, GPU transfer or timing proof",
                          primary_before=before["primary_presented"], primary_after=after["primary_presented"])
        else:
            report.update(passed=True, shared_layout="general" if args.general_layout else "optimal", memory_handle="android_hardware_buffer", distinct_logical_devices=True, queue_index_per_device=0,
                          gpu_transfer_rounds=3, dimensions=[64, 36],
                          consumer_shader_rendered=True, scaling_filters=[0, 1, 2], fxaa_rounds=[2],
                          cpu_readback_scope="assertion buffer only; shared image is never CPU mapped",
                          primary_before=before["primary_presented"], primary_after=after["primary_presented"])
    except Exception as failure:
        report["error"] = str(failure)
        raise
    finally:
        try:
            args.output.with_suffix(".log").write_text(adb("logcat", "-d", "-s", "wwhd:I", "AndroidRuntime:E", "*:S") + "\n")
            adb("shell", "am", "force-stop", PACKAGE)
        except Exception as failure:
            report.update(passed=False, cleanup_error=str(failure))
            raise
        finally: args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
