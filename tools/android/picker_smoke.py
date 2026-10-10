#!/usr/bin/env python3
"""Select authored inputs through real DocumentsUI and check durable read grants."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import time
import uuid
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", default="emulator-5554")
    parser.add_argument("--key-correction-only", action="store_true", help="run the imported-image key correction case without repeating access-recovery cases")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reboot", action="store_true", help="Also verify grants after a real emulator reboot")
    parser.add_argument("--grant-settle-seconds", type=float, default=12,
                        help="Allow Android's delayed URI-grant disk write before reboot; use 0 to probe immediate reboot")
    args = parser.parse_args()
    if not 0 <= args.grant_settle_seconds <= 60: parser.error("Grant settling time must be between 0 and 60 seconds")
    package = "org.wwhdrecomp.wwhd"
    fixture = "wwhd-picker-fixture-" + uuid.uuid4().hex
    prefix = [args.adb, "-s", args.serial]
    pointer = "no_backup/ondevice/setup.json"
    ui_path = "/data/local/tmp/wwhd-picker-smoke.xml"
    jobs = set()
    observations = []

    def adb(*command, check=True, data=None):
        if command[0] == "shell": command = ("shell", shlex.join(command[1:]))
        return subprocess.run(prefix + list(command), check=check, input=data, capture_output=True, timeout=30)

    def shell(*command, **kwargs):
        return adb("shell", *command, **kwargs)

    def private(path):
        result = shell("run-as", package, "cat", path, check=False)
        return result.stdout if result.returncode == 0 else None

    def ui():
        deadline = time.monotonic() + 15
        while True:
            shell("rm", "-f", ui_path)
            result = shell("uiautomator", "dump", ui_path)
            document = shell("cat", ui_path, check=False)
            if document.returncode == 0:
                return list(ET.fromstring(document.stdout).iter("node"))
            detail = (result.stdout + result.stderr).decode(errors="replace")
            # Window transitions just after boot may temporarily have no
            # accessibility root. Retry that observed condition, never old XML.
            if "null root node" not in detail or time.monotonic() >= deadline:
                raise AssertionError("Cannot obtain fresh UI hierarchy: " + detail)
            time.sleep(.2)

    def find(label, attribute="text", nodes=None, resource=None):
        return next((node for node in (ui() if nodes is None else nodes)
                     if node.get(attribute) == label and (resource is None or node.get("resource-id") == resource)), None)

    def tap(label, attribute="text", resource=None, scroll=False):
        deadline = time.monotonic() + 20
        nodes = []
        swipes = 0
        while time.monotonic() < deadline:
            nodes = ui()
            node = find(label, attribute, nodes=nodes, resource=resource)
            if node is not None:
                if node.get("enabled") != "true": raise AssertionError("Disabled control: " + label)
                x1, y1, x2, y2 = map(int, re.findall(r"\d+", node.get("bounds")))
                shell("input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))
                return
            if scroll and swipes < 4:
                containers = []
                for candidate in nodes:
                    if candidate.get("scrollable") != "true": continue
                    x1, y1, x2, y2 = map(int, re.findall(r"\d+", candidate.get("bounds")))
                    if y2 - y1 > 300 and x2 - x1 > 150:
                        containers.append((x1, y1, x2, y2))
                if containers:
                    x1, y1, x2, y2 = max(containers, key=lambda b: (b[2] - b[0]) * (b[3] - b[1]))
                    x = str((x1 + x2) // 2)
                    shell("input", "swipe", x, str(y2 - (y2-y1)//8), x, str(y1 + (y2-y1)//4), "250")
                    swipes += 1
        raise AssertionError("Missing control: " + label + "; visible controls: " + repr([
            (n.get("text"), n.get("resource-id")) for n in nodes if n.get("text")]))

    def start(component, *extra):
        deadline = time.monotonic() + 20
        while True:
            result = shell("am", "start", "--activity-clear-top", "-n", package + "/." + component, *extra, check=False)
            if result.returncode == 0: return
            if time.monotonic() >= deadline:
                raise AssertionError("Cannot open " + component + ": " + (result.stdout + result.stderr).decode(errors="replace"))
            time.sleep(0.5)

    def home():
        start("SetupActivity")
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            resumed = shell("dumpsys", "activity", "activities").stdout.decode()
            if any("/.SetupActivity" in line and "topResumedActivity=" in line for line in resumed.splitlines()): return
            time.sleep(0.1)
        raise AssertionError("Setup screen did not resume")

    def open_picker(button):
        tap(button)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            nodes = ui()
            if find("Show roots", "content-desc", nodes=nodes) is not None: return
            # A tap during a task/window transition can be discarded. Retry
            # only while the observed setup screen still owns the control.
            if not any("documentsui" in node.get("package", "") for node in nodes) and find(button, nodes=nodes) is not None:
                tap(button)
        raise AssertionError("Picker did not open")

    def choose(button, *parts, folder=False):
        print("Selecting: " + button, flush=True)
        open_picker(button)
        tap("Show roots", "content-desc")
        tap(shell("getprop", "ro.product.model").stdout.decode().strip(), resource="android:id/title")
        tap("Documents", resource="android:id/title")
        tap(fixture, resource="android:id/title")
        for part in parts: tap(part, resource="android:id/title", scroll=True)
        if folder:
            tap("USE THIS FOLDER")
            tap("ALLOW")
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                nodes = ui()
                if not any("documentsui" in node.get("package", "") for node in nodes): break
                # A confirmation tap during the dialog's entrance transition
                # can be discarded. Retry only the observed consent button.
                if find("ALLOW", nodes=nodes) is not None: tap("ALLOW")
            else: raise AssertionError("Folder access confirmation did not close")

    def selected(kind=None, key=None):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            raw = private(pointer)
            if raw:
                identity = json.loads(raw)["job"]
                if re.fullmatch(r"[0-9a-f]{32}", identity):
                    source = private("no_backup/ondevice/jobs/" + identity + "/source.json")
                    if source and fixture in source.decode():
                        jobs.add(identity)
                        value = json.loads(source)
                        if (kind is None or value["kind"] == kind) and (key is None or key in value):
                            return identity, value
            time.sleep(0.1)
        raise AssertionError("Picker did not publish the selected input")

    def probe(mode):
        shell("run-as", package, "rm", "-f", "files/picker-smoke.json")
        deadline = time.monotonic() + 20
        while True:
            result = shell("am", "broadcast", "--include-stopped-packages", "-n", package + "/.PickerSmokeReceiver",
                           "--es", "mode", mode, "--es", "fixture", fixture, check=False)
            if result.returncode == 0: break
            if time.monotonic() >= deadline:
                raise AssertionError("Cannot inspect picker grants: " + (result.stdout + result.stderr).decode(errors="replace"))
            time.sleep(0.5)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            raw = private("files/picker-smoke.json")
            if raw:
                result = json.loads(raw)
                if not result.get("passed"): raise AssertionError(result)
                return result
            time.sleep(0.1)
        raise AssertionError("Picker grant observation timed out")

    def wait_text(fragment):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if any(fragment in node.get("text", "") for node in ui()): return
        raise AssertionError("Missing feedback: " + fragment + "; visible text: " + repr([node.get("text", "") for node in ui() if node.get("text")]))

    def preservation(identity):
        root = "no_backup/ondevice/jobs/" + identity
        result = {name: private(root + "/" + name) for name in
                  ("source.json", "input/picker-preservation.bin", "import-checkpoints/picker-preservation.json")}
        if any(value is None for value in result.values()): raise AssertionError("Preservation fixture missing")
        return result

    if shell("getprop", "ro.kernel.qemu").stdout.strip() != b"1":
        raise RuntimeError("This probe runs only on an emulator")
    original = private(pointer)
    active = {name: private("no_backup/ondevice/" + name + ".json") for name in ("active", "previous")}
    report = {"passed": False, "observations": observations, "grant_settle_seconds": args.grant_settle_seconds, "key_correction_only": args.key_correction_only}
    try:
        with tempfile.TemporaryDirectory(prefix="wwhd-picker-") as temporary:
            root = Path(temporary) / fixture
            for directory in ("game/code", "game/content", "game/meta"): (root / directory).mkdir(parents=True)
            for path in ("game/code/cking.rpx", "game/content/asset", "game/meta/meta.xml", "dump.wua", "dump.wux"):
                (root / path).write_bytes(b"authored unsupported synthetic picker input\n")
            for path in ("disc.key", "common.key"): (root / path).write_bytes(b"*" * 16)
            (root / "replacement.key").write_bytes(b"+" * 16)
            shell("mkdir", "-p", "/sdcard/Documents")
            adb("push", str(root), "/sdcard/Documents/" + fixture)
        shell("am", "force-stop", package)
        shell("run-as", package, "rm", "-f", pointer)
        home()
        if not args.key_correction_only:
            choose("Choose extracted game folder", "game", folder=True)
            identity, source = selected()
            if source["kind"] != "folder": raise AssertionError("Wrong folder kind")
            observations.append(probe("verify"))
            shell("am", "force-stop", package)
            home()
            if selected()[0] != identity: raise AssertionError("App restart lost selection")
            observations.append({"after_restart": probe("verify")})
            probe("revoke_dump")
            preserved = preservation(identity)
            wait_text("Dump access is missing")
            choose("Restore access to selected dump", "game", folder=True)
            wait_text("Access restored.")
            if selected()[0] != identity or preservation(identity) != preserved: raise AssertionError("Folder access repair discarded private state")
            observations.append({"folder_access_repaired": probe("verify")})
            home()
            choose("Choose WUA archive", "dump.wua")
            selected(kind="archive")
            observations.append(probe("verify"))
            home()
        choose("Choose WUD / WUX disc image", "dump.wux")
        identity, source = selected(kind="image")
        if source["kind"] != "image": raise AssertionError("Wrong image kind")
        if find("Start / resume / retry setup").get("enabled") != "false": raise AssertionError("Missing keys did not block start")
        choose("Choose disc key file", "disc.key")
        selected(key="disc_uri")
        choose("Choose common key file", "common.key")
        selected(key="common_uri")
        if find("Start / resume / retry setup").get("enabled") != "true": raise AssertionError("Complete input cannot start")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.with_suffix(".png").write_bytes(adb("exec-out", "screencap", "-p").stdout)
        observations.append(probe("verify"))
        if not args.key_correction_only:
            probe("revoke_dump")
            preserved = preservation(identity)
            wait_text("Dump access is missing")
            choose("Restore access to selected dump", "dump.wua")
            wait_text("Choose the same previously selected")
            if selected()[0] != identity or preservation(identity) != preserved: raise AssertionError("Wrong-document repair changed private state")
            choose("Restore access to selected dump", "dump.wux")
            wait_text("Access restored.")
            if selected()[0] != identity or preservation(identity) != preserved: raise AssertionError("Image access repair discarded private state")
            observations.append({"image_access_repaired": probe("verify"), "wrong_document_rejected": True, "private_state_preserved": True})
            probe("revoke_common")
            wait_text("Common key needs access")
            if find("Start / resume / retry setup").get("enabled") != "false": raise AssertionError("Lost key access did not block start")
            choose("Common key needs access — choose again", "common.key")
            wait_text("Common key selected — change")
            if selected()[0] != identity: raise AssertionError("Key access repair replaced the job")
            observations.append({"key_access_repaired": probe("verify")})
            shell("am", "force-stop", package)
            home()
            observations.append({"image_after_restart": probe("verify")})
        if args.reboot:
            # AOSP schedules URI-grant persistence 10 seconds later. Keep the
            # immediate-reboot failure reproducible with an explicit zero delay.
            print("Allowing URI-grant persistence for " + str(args.grant_settle_seconds) + " seconds", flush=True)
            time.sleep(args.grant_settle_seconds)
            print("Rebooting the emulator to verify persisted grants", flush=True)
            before_boot = shell("cat", "/proc/sys/kernel/random/boot_id").stdout.strip()
            # Exercise Android's framework shutdown/reboot path.
            shell("svc", "power", "reboot", check=False)
            deadline = time.monotonic() + 120
            while time.monotonic() < deadline:
                state = adb("get-state", check=False)
                if state.stdout.strip() == b"device":
                    boot = shell("getprop", "sys.boot_completed", check=False)
                    identity_result = shell("cat", "/proc/sys/kernel/random/boot_id", check=False)
                    if (boot.returncode == 0 and boot.stdout.strip() == b"1" and identity_result.returncode == 0
                            and identity_result.stdout.strip() and identity_result.stdout.strip() != before_boot): break
                time.sleep(1)
            else: raise AssertionError("Emulator did not finish rebooting")
            shell("wm", "dismiss-keyguard")
            home()
            if selected(kind="image")[0] != identity: raise AssertionError("Reboot lost input selection")
            ready_deadline = time.monotonic() + 30
            while True:
                try:
                    verified = probe("verify")
                    break
                except AssertionError as failure:
                    if "No root for primary" not in str(failure) or time.monotonic() >= ready_deadline: raise
                    time.sleep(1)
            observations.append({"after_reboot": verified, "kernel_boot_changed": True})
        observations.append({"imported_failed_image": probe("import_failed_image")})
        home()
        wait_text("Private dump retained")
        job_root = "no_backup/ondevice/jobs/" + identity
        before_key_retry = {name: private(job_root + "/" + name) for name in
                            ("source.json", "job.json", "input/dump", "input/common.key", "import-checkpoints/0.json")}
        old_disc = private(job_root + "/input/disc.key")
        if any(value is None for value in before_key_retry.values()) or old_disc != b"*" * 16:
            raise AssertionError("Private image/key correction fixture is incomplete")
        choose("Disc key selected — change", "replacement.key")
        wait_text("Replacement key selected")
        pending = private(job_root + "/replace-disc-key.json")
        if not pending or "replacement.key" not in json.loads(pending)["uri"]:
            raise AssertionError("Picker did not queue correction for the imported job")
        if private(job_root + "/input/disc.key") != old_disc:
            raise AssertionError("UI replaced a private key before the worker checkpoint")
        shell("am", "force-stop", package)
        home()
        if private(job_root + "/replace-disc-key.json") != pending:
            raise AssertionError("App restart lost the pending key correction")
        observations.append({"key_replacement_after_restart": probe("apply_key_replacement")})
        if private(job_root + "/input/disc.key") != b"+" * 16 or private(job_root + "/replace-disc-key.json") is not None:
            raise AssertionError("Worker did not consume the corrected private key")
        if any(private(job_root + "/" + name) != value for name, value in before_key_retry.items()):
            raise AssertionError("Key correction changed private dump, manifest or other key")
        if selected()[0] != identity: raise AssertionError("Key correction selected another job")
        observations.append({"imported_key_corrected_same_job": True,
                             "dump_manifest_and_other_key_unchanged": True})
        home()
        open_picker("Choose WUA archive")
        for attempt in range(6):
            shell("input", "keyevent", "4")
            if find("Choose extracted game folder") is not None: break
        else: raise AssertionError("Back did not cancel the picker")
        if selected()[0] != identity: raise AssertionError("Picker cancellation changed selection")
        observations.append({"cancellation_preserved_selection": True})
        for name, value in active.items():
            if private("no_backup/ondevice/" + name + ".json") != value: raise AssertionError("Picker changed installed build")
        report["passed"] = True
    except Exception as failure:
        report["error"] = str(failure)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        try:
            args.output.with_name(args.output.stem + "-failure.png").write_bytes(adb("exec-out", "screencap", "-p").stdout)
        except Exception as screenshot_error:
            report["failure_screenshot_error"] = str(screenshot_error)
        raise
    finally:
        try:
            grant_cleanup_error = None
            try: report["cleanup"] = probe("cleanup")
            except Exception as failure: grant_cleanup_error = failure
            # Restore private selection/files even if releasing the test grants
            # failed; preserve that failure in the result instead of hiding it.
            shell("am", "force-stop", package)
            raw = private(pointer)
            if raw:
                identity = json.loads(raw)["job"]
                if re.fullmatch(r"[0-9a-f]{32}", identity):
                    source = private("no_backup/ondevice/jobs/" + identity + "/source.json")
                    if source and fixture in source.decode(): jobs.add(identity)
            if original is None: shell("run-as", package, "rm", "-f", pointer)
            else: shell("run-as", package, "sh", "-c", "cat > " + pointer, data=original)
            for identity in jobs: shell("run-as", package, "rm", "-rf", "no_backup/ondevice/jobs/" + identity)
            shell("rm", "-rf", "/sdcard/Documents/" + fixture)
            shell("rm", "-f", ui_path)
            report["settings_restored"] = private(pointer) == original
            if not report["settings_restored"]: raise AssertionError("Original setup selection was not restored")
            if grant_cleanup_error is not None: raise grant_cleanup_error
        except Exception as failure:
            report["passed"] = False
            report["cleanup_error"] = str(failure)
            raise
        finally:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + "\n")
    print("DocumentsUI picker and persisted-grant checks passed")


if __name__ == "__main__":
    main()
