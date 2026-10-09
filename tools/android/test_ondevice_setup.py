"""Real shared translation/host compilation through the durable job runner."""
import ctypes
import fcntl
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from ondevice_setup import run, process_resources
from setup_adapter import Adapter, digest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("job_fixture", ROOT / "tools/recomp/android_fixture.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class SetupJobTests(unittest.TestCase):
    def setUp(self):
        if not shutil.which("clang") or not shutil.which("ar"):
            self.skipTest("host clang/ar required")
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.package = self.root / "package"
        for name in ("tools/recomp", "runtime/include"):
            destination = "sdk/include" if name == "runtime/include" else name
            shutil.copytree(ROOT / name, self.package / destination)
        (self.package / "tools/installer").mkdir()
        shutil.copyfile(ROOT / "tools/installer/setup.py", self.package / "tools/installer/setup.py")
        shutil.copyfile(ROOT / "tools/rpx.py", self.package / "tools/rpx.py")
        link = (["-shared", "-Wl,-all_load", "{gamecode}", "-o", "{out}"] if sys.platform == "darwin" else
                ["-shared", "-Wl,--whole-archive", "{gamecode}", "-Wl,--no-whole-archive", "-o", "{out}"])
        manifest = {"gamecode_cflags": ["-std=c11", "-fPIC", "-I{gen}", "-I{sdk}/include"], "link": link}
        (self.package / "sdk/manifest.json").write_text(json.dumps(manifest))
        self.job = self.root / "job"
        self.game = self.job / "input/game"
        (self.game / "code").mkdir(parents=True)
        (self.game / "content").mkdir()
        (self.game / "meta").mkdir()
        (self.game / "meta/meta.xml").write_text("<menu/>")
        (self.game / "code/cking.rpx").write_bytes(fixture.synthetic_rpx())
        self.configuration = self.job / "job.json"
        self.value = {"schema": 1, "jobs": 2, "port_revision": "fixture-v1",
                      "input": {"kind": "folder", "path": "input/game"},
                      "compiler": {"identity": "authored-host-compiler", "cc": [shutil.which("clang")],
                                   "cxx": [shutil.which("clang++") or shutil.which("clang")],
                                   "ar": [shutil.which("ar")], "rsp": False}}

    def adapter(self, *args):
        adapter = Adapter(*args)
        adapter.setup.SUPPORTED_RPX_SHA256 = digest(self.game / "code/cking.rpx")
        original = adapter.setup.recompile
        def recompile(game, generated):
            count = original(game, generated)
            (Path(generated) / "code_harness.c").write_text(
                fixture.HARNESS.replace("int main(void)", "int synthetic_check(void)"))
            return count + 1
        adapter.setup.recompile = recompile
        return adapter

    def native(self, command, env=None, cancel=lambda: False):
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return result.returncode, result.stdout

    def execute(self, native=None):
        self.configuration.write_text(json.dumps(self.value))
        return run(self.configuration, self.package, native or self.native, self.adapter)

    def state(self):
        return json.loads((self.job / "state.json").read_text())

    def test_full_job_loads_result_and_reuses_after_restart(self):
        first = self.execute()
        self.assertEqual(ctypes.CDLL(first["library"]).synthetic_check(), 0)
        self.assertEqual(first["game"], str(self.game))
        state = self.state()
        self.assertEqual(state["state"], "ready_to_activate")
        self.assertGreater(state["resources_latest"]["worker_peak_rss_bytes"], 0)
        self.assertGreater(state["resources_latest"]["largest_reaped_child_peak_rss_bytes"], 0)
        self.assertGreaterEqual(state["resources_latest"]["reaped_children_cpu_seconds"],
                                state["resources_before"]["reaped_children_cpu_seconds"])
        def unexpected(*args, **kwargs): self.fail("Completed job invoked a compiler")
        self.assertEqual(first, self.execute(unexpected))

    def test_pause_and_failed_update_preserve_last_ready_candidate(self):
        first = self.execute()
        ready = (self.job / "ready.json").read_bytes()
        (self.job / "pause.json").write_text('{"reason":"manual"}')
        self.assertIsNone(self.execute())
        self.assertEqual(self.state()["state"], "paused")
        self.assertEqual(ready, (self.job / "ready.json").read_bytes())
        (self.job / "pause.json").unlink()
        self.value["port_revision"] = "fixture-v2"
        def fail(*args, **kwargs): return 1, b"authored compiler failure"
        with self.assertRaisesRegex(Exception, "authored compiler failure"): self.execute(fail)
        self.assertEqual(self.state()["state"], "failed")
        self.assertEqual(ready, (self.job / "ready.json").read_bytes())
        self.assertEqual(ctypes.CDLL(first["library"]).synthetic_check(), 0)
        second = self.execute()
        self.assertNotEqual(first["library"], second["library"])

    def test_low_space_update_keeps_ready_library_and_retries_after_space_restored(self):
        from types import SimpleNamespace
        from unittest.mock import patch
        first = self.execute()
        ready = (self.job / "ready.json").read_bytes()
        self.value["port_revision"] = "fixture-low-space-update"
        with patch("setup_adapter.shutil.disk_usage", return_value=SimpleNamespace(free=0)):
            with self.assertRaisesRegex(Exception, "1 GiB working reserve"):
                self.execute(lambda *a, **k: self.fail("must not launch a compiler"))
        self.assertEqual(self.state()["state"], "failed")
        self.assertEqual((self.job / "ready.json").read_bytes(), ready)
        self.assertEqual(ctypes.CDLL(first["library"]).synthetic_check(), 0)
        second = self.execute()
        self.assertNotEqual(first["library"], second["library"])
        self.assertEqual(ctypes.CDLL(second["library"]).synthetic_check(), 0)

    def test_rejects_outside_input_and_unsupported_manifest(self):
        self.value["input"]["path"] = "../outside"
        with self.assertRaisesRegex(ValueError, "inside the setup job"): self.execute()
        self.value["schema"] = 99
        with self.assertRaisesRegex(ValueError, "job version"): self.execute()
        self.value["schema"] = 1
        self.value["jobs"] = 3
        with self.assertRaisesRegex(ValueError, "concurrency"): self.execute()

    def test_second_worker_cannot_change_state(self):
        with (self.job / "worker.lock").open("a+b") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            with self.assertRaisesRegex(RuntimeError, "already running"): self.execute()
            self.assertFalse((self.job / "state.json").exists())

    def test_manual_pause_survives_a_fresh_worker(self):
        (self.job / "manual-pause.json").write_text('{"schema":1}')
        self.assertIsNone(self.execute())
        self.assertEqual(self.state()["state"], "paused")
        self.assertFalse((self.job / "ready.json").exists())
        (self.job / "manual-pause.json").unlink()
        self.assertEqual(ctypes.CDLL(self.execute()["library"]).synthetic_check(), 0)

    def test_invalid_private_key_can_be_corrected_in_same_job(self):
        first = self.execute()
        ready = (self.job / "ready.json").read_bytes()
        dump = self.job / "input/dump"
        dump.write_bytes(b"authored synthetic disc input")
        disc = self.job / "input/disc.key"
        disc.write_bytes(b"invalid authored key file")
        (self.job / "input/common.key").write_bytes(b"*" * 16)
        self.value["input"] = {"kind": "image", "path": "input/dump",
                               "disc_key": "input/disc.key", "common_key": "input/common.key"}
        self.value["extractor"] = {"path": "authored-extractor", "identity": "authored-fixture"}
        with self.assertRaisesRegex(ValueError, "Invalid disc key file"): self.execute()
        self.assertEqual(self.state()["state"], "failed")
        self.assertEqual(ready, (self.job / "ready.json").read_bytes())
        self.assertEqual(ctypes.CDLL(first["library"]).synthetic_check(), 0)
        configuration = self.configuration.read_bytes()
        disc.write_bytes(b"+" * 16)
        called = []

        def corrected_adapter(*args):
            adapter = self.adapter(*args)
            def extract(path, kind, extractor, identity, native, keys):
                self.assertEqual(path, dump)
                self.assertEqual(keys.disc, b"+" * 16)
                self.assertEqual(keys.common, b"*" * 16)
                called.append(True)
                return self.game  # Authored extraction boundary; no disc decryption is claimed.
            adapter.extract = extract
            return adapter

        result = run(self.configuration, self.package, self.native, corrected_adapter)
        self.assertEqual(called, [True])
        self.assertEqual(self.configuration.read_bytes(), configuration)
        self.assertEqual(dump.read_bytes(), b"authored synthetic disc input")
        state = self.state()
        self.assertEqual(state["state"], "ready_to_activate")
        self.assertGreater(state["resources_latest"]["worker_peak_rss_bytes"], 0)
        self.assertGreater(state["resources_latest"]["largest_reaped_child_peak_rss_bytes"], 0)
        self.assertGreaterEqual(state["resources_latest"]["reaped_children_cpu_seconds"],
                                state["resources_before"]["reaped_children_cpu_seconds"])
        self.assertEqual(ctypes.CDLL(result["library"]).synthetic_check(), 0)


class ResourceMetricsTests(unittest.TestCase):
    def test_platform_units_and_separate_lifetime_scopes(self):
        from types import SimpleNamespace
        from unittest.mock import patch
        own = SimpleNamespace(ru_maxrss=123, ru_utime=2., ru_stime=3.)
        child = SimpleNamespace(ru_maxrss=456, ru_utime=7., ru_stime=11.)
        for platform, scale in (("linux", 1024), ("android", 1024), ("darwin", 1)):
            with self.subTest(platform=platform), patch("ondevice_setup.sys.platform", platform), \
                    patch("ondevice_setup.resource.getrusage", side_effect=[own, child]):
                measured = process_resources()
            self.assertEqual(measured["worker_peak_rss_bytes"], 123 * scale)
            self.assertEqual(measured["largest_reaped_child_peak_rss_bytes"], 456 * scale)
            self.assertEqual(measured["reaped_children_cpu_seconds"], 18.)
            self.assertIn("not additive", measured["scope"])
