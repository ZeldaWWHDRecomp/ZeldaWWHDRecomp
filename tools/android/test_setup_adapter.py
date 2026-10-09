"""Game-free adapter checks using the production installer and recompiler."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

from setup_adapter import Adapter, Paused, atomic_json, digest, inventory

PACKAGE = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("android_fixture", PACKAGE / "tools/recomp/android_fixture.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class ChecksumPauseTests(unittest.TestCase):
    def test_pause_between_bounded_reads_closes_input(self):
        import io
        class Source(io.BytesIO):
            bytes_read = 0
            def read(self, size=-1):
                self.assert_bounded(size)
                block = super().read(size)
                self.bytes_read += len(block)
                return block
            @staticmethod
            def assert_bounded(size):
                if not 0 < size <= 1024 * 1024:
                    raise AssertionError("checksum read must be bounded")
        source = Source(b"x" * (3 * 1024 * 1024))
        def checkpoint():
            if source.bytes_read:
                raise Paused()
        with patch("setup_adapter.Path.open", return_value=source):
            with self.assertRaises(Paused):
                digest(Path("authored-input"), checkpoint)
        self.assertEqual(source.bytes_read, 1024 * 1024)
        self.assertTrue(source.closed)


class AtomicRecordTests(unittest.TestCase):
    def test_disk_full_keeps_previous_record_and_removes_temporary_write(self):
        import errno
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "complete.json"
            atomic_json(marker, {"generation": "previous"})
            previous = marker.read_bytes()
            with patch("setup_adapter.os.fsync", side_effect=OSError(errno.ENOSPC, "authored disk-full fault")):
                with self.assertRaises(OSError):
                    atomic_json(marker, {"generation": "replacement"})
            self.assertEqual(marker.read_bytes(), previous)
            self.assertFalse(marker.with_suffix(".tmp").exists())
            atomic_json(marker, {"generation": "replacement"})
            self.assertNotEqual(marker.read_bytes(), previous)


class TranslationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / "game"
        (self.game / "code").mkdir(parents=True)
        self.rpx = self.game / "code/cking.rpx"
        self.rpx.write_bytes(fixture.synthetic_rpx())
        self.events = []

    def adapter(self, revision="port-v1", pause=lambda: False):
        adapter = Adapter(PACKAGE, self.root / "jobs", revision, self.events.append, pause)
        # Only the fixture's test-local installer accepts synthetic game bytes.
        adapter.setup.SUPPORTED_RPX_SHA256 = digest(self.rpx)
        return adapter

    def test_matches_desktop_and_resumes_after_worker_restart(self):
        desktop = self.root / "desktop"
        subprocess.run([sys.executable, str(PACKAGE / "tools/recomp/recomp.py"),
                        str(self.rpx), str(desktop)], check=True, stdout=subprocess.DEVNULL)
        generated = self.adapter().translate(self.game)
        self.assertEqual(inventory(desktop), inventory(generated))
        restarted = self.adapter()
        restarted.setup.recompile = lambda *args: self.fail("completed C must be reused")
        self.assertEqual(generated, restarted.translate(self.game))
        self.assertEqual(self.events[-1]["state"], "reused")

    def test_corruption_and_untracked_files_trigger_translation(self):
        generated = self.adapter().translate(self.game)
        expected = inventory(generated)
        (generated / "code_000.c").write_text("corrupted")
        (generated / "stale.c").write_text("stale")
        self.adapter().translate(self.game)
        self.assertEqual(expected, inventory(generated))
        self.assertEqual(self.events[-1]["state"], "complete")

    def test_runtime_only_port_update_reuses_translation(self):
        previous = self.adapter().translate(self.game)
        updated_adapter = self.adapter("port-v2")
        updated_adapter.setup.recompile = lambda *args: self.fail("runtime-only update must reuse C")
        self.assertEqual(previous, updated_adapter.translate(self.game))
        self.assertEqual(self.events[-1]["state"], "reused")

    def test_changed_hooks_invalidate_and_preserve_previous_translation(self):
        import shutil
        package = self.root / "package"
        shutil.copytree(PACKAGE / "tools", package / "tools",
                        ignore=shutil.ignore_patterns("__pycache__"))
        def adapter():
            value = Adapter(package, self.root / "jobs", "same-port", self.events.append)
            value.setup.SUPPORTED_RPX_SHA256 = digest(self.rpx)
            return value
        previous = adapter().translate(self.game)
        expected = inventory(previous)
        hooks = package / "tools/recomp/hooks_update_test.txt"
        hooks.write_text("02000000\n")
        updated = adapter().translate(self.game)
        self.assertNotEqual(previous, updated)
        self.assertEqual(inventory(previous), expected)
        self.assertNotEqual(inventory(updated), expected)

    def test_low_space_blocks_new_translation_but_allows_verified_reuse(self):
        adapter = self.adapter()
        with patch("setup_adapter.shutil.disk_usage", return_value=SimpleNamespace(free=0)):
            with self.assertRaisesRegex(adapter.setup.SetupError, "1 GiB working reserve"):
                adapter.translate(self.game)
        self.assertFalse(list((self.root / "jobs").rglob("complete.json")))
        generated = self.adapter().translate(self.game)
        with patch("setup_adapter.shutil.disk_usage", return_value=SimpleNamespace(free=0)):
            self.assertEqual(generated, self.adapter().translate(self.game))

    def test_damaged_checkpoint_metadata_is_rebuilt(self):
        generated = self.adapter().translate(self.game)
        expected = inventory(generated)
        (generated.parent / "complete.json").write_text("null")
        self.adapter().translate(self.game)
        self.assertEqual(expected, inventory(generated))
        self.assertEqual(self.events[-1]["state"], "complete")

    def test_pause_inside_unchanged_translator_unwinds_and_retries(self):
        import importlib
        import threading
        import time
        with patch.object(sys, "path", [str(PACKAGE / "tools/recomp"),
                                         str(PACKAGE / "tools")] + sys.path):
            ppc2c = importlib.import_module("ppc2c")
        requested = threading.Event()
        original = ppc2c.translate
        calls = []
        def slow_instruction(*args):
            calls.append(args[0])
            active = [tool for tool in range(6)
                      if sys.monitoring.get_tool(tool) == "wwhd-translation-pause"]
            self.assertEqual(len(active), 1)
            self.assertEqual(sys.monitoring.get_events(active[0]), 0)
            requested.set()
            # Let the policy watcher arm interruption before the next real
            # translator line; no production translator file is modified.
            time.sleep(.2)
            return original(*args)
        adapter = self.adapter(pause=requested.is_set)
        original_runner = adapter.setup.run_logged
        tools_before = [sys.monitoring.get_tool(tool) for tool in range(6)]
        started = time.monotonic()
        with patch.object(ppc2c, "translate", slow_instruction):
            with self.assertRaises(Paused):
                adapter.translate(self.game)
        self.assertLess(time.monotonic() - started, 3)
        self.assertEqual(len(calls), 1)
        self.assertIs(adapter.setup.run_logged, original_runner)
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))
        self.assertFalse(list((self.root / "jobs").rglob("complete.json")))
        self.assertTrue(any(event.get("restart_on_resume") for event in self.events))
        self.assertEqual([sys.monitoring.get_tool(tool) for tool in range(6)], tools_before)
        self.assertFalse(any(thread.name == "android-translation-pause"
                             for thread in threading.enumerate()))
        generated = self.adapter().translate(self.game)
        desktop = self.root / "desktop-after-pause"
        subprocess.run([sys.executable, str(PACKAGE / "tools/recomp/recomp.py"),
                        str(self.rpx), str(desktop)], check=True, stdout=subprocess.DEVNULL)
        self.assertEqual(inventory(generated), inventory(desktop))

    def test_pause_before_work(self):
        with self.assertRaises(Paused):
            self.adapter(pause=lambda: True).translate(self.game)
        self.assertFalse(list((self.root / "jobs").glob("translation-*")))

    def test_pause_after_completion_keeps_checkpoint(self):
        def pause():
            return any(event.get("state") == "complete" for event in self.events)
        with self.assertRaises(Paused):
            self.adapter(pause=pause).translate(self.game)
        restarted = self.adapter()
        restarted.setup.recompile = lambda *args: self.fail("checkpoint should survive pause")
        self.assertTrue(restarted.translate(self.game).is_dir())

    def test_failed_translation_is_retried(self):
        adapter = self.adapter()
        original = adapter.setup.run_logged
        def fail(game, output):
            Path(output).mkdir()
            (Path(output) / "partial.c").write_text("partial")
            raise RuntimeError("simulated process failure")
        adapter.setup.recompile = fail
        with self.assertRaisesRegex(RuntimeError, "simulated"):
            adapter.translate(self.game)
        self.assertIs(adapter.setup.run_logged, original)
        self.assertFalse(list((self.root / "jobs").rglob("complete.json")))
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))
        self.assertTrue(self.adapter().translate(self.game).is_dir())

    def test_unsupported_dump_rejected(self):
        adapter = self.adapter()
        self.rpx.write_bytes(b"wrong dump")
        with self.assertRaises(adapter.setup.SetupError):
            adapter.translate(self.game)


class CompileTests(unittest.TestCase):
    setUp = TranslationTests.setUp
    adapter = TranslationTests.adapter

    def prepare(self):
        import shutil
        from types import SimpleNamespace
        compiler = shutil.which("clang")
        if not compiler:
            self.skipTest("clang is required for native object validation")
        generated = self.adapter().translate(self.game)
        toolchain = SimpleNamespace(cc=[compiler], env=None)
        manifest = {"gamecode_cflags": ["-std=c11", "-I{gen}", "-I" + str(PACKAGE / "runtime/include")]}
        def native(command, env=None, cancel=lambda: False):
            result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            return result.returncode, result.stdout
        return generated, toolchain, manifest, native

    def compile(self, adapter, prepared, native=None, identity="toolchain-v1", **options):
        generated, toolchain, manifest, runner = prepared
        return adapter.compile(toolchain, manifest, generated, identity, native or runner,
                               header_roots=[PACKAGE / "runtime/include"], **options)

    def test_pause_cancels_active_compile_keeps_verified_objects_and_retries(self):
        prepared = self.prepare()
        objects = self.compile(self.adapter(), prepared)
        missing = Path(objects[0])
        missing.unlink()
        retained = {Path(path): Path(path).read_bytes() for path in objects[1:]}
        pause = [False]
        adapter = self.adapter(pause=lambda: pause[0])
        def interrupted(command, env=None, cancel=None):
            target = Path(command[command.index("-o") + 1])
            target.write_bytes(b"authored incomplete object")
            target.with_name("clang-random-intermediate.tmp").write_bytes(b"authored compiler temporary")
            pause[0] = True
            self.assertTrue(cancel())
            # Resume cannot turn an acknowledged pause into a setup failure.
            pause[0] = False
            raise InterruptedError("native child cancelled and reaped")
        with self.assertRaises(Paused):
            self.compile(adapter, prepared, native=interrupted)
        self.assertFalse(missing.exists())
        self.assertFalse(list((self.root / "jobs").rglob("*.pending-*")))
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))
        self.assertFalse(list((self.root / "jobs").rglob("clang-random-intermediate.tmp")))
        for path, data in retained.items(): self.assertEqual(path.read_bytes(), data)
        calls = []
        def retry(command, **kwargs):
            calls.append(command)
            return prepared[3](command, **kwargs)
        self.assertEqual(objects, self.compile(self.adapter(), prepared, native=retry))
        self.assertEqual(len(calls), 1)

    def test_restart_removes_abandoned_command_directory_and_reuses_objects(self):
        prepared = self.prepare()
        objects = self.compile(self.adapter(), prepared)
        abandoned = Path(objects[0]).parent / "pending-authored-process-death"
        abandoned.mkdir()
        (abandoned / "clang-random-intermediate.tmp").write_bytes(b"authored incomplete output")
        def no_process(*args, **kwargs): self.fail("verified objects must be reused")
        self.assertEqual(objects, self.compile(self.adapter(), prepared, native=no_process))
        self.assertFalse(abandoned.exists())

    def test_unrequested_native_interruption_remains_an_error(self):
        prepared = self.prepare()
        def interrupted(*args, **kwargs):
            raise InterruptedError("authored unrelated native failure")
        with self.assertRaisesRegex(InterruptedError, "unrelated"):
            self.compile(self.adapter(), prepared, native=interrupted)
        self.assertFalse(list((self.root / "jobs").rglob("*.pending-*")))

    def test_reuses_real_objects_after_restart_and_recovers_corruption(self):
        prepared = self.prepare()
        objects = self.compile(self.adapter(), prepared, jobs=2)
        self.assertEqual(len(objects), 3)
        def unexpected(*args, **kwargs): self.fail("verified objects must be reused")
        self.assertEqual(objects, self.compile(self.adapter(), prepared, native=unexpected))
        Path(objects[0]).write_bytes(b"corrupt")
        launches = []
        def counted(command, **kwargs):
            launches.append(command)
            return prepared[3](command, **kwargs)
        self.compile(self.adapter(), prepared, native=counted)
        self.assertEqual(len(launches), 1)
        self.assertNotEqual(Path(objects[0]).read_bytes(), b"corrupt")

    def test_low_space_retains_objects_and_retries_only_missing_output(self):
        prepared = self.prepare()
        objects = self.compile(self.adapter(), prepared)
        Path(objects[0]).unlink()
        retained = {path: Path(path).read_bytes() for path in objects[1:]}
        with patch("setup_adapter.shutil.disk_usage", return_value=SimpleNamespace(free=0)):
            with self.assertRaisesRegex(Exception, "Verified checkpoints are kept"):
                self.compile(self.adapter(), prepared, native=lambda *a, **k: self.fail("must not compile"))
        self.assertTrue(all(Path(path).read_bytes() == data for path, data in retained.items()))
        calls = []
        def counted(command, **kwargs):
            calls.append(command)
            return prepared[3](command, **kwargs)
        self.compile(self.adapter(), prepared, native=counted)
        self.assertEqual(len(calls), 1)

    def test_enospc_sync_discards_partial_object_and_retries(self):
        import errno
        import os
        prepared = self.prepare()
        original_sync = os.fsync
        calls = []
        def fail_first_sync(fd):
            calls.append(fd)
            if len(calls) == 1: raise OSError(errno.ENOSPC, "authored disk-full fault")
            original_sync(fd)
        with patch("setup_adapter.os.fsync", side_effect=fail_first_sync):
            with self.assertRaises(OSError) as failure:
                self.compile(self.adapter(), prepared)
        self.assertEqual(failure.exception.errno, errno.ENOSPC)
        self.assertFalse(list((self.root / "jobs").rglob("*.pending-*")))
        retained = list((self.root / "jobs").glob("compile-*/obj/*.o.json"))
        self.assertEqual(len(retained), 2)
        launches = []
        def counted(command, **kwargs):
            launches.append(command)
            return prepared[3](command, **kwargs)
        self.compile(self.adapter(), prepared, native=counted)
        self.assertEqual(len(launches), 1)

    def test_failure_preserves_other_objects_for_retry(self):
        prepared = self.prepare()
        def failing(command, **kwargs):
            if command[command.index("-c") + 1].endswith("imports.c"):
                Path(command[command.index("-o") + 1]).write_bytes(b"partial")
                return 1, b"simulated compiler failure"
            return prepared[3](command, **kwargs)
        adapter = self.adapter()
        with self.assertRaises(adapter.setup.SetupError):
            self.compile(adapter, prepared, native=failing)
        self.assertFalse(list((self.root / "jobs").rglob("*.pending-*")))
        launches = []
        def counted(command, **kwargs):
            launches.append(command)
            return prepared[3](command, **kwargs)
        objects = self.compile(self.adapter(), prepared, native=counted)
        self.assertEqual(len(objects), 3)
        self.assertEqual(len(launches), 1)

    def test_pause_keeps_completed_object(self):
        prepared = self.prepare()
        self.events.clear()
        adapter = self.adapter(pause=lambda: any(event.get("stage") == "compile" and
                                                 event.get("state") == "compiled" for event in self.events))
        with self.assertRaises(Paused): self.compile(adapter, prepared)
        launches = []
        def counted(command, **kwargs):
            launches.append(command)
            return prepared[3](command, **kwargs)
        self.compile(self.adapter(), prepared, native=counted)
        self.assertEqual(len(launches), 2)

    def test_toolchain_change_creates_new_objects_and_retains_old(self):
        prepared = self.prepare()
        previous = self.compile(self.adapter(), prepared)
        updated = self.compile(self.adapter(), prepared, identity="toolchain-v2")
        self.assertNotEqual(previous, updated)
        self.assertTrue(all(Path(path).is_file() for path in previous))

    def test_header_and_flags_changes_invalidate_objects(self):
        generated, toolchain, manifest, native = self.prepare()
        header = self.root / "headers" / "revision.h"
        header.parent.mkdir()
        header.write_text("#define BUILD_REVISION 1\n")
        manifest["gamecode_cflags"] += ["-include", str(header)]
        def build():
            return self.adapter().compile(toolchain, manifest, generated, "toolchain-v1", native,
                                          header_roots=[PACKAGE / "runtime/include", header.parent])
        initial = build()
        header.write_text("#define BUILD_REVISION 2\n")
        changed_header = build()
        self.assertNotEqual(initial, changed_header)
        manifest["gamecode_cflags"].append("-O2")
        self.assertNotEqual(changed_header, build())



class LinkTests(unittest.TestCase):
    setUp = TranslationTests.setUp
    adapter = TranslationTests.adapter

    def prepare(self):
        import shutil
        generated, toolchain, manifest, native = CompileTests.prepare(self)
        (generated / "code_harness.c").write_text(fixture.HARNESS.replace("int main(void)", "int synthetic_check(void)"))
        toolchain.cxx = [shutil.which("clang++") or toolchain.cc[0]]
        toolchain.ar = [shutil.which("ar")]
        toolchain.rsp = False
        manifest["gamecode_cflags"].append("-fPIC")
        manifest["link"] = (["-shared", "-Wl,-all_load", "{gamecode}", "-o", "{out}"]
                            if sys.platform == "darwin" else
                            ["-shared", "-Wl,--whole-archive", "{gamecode}", "-Wl,--no-whole-archive", "-o", "{out}"])
        adapter = self.adapter()
        objects = adapter.compile(toolchain, manifest, generated, "test-toolchain", native,
                                  header_roots=[PACKAGE / "runtime/include"])
        return toolchain, manifest, objects, native

    def link(self, prepared, native=None, adapter=None):
        toolchain, manifest, objects, runner = prepared
        return (adapter or self.adapter()).link(toolchain, manifest, objects, "test-toolchain", native or runner,
                                               sdk_roots=[PACKAGE / "runtime/include"])

    def test_loads_translated_function_and_reuses_after_restart(self):
        import ctypes
        prepared = self.prepare()
        library = self.link(prepared)
        loaded = ctypes.CDLL(str(library))
        self.assertEqual(loaded.synthetic_check(), 0)
        self.assertFalse(library.stat().st_mode & 0o222)
        def unexpected(*args, **kwargs): self.fail("completed link must be reused")
        self.assertEqual(library, self.link(prepared, native=unexpected))

    def test_pause_cancels_active_link_preserves_previous_library_and_retries(self):
        prepared = self.prepare()
        previous = self.link(prepared)
        original = previous.read_bytes()
        prepared[1]["link"].append("-O1")
        pause = [False]
        adapter = self.adapter(pause=lambda: pause[0])
        calls = []
        def interrupted(command, env=None, cancel=None):
            calls.append(command)
            if command[0] == prepared[0].ar[0]:
                return prepared[3](command, env=env, cancel=cancel)
            Path(command[command.index("-o") + 1]).write_bytes(b"authored incomplete library")
            pause[0] = True
            self.assertTrue(cancel())
            raise InterruptedError("native linker cancelled and reaped")
        with self.assertRaises(Paused):
            self.link(prepared, native=interrupted, adapter=adapter)
        self.assertEqual(len(calls), 2)
        self.assertEqual(previous.read_bytes(), original)
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))
        replacement = self.link(prepared)
        self.assertNotEqual(previous, replacement)
        self.assertTrue(replacement.is_file())
        self.assertEqual(previous.read_bytes(), original)

    def test_failed_update_preserves_prior_library_and_retries(self):
        prepared = self.prepare()
        previous = self.link(prepared)
        previous_bytes = previous.read_bytes()
        prepared[1]["link"].append("-Wl,--this-option-does-not-exist")
        adapter = self.adapter()
        with self.assertRaises(adapter.setup.SetupError): self.link(prepared, adapter=adapter)
        self.assertEqual(previous.read_bytes(), previous_bytes)
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))
        prepared[1]["link"].pop()
        self.assertEqual(previous, self.link(prepared))

    def test_low_space_update_preserves_previous_library(self):
        prepared = self.prepare()
        previous = self.link(prepared)
        original = previous.read_bytes()
        with patch("setup_adapter.shutil.disk_usage", return_value=SimpleNamespace(free=0)):
            self.assertEqual(previous, self.link(prepared, native=lambda *a, **k: self.fail("must reuse")))
            prepared[1]["link"].append("-O1")
            with self.assertRaisesRegex(Exception, "1 GiB working reserve"):
                self.link(prepared, native=lambda *a, **k: self.fail("must not link"))
        self.assertEqual(previous.read_bytes(), original)
        self.assertFalse(list((self.root / "jobs").rglob("pending-*")))

    def test_corrupt_library_is_relinked(self):
        prepared = self.prepare()
        library = self.link(prepared)
        library.chmod(0o600)
        library.write_bytes(b"corrupt")
        calls = []
        def counted(command, **kwargs):
            calls.append(command)
            return prepared[3](command, **kwargs)
        self.link(prepared, native=counted)
        self.assertEqual(len(calls), 2)
        self.assertNotEqual(library.read_bytes(), b"corrupt")


if __name__ == "__main__":
    unittest.main()
