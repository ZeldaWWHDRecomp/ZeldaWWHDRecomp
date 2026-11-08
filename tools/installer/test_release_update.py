"""Incremental updates: the translation cache (build_cache.py), the earlier-release transfer (release_update.py)
and setup's staged build swap. Game-free: synthetic folders and files only.

usage: python3 tools/installer/test_release_update.py [-v]
"""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import build_cache  # noqa: E402
import release_update  # noqa: E402


def write(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


class TranslationCacheTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.pkg = self.root / "pkg"
        write(self.pkg / "tools/recomp/recomp.py", "translator 1\n")
        write(self.pkg / "tools/rpx.py", "reader\n")
        self.rpx = write(self.root / "game/code/cking.rpx", "game code")

    def key(self, args=(), env=None, gen=None):
        return build_cache.translation_key(self.pkg, self.rpx, list(args), gen or self.root / "gen", (), env or {})

    def test_key_covers_translator_game_args_and_settings_not_the_output_folder(self):
        k = self.key()
        self.assertEqual(k, self.key(gen=self.root / "elsewhere/gen"))
        self.assertNotEqual(k, self.key(["--mod-hooks"]))
        self.assertNotEqual(k, self.key(env={"WWHD_RECOMP_PLAIN": "1"}))
        self.assertEqual(k, self.key(env={"UNRELATED": "1"}))
        write(self.pkg / "tools/recomp/__pycache__/recomp.cpython-39.pyc", "cache")
        self.assertEqual(k, self.key())
        write(self.pkg / "tools/recomp/ppc2c.py", "new translator file\n")
        self.assertNotEqual(k, self.key())
        k2 = self.key()
        self.rpx.write_text("another game")
        self.assertNotEqual(k2, self.key())

    def test_named_files_count_by_content(self):
        hooks = write(self.root / "work/decomp_hooks.txt", "02000000\n")
        args = ["--decomp-hooks", str(hooks)]
        k = build_cache.translation_key(self.pkg, self.rpx, args, self.root / "gen", [hooks], {})
        moved = write(self.root / "other/decomp_hooks.txt", "02000000\n")
        self.assertEqual(k, build_cache.translation_key(self.pkg, self.rpx, ["--decomp-hooks", str(moved)],
                                                        self.root / "gen", [moved], {}))
        hooks.write_text("02000004\n")
        self.assertNotEqual(k, build_cache.translation_key(self.pkg, self.rpx, args, self.root / "gen", [hooks], {}))

    def test_store_fetch_verify_and_trim(self):
        cache = build_cache.TranslationCache(self.root / "cache", keep=2)
        gen = self.root / "gen"
        write(gen / "code_000.c", "int a;")
        write(gen / "sub/table.c", "int t;")
        cache.store("a" * 64, gen)
        out = self.root / "out"
        self.assertTrue(cache.fetch("a" * 64, out))
        self.assertEqual((out / "sub/table.c").read_text(), "int t;")
        self.assertFalse(cache.fetch("b" * 64, self.root / "out2"))
        # a damaged copy is not used
        import zlib
        (cache.entry("a" * 64) / "gen/code_000.c.z").write_bytes(zlib.compress(b"int damaged;"))
        self.assertFalse(cache.fetch("a" * 64, self.root / "out3"))
        # at most `keep`; only folders the cache wrote are removed
        os.utime(cache.entry("a" * 64) / "complete.json", (900, 900))
        foreign = write(self.root / "cache/cccccccccccccccc/note.txt", "not the cache's")
        for i, k in enumerate(("d", "e", "f")):
            write(gen / "code_000.c", "int %s;" % k)
            cache.store(k * 64, gen)
            os.utime(cache.entry(k * 64) / "complete.json", (1000 + i, 1000 + i))
        cache.trim()
        kept = sorted(p.name for p in (self.root / "cache").iterdir())
        self.assertEqual(kept, sorted(["cccccccccccccccc", "e" * 16, "f" * 16]))
        self.assertTrue(foreign.is_file())


class ObjectCacheAcrossReleases(unittest.TestCase):
    def test_another_release_folder_reuses_objects_unless_debug_information_holds_paths(self):
        import setup
        from types import SimpleNamespace
        tc = SimpleNamespace(cc=[sys.executable], env=None)

        def base(pkg, extra=()):
            with mock.patch.object(setup, "PKG", pkg):
                flags = ["-I%s/sdk/include" % pkg, "-I/data/work/gen", "-O3"] + list(extra)
                return setup.object_cache(tc, "/data", flags, "/data/work/gen").base

        self.assertEqual(base("/a/WindWakerHD-v1"), base("/b/WindWakerHD-v2"))
        self.assertNotEqual(base("/a/WindWakerHD-v1", ["-g"]), base("/b/WindWakerHD-v2", ["-g"]))
        self.assertNotEqual(base("/a/WindWakerHD-v1"), base("/a/WindWakerHD-v1", ["-O2"]))


class TransferTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.old = root / "WindWakerHD-v1"
        self.new = root / "WindWakerHD-v2"
        write(self.old / "portable.txt", "")
        write(self.new / "portable.txt", "")
        d = self.old / "data"
        write(d / "install.json", json.dumps({"version": "v1", "platform": "macos-arm64", "exe": "bin/wwhd",
                                              "game_dir": "game", "installed": "2026-01-01 00:00:00"}))
        write(d / "game/code/cking.rpx", "game")
        write(d / "save/user/cking.sav", "save")
        write(d / "user/settings.ini", "settings")
        write(d / "user/shader-prebuild/live/shaders.bin", "cache")
        write(d / "bin/wwhd", "old game")
        write(d / "code-builds/0123456789abcdef/ready.json", "{}")
        write(d / "setup.log", "old log")
        write(self.new / "data/setup.log", "new log")

    def info(self):
        return release_update.release_info(self.old, "macos-arm64")

    def test_found_next_to_the_new_release_and_not_for_another_platform(self):
        found = release_update.earlier_releases(self.new, "macos-arm64")
        self.assertEqual([f["version"] for f in found], ["v1"])
        self.assertEqual(release_update.earlier_releases(self.new, "linux-x86_64"), [])
        self.assertTrue(found[0]["game_inside"])

    def test_plan_brings_user_data_not_builds_or_logs(self):
        self.assertEqual(release_update.plan(self.old / "data", self.new / "data"), ["game", "save", "user"])

    def run_transfer(self, mode, interrupt_after=None):
        new_data = self.new / "data"
        journal = release_update.start(self.info(), new_data, mode, "v2")
        hook = None
        if interrupt_after:
            def hook(name):
                if name == interrupt_after:
                    raise KeyboardInterrupt
        try:
            release_update.transfer(journal, new_data, after_entry=hook)
        except KeyboardInterrupt:
            pending = release_update.pending(new_data)
            self.assertIsNotNone(pending)
            release_update.transfer(pending, new_data)
        self.assertIsNone(release_update.pending(new_data))
        self.assertEqual((new_data / "save/user/cking.sav").read_text(), "save")
        self.assertEqual((new_data / "user/shader-prebuild/live/shaders.bin").read_text(), "cache")
        self.assertEqual((new_data / "setup.log").read_text(), "new log")
        self.assertFalse((new_data / "code-builds").exists())

    @unittest.skipUnless(sys.platform == "darwin" or sys.platform.startswith("linux"), "clones")
    def test_clone_keeps_the_old_release_whole(self):
        if release_update.transfer_mode(self.old / "data", self.new / "data") != "clone":
            self.skipTest("this file system cannot clone")
        self.run_transfer("clone", interrupt_after="game")
        self.assertEqual((self.old / "data/save/user/cking.sav").read_text(), "save")
        self.assertTrue((self.old / "data/bin/wwhd").is_file())
        self.assertFalse((self.new / "data/bin.previous").exists())
        # cloned, not shared: a change here leaves the earlier release's copy
        (self.new / "data/user/settings.ini").write_text("changed")
        self.assertEqual((self.old / "data/user/settings.ini").read_text(), "settings")

    def test_move_is_resumable_and_keeps_the_old_game_as_previous_build(self):
        self.run_transfer("move", interrupt_after="save")
        self.assertFalse((self.old / "data/save").exists())
        self.assertTrue((self.old / "data/install.json").is_file())  # (not moved: the old release's own record)
        self.assertEqual((self.new / "data/bin.previous/wwhd").read_text(), "old game")
        self.assertTrue((self.new / "data/bin.previous/.wwhd-previous-build.json").is_file())
        self.assertTrue((self.old / release_update.NOTE).is_file())
        self.assertEqual(release_update.earlier_releases(self.new, "macos-arm64"), [])  # not offered again
        self.assertTrue((self.old / "data/setup.log").is_file())  # nothing deleted, only taken
        self.assertTrue((self.old / "data/code-builds/0123456789abcdef/ready.json").is_file())

    def test_refuses_an_entry_the_new_folder_has(self):
        write(self.new / "data/save/user/other.sav", "x")
        with self.assertRaises(ValueError):
            release_update.plan(self.old / "data", self.new / "data")


class StagedSwapTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.data = Path(self.tmp.name)
        import setup
        self.setup = setup

    def stage(self, text):
        staging = self.data / self.setup.STAGING_DIR
        write(staging / "wwhd", text)
        write(staging / self.setup.STAGING_MARK, json.dumps({"state": "verified"}))
        return staging

    def test_publish_keeps_one_previous_build(self):
        exe_dir = self.data / "bin"
        write(exe_dir / "wwhd", "v1")
        self.setup.publish_build(str(self.stage("v2")), str(exe_dir), str(self.data))
        self.assertEqual((exe_dir / "wwhd").read_text(), "v2")
        self.assertEqual((self.data / "bin.previous/wwhd").read_text(), "v1")
        self.setup.publish_build(str(self.stage("v3")), str(exe_dir), str(self.data))
        self.assertEqual((self.data / "bin.previous/wwhd").read_text(), "v2")
        self.assertEqual(sorted(p.name for p in self.data.iterdir()), ["bin", "bin.previous"])

    def test_previous_folder_not_made_by_setup_is_kept_aside(self):
        exe_dir = self.data / "bin"
        write(exe_dir / "wwhd", "v1")
        write(self.data / "bin.previous/mine.txt", "the player's")
        self.setup.publish_build(str(self.stage("v2")), str(exe_dir), str(self.data))
        aside = [p for p in self.data.iterdir() if p.name.startswith("bin.previous.")]
        self.assertEqual(len(aside), 1)
        self.assertEqual((aside[0] / "mine.txt").read_text(), "the player's")

    def test_interrupted_swap_is_finished_with_the_checked_build(self):
        exe_dir = self.data / "bin"
        self.stage("v2")
        write(self.data / "bin.previous/wwhd", "v1")  # interrupted after the first rename
        self.setup.recover_build_swap(str(self.data), str(exe_dir))
        self.assertEqual((exe_dir / "wwhd").read_text(), "v2")

    def test_unchecked_staging_is_never_swapped_in(self):
        staging = self.data / self.setup.STAGING_DIR
        write(staging / "wwhd", "half linked")
        write(staging / self.setup.STAGING_MARK, json.dumps({"state": "linking"}))
        self.setup.recover_build_swap(str(self.data), str(self.data / "bin"))
        self.assertFalse((self.data / "bin").exists())

    def test_old_code_builds_of_earlier_releases_go_only_when_setup_made_them(self):
        key = "ab" * 32
        write(self.data / "code-builds" / key[:16] / "ready.json", json.dumps({"fingerprint": key}))
        key2 = "cd" * 32
        write(self.data / "code-builds" / key2[:16] / "ready.json", json.dumps({"fingerprint": key2, "release": "v2"}))
        write(self.data / "code-builds/notmine/file", "x")
        write(self.data / "code-builds" / ("ef" * 8) / "ready.json", json.dumps({"fingerprint": "00" * 32}))
        with mock.patch.object(self.setup.LOG, "write"):
            self.setup.clean_old_code_builds(str(self.data), "v2")
        self.assertEqual(sorted(p.name for p in (self.data / "code-builds").iterdir()),
                         sorted([key2[:16], "notmine", "ef" * 8]))


if __name__ == "__main__":
    unittest.main()
