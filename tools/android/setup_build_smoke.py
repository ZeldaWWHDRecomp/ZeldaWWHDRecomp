#!/usr/bin/env python3
"""Run an authored synthetic build through the debug foreground setup service.

Exercises real tools, shared translation and production activation. It does not
establish supported-game setup or automatic successful rebuild after an APK update.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import time
import zipfile

PACKAGE = "org.wwhdrecomp.wwhd"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--reuse-installed", action="store_true", help="reuse only a byte-identical installed APK; avoids staging another compiler APK")
    parser.add_argument("--sample-resources", action="store_true", help="sample app/child PSS/RSS and private storage during the first synthetic build")
    args = parser.parse_args()
    prefix = [args.adb, "-s", args.serial]

    def adb(*command, data=None, check=True):
        if command[0] == "shell": command = ("shell", shlex.join(command[1:]))
        return subprocess.run(prefix + list(command), input=data, check=check,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=45)

    def shell(*command, **kwargs):
        return adb("shell", *command, **kwargs).stdout

    if shell("getprop", "ro.kernel.qemu").strip() != b"1":
        parser.error("requires an emulator")
    with zipfile.ZipFile(args.apk) as apk:
        if "assets/toolchain-apk.json" not in apk.namelist():
            parser.error("requires the own-compiler debug APK")
        import io
        with zipfile.ZipFile(io.BytesIO(apk.read("assets/python-source.zip"))) as sources:
            if "tools/android/service_fixture.py" not in sources.namelist():
                parser.error("requires explicit --service-fixture Python packaging")
        package_metadata = {name: json.loads(apk.read("assets/" + name + ".json"))
                            for name in ("toolchain-apk", "runtime-sdk", "python-build")}
    was_installed = bool(shell("pm", "path", PACKAGE, check=False).strip())
    original = {}
    for name in ("setup.json", "active.json", "previous.json"):
        if not was_installed:
            original[name] = None
            continue
        value = adb("shell", "run-as", PACKAGE, "cat", "no_backup/ondevice/" + name, check=False)
        if value.returncode not in (0, 1): raise RuntimeError("Cannot inspect setup metadata")
        original[name] = value.stdout if value.returncode == 0 else None
    report = {"passed": False, "scope": "debug synthetic full-service build", "observations": []}
    report["package_metadata"] = package_metadata
    report["apk_bytes"] = args.apk.stat().st_size
    report["device"] = {key: shell("getprop", prop).decode().strip() for key, prop in
                        (("api", "ro.build.version.sdk"), ("abi", "ro.product.cpu.abi"),
                         ("fingerprint", "ro.build.fingerprint"), ("model", "ro.product.model"))}
    job = None
    sampler = None

    def control(mode):
        shell("run-as", PACKAGE, "rm", "-f", "files/setup-service-smoke.json")
        shell("am", "start", "-W", "--activity-clear-top", "-n",
              PACKAGE + "/.SetupServiceSmokeActivity", "--es", "mode", mode)
        value = json.loads(shell("run-as", PACKAGE, "cat", "files/setup-service-smoke.json"))
        if not value.get("passed"): raise AssertionError(value)
        return value

    def until(label, predicate, timeout=120):
        deadline = time.monotonic() + timeout
        latest = {}
        while time.monotonic() < deadline:
            try: latest = json.loads(shell("run-as", PACKAGE, "cat", job + "/host.json"))
            except (ValueError, subprocess.CalledProcessError): pass
            if predicate(latest):
                report["observations"].append({"check": label, **latest})
                return latest
            if latest.get("state") == "failed": raise AssertionError(latest)
            time.sleep(.1)
        raise AssertionError(label + " timed out: " + repr(latest))

    try:
        shell("am", "force-stop", PACKAGE)
        # Prevent installation from dispatching an unrelated retained job.
        if was_installed:
            shell("run-as", PACKAGE, "rm", "-f", "no_backup/ondevice/setup.json")
        if not args.reuse_installed: adb("install", "-r", str(args.apk))
        paths = shell("pm", "path", PACKAGE).decode().splitlines()
        if len(paths) != 1 or not paths[0].startswith("package:"):
            raise AssertionError("Expected one installed base APK")
        installed = shell("sha256sum", paths[0][len("package:"):]).decode().split()[0]
        with args.apk.open("rb") as stream:
            expected = hashlib.file_digest(stream, "sha256").hexdigest()
        if installed != expected: raise AssertionError("Installed APK differs; reinstall required")
        report["installed_apk_sha256"] = installed
        installed_dir = str(Path(paths[0][len("package:"):]).parent)
        report["installed_apk_native_allocated_bytes"] = int(shell("du", "-sk", installed_dir).split()[0]) * 1024
        shell("logcat", "-c")
        identity = control("create")["job"]
        if not re.fullmatch(r"[0-9a-f]{32}", identity): raise AssertionError("Invalid fixture job")
        job = "no_backup/ondevice/jobs/" + identity
        until("manual checkpoint before build", lambda s: s.get("state") == "paused")
        control("stop")
        deadline = time.monotonic() + 10
        while b".SetupService}" in shell("dumpsys", "activity", "services", PACKAGE):
            if time.monotonic() >= deadline: raise AssertionError("Base service did not stop")
            time.sleep(.1)
        if args.sample_resources:
            from resource_sampler import ResourceSampler

            configuration = json.loads(shell("run-as", PACKAGE, "cat", job + "/job.json"))
            configuration["debug_resource_probe"] = True
            shell("run-as", PACKAGE, "sh", "-c", "cat > " + shlex.quote(job + "/job.json.resource-pending"),
                  data=(json.dumps(configuration) + "\n").encode())
            shell("run-as", PACKAGE, "mv", job + "/job.json.resource-pending", job + "/job.json")
            report["resource_workload"] = "shared synthetic translation and full runtime link, plus an authored 8192-function compiler-observation unit"
            report["compiler_jobs"] = configuration.get("jobs", 1)

            def sampler_shell(*command):
                return subprocess.run(prefix + ["shell", shlex.join(command)], check=True,
                                      capture_output=True, timeout=5).stdout

            sampler = ResourceSampler(sampler_shell, PACKAGE, job)
            sampler.start()
        started = time.monotonic()
        control("synthetic-build")
        shell("input", "keyevent", "KEYCODE_HOME")
        until("synthetic full-service activation", lambda s: s.get("state") == "complete")
        report["elapsed_seconds"] = time.monotonic() - started
        if sampler is not None:
            report["resource_samples"] = sampler.stop()
            sampler = None
            samples = report["resource_samples"]
            if not samples["complete_native_child_samples"]:
                raise AssertionError("Resource capture missed all complete memory samples containing native children")
        report["worker_metrics"] = json.loads(shell("run-as", PACKAGE, "cat", job + "/state.json"))
        resources = report["worker_metrics"]["resources_latest"]
        if min(resources["worker_peak_rss_bytes"], resources["largest_reaped_child_peak_rss_bytes"]) <= 0:
            raise AssertionError("Missing measured worker/native-child memory")
        active = json.loads(shell("run-as", PACKAGE, "cat", "no_backup/ondevice/active.json"))
        ready = json.loads(shell("run-as", PACKAGE, "cat", job + "/ready.json"))
        if "/jobs/" + identity + "/" not in ready["library"]:
            raise AssertionError("Candidate belongs to another job")
        if not ready["library"].endswith("/" + active["library"]):
            raise AssertionError("Service did not activate its compiled library")
        if not ready["game"].endswith("/" + active["game"]):
            raise AssertionError("Service did not pair its private assets")
        writable = adb("shell", "run-as", PACKAGE, "test", "-w",
                       "no_backup/ondevice/" + active["library"], check=False)
        if writable.returncode != 1:
            raise AssertionError("Activated library must be read-only")
        library_path = "no_backup/ondevice/" + active["library"]
        modification = shell("run-as", PACKAGE, "stat", "-c", "%Y", library_path)
        # A second service instance must reuse the completed link rather than rewrite it.
        shell("am", "force-stop", PACKAGE)
        time.sleep(1.1)
        resumed = time.monotonic()
        control("synthetic-build")
        until("fresh service reuses completed generation", lambda s:
              s.get("state") == "complete" and s.get("updated", 0) > report["observations"][1]["updated"])
        if json.loads(shell("run-as", PACKAGE, "cat", "no_backup/ondevice/active.json")) != active:
            raise AssertionError("Restart changed the completed selection")
        if shell("run-as", PACKAGE, "stat", "-c", "%Y", library_path) != modification:
            raise AssertionError("Restart rewrote the completed linked library")
        report["resume_seconds"] = time.monotonic() - resumed
        report["active"] = active
        report["ready"] = ready
        report["passed"] = True
    except Exception as failure:
        report["error"] = str(failure)
        raise
    finally:
        if sampler is not None:
            try: report["resource_samples"] = sampler.stop()
            except Exception as failure: report["resource_sampling_error"] = str(failure)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        try:
            args.output.with_suffix(".log").write_bytes(shell("logcat", "-d", "-s",
                "wwhd-setup:V", "python.stderr:V", "AndroidRuntime:E", "*:S"))
            shell("am", "force-stop", PACKAGE)
            if shell("pm", "path", PACKAGE, check=False).strip():
                for name, data in original.items():
                    path = "no_backup/ondevice/" + name
                    if data is None: shell("run-as", PACKAGE, "rm", "-f", path)
                    else: shell("run-as", PACKAGE, "sh", "-c", "cat > " + shlex.quote(path), data=data)
                if job is not None: shell("run-as", PACKAGE, "rm", "-rf", job)
        except Exception as failure:
            report.update(passed=False, cleanup_error=str(failure))
            raise
        finally: args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"passed": report["passed"], "elapsed_seconds": report.get("elapsed_seconds")}))


if __name__ == "__main__":
    main()
