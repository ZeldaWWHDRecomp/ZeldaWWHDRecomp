"""Real shared translation/host compilation through the durable job runner."""
from debug_fixture_build import register as register_fixture_build
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
        for sibling in ("setup.py", "code_mods.py", "decomp_setup.py", "shader_prebuild.py"):
            shutil.copyfile(ROOT / "tools/installer" / sibling, self.package / "tools/installer" / sibling)
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
        register_fixture_build(adapter.setup)
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

    def test_shader_assembly_uses_apk_decoder_path_and_two_threads(self):
        import os
        from unittest.mock import patch
        sys.path.insert(0, str(ROOT / "tools"))
        import shaderprep
        import test_shaderprep
        for name in ("shaderprep.py", "shaderprep_format.py"):
            shutil.copyfile(ROOT / "tools" / name, self.package / "tools" / name)
        program = b"synthetic vertex program".ljust(32, b".")
        (self.game / "content/synthetic.sharcfb").write_bytes(test_shaderprep.sharcfb([(shaderprep.VS, program)]))
        record = self.root / "record"
        with record.open("wb") as stream:
            shaderprep.write_record(stream, 1, shaderprep.shader_record(
                1, program, test_shaderprep.compact([(160, 0, 0, 0)]), [(0xa21a, 7)]))
        (self.package / "tools/shadercache").mkdir()
        shaderprep.cmd_template(record, self.package / "tools/shadercache/template.bin")
        self.value["yaz0"] = str(self.root / "nativeLibraryDir/libyaz0.so")
        builds = []
        real_module_from_spec = importlib.util.module_from_spec
        def module_from_spec(found):
            # the job loads shaderprep from the package: record how it calls the build
            module = real_module_from_spec(found)
            if found.name == "wwhd_shaderprep":
                found.loader.exec_module(module)
                original = module.cmd_build
                module.cmd_build = lambda *a, **k: (builds.append(k), original(*a, **k))[1]
                found.loader = type("Loaded", (), {"exec_module": staticmethod(lambda m: None)})()
            return module
        with patch.dict(os.environ, {}), patch("importlib.util.module_from_spec", module_from_spec):
            result = self.execute()
            self.assertEqual(os.environ.get("WWHD_YAZ0_LIB"), self.value["yaz0"])
        self.assertEqual(result["shader_prebuild"]["status"], "warm_pending")
        self.assertEqual(builds[-1]["jobs"], 2)
        self.assertTrue(builds[-1]["threads"])

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

    def test_code_mod_choice_reaches_translation_and_result(self):
        seen = []
        def adapter(*args):
            value = self.adapter(*args)
            translate = value.translate
            def recorded(game, hooks=False):
                seen.append(hooks)
                return translate(game)  # the synthetic harness has no mod-hook runtime to link against
            value.translate = recorded
            return value
        self.configuration.write_text(json.dumps({**self.value, "code_mods": True}))
        result = run(self.configuration, self.package, self.native, adapter)
        self.assertEqual(seen, [True])
        self.assertIs(result["code_mods"], True)
        self.assertIs(json.loads((self.job / "ready.json").read_text())["code_mods"], True)
        self.assertIs(self.execute()["code_mods"], False)  # no choice in the job: without code mods
        self.configuration.write_text(json.dumps({**self.value, "code_mods": "yes"}))
        with self.assertRaisesRegex(ValueError, "true or false"):
            run(self.configuration, self.package, self.native, adapter)

    def test_randomized_game_is_translated_from_its_own_files(self):
        """job "randomize": the in-app randomizer makes the seed first; its files are translated, the game stays"""
        seen = []
        overlay = self.job / "seed/out"

        def adapter(*args):
            value = self.adapter(*args)
            translate = value.translate

            def randomize(game, randomize, native_run):
                seen.append(("randomize", Path(game), randomize["exe"]))
                return overlay, {"id": "0123456789abcdef", "hash": "Kane Ankle", "permalink": "P"}
            value.randomize = randomize

            def recorded(game, hooks=False):
                seen.append(("translate", Path(game)))
                return translate(self.game)  # (the synthetic harness stands in for the seed's code)
            value.translate = recorded
            return value
        self.configuration.write_text(json.dumps({**self.value, "randomize": {"exe": "x", "data": "d", "storage": "s"}}))
        result = run(self.configuration, self.package, self.native, adapter)
        self.assertEqual(seen, [("randomize", self.game, "x"), ("translate", overlay)])
        self.assertEqual(result["game"], str(self.game))  # the installed game; the seed's files go over it
        self.assertEqual(result["randomizer"], {"files": str(overlay), "seed": "0123456789abcdef",
                                                "hash": "Kane Ankle", "permalink": "P"})

    def test_installed_game_input_only_for_a_randomized_game_inside_storage(self):
        def adapter(*args):
            value = self.adapter(*args)
            value.randomize = lambda game, randomize, native_run: (game, {"id": "0123456789abcdef"})
            return value
        installed = {**self.value, "input": {"kind": "installed", "path": str(self.game)}}
        self.configuration.write_text(json.dumps(installed))
        with self.assertRaisesRegex(ValueError, "Only a randomized game"):
            run(self.configuration, self.package, self.native, adapter)
        self.configuration.write_text(json.dumps({**installed, "randomize": {"exe": "x", "data": "d", "storage": str(self.root / "other")}}))
        with self.assertRaisesRegex(ValueError, "outside setup storage"):
            run(self.configuration, self.package, self.native, adapter)
        self.configuration.write_text(json.dumps({**installed, "randomize": {"exe": "x", "data": "d", "storage": str(self.root)}}))
        result = run(self.configuration, self.package, self.native, adapter)
        self.assertEqual(result["game"], str(self.game))
        self.assertEqual(result["randomizer"]["seed"], "0123456789abcdef")

    def test_adapter_runs_the_randomizer_through_the_native_bridge(self):
        sys.path.insert(0, str(ROOT / "tools/installer"))
        import randomizer_seeds
        adapter = self.adapter(self.package, self.root / "checkpoints", "fixture-v1")
        commands = []

        def native(command, env=None, cancel=lambda: False):
            commands.append(command)
            return 0, b'[rando] Randomizing...\n{"ok": true, "permalink": "P"}\n'

        def make(setup, ctx, say, **k):
            self.assertEqual(randomizer_seeds.call(setup, "w", "permalink")["permalink"], "P")
            self.assertEqual(ctx.data_dir, str((self.root / "storage").resolve()))
            return {"id": "fedcba9876543210"}
        original = randomizer_seeds.make
        randomizer_seeds.make = make
        try:
            out, entry = adapter.randomize(self.game, {"exe": "/apk/libwwhd-rando-driver.so", "data": "/data/rando",
                                                       "storage": str(self.root / "storage")}, native)
        finally:
            randomizer_seeds.make, randomizer_seeds.RUNNER, randomizer_seeds.TOOL = original, None, None
        self.assertEqual(commands, [["/apk/libwwhd-rando-driver.so", "--data", "/data/rando", "--work", "w", "permalink"]])
        self.assertEqual(out, (self.root / "storage").resolve() / "randomizer/seeds/fedcba9876543210/out")
        self.assertEqual(entry["id"], "fedcba9876543210")

    def seed_import_job(self):
        """an official randomizer output folder copied privately (input/game) and the installed game in storage"""
        storage = self.root / "storage"
        installed = storage / "game"
        shutil.copytree(self.game, installed)
        for name in ("code/app.xml", "meta/meta.xml"):
            (self.game / name).write_text("<menu><title_id>0005000010143599</title_id></menu>")
        return storage, installed, {**self.value, "input": {**self.value["input"], "seed_import": {"installed": str(installed)}},
                                    "randomize": {"exe": "x", "data": "d", "storage": str(storage)}}

    def test_imported_seed_is_built_beside_the_installed_game(self):
        """Randomized game... > Import a seed...: the copied output folder goes beside the installed game (its changed
        files in setup storage), the copy goes, and a resumed job builds from the kept files"""
        storage, installed, job = self.seed_import_job()
        overlay = storage / "randomizer/seeds/0123456789abcdef/out"
        seen = []

        def adapter(*args):
            value = self.adapter(*args)
            translate = value.translate

            def import_seed(folder, game, randomize):
                seen.append(("import", Path(folder), Path(game), randomize["storage"]))
                (overlay / "code").mkdir(parents=True, exist_ok=True)
                (overlay / "code/cking.rpx").write_bytes(b"seed")
                return overlay, {"id": "0123456789abcdef", "hash": "", "permalink": ""}
            value.import_seed = import_seed
            value.randomize = lambda *a: self.fail("an imported seed is not randomized again")

            def recorded(game, hooks=False):
                seen.append(("translate", Path(game)))
                return translate(installed)  # (the synthetic harness stands in for the seed's code)
            value.translate = recorded
            return value
        self.configuration.write_text(json.dumps(job))
        result = run(self.configuration, self.package, self.native, adapter)
        self.assertEqual(seen, [("import", self.game, installed.resolve(), str(storage)), ("translate", overlay)])
        self.assertEqual(result["game"], str(installed.resolve()))
        self.assertEqual(result["randomizer"]["files"], str(overlay))
        self.assertFalse(self.game.exists())  # the job's private copy of the output folder
        self.assertEqual(json.loads((self.job / "seed-import.json").read_text())["id"], "0123456789abcdef")
        seen.clear()
        result = run(self.configuration, self.package, self.native, adapter)  # resumed: no copy, no new import
        self.assertEqual(seen, [("translate", overlay)])
        self.assertEqual(result["randomizer"]["seed"], "0123456789abcdef")

    def test_seed_import_needs_the_installed_game_in_storage(self):
        storage, installed, job = self.seed_import_job()
        for broken, why in (({**job, "randomize": None}, "beside the installed game"),
                            ({**job, "input": {**job["input"], "seed_import": {"installed": str(self.root / "elsewhere")}}},
                             "outside setup storage")):
            self.configuration.write_text(json.dumps(broken))
            with self.assertRaisesRegex(ValueError, why):
                run(self.configuration, self.package, self.native, self.adapter)

    def test_randomized_folder_is_not_installed_as_the_game(self):
        self.seed_import_job()
        with self.assertRaisesRegex(ValueError, "Import a seed"):
            self.execute()

    def test_adapter_imports_with_the_desktop_import(self):
        sys.path.insert(0, str(ROOT / "tools/installer"))
        import randomizer_seeds
        adapter = self.adapter(self.package, self.root / "checkpoints", "fixture-v1")
        calls = []

        def import_seed(setup, ctx, folder, say):
            calls.append((ctx.data_dir, ctx.game_dir, folder))
            say("  Imported")
            return {"id": "fedcba9876543210"}
        original = randomizer_seeds.import_seed
        randomizer_seeds.import_seed = import_seed
        try:
            out, entry = adapter.import_seed(self.game, self.root / "storage/game", {"storage": str(self.root / "storage")})
        finally:
            randomizer_seeds.import_seed = original
        self.assertEqual(calls, [(str((self.root / "storage").resolve()), str((self.root / "storage/game").resolve()),
                                  str(self.game))])
        self.assertEqual(out, (self.root / "storage").resolve() / "randomizer/seeds/fedcba9876543210/out")

    def test_rejects_outside_input_and_unsupported_manifest(self):
        self.value["input"]["path"] = "../outside"
        with self.assertRaisesRegex(ValueError, "inside the setup job"): self.execute()
        self.value["schema"] = 99
        with self.assertRaisesRegex(ValueError, "job version"): self.execute()
        self.value["schema"] = 1
        self.value["jobs"] = 5
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

    def test_current_android_private_key_copies_accept_cemu_lists(self):
        self.value["input"] = {"kind": "image", "path": "input/dump",
                               "disc_key": "input/disc.key", "common_key": "input/common.key"}
        (self.job / "input/dump").write_bytes(b"authored synthetic disc input")
        self.value["extractor"] = {"path": "authored-extractor", "identity": "authored-fixture"}
        self.configuration.write_text(json.dumps(self.value))
        disc, common = bytes(range(16)), bytes(range(16, 32))
        payload = b"# synthetic Cemu list\njunk\n" + ("00" * 16 + " # wrong\n" + common.hex() +
                  " # common\n" + disc.hex() + " # disc\n").encode()
        for name in ("disc", "common"):
            (self.job / ("input/" + name + ".key")).write_bytes(payload)

        def adapter_factory(*args):
            adapter = self.adapter(*args)
            def info(image, keys):
                if keys.disc != disc: return "disc_key_wrong", "", None
                if keys.common != common: return "common_key_wrong", "", None
                return None, "", {"title_id": "0005000010143500"}
            def extract(path, kind, extractor, identity, native, request):
                keys, _, why, notices = adapter.setup.resolve_disc_keys(str(path), request, info_reader=info)
                self.assertIsNone(why)
                self.assertEqual((keys.disc, keys.common), (disc, common))
                return self.game  # authored boundary; encrypted-image checks live in test_setup_keys
            adapter.extract = extract
            return adapter

        for _ in range(2):  # a fresh worker can still use the current app's private-copy schema
            result = run(self.configuration, self.package, self.native, adapter_factory)
            self.assertTrue(Path(result["library"]).is_file())
            self.assertEqual((self.job / "input/disc.key").read_bytes(), payload)

    def test_empty_or_garbage_memory_key_identifies_its_role(self):
        self.value["input"] = {"kind": "image", "path": "input/dump"}
        (self.job / "input/dump").write_bytes(b"authored synthetic disc input")
        self.value["extractor"] = {"path": "authored-extractor", "identity": "authored-fixture"}
        self.configuration.write_text(json.dumps(self.value))
        for name in ("disc", "common"):
            for value in (b"", b"garbage"):
                with self.assertRaisesRegex(ValueError, "Invalid " + name + " key file"):
                    run(self.configuration, self.package, self.native, self.adapter, key_files={name: value})
                self.assertEqual(self.state()["state"], "failed")
                self.assertFalse((self.job / "input/disc.key").exists())
                self.assertFalse((self.job / "input/common.key").exists())

    def test_cemu_list_from_read_only_memory_supplies_both_keys_without_persistence(self):
        self.value["input"] = {"kind": "image", "path": "input/dump"}
        (self.job / "input/dump").write_bytes(b"authored synthetic disc input")
        self.value["extractor"] = {"path": "authored-extractor", "identity": "authored-fixture"}
        self.configuration.write_text(json.dumps(self.value))
        disc, common = bytes(range(16)), bytes(range(16, 32))
        payload = b"# synthetic Cemu list\njunk\n" + ("00" * 16 + " # wrong\n" + common.hex() +
                  " # common\n" + disc.hex() + " # disc\n").encode()

        def adapter_factory(*args):
            adapter = self.adapter(*args)
            def info(image, keys):
                if keys.disc != disc: return "disc_key_wrong", "", None
                if keys.common != common: return "common_key_wrong", "", None
                return None, "", {"title_id": "0005000010143500"}
            def extract(path, kind, extractor, identity, native, request):
                keys, _, why, notices = adapter.setup.resolve_disc_keys(str(path), request,
                    info_reader=info, file_reader=request["_entries"].get)
                self.assertIsNone(why)
                self.assertEqual((keys.disc, keys.common), (disc, common))
                for notice in notices:
                    adapter.emit({"stage": "extract", "state": "keys_found", "detail": notice})
                return self.game  # authored extraction boundary, not a cryptographic disc fixture
            adapter.extract = extract
            return adapter

        for field in ("disc", "common"):
            result = run(self.configuration, self.package, self.native, adapter_factory, key_files={field: payload})
            self.assertTrue(Path(result["library"]).is_file())
            self.assertEqual(len(self.state()["key_sources"]), 2)
            for path in self.job.rglob("*"):
                if path.suffix == ".json":
                    self.assertNotIn(disc.hex(), path.read_text())
                    self.assertNotIn(common.hex(), path.read_text())
            self.assertFalse((self.job / "input/disc.key").exists())
            self.assertFalse((self.job / "input/common.key").exists())

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
