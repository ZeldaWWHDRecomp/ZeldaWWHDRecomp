#!/usr/bin/env python3
"""Interrupt real Python/compiler-data unpacking and verify fresh retry.

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
    parser.add_argument("--damage-complete", action="store_true", help="corrupt a completed cache while retaining its marker, then require repair")
    parser.add_argument("--obsolete-generations", action="store_true", help="seed obsolete resource copies and require cleanup while reusing a healthy current cache")
    args = parser.parse_args()
    if args.damage_complete and args.obsolete_generations:
        parser.error("choose cache damage or obsolete-generation cleanup")
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
            with zipfile.ZipFile(io.BytesIO(apk.read("assets/python-home.zip"))) as home:
                expected_files[observed_file] = hashlib.sha256(home.read(observed_file.removeprefix("home/"))).hexdigest()
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
    report = {"passed": False, "scope": "completed resource-cache damage and repair before use" if args.damage_complete else "process death during real resource unpacking", "resource": args.resource, "apk_sha256": expected_apk}
    preserved = False
    owned = False
    had_generation = False
    original_marker = None
    fixture_output = None
    fixture_outputs = []
    obsolete = []
    unrecognized = None
    marker_stat = None
    preserved_install_files = {}
    for selection in (metadata["active.json"], metadata["previous.json"]):
        if selection is None: continue
        value = json.loads(selection)
        for key, suffix in (("library", ""), ("game", "/code/cking.rpx")):
            relative = value.get(key)
            if not isinstance(relative, str) or relative.startswith("/") or ".." in relative.split("/"):
                raise AssertionError("Cannot safely inspect existing install reference")
            path = "no_backup/ondevice/" + relative + suffix
            preserved_install_files[path] = shell("run-as", PACKAGE, "sha256sum", path).split()[0]
    save_probe = None
    save_bytes = b"authored resource-repair save preservation probe\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        shell("am", "force-stop", PACKAGE)
        if exists(pending): raise AssertionError("Pre-existing incomplete resources; run normal recovery first")
        had_generation = exists(generation)
        original_marker = private(generation + "/complete") if had_generation else None
        if had_generation and original_marker != b"\x01":
            raise AssertionError("Pre-existing invalid resource cache; run normal recovery first")
        if args.obsolete_generations and not had_generation:
            raise AssertionError("Obsolete-generation probe requires a prepared matching cache")
        owned = True
        save_candidate = "/sdcard/Android/data/" + PACKAGE + "/files/save/resource-probe-" + uuid.uuid4().hex
        shell("mkdir", "-p", save_candidate.rsplit("/", 1)[0])
        shell("sh", "-c", "cat > " + shlex.quote(save_candidate), data=save_bytes)
        save_probe = save_candidate
        if had_generation and not args.obsolete_generations:
            shell("run-as", PACKAGE, "mv", generation, backup)
            preserved = True
        if args.obsolete_generations:
            parent, current = generation.rsplit("/", 1)
            old = parent + "/" + (uuid.uuid4().hex * 4)[:len(current)]
            if exists(old) or exists(old + ".pending"): raise AssertionError("Obsolete probe path collision")
            obsolete = [old, old + ".pending"]
            shell("run-as", PACKAGE, "cp", "-a", generation, old)
            report["obsolete_copy_allocated_bytes"] = int(shell("run-as", PACKAGE, "du", "-sk", old).split()[0]) * 1024
            shell("run-as", PACKAGE, "mkdir", old + ".pending")
            shell("run-as", PACKAGE, "sh", "-c", "cat > " + shlex.quote(old + ".pending/partial"), data=b"authored incomplete resource bytes\n")
            candidate = old + ".probe-keep-" + uuid.uuid4().hex
            if exists(candidate): raise AssertionError("Unrecognized probe path collision")
            unrecognized = candidate
            shell("run-as", PACKAGE, "mkdir", unrecognized)
            shell("run-as", PACKAGE, "sh", "-c", "cat > " + shlex.quote(unrecognized + "/sentinel"), data=save_bytes)
            marker_stat = shell("run-as", PACKAGE, "stat", "-c", "%i:%Y:%s", generation + "/complete")
            report["cache_parent_before_bytes"] = int(shell("run-as", PACKAGE, "du", "-sk", parent).split()[0]) * 1024
            report["scope"] = "obsolete generation and interrupted-tree cleanup after healthy current-cache verification"
        shell("run-as", PACKAGE, "rm", "-f", "files/python-smoke.json")
        if not args.obsolete_generations:
            shell("am", "start", "-W", "-n", PACKAGE + "/.PythonSmokeActivity",
                  "--ez", "compiler", "true" if args.resource == "compiler" else "false")
        if args.damage_complete:
            report["scope"] = "completed resource-cache damage and repair before use"
            deadline = time.monotonic() + 180
            while private("files/python-smoke.json") is None:
                if time.monotonic() >= deadline: raise AssertionError("Initial cache preparation timed out")
                time.sleep(.2)
            initial = json.loads(private("files/python-smoke.json"))
            if not initial.get("passed"): raise AssertionError(initial)
            fixture_outputs.append(initial["output"])
            shell("am", "force-stop", PACKAGE)
            if private(generation + "/complete") != b"\x01": raise AssertionError("Initial cache lacks valid marker")
            damaged = bytearray(private(generation + "/" + observed_file))
            if not damaged: raise AssertionError("Cannot damage an empty resource")
            damaged[len(damaged)//2] ^= 1
            shell("run-as", PACKAGE, "sh", "-c", "cat > " + shlex.quote(generation + "/" + observed_file), data=damaged)
            extra = generation + ("/home/unbundled-resource-probe" if args.resource == "python" else "/unbundled-resource-probe")
            shell("run-as", PACKAGE, "sh", "-c", "cat > " + shlex.quote(extra), data=b"authored unbundled cache file\n")
            if private(generation + "/complete") != b"\x01": raise AssertionError("Damage changed completion marker")
            report.update(completed_cache_damaged=True, valid_marker_retained=True)
        elif not args.obsolete_generations:
            deadline = time.monotonic() + 90
            while not exists(pending + "/" + observed_file):
                if private("files/python-smoke.json") is not None or time.monotonic() >= deadline:
                    raise AssertionError("Did not observe unpacking before completion")
                time.sleep(.1)
            if exists(pending + "/complete") or exists(generation):
                raise AssertionError("Probe missed incomplete preparation")
            pid = shell("pidof", PACKAGE + ":setup").strip()
            if not pid.isdigit(): raise AssertionError("Cannot identify live setup process")
            shell("am", "force-stop", PACKAGE)
            if shell("pidof", PACKAGE + ":setup", check=False).strip():
                raise AssertionError("Interrupted setup process is still live")
            report["terminated_setup_pid"] = int(pid)
            if exists(pending + "/complete") or exists(generation):
                raise AssertionError("Preparation completed before termination")
            poison = pending + "/incomplete-probe-sentinel"
            shell("run-as", PACKAGE, "sh", "-c", "cat > " + shlex.quote(poison), data=b"authored incomplete bytes\n")
            report["interrupted_tree_observed"] = True
        shell("run-as", PACKAGE, "rm", "-f", "files/python-smoke.json")
        started = time.monotonic()
        shell("am", "start", "-W", "-n", PACKAGE + "/.PythonSmokeActivity",
              "--ez", "compiler", "true" if args.resource == "compiler" else "false")
        deadline = time.monotonic() + 180
        while private("files/python-smoke.json") is None:
            if time.monotonic() >= deadline: raise AssertionError("Resource retry timed out")
            time.sleep(.2)
        result = json.loads(private("files/python-smoke.json"))
        if not result.get("passed"): raise AssertionError(result)
        fixture_output = result["output"]
        fixture_outputs.append(fixture_output)
        if private(generation + "/complete") != b"\x01" or exists(pending) or exists(generation + "/incomplete-probe-sentinel"):
            raise AssertionError("Retry accepted or retained incomplete resources")
        if args.damage_complete and exists(extra): raise AssertionError("Repair retained unbundled cache data")
        if args.obsolete_generations:
            if any(exists(path) for path in obsolete): raise AssertionError("Obsolete generation or partial tree retained")
            if private(unrecognized + "/sentinel") != save_bytes: raise AssertionError("Unrecognized sibling was removed")
            if shell("run-as", PACKAGE, "stat", "-c", "%i:%Y:%s", generation + "/complete") != marker_stat:
                raise AssertionError("Cleanup republished the healthy current cache")
            report["cache_parent_after_bytes"] = int(shell("run-as", PACKAGE, "du", "-sk", parent).split()[0]) * 1024
            report["cache_allocation_reclaimed_bytes"] = report["cache_parent_before_bytes"] - report["cache_parent_after_bytes"]
            if report["cache_allocation_reclaimed_bytes"] < report["obsolete_copy_allocated_bytes"]:
                raise AssertionError("Cleanup did not reclaim the obsolete cache allocation")
            report["obsolete_generations_removed"] = True
            report["healthy_cache_marker_unchanged"] = True
            report["unrecognized_sibling_preserved"] = True
        for name, expected in expected_files.items():
            actual = shell("run-as", PACKAGE, "sha256sum", generation + "/" + name).decode().split()[0]
            if actual != expected: raise AssertionError("Recovered bundled file bytes differ")
        if args.resource == "compiler":
            metrics = json.loads(private(fixture_output + "/metrics.json"))
            if not metrics.get("compiler_load_verified") or not metrics.get("full_runtime_load_verified"):
                raise AssertionError("Recovered compiler did not compile/link/load the full runtime fixture")
            report["recovered_compiler_build_load_verified"] = True
        report.update(passed=True, retry_seconds=time.monotonic() - started,
                      incomplete_tree_replaced=not (args.damage_complete or args.obsolete_generations), completed_cache_repaired=args.damage_complete,
                      bundled_file_samples_verified=len(expected_files))
    except Exception as failure:
        report["error"] = str(failure)
        raise
    finally:
        try:
            shell("am", "force-stop", PACKAGE)
            if owned: shell("run-as", PACKAGE, "rm", "-rf", pending)
            for path in obsolete: shell("run-as", PACKAGE, "rm", "-rf", path)
            if unrecognized is not None: shell("run-as", PACKAGE, "rm", "-rf", unrecognized)
            if preserved or exists(backup):
                shell("run-as", PACKAGE, "rm", "-rf", generation)
                shell("run-as", PACKAGE, "mv", backup, generation)
            elif owned and not had_generation:
                shell("run-as", PACKAGE, "rm", "-rf", generation)
            for fixture_output in fixture_outputs:
                if re.fullmatch(r"/data/(?:data|user/\d+)/org\.wwhdrecomp\.wwhd/no_backup/ondevice/python-fixture-\d+", fixture_output):
                    shell("run-as", PACKAGE, "rm", "-rf", fixture_output)
            report["install_files_checked"] = len(preserved_install_files)
            report["install_files_preserved"] = all(
                shell("run-as", PACKAGE, "sha256sum", path).split()[0] == digest
                for path, digest in preserved_install_files.items())
            report["authored_save_preserved"] = save_probe is None or shell("cat", save_probe) == save_bytes
            if save_probe: shell("rm", "-f", save_probe)
            report["metadata_preserved"] = all(private("no_backup/ondevice/" + name) == value for name, value in metadata.items())
            report["original_cache_restored"] = not had_generation or (
                exists(generation) and private(generation + "/complete") == original_marker and not exists(backup))
            if not report["metadata_preserved"] or not report["original_cache_restored"] or not report["install_files_preserved"] or not report["authored_save_preserved"]:
                raise AssertionError("Resource probe did not restore prior state")
        except Exception as failure:
            report.update(passed=False, cleanup_error=str(failure))
            raise
        finally: args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__": main()
