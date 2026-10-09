#!/usr/bin/env python3
"""Pause real service resource unpacking and resume the same synthetic build.

Preserves the existing matching resource cache by renaming it aside, preserves
setup metadata byte for byte, and deletes only this probe's authored outputs.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import shlex
import subprocess
import time
import uuid
import zipfile

PACKAGE = "org.wwhdrecomp.wwhd"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--resource", choices=("python", "compiler"), default="python")
    args = parser.parse_args()
    prefix = [args.adb, "-s", args.serial]

    def adb(*command, check=True, data=None):
        if command[0] == "shell": command = ("shell", shlex.join(command[1:]))
        return subprocess.run(prefix + list(command), input=data, capture_output=True, check=check, timeout=30)

    def shell(*command, **kwargs): return adb("shell", *command, **kwargs).stdout
    def exists(path): return adb("shell", "run-as", PACKAGE, "test", "-e", path, check=False).returncode == 0
    def private(path):
        result = adb("shell", "run-as", PACKAGE, "cat", path, check=False)
        if result.returncode not in (0, 1): raise RuntimeError("Cannot inspect emulator metadata")
        return result.stdout if result.returncode == 0 else None

    if shell("getprop", "ro.kernel.qemu").strip() != b"1": parser.error("requires an emulator")
    paths = shell("pm", "path", PACKAGE).decode().splitlines()
    if len(paths) != 1 or not paths[0].startswith("package:"): parser.error("requires one installed debug APK")
    with args.apk.open("rb") as stream: expected_apk = hashlib.file_digest(stream, "sha256").hexdigest()
    if shell("sha256sum", paths[0][8:]).decode().split()[0] != expected_apk:
        parser.error("installed APK differs from supplied APK")
    with zipfile.ZipFile(args.apk) as apk:
        if args.resource == "python":
            revisions = []
            for asset in ("python-home.zip", "python-source.zip"):
                with apk.open("assets/" + asset) as stream:
                    revisions.append(hashlib.file_digest(stream, "sha256").hexdigest())
            with zipfile.ZipFile(io.BytesIO(apk.read("assets/python-source.zip"))) as source:
                expected_files = {"source/" + name: hashlib.sha256(source.read(name)).hexdigest() for name in (
                    "tools/installer/setup.py", "tools/recomp/recomp.py", "tools/android/ondevice_setup.py")}
            generation = "no_backup/embedded-python/" + "".join(revisions)
            observed_file = "home/lib/python3.14/__future__.py"
        else:
            toolchain = json.loads(apk.read("assets/toolchain-apk.json"))
            identity = toolchain["compiler_identity"]
            if not re.fullmatch(r"[0-9a-f]{64}", identity): parser.error("invalid compiler identity")
            data = apk.read("assets/toolchain-data.zip")
            if hashlib.sha256(data).hexdigest() != toolchain["data_sha256"]:
                parser.error("compiler data checksum mismatch")
            with zipfile.ZipFile(io.BytesIO(data)) as bundle:
                files = [n for n in bundle.namelist() if not n.endswith("/")]
                observed_file = files[0]
                samples = [files[0], "sysroot/usr/include/android/api-level.h", files[-1]]
                expected_files = {name: hashlib.sha256(bundle.read(name)).hexdigest() for name in samples}
            generation = "no_backup/toolchains/" + identity
    pending = generation + ".pending"
    backup = generation + ".probe-backup-" + uuid.uuid4().hex
    metadata = {name: private("no_backup/ondevice/" + name) for name in ("setup.json", "active.json", "previous.json")}
    report = {"passed": False, "scope": "manual service pause during real resource unpacking", "resource": args.resource, "apk_sha256": expected_apk}
    preserved = False
    owned = False
    had_generation = False
    original_marker = None
    job = None
    fixture_marker = private("no_backup/setup-service-fixture.json")

    def control(mode):
        shell("run-as", PACKAGE, "rm", "-f", "files/setup-service-smoke.json")
        shell("am", "start", "-W", "--activity-clear-top", "-n",
              PACKAGE + "/.SetupServiceSmokeActivity", "--es", "mode", mode)
        value = json.loads(private("files/setup-service-smoke.json"))
        if not value.get("passed"): raise AssertionError(value)
        return value

    def host_until(state, timeout=180):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = private(job + "/host.json")
            value = json.loads(raw) if raw else {}
            if value.get("state") == state: return value
            if value.get("state") == "failed": raise AssertionError(value)
            time.sleep(.1)
        raise AssertionError("Service did not reach " + state)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        shell("am", "force-stop", PACKAGE)
        if exists(pending): raise AssertionError("Pre-existing incomplete resources; run normal recovery first")
        had_generation = exists(generation)
        original_marker = private(generation + "/complete") if had_generation else None
        if had_generation and original_marker != b"\x01":
            raise AssertionError("Pre-existing invalid resource cache; run normal recovery first")
        owned = True
        if had_generation:
            shell("run-as", PACKAGE, "mv", generation, backup)
            preserved = True
        identity = control("create")["job"]
        if not re.fullmatch(r"[0-9a-f]{32}", identity): raise AssertionError("Invalid fixture identity")
        job = "no_backup/ondevice/jobs/" + identity
        host_until("paused")
        control("stop")
        deadline = time.monotonic() + 10
        while b".SetupService}" in shell("dumpsys", "activity", "services", PACKAGE):
            if time.monotonic() >= deadline: raise AssertionError("Base service did not stop")
            time.sleep(.1)
        control("synthetic-recovery")
        control("resume")
        deadline = time.monotonic() + 90
        while not exists(pending + "/" + observed_file):
            if time.monotonic() >= deadline:
                raise AssertionError("Did not observe unpacking before completion")
            time.sleep(.1)
        if exists(pending + "/complete") or exists(generation):
            raise AssertionError("Probe missed incomplete preparation")
        pid = shell("pidof", PACKAGE + ":setup").strip()
        if not pid.isdigit(): raise AssertionError("Cannot identify live setup process")
        started = time.monotonic()
        control("pause")
        paused = host_until("paused", timeout=15)
        report["pause_seconds"] = time.monotonic() - started
        if paused.get("reason") != "manual": raise AssertionError(paused)
        if shell("pidof", PACKAGE + ":setup").strip() != pid:
            raise AssertionError("Pause restarted or terminated the setup process")
        if exists(pending) or exists(generation):
            raise AssertionError("Paused resource stage retained incomplete or published data")
        power = shell("dumpsys", "power").decode()
        locks = power.split("Wake Locks:", 1)[1].split("Suspend Blockers:", 1)[0]
        if "wwhd:setup" in locks: raise AssertionError("Paused service retains wake lock")
        report.update(paused_setup_pid=int(pid), incomplete_tree_removed=True, wake_released=True)
        started = time.monotonic()
        control("resume")
        host_until("complete")
        if private(generation + "/complete") != b"\x01" or exists(pending):
            raise AssertionError("Resume did not publish a complete generation")
        ready = json.loads(private(job + "/ready.json"))
        active = json.loads(private("no_backup/ondevice/active.json"))
        if not ready["library"].endswith("/" + active["library"]):
            raise AssertionError("Resumed build was not activated")
        metrics = json.loads(private(job + "/state.json"))
        if metrics.get("state") != "ready_to_activate": raise AssertionError(metrics)
        report["resumed_build_activated"] = True
        for name, expected in expected_files.items():
            actual = shell("run-as", PACKAGE, "sha256sum", generation + "/" + name).decode().split()[0]
            if actual != expected: raise AssertionError("Recovered bundled file bytes differ")
        report.update(passed=True, retry_seconds=time.monotonic() - started,
                      bundled_file_samples_verified=len(expected_files))
    except Exception as failure:
        report["error"] = str(failure)
        raise
    finally:
        try:
            shell("am", "force-stop", PACKAGE)
            if owned: shell("run-as", PACKAGE, "rm", "-rf", pending)
            if preserved or exists(backup):
                shell("run-as", PACKAGE, "rm", "-rf", generation)
                shell("run-as", PACKAGE, "mv", backup, generation)
            elif owned and not had_generation:
                shell("run-as", PACKAGE, "rm", "-rf", generation)
            for name, data in metadata.items():
                path = "no_backup/ondevice/" + name
                if data is None: shell("run-as", PACKAGE, "rm", "-f", path)
                else: shell("run-as", PACKAGE, "sh", "-c", "cat > " + shlex.quote(path), data=data)
            if fixture_marker is None:
                shell("run-as", PACKAGE, "rm", "-f", "no_backup/setup-service-fixture.json")
            else:
                shell("run-as", PACKAGE, "sh", "-c", "cat > no_backup/setup-service-fixture.json", data=fixture_marker)
            if job is not None: shell("run-as", PACKAGE, "rm", "-rf", job)
            report["metadata_preserved"] = all(private("no_backup/ondevice/" + name) == value for name, value in metadata.items()) and private("no_backup/setup-service-fixture.json") == fixture_marker
            report["original_cache_restored"] = not had_generation or (
                exists(generation) and private(generation + "/complete") == original_marker and not exists(backup))
            if not report["metadata_preserved"] or not report["original_cache_restored"]:
                raise AssertionError("Resource probe did not restore prior state")
        except Exception as failure:
            report.update(passed=False, cleanup_error=str(failure))
            raise
        finally: args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__": main()
