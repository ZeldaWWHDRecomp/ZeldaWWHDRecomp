#!/usr/bin/env python3
"""Prove automatic synthetic SDK rebuild/activation and failed-update preservation.

Installs three own-compiler debug APKs on an emulator. Only the debug service's
authored input adapter differs from release; package recovery, tools, checkpoints
and activation run through production code. Restores metadata and the first APK.
"""
import argparse
import hashlib
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
    parser.add_argument("--initial-apk", type=Path, required=True)
    parser.add_argument("--updated-apk", type=Path, required=True)
    parser.add_argument("--failed-apk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prefix = [args.adb, "-s", args.serial]

    def adb(*command, check=True, data=None):
        if command[0] == "shell": command = ("shell", shlex.join(command[1:]))
        return subprocess.run(prefix + list(command), input=data, check=check, capture_output=True,
                              timeout=120 if command[0] == "install" else 30)

    def shell(*command, **kwargs): return adb("shell", *command, **kwargs).stdout
    def private(path):
        result = adb("shell", "run-as", PACKAGE, "cat", path, check=False)
        if result.returncode not in (0, 1): raise RuntimeError("Cannot inspect fixture metadata")
        return result.stdout if result.returncode == 0 else None

    if shell("getprop", "ro.kernel.qemu").strip() != b"1": parser.error("requires an emulator")
    apks = (args.initial_apk, args.updated_apk, args.failed_apk)
    identities = []
    native_inventory = None
    for revision, path in enumerate(apks, 1):
        with zipfile.ZipFile(path) as apk:
            import io
            if "assets/toolchain-apk.json" not in apk.namelist(): parser.error("requires own-compiler APKs")
            with zipfile.ZipFile(io.BytesIO(apk.read("assets/python-source.zip"))) as sources:
                if "tools/android/service_fixture.py" not in sources.namelist(): parser.error("requires explicit debug service fixture")
                manifest = json.loads(sources.read("sdk/manifest.json"))
                if manifest.get("update_fixture_revision") != revision or manifest.get("update_fixture_fail_link") != (revision == 3):
                    parser.error("requires fixture SDK revisions 1, 2 and failing 3")
            runtime = json.loads(apk.read("assets/runtime-sdk.json"))
            if runtime["identity"] != manifest["identity"]: parser.error("SDK identities differ")
            identity = {"revision": revision, "runtime_identity": runtime["identity"],
                        "object_sha256": manifest["files"]["obj/update_fixture.o"],
                        "pipeline_identity": hashlib.sha256(apk.read("assets/python-source.zip")).hexdigest(),
                        "apk_bytes": path.stat().st_size}
            with path.open("rb") as stream: identity["apk_sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
            identities.append(identity)
            native = {n: hashlib.sha256(apk.read(n)).hexdigest() for n in apk.namelist() if n.startswith("lib/")}
            if native_inventory is not None and native != native_inventory:
                parser.error("fixture APKs must keep installed native dependencies byte-identical")
            native_inventory = native
    if len({item["object_sha256"] for item in identities}) != 3: parser.error("runtime objects did not actually change")
    saved = {name: private(name) for name in ("no_backup/ondevice/setup.json", "no_backup/ondevice/active.json",
             "no_backup/ondevice/previous.json", "no_backup/setup-service-fixture.json")}
    report = {"passed": False, "scope": "automatic authored runtime SDK update via actual APK replacement",
              "apks": identities, "observations": []}
    job = None
    save = "/sdcard/Android/data/" + PACKAGE + "/files/save/update-fixture-" + uuid.uuid4().hex
    save_bytes = b"authored save preservation fixture\n"

    def record(path):
        value = private(path)
        if value is None: raise AssertionError("Missing fixture record: " + path)
        return json.loads(value)

    def control(mode):
        shell("run-as", PACKAGE, "rm", "-f", "files/setup-service-smoke.json")
        shell("am", "start", "-W", "--activity-clear-top", "-n", PACKAGE + "/.SetupServiceSmokeActivity", "--es", "mode", mode)
        result = record("files/setup-service-smoke.json")
        if not result.get("passed"): raise AssertionError(result)
        return result

    def wait(label, predicate, timeout=120):
        deadline, latest = time.monotonic() + timeout, {}
        while time.monotonic() < deadline:
            try: latest = record(job + "/host.json")
            except (ValueError, AssertionError): pass
            if predicate(latest):
                report["observations"].append({"check": label, **latest})
                return latest
            if latest.get("state") == "failed": raise AssertionError(label + ": " + repr(latest))
            time.sleep(.2)
        raise AssertionError(label + " timed out: " + repr(latest))

    def translations():
        paths = shell("run-as", PACKAGE, "find", job + "/checkpoints", "-name", "complete.json").decode().splitlines()
        # Compare complete records as well as emitted bytes: a fresh translation
        # into the same directory must not masquerade as checkpoint reuse.
        return [record(path) for path in sorted(paths) if "/translation-" in path]

    def capture_metrics(label):
        measured = record(job + "/state.json")
        resources = measured["resources_latest"]
        if min(resources["worker_peak_rss_bytes"], resources["largest_reaped_child_peak_rss_bytes"]) <= 0:
            raise AssertionError("Missing measured worker/native-child memory")
        report.setdefault("worker_metrics", {})[label] = measured

    def check_library(active):
        path = "no_backup/ondevice/" + active["library"]
        if adb("shell", "run-as", PACKAGE, "test", "-w", path, check=False).returncode != 1:
            raise AssertionError("Completed runtime must remain read-only")
        if shell("run-as", PACKAGE, "sha256sum", path).decode().split()[0] != active["sha256"]:
            raise AssertionError("Retained runtime bytes changed")

    try:
        shell("am", "force-stop", PACKAGE)
        if shell("pm", "path", PACKAGE, check=False).strip():
            shell("run-as", PACKAGE, "rm", "-f", "no_backup/ondevice/setup.json", "no_backup/setup-service-fixture.json")
        print("Installing initial runtime SDK", flush=True)
        adb("install", "-r", str(apks[0]))
        shell("logcat", "-c")
        identity = control("create")["job"]
        if not re.fullmatch(r"[0-9a-f]{32}", identity): raise AssertionError("Invalid authored job identity")
        job = "no_backup/ondevice/jobs/" + identity
        wait("initial manual pause", lambda s: s.get("state") == "paused")
        control("stop")
        deadline = time.monotonic() + 10
        while b".SetupService}" in shell("dumpsys", "activity", "services", PACKAGE):
            if time.monotonic() >= deadline: raise AssertionError("Initial base service did not stop")
            time.sleep(.1)
        control("synthetic-recovery")
        started = time.monotonic()
        control("resume")  # Same start factory used by launcher and recovery receiver.
        shell("input", "keyevent", "KEYCODE_HOME")
        initial = wait("initial fixture activated", lambda s: s.get("state") == "complete")
        report["initial_seconds"] = time.monotonic() - started
        capture_metrics("initial")
        first = record("no_backup/ondevice/active.json")
        if first["runtime_identity"] != identities[0]["runtime_identity"] or record(job + "/update-fixture-check.json")["revision"] != 1:
            raise AssertionError("Initial runtime SDK was not executed and activated")
        check_library(first)
        generated = translations()
        if len(generated) != 1: raise AssertionError("Expected one initial translation generation")
        shell("mkdir", "-p", save.rsplit("/", 1)[0])
        shell("sh", "-c", "cat > " + shlex.quote(save), data=save_bytes)
        print("Installing changed runtime SDK; waiting for automatic rebuild", flush=True)
        started = time.monotonic()
        adb("install", "-r", str(apks[1]))
        updated = wait("package replacement automatically rebuilt and activated", lambda s:
                       s.get("state") == "complete" and s.get("updated", 0) > initial["updated"] and
                       record("no_backup/ondevice/active.json")["runtime_identity"] == identities[1]["runtime_identity"])
        report["update_seconds_including_install"] = time.monotonic() - started
        capture_metrics("updated")
        second = record("no_backup/ondevice/active.json")
        if second["library"] == first["library"] or second["pipeline_identity"] != identities[1]["pipeline_identity"]:
            raise AssertionError("Updated SDK did not activate a new candidate")
        if record(job + "/update-fixture-check.json")["revision"] != 2:
            raise AssertionError("Updated runtime code was not executed")
        if record("no_backup/ondevice/previous.json") != first: raise AssertionError("Update lost the previous valid build")
        current_translations = translations()
        if len(current_translations) != 1 or current_translations != generated:
            raise AssertionError("SDK-only update failed to reuse identical translated C/header inventory")
        check_library(first)
        check_library(second)
        if shell("cat", save) != save_bytes: raise AssertionError("Update changed saves")
        retained = {name: private(name) for name in ("no_backup/ondevice/active.json", "no_backup/ondevice/previous.json", job + "/ready.json")}
        print("Installing unresolved runtime SDK; waiting for automatic failure", flush=True)
        started = time.monotonic()
        adb("install", "-r", str(apks[2]))
        failed = wait("broken runtime SDK automatically failed without activation", lambda s:
                      s.get("state") == "failed" and s.get("updated", 0) > updated["updated"])
        report["failed_update_seconds_including_install"] = time.monotonic() - started
        capture_metrics("failed_update")
        if "wwhd_update_fixture_missing" not in failed.get("error", ""):
            raise AssertionError("Failure did not come from the deliberately unresolved runtime object")
        if any(private(name) != data for name, data in retained.items()): raise AssertionError("Failed rebuild changed active/previous/ready records")
        if shell("cat", save) != save_bytes: raise AssertionError("Failed rebuild changed saves")
        check_library(first)
        check_library(second)
        shell("run-as", PACKAGE, "rm", "-f", "files/active-runtime-smoke.json")
        shell("am", "start", "-W", "-n", PACKAGE + "/.ActiveRuntimeSmokeActivity")
        deadline = time.monotonic() + 20
        while private("files/active-runtime-smoke.json") is None:
            if time.monotonic() >= deadline: raise AssertionError("Retained runtime load timed out")
            time.sleep(.2)
        loaded = record("files/active-runtime-smoke.json")
        if not loaded.get("passed") or loaded.get("revision") != 2: raise AssertionError(loaded)
        report["observations"].append({"check": "previous runtime still loads and executes after failed update", **loaded})
        if record(job + "/host.json") != failed: raise AssertionError("Failed job automatically retried")
        report.update(passed=True, byte_identical_generated_inventory=True, translation_checkpoint_reused=True, saves_preserved=True,
                      active_previous_ready_preserved_on_failure=True)
    except Exception as failure:
        report["error"] = str(failure)
        raise
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.with_suffix(".log").write_bytes(shell("logcat", "-d", "-s", "wwhd-setup:V", "python.stderr:V", "AndroidRuntime:E", "*:S"))
        try:
            shell("am", "force-stop", PACKAGE)
            if shell("pm", "path", PACKAGE, check=False).strip():
                shell("run-as", PACKAGE, "rm", "-f", "no_backup/ondevice/setup.json", "no_backup/setup-service-fixture.json")
                adb("install", "-r", str(apks[0]))
                shell("am", "force-stop", PACKAGE)
                for name, value in saved.items():
                    if value is None: shell("run-as", PACKAGE, "rm", "-f", name)
                    else: shell("run-as", PACKAGE, "sh", "-c", "cat > " + shlex.quote(name), data=value)
                if job: shell("run-as", PACKAGE, "rm", "-rf", job)
                shell("rm", "-f", save)
            report["metadata_restored"] = all(private(name) == value for name, value in saved.items())
            if not report["metadata_restored"]: raise AssertionError("Original metadata was not restored")
        except Exception as failure:
            report.update(passed=False, cleanup_error=str(failure))
            raise
        finally: args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"passed": report["passed"], "observations": len(report["observations"])}))


if __name__ == "__main__": main()
