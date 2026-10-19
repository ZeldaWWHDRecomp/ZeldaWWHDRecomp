#!/usr/bin/env python3
"""Select authored inputs through real DocumentsUI and check durable read grants."""
import argparse
import json
from pathlib import Path
import re
import shlex
import adb_util
import tempfile
import time
import uuid
from urllib.parse import unquote
import xml.etree.ElementTree as ET


def wait_for_feedback(fragment, read_nodes, expand_details, scroll_forward, rewind,
                      *, clock=time.monotonic, timeout=60):
    """Search the scrollable setup screen, including its collapsed technical record.

    A hierarchy dump can take seconds on the CI emulator. Do not spend the entire
    feedback timeout dumping every page twice and repeatedly returning to the top.
    Inspect each post-scroll snapshot on the next iteration. Two bounded passes
    allow a refresh/window transition without an unbounded scrolling retry loop.
    """
    deadline = clock() + timeout
    seen = []
    for _ in range(2):
        previous = None
        for _ in range(12):
            if clock() >= deadline:
                break
            nodes = read_nodes()
            seen = [node.get("text", "") for node in nodes if node.get("text")]
            if any(fragment in text for text in seen):
                rewind()
                return
            if any(node.get("text") == "Details" for node in nodes):
                expand_details()
                previous = None
                continue
            current = [(node.get("text"), node.get("bounds")) for node in nodes]
            if current == previous or not scroll_forward(nodes):
                break
            previous = current
        if clock() >= deadline:
            break
        rewind()
    raise AssertionError("Missing feedback: " + fragment + "; text on the last screen: " + repr(seen))


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
    adb_util.wait_until_ready(prefix)
    pointer = "no_backup/ondevice/setup.json"
    ui_path = "/data/local/tmp/wwhd-picker-smoke.xml"
    jobs = set()
    observations = []

    def adb(*command, check=True, data=None):
        if command[0] == "shell": command = ("shell", shlex.join(command[1:]))
        return adb_util.run(prefix + list(command), check=check, input=data, capture_output=True, timeout=30)

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

    KINDS = {"folder": "Extracted game folder", "archive": "Cemu archive (.wua)",
             "image": "Disc image (.wud / .wux) with keys"}
    CHOOSE = ("Choose your game", "Choose a different game")
    RESTORE = "Allow access to the chosen game again"
    DISC, COMMON = "Choose the disc key file", "Choose the common key file"

    def start_control():
        nodes = ui()
        return next((node for node in nodes if node.get("text") in ("Start", "Continue")), {"enabled": "false"})

    def find(label, attribute="text", nodes=None, resource=None):
        return next((node for node in (ui() if nodes is None else nodes)
                     if node.get(attribute) == label and (resource is None or node.get("resource-id") == resource)), None)

    def swipe(nodes, forward=True):
        """Scroll the largest scrollable view one step; False if there is none."""
        containers = []
        for candidate in nodes:
            if candidate.get("scrollable") != "true": continue
            x1, y1, x2, y2 = map(int, re.findall(r"\d+", candidate.get("bounds")))
            if y2 - y1 > 300 and x2 - x1 > 150:
                containers.append((x1, y1, x2, y2))
        if not containers: return False
        x1, y1, x2, y2 = max(containers, key=lambda b: (b[2] - b[0]) * (b[3] - b[1]))
        x = str((x1 + x2) // 2)
        low, high = str(y2 - (y2-y1)//8), str(y1 + (y2-y1)//4)
        shell("input", "swipe", x, *((low, x, high) if forward else (high, x, low)), "250")
        return True

    def screen(nodes):
        return [(node.get("text"), node.get("bounds")) for node in nodes]

    def to_top():
        """The setup screen scrolled back to its start, where the smoke's controls are."""
        nodes = ui()
        for _ in range(8):
            if not swipe(nodes, forward=False): return
            after = ui()
            if screen(after) == screen(nodes): return
            nodes = after
        raise AssertionError("Setup screen did not scroll back to its start")

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
            if scroll and swipes < 4 and swipe(nodes):
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
        if button in KINDS.values():
            # the kind menu: the choose button, then the kind
            nodes = ui()
            chooser = next(label for label in CHOOSE if find(label, nodes=nodes) is not None)
            tap(chooser)
            tap(button)
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if find("Show roots", "content-desc") is not None: return
            raise AssertionError("Picker did not open")
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

    def selected(kind=None, key=None, document=None):
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
                        # Picker results arrive asynchronously. An earlier image job from
                        # this fixture must not satisfy a wait for a different document.
                        matches_document = document is None or unquote(value.get("uri", "")).endswith(
                            "/" + fixture + "/" + document)
                        if matches_document and (kind is None or value["kind"] == kind) and (key is None or key in value):
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
        wait_for_feedback(fragment, ui, lambda: tap("Details"), swipe, to_top)

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
            for path in ("game/code/cking.rpx", "game/content/asset", "game/meta/meta.xml", "dump.wua", "dump.wux", "dump-multikey.wux"):
                (root / path).write_bytes(b"authored unsupported synthetic picker input\n")
            for path in ("disc.key", "common.key"): (root / path).write_bytes(b"*" * 16)
            (root / "replacement.key").write_bytes(b"+" * 16)
            # Authored Cemu-style list, deliberately distinct from the raw single-key files.
            (root / "keys.txt").write_text("01" * 16 + "\n" + "02" * 16 + "\n")
            shell("mkdir", "-p", "/sdcard/Documents")
            adb("push", str(root), "/sdcard/Documents/" + fixture)
        shell("am", "force-stop", package)
        shell("run-as", package, "rm", "-f", pointer)
        home()
        if not args.key_correction_only:
            choose(KINDS["folder"], "game", folder=True)
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
            choose(RESTORE, "game", folder=True)
            wait_text("Access restored.")
            if selected()[0] != identity or preservation(identity) != preserved: raise AssertionError("Folder access repair discarded private state")
            observations.append({"folder_access_repaired": probe("verify")})
            home()
            choose(KINDS["archive"], "dump.wua")
            selected(kind="archive")
            observations.append(probe("verify"))
            home()
        choose(KINDS["image"], "dump-multikey.wux")
        selected(kind="image", document="dump-multikey.wux")
        choose(DISC, "keys.txt")
        _, multi_source = selected(key="common_uri", document="dump-multikey.wux")
        if multi_source.get("disc_uri") != multi_source["common_uri"]:
            raise AssertionError("Multi-key document did not supply both roles")
        if start_control().get("enabled") != "true":
            raise AssertionError("Multi-key document did not enable start")
        observations.append({"multikey_document_supplied_both_roles": True, **probe("verify")})
        home()
        choose(KINDS["image"], "dump.wux")
        identity, source = selected(kind="image", document="dump.wux")
        if any(key + "_uri" in source for key in ("disc", "common")):
            raise AssertionError("Independent single-key image inherited a key selection")
        if source["kind"] != "image": raise AssertionError("Wrong image kind")
        if start_control().get("enabled") != "false": raise AssertionError("Missing keys did not block start")
        choose(DISC, "disc.key")
        _, disc_source = selected(key="disc_uri")
        if "common_uri" in disc_source:
            raise AssertionError("Disc-only file also selected the common key")
        if start_control().get("enabled") != "false":
            raise AssertionError("Disc-only file enabled start without a common key")
        if find(COMMON) is None:
            raise AssertionError("Disc-only file hid the common-key prompt")
        shell("am", "force-stop", package)
        home()
        _, restarted_source = selected(key="disc_uri")
        if "common_uri" in restarted_source or start_control().get("enabled") != "false" or find(COMMON) is None:
            raise AssertionError("Restart lost the separate common-key requirement")
        observations.append({"disc_only_requires_common_before_and_after_restart": True,
                             "single_key_image": "dump.wux", "multi_key_image": "dump-multikey.wux"})
        choose(COMMON, "common.key")
        selected(key="common_uri")
        if start_control().get("enabled") != "true": raise AssertionError("Complete input cannot start")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.with_suffix(".png").write_bytes(adb("exec-out", "screencap", "-p").stdout)
        observations.append(probe("verify"))
        if not args.key_correction_only:
            probe("revoke_dump")
            preserved = preservation(identity)
            wait_text("Dump access is missing")
            choose(RESTORE, "dump.wua")
            wait_text("Choose the same previously selected")
            if selected()[0] != identity or preservation(identity) != preserved: raise AssertionError("Wrong-document repair changed private state")
            choose(RESTORE, "dump.wux")
            wait_text("Access restored.")
            if selected()[0] != identity or preservation(identity) != preserved: raise AssertionError("Image access repair discarded private state")
            observations.append({"image_access_repaired": probe("verify"), "wrong_document_rejected": True, "private_state_preserved": True})
            probe("revoke_common")
            wait_text("Common key: choose the key again")
            if start_control().get("enabled") != "false": raise AssertionError("Lost key access did not block start")
            choose("Common key: choose the key again", "common.key")
            wait_text("Common key chosen (change)")
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
            adb_util.reboot_and_wait(prefix, previous_boot_id=before_boot)
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
                            ("source.json", "job.json", "input/dump", "import-checkpoints/0.json")}
        def no_private_keys():
            if any(private(job_root + "/input/" + key + ".key") is not None for key in ("disc", "common")):
                raise AssertionError("New job saved private key copies")
            configured = json.loads(private(job_root + "/job.json"))["input"]
            if any(key + "_key" in configured for key in ("disc", "common")):
                raise AssertionError("New job retained private key destinations")
        if any(value is None for value in before_key_retry.values()):
            raise AssertionError("Private image correction fixture is incomplete")
        no_private_keys()
        before_source = json.loads(before_key_retry["source.json"])
        choose("Disc key chosen (change)", "replacement.key")
        wait_text("Replacement key file selected")
        pending = private(job_root + "/replace-disc-key.json")
        if not pending or "replacement.key" not in json.loads(pending)["uri"]:
            raise AssertionError("Picker did not queue correction for the imported job")
        no_private_keys()
        if private(job_root + "/source.json") != before_key_retry["source.json"]:
            raise AssertionError("UI applied replacement before the worker checkpoint")
        shell("am", "force-stop", package)
        home()
        if private(job_root + "/replace-disc-key.json") != pending:
            raise AssertionError("App restart lost the pending key correction")
        observations.append({"key_replacement_after_restart": probe("apply_key_replacement")})
        no_private_keys()
        if private(job_root + "/replace-disc-key.json") is not None:
            raise AssertionError("Worker did not consume the corrected key URI")
        after_source = json.loads(private(job_root + "/source.json"))
        expected_source = {**before_source, "disc_uri": json.loads(pending)["uri"]}
        if after_source != expected_source:
            raise AssertionError("Key correction changed more than the selected key URI")
        if any(private(job_root + "/" + name) != value for name, value in before_key_retry.items() if name != "source.json"):
            raise AssertionError("Key correction changed private dump or manifest")
        if selected()[0] != identity: raise AssertionError("Key correction selected another job")
        observations.append({"imported_key_corrected_same_job": True,
                             "dump_manifest_and_other_key_unchanged": True, "no_private_key_copies": True})
        observations.append({"legacy_keys_seeded": probe("seed_legacy_keys")})
        for key in ("disc", "common"):
            if private(job_root + "/input/" + key + ".key") != b"*" * 16:
                raise AssertionError("Legacy key fixture is incomplete")
        shell("am", "force-stop", package)
        home()
        observations.append({"legacy_keys_resumed": probe("apply_key_replacement")})
        for key in ("disc", "common"):
            if private(job_root + "/input/" + key + ".key") != b"*" * 16:
                raise AssertionError("Interrupted legacy job lost a key")
        observations.append({"legacy_keys_completed": probe("complete_legacy_keys")})
        no_private_keys()
        home()
        open_picker(KINDS["archive"])
        for attempt in range(6):
            shell("input", "keyevent", "4")
            nodes = ui()
            if any(find(label, nodes=nodes) is not None for label in CHOOSE): break
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
