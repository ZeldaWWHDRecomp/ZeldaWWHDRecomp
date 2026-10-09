#!/usr/bin/env python3
"""Unit tests for the installer's helpers (no game files, no network): python3 test_setup.py"""
import hashlib
import json
import os
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup  # noqa: E402

KEY_HEX = "0011223344556677" "8899aabbccddeeff"  # made-up test value


class GuestBuildConfig(unittest.TestCase):
    def test_toolchain_argument_vector(self):
        with tempfile.TemporaryDirectory() as d:
            tc = setup.Toolchain(["compiler with spaces", "cc", "-target", "x86_64-linux-gnu.2.35"], [], [])
            with mock.patch.object(setup, "ensure_setup_python", return_value=sys.executable):
                setup.write_guest_build_config(d, tc)
            with open(os.path.join(d, "guest-sdk.json"), encoding="utf-8") as f:
                config = json.load(f)
            self.assertEqual(config["compiler"], tc.cc)
            self.assertEqual(config["python"], [sys.executable])
            self.assertTrue(config["builder"].endswith("build_guest_mod.py"))
            self.assertFalse(os.path.exists(os.path.join(d, "guest-sdk.json.tmp")))


    def test_portable_paths_survive_release_move(self):
        with tempfile.TemporaryDirectory() as directory:
            release = os.path.join(directory, "release")
            data = os.path.join(release, "data")
            paths = {"python": os.path.join(release, "tools", "python", "python.exe"),
                     "compiler": os.path.join(data, "toolchain", "bin", "zig"),
                     "builder": os.path.join(release, "tools", "guestmod", "build_guest_mod.py")}
            for path in paths.values():
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w") as f:
                    f.write("")  # fixture paths only
            os.makedirs(os.path.join(release, "sdk", "include"))
            cache = os.path.join(data, "toolchain", "zig-cache")
            tc = setup.Toolchain([paths["compiler"], "cc", "-target", "x86_64-linux-gnu.2.35"], [], [],
                                 env={"ZIG_GLOBAL_CACHE_DIR": cache, "UNRELATED_ENV": "not-persisted"})
            with mock.patch.multiple(setup, PKG=release, PORTABLE=True), mock.patch.object(setup, "ensure_setup_python", return_value=paths["python"]):
                setup.write_guest_build_config(data, tc)
            moved = os.path.join(directory, "moved-release")
            os.rename(release, moved)
            moved_data = os.path.join(moved, "data")
            with open(os.path.join(moved_data, "guest-sdk.json")) as f:
                config = json.load(f)
            self.assertEqual(config["format_version"], 2)
            for key in ("python", "compiler"):
                self.assertFalse(os.path.isabs(config[key][0]))
                self.assertTrue(os.path.isfile(os.path.join(moved_data, config[key][0])))
            self.assertTrue(os.path.isfile(os.path.join(moved_data, config["builder"])))
            self.assertTrue(os.path.isdir(os.path.join(moved_data, config["include"])))
            self.assertEqual(os.path.normpath(os.path.join(moved_data, config["zig_cache"])),
                             os.path.join(moved_data, "toolchain", "zig-cache"))
            self.assertNotIn("UNRELATED_ENV", config)



class SetupPython(unittest.TestCase):
    def test_probe_checks_codec_and_version_in_isolation(self):
        with mock.patch.object(setup.subprocess, "run", return_value=mock.Mock(returncode=0)) as run:
            self.assertTrue(setup.python_setup_capable("python with spaces"))
        command = run.call_args.args[0]
        self.assertEqual(command[:3], ["python with spaces", "-I", "-c"])
        self.assertIn("compression import zstd", command[3])
        self.assertIn("(3, 14)", command[3])
        for result in (mock.Mock(returncode=1),):
            with mock.patch.object(setup.subprocess, "run", return_value=result):
                self.assertFalse(setup.python_setup_capable("python"))
        with mock.patch.object(setup.subprocess, "run", side_effect=setup.subprocess.TimeoutExpired("python", 15)):
            self.assertFalse(setup.python_setup_capable("python"))

    def test_windows_bundled_only_even_with_capable_ambient(self):
        with mock.patch.multiple(setup, IS_WIN=True, PKG="release"), \
             mock.patch.object(setup, "python_setup_capable", return_value=True) as probe, \
             mock.patch.object(setup, "download") as download:
            self.assertEqual(setup.ensure_setup_python("data"), os.path.join("release", "tools", "python", "python.exe"))
            probe.assert_called_once_with(os.path.join("release", "tools", "python", "python.exe"))
            download.assert_not_called()
        with mock.patch.multiple(setup, IS_WIN=True), \
             mock.patch.object(setup, "python_setup_capable", return_value=False), \
             mock.patch.object(setup, "download") as download:
            with self.assertRaisesRegex(setup.SetupError, "complete Windows release"):
                setup.ensure_setup_python("data")
            download.assert_not_called()

    def test_capable_ambient_no_download(self):
        with mock.patch.multiple(setup, IS_WIN=False), \
             mock.patch.object(setup, "python_setup_capable", return_value=True), \
             mock.patch.object(setup, "download") as download:
            self.assertEqual(setup.ensure_setup_python("data"), sys.executable)
            download.assert_not_called()

    def test_old_or_missing_codec_provisions_each_supported_platform(self):
        for mac, arch, key in ((False, "x86_64", "linux"), (False, "aarch64", "linux-aarch64"),
                               (True, "aarch64", "macos"), (True, "x86_64", "macos-x86_64")):
            with self.subTest(key=key), tempfile.TemporaryDirectory() as d:
                def fetch(url, dst, sha, size, label):
                    self.assertEqual(url, setup.load_toolchains()["python"][key]["url"])
                    with open(dst, "wb") as f:
                        f.write(b"authored archive placeholder")
                def unpack(command, **kwargs):
                    os.makedirs(os.path.join(command[-1], "python", "bin"))
                with mock.patch.multiple(setup, IS_WIN=False, IS_MAC=mac, IS_LINUX=not mac), \
                     mock.patch.object(setup, "host_arch", return_value=arch), \
                     mock.patch.object(setup, "python_setup_capable", side_effect=[False, True]), \
                     mock.patch.object(setup, "download", side_effect=fetch), \
                     mock.patch.object(setup, "run_logged", side_effect=unpack), \
                     mock.patch.object(setup.shutil, "which", return_value="tar"):
                    result = setup.ensure_setup_python(d)
                self.assertTrue(result.endswith(os.path.join("setup-python", "python", "bin", "python3")))
                with open(os.path.join(d, "setup-python", ".wwhd-python")) as f:
                    self.assertEqual(f.read().strip(), setup.load_toolchains()["python"][key]["sha256"])

    def test_cached_private_python_rechecked_for_codec(self):
        with tempfile.TemporaryDirectory() as d:
            root = os.path.join(d, "setup-python")
            os.makedirs(root)
            with open(os.path.join(root, ".wwhd-python"), "w") as f:
                f.write(setup.load_toolchains()["python"]["linux"]["sha256"])
            with mock.patch.multiple(setup, IS_WIN=False, IS_MAC=False, IS_LINUX=True), \
                 mock.patch.object(setup, "host_arch", return_value="x86_64"), \
                 mock.patch.object(setup, "python_setup_capable", side_effect=[False, True]) as probe, \
                 mock.patch.object(setup, "download") as download:
                result = setup.ensure_setup_python(d)
            self.assertEqual(probe.call_count, 2)
            download.assert_not_called()
            self.assertTrue(result.startswith(root))

    def test_failed_provision_does_not_rewrite_bridge(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "guest-sdk.json")
            original = '{"format_version": 2, "python": ["old-python"]}'
            with open(path, "w") as f:
                f.write(original)
            with mock.patch.object(setup, "ensure_setup_python", side_effect=setup.SetupError("download failed")):
                with self.assertRaisesRegex(setup.SetupError, "download failed"):
                    setup.repair_guest_python(d)
            with open(path) as f:
                self.assertEqual(f.read(), original)
            self.assertFalse(os.path.exists(path + ".tmp"))

    def test_portable_repair_keeps_external_python_absolute(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "guest-sdk.json")
            with open(path, "w") as f:
                json.dump({"format_version": 2}, f)
            outside = os.path.abspath(os.path.join(d, "..", "external-python"))
            with mock.patch.object(setup, "ensure_setup_python", return_value=outside), \
                 mock.patch.multiple(setup, PORTABLE=True, PKG=d):
                setup.repair_guest_python(d)
            with open(path) as f:
                self.assertEqual(json.load(f)["python"], [outside])

    def test_repair_preserves_compiler_and_config_fields(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "guest-sdk.json")
            config = {"format_version": 2, "python": ["old-python"], "compiler": ["cc", "-flag"], "include": "sdk"}
            with open(path, "w") as f:
                json.dump(config, f)
            with mock.patch.object(setup, "ensure_setup_python", return_value="new-python"), mock.patch.object(setup, "PORTABLE", False):
                setup.repair_guest_python(d)
            with open(path) as f:
                result = json.load(f)
            self.assertEqual(result, dict(config, python=["new-python"]))


class Keys(unittest.TestCase):
    def test_raw_and_hex(self):
        raw = bytes(range(16))
        self.assertEqual(setup.parse_key(raw), raw)
        self.assertEqual(setup.parse_key(KEY_HEX), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key(KEY_HEX.upper() + "\r\n"), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key("0x" + KEY_HEX), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key(" ".join(KEY_HEX[i:i + 8] for i in range(0, 32, 8))), bytes.fromhex(KEY_HEX))
        self.assertEqual(setup.parse_key(KEY_HEX.encode()), bytes.fromhex(KEY_HEX))

    def test_malformed(self):
        for bad in ("", "1234", KEY_HEX[:-1], KEY_HEX + "0", "zz" * 16, b"\xff" * 15, b"\xff" * 17):
            self.assertIsNone(setup.parse_key(bad), bad)

    def test_key_file(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "k.key")
            with open(p, "wb") as f:
                f.write(bytes(range(16)))
            self.assertEqual(setup.read_key_file(p), bytes(range(16)))
            with open(p, "w") as f:
                f.write(KEY_HEX + "\n")
            self.assertEqual(setup.read_key_file(p), bytes.fromhex(KEY_HEX))
            with open(p, "wb") as f:
                f.write(b"x" * 5000)
            self.assertIsNone(setup.read_key_file(p))
            self.assertIsNone(setup.read_key_file(os.path.join(d, "missing")))

    def test_stdin_blob(self):
        k = setup.Keys()
        k.disc, k.common = bytes(16), bytes.fromhex(KEY_HEX)
        self.assertEqual(k.stdin_blob(), ("disc %s\ncommon %s\n" % ("00" * 16, KEY_HEX)).encode())


class Paths(unittest.TestCase):
    def test_clean_path(self):
        self.assertEqual(setup.clean_path('"/tmp/a b/c.wux"'), os.path.abspath("/tmp/a b/c.wux"))
        self.assertEqual(setup.clean_path("'/tmp/x'"), os.path.abspath("/tmp/x"))
        self.assertEqual(setup.clean_path("  "), "")
        if not setup.IS_WIN:
            self.assertEqual(setup.clean_path("/tmp/My\\ Game.wux "), "/tmp/My Game.wux")

    def test_game_folder(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertFalse(setup.valid_game_folder(d))
            os.makedirs(os.path.join(d, "code"))
            os.makedirs(os.path.join(d, "content"))
            os.makedirs(os.path.join(d, "meta"))
            open(os.path.join(d, "code", "cking.rpx"), "wb").close()
            with open(os.path.join(d, "meta", "meta.xml"), "w") as f:
                f.write('<menu><title_id type="hexBinary" length="8">0005000010143500</title_id></menu>')
            self.assertTrue(setup.valid_game_folder(d))
            self.assertEqual(setup.game_folder_title(d), "0005000010143500")


class DataDir(unittest.TestCase):
    """default_data_dir: portable.txt next to the release means <release>/data; without it (a source
    build, or an AppImage whose mount is read-only, issue #55) the per-user folder of earlier
    releases. --data-dir overrides both (setup_gui.cpp data_dir_of mirrors this)."""

    def setUp(self):
        self._portable = setup.PORTABLE

    def tearDown(self):
        setup.PORTABLE = self._portable

    def test_portable_uses_the_release_folder(self):
        setup.PORTABLE = True
        self.assertEqual(setup.default_data_dir(), os.path.join(setup.PKG, "data"))

    def test_without_the_marker_uses_the_per_user_folder(self):
        setup.PORTABLE = False
        self.assertEqual(setup.default_data_dir(), setup.legacy_data_dir())

    @unittest.skipUnless(setup.IS_LINUX, "the XDG rule is the Linux one")
    def test_legacy_respects_xdg_data_home(self):
        saved = os.environ.get("XDG_DATA_HOME")
        os.environ["XDG_DATA_HOME"] = "/tmp/wwhd-xdg"
        try:
            self.assertEqual(setup.legacy_data_dir(), os.path.join("/tmp/wwhd-xdg", "wwhd"))
        finally:
            if saved is None:
                os.environ.pop("XDG_DATA_HOME", None)
            else:
                os.environ["XDG_DATA_HOME"] = saved

    def test_legacy_folder_name(self):
        # the same name as host::config_dir on Linux; "WWHD" on Windows (setup_gui.cpp data_dir_of)
        self.assertEqual(os.path.basename(setup.legacy_data_dir()), "WWHD" if setup.IS_WIN else "wwhd")


class Titles(unittest.TestCase):
    def test_supported_ok(self):
        setup.check_title("0005000010143500")   # USA, the canonical build
        setup.check_title("0005000010143600")   # Europe (tools/recomp/builds/eu.json)

    def test_unsupported(self):
        with self.assertRaisesRegex(setup.SetupError, "Japan.*can be built from"):
            setup.check_title("0005000010143400")
        with self.assertRaisesRegex(setup.SetupError, "not The Wind Waker HD"):
            setup.check_title("000500001010ec00")


def _title(tid, version, files=10, size=1000):
    return {"id": tid, "version": version, "folder": "%s_v%d" % (tid, version), "files": files, "bytes": size}


class ArchiveTitles(unittest.TestCase):
    """Which title of a Cemu archive (.wua) is used (info as wwhd-extract --title 0005000010143500 info prints it)."""

    BASE, UPDATE = _title("0005000010143500", 0), _title("0005000e10143500", 16)

    def info(self, titles, selected=None):
        i = {"format": "wua", "titles": titles}
        if selected:
            i.update(selected=selected["folder"], title_id=selected["id"], version=str(selected["version"]),
                     files=str(selected["files"]), bytes=str(selected["bytes"]))
        return i

    def test_base_only(self):
        self.assertEqual(setup.archive_choice(self.info([self.BASE], self.BASE)), ("0005000010143500_v0", []))

    def test_update_not_used(self):
        folder, notes = setup.archive_choice(self.info([self.UPDATE, self.BASE, _title("0005000c10143500", 3)], self.BASE))
        self.assertEqual(folder, "0005000010143500_v0")
        self.assertEqual(len(notes), 2)
        self.assertIn("the update for The Wind Waker HD (USA), version 16", notes[0])
        self.assertIn("version 0", notes[0])
        self.assertIn("downloadable content", notes[1])

    def test_update_without_game(self):
        with self.assertRaisesRegex(setup.SetupError, "only the update"):
            setup.archive_choice(self.info([self.UPDATE]))

    def test_european_archive(self):
        eu, eu_update = _title("0005000010143600", 0), _title("0005000e10143600", 16)
        folder, notes = setup.archive_choice(self.info([eu, eu_update], eu))
        self.assertEqual(folder, "0005000010143600_v0")
        self.assertIn("the update for The Wind Waker HD (Europe), version 16", notes[0])

    def test_other_region(self):
        with self.assertRaisesRegex(setup.SetupError, "archive contains the Japan version"):
            setup.archive_choice(self.info([_title("0005000010143400", 0), _title("0005000e10143400", 16)]))

    def test_other_game(self):
        with self.assertRaisesRegex(setup.SetupError, "does not contain The Wind Waker HD.*title 00050000-1010EC00"):
            setup.archive_choice(self.info([_title("000500001010ec00", 0)]))
        with self.assertRaisesRegex(setup.SetupError, "no Wii U titles"):
            setup.archive_choice(self.info([]))

    def test_other_version(self):
        v2 = _title("0005000010143500", 2)
        with self.assertRaisesRegex(setup.SetupError, "version 2.*built for version 0"):
            setup.archive_choice(self.info([v2], v2))

    def test_title_desc(self):
        self.assertEqual(setup.title_desc("0005000010143500", 0), "The Wind Waker HD (USA), version 0")
        self.assertEqual(setup.title_desc("0005000E10143400"), "the update for The Wind Waker HD (Japan)")

    def test_plan(self):
        self.assertEqual(setup.plan_steps("archive"), ["archive", "compiler", "extract", "translate", "compile", "app"])
        self.assertEqual(setup.EXTRACT_ERRORS[10], "wrong_title")


class LanguageSourceBuild(unittest.TestCase):
    """A language source lends a European or Japanese game's text to the USA code, so it is only for
    the USA build (docs/language-packs.md, docs/builds.md)."""

    def make(self, d, rpx):
        os.makedirs(os.path.join(d, "code"), exist_ok=True)
        with open(os.path.join(d, "code", "cking.rpx"), "wb") as f:
            f.write(rpx)
        return d

    def setUp(self):
        self.saved = setup.game_builds.by_sha256
        usa = setup.game_builds.Build({"name": "USA", "title_id": "0005000010143500",
                                       "rpx_sha256": hashlib.sha256(b"usa").hexdigest()})
        eu = setup.game_builds.Build({"name": "EU", "title_id": "0005000010143600",
                                      "code_bounds": ["02000000", "03000000"],
                                      "data_bounds": ["10000000", "10500000"],
                                      "rpx_sha256": hashlib.sha256(b"eu").hexdigest()})
        setup.game_builds.by_sha256 = lambda dg: next((b for b in (usa, eu) if b.sha256 == dg), None)

    def tearDown(self):
        setup.game_builds.by_sha256 = self.saved

    def test_usa_build_allows_it(self):
        with tempfile.TemporaryDirectory() as d:
            setup.check_language_source_allowed(self.make(d, b"usa"))

    def test_nothing_installed_yet_allows_it(self):
        with tempfile.TemporaryDirectory() as d:
            setup.check_language_source_allowed(d)

    def test_european_build_refuses_it(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, b"eu")
            with self.assertRaisesRegex(setup.SetupError, "EU build.*own languages.*only for the USA build"):
                setup.check_language_source_allowed(d)


class GameVersion(unittest.TestCase):
    """code/cking.rpx must be one of the builds the port knows (version 0 of a region,
    tools/recomp/builds.py); synthetic files, made-up bytes."""

    def make(self, d, rpx=b"made-up rpx", app_tid="0005000010143500", app_ver="0000"):
        os.makedirs(os.path.join(d, "code"), exist_ok=True)
        with open(os.path.join(d, "code", "cking.rpx"), "wb") as f:
            f.write(rpx)
        with open(os.path.join(d, "code", "app.xml"), "w") as f:
            f.write('<app><title_id type="hexBinary" length="8">%s</title_id>\n'
                    '<title_version type="hexBinary" length="2">%s</title_version></app>' % (app_tid, app_ver))

    def setUp(self):
        self.saved = (setup.SUPPORTED_BUILDS, setup.game_builds.by_sha256)
        fake = [setup.game_builds.Build({"name": "USA", "title_id": "0005000010143500",
                                         "rpx_sha256": hashlib.sha256(b"made-up rpx").hexdigest()}),
                setup.game_builds.Build({"name": "EU", "title_id": "0005000010143600",
                                         "code_bounds": ["02000000", "03000000"],
                                         "data_bounds": ["10000000", "10500000"],
                                         "rpx_sha256": hashlib.sha256(b"made-up eu rpx").hexdigest()})]
        setup.SUPPORTED_BUILDS = {b.title_id: b for b in fake}
        setup.game_builds.by_sha256 = lambda d: next((b for b in fake if b.sha256 == d), None)

    def tearDown(self):
        setup.SUPPORTED_BUILDS, setup.game_builds.by_sha256 = self.saved

    def test_expected_file(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d)
            self.assertEqual(setup.check_game_version(d).name, "USA")

    def test_other_build(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, rpx=b"made-up eu rpx", app_tid="0005000010143600")
            self.assertEqual(setup.check_game_version(d).name, "EU")

    def test_update_merged_in(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, rpx=b"other code", app_ver="0010")
            with self.assertRaisesRegex(setup.SetupError, "version 16 of the game.*update merged in.*"
                                                          "00050000-10143500 \\(USA\\).*version 0.*"
                                                          "Use the game's own files"):
                setup.check_game_version(d)
            self.make(d, rpx=b"other code", app_tid="0005000E10143500", app_ver="0000")
            with self.assertRaisesRegex(setup.SetupError, "from the update.*merged in"):
                setup.check_game_version(d)

    def test_unknown_build(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, rpx=b"damaged")
            with self.assertRaisesRegex(setup.SetupError, "not a file the port knows \\(SHA-256 [0-9a-f]{16}\\.\\.\\.\\)"):
                setup.check_game_version(d)
            os.remove(os.path.join(d, "code", "app.xml"))
            with self.assertRaisesRegex(setup.SetupError, "not a file the port knows"):
                setup.check_game_version(d)

    def test_unsupported_region(self):
        with tempfile.TemporaryDirectory() as d:
            self.make(d, rpx=b"jp", app_tid="0005000010143400")
            with self.assertRaisesRegex(setup.SetupError, "The Wind Waker HD \\(Japan\\)"):
                setup.check_game_version(d)

    def test_missing(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(setup.SetupError, "cannot read"):
                setup.check_game_version(d)

    @unittest.skipUnless(os.environ.get("WWHD_GAME_DIR"), "WWHD_GAME_DIR (your own extracted game) not set")
    def test_real_game(self):
        setup.SUPPORTED_BUILDS, setup.game_builds.by_sha256 = self.saved
        build = setup.check_game_version(os.environ["WWHD_GAME_DIR"])  # read only
        self.assertIn(build.title_id, setup.SUPPORTED_BUILDS)


class Recipe(unittest.TestCase):
    def test_substitution(self):
        m = {"sdk": "/p/sdk", "gamecode": "/w/libgamecode.a", "out": "/d/bin/wwhd"}
        self.assertEqual(setup.sub("{sdk}/obj/a.o", m), "/p/sdk/obj/a.o")
        self.assertEqual(setup.sub("-I{sdk}/include", m), "-I/p/sdk/include")
        self.assertEqual(setup.sub("{gamecode}", m), "/w/libgamecode.a")
        self.assertEqual(setup.sub("-O3", m), "-O3")
        self.assertEqual(setup.fwd("C:\\a\\b"), "C:/a/b")

    def test_toolchains_pinned(self):
        tcs = setup.load_toolchains()
        for name, tc in tcs["toolchains"].items():
            if tc["kind"] != "xcode-clt":
                self.assertRegex(tc["sha256"], r"^[0-9a-f]{64}$", name)
                self.assertTrue(tc["url"].startswith("https://"), name)
        for name, py in tcs["python"].items():
            self.assertRegex(py["sha256"], r"^[0-9a-f]{64}$", name)

    def test_jobs(self):
        self.assertGreaterEqual(setup.default_jobs(), 1)


class Arch(unittest.TestCase):
    def test_normalize(self):
        for a, want in (("x86_64", "x86_64"), ("AMD64", "x86_64"), ("aarch64", "aarch64"), ("arm64", "aarch64")):
            self.assertEqual(setup.normalize_arch(a), want)
        self.assertIn(setup.host_arch(), ("x86_64", "aarch64"))

    def test_linux_pins_per_arch(self):
        tcs = setup.load_toolchains()
        self.assertEqual(tcs["toolchains"]["zig-0.16.0"]["target"].split("-")[0], "x86_64")
        self.assertEqual(tcs["toolchains"]["zig-0.16.0-aarch64"]["target"].split("-")[0], "aarch64")
        self.assertIn("aarch64", tcs["toolchains"]["zig-0.16.0-aarch64"]["url"])
        self.assertIn("aarch64", tcs["python"]["linux-aarch64"]["url"])


class NoScriptHost(unittest.TestCase):
    """Antivirus heuristics read "unsigned program starts PowerShell" as a dropper (issue #58): the Windows setup
    uses the Windows API instead (the release ships Python, setup.py uses ctypes)."""

    def test_no_powershell(self):
        here = os.path.dirname(os.path.abspath(__file__))
        for name in ("setup.py", "install-windows.bat"):
            with open(os.path.join(here, name), encoding="utf-8") as f:
                self.assertNotIn("powershell", f.read().lower(), name)
        self.assertFalse(os.path.exists(os.path.join(here, "bootstrap-windows.ps1")))

    @unittest.skipUnless(setup.IS_WIN, "Windows only")
    def test_shortcut(self):
        self.assertTrue(os.path.isdir(setup.win_known_folder(0x02)))
        self.assertTrue(os.path.isdir(setup.win_known_folder(0x10)))
        with tempfile.TemporaryDirectory() as d:
            link = os.path.join(d, "Wind Waker HD test.lnk")
            setup.win_shortcut(link, sys.executable, "--game game --save save", d, sys.executable)
            with open(link, "rb") as f:
                head = f.read(20)
            self.assertEqual(head[:4], b"\x4c\x00\x00\x00")  # a shell link header
            self.assertEqual(head[4:20], bytes.fromhex("0114020000000000c000000000000046"))  # its CLSID
            setup.win_shortcut(link, sys.executable, workdir=d)  # replaces it


class BundledPython(unittest.TestCase):
    """The Windows release ships the pinned embeddable Python in tools/python; guard.py allows exactly its files."""

    def setUp(self):
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "release"))
        import guard
        self.guard = guard
        with open(guard.PYTHON_FILES) as f:
            self.expected = json.load(f)

    def test_manifest_matches_pin(self):
        pin = setup.load_toolchains()["python"]["windows"]
        self.assertEqual(self.expected["sha256"], pin["sha256"])
        self.assertEqual(self.expected["url"], pin["url"])
        for name in ("python.exe", "pythonw.exe", "python3.dll", "LICENSE.txt"):
            self.assertIn(name, self.expected["files"])

    def test_guard_rejects_other_files(self):
        with tempfile.TemporaryDirectory() as d:
            py = os.path.join(d, "WindWakerHD-x", "tools", "python")
            os.makedirs(py)
            with open(os.path.join(py, "python.exe"), "wb") as f:
                f.write(b"MZ not the real one")
            with open(os.path.join(py, "dropper.exe"), "wb") as f:
                f.write(b"MZ")
            problems, _ = self.guard.scan(d)
            text = "\n".join(problems)
            self.assertIn("python.exe: differs", text)
            self.assertIn("dropper.exe: not a file", text)
            self.assertIn("lacks files", text)

    def test_windows_finds_bundled_python(self):
        # what the GUI and --console-setup start (tools/installer/gui/console_setup_win.cpp)
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "gui", "console_setup_win.cpp")) as f:
            self.assertIn('"tools\\\\python\\\\python.exe"', f.read())


class NonInteractive(unittest.TestCase):
    def test_ui_refuses_to_prompt(self):
        ui = setup.UI(False)
        with self.assertRaises(setup.SetupError):
            ui.ask("question")
        self.assertTrue(ui.yesno("q", True))
        self.assertFalse(ui.yesno("q", True, noninteractive=False))


def _game_folder(root, title_id, packs, extra=()):
    """A SYNTHETIC game folder: meta.xml with a title id, dummy files named like the language packs
    (a SARC tag and made-up bytes, no game data)."""
    os.makedirs(os.path.join(root, "meta"), exist_ok=True)
    with open(os.path.join(root, "meta", "meta.xml"), "w") as f:
        f.write('<menu><title_id type="hexBinary" length="8">%s</title_id></menu>' % title_id)
    pack = os.path.join(root, "content", "Common", "Pack")
    os.makedirs(pack, exist_ok=True)
    for name in packs:
        with open(os.path.join(pack, name), "wb") as f:
            f.write(b"SARC synthetic " + name.encode())
    for rel, data in extra:
        os.makedirs(os.path.dirname(os.path.join(root, rel)), exist_ok=True)
        with open(os.path.join(root, rel), "wb") as f:
            f.write(data)


EU_PACKS = ["permanent_2d_EuEnglish.pack", "permanent_2d_EuFrench.pack", "permanent_2d_EuGerman.pack",
            "permanent_2d_EuItalian.pack", "permanent_2d_EuSpanish.pack"]


class LanguageSources(unittest.TestCase):
    """setup --language-source on synthetic folders (no game files in the tests)."""

    def test_titles(self):
        self.assertEqual(setup.language_source_region("0005000010143600"), "EU")
        self.assertEqual(setup.language_source_region("0005000010143400"), "JP")
        with self.assertRaisesRegex(setup.SetupError, "USA game"):
            setup.language_source_region("0005000010143500")
        with self.assertRaisesRegex(setup.SetupError, "update"):
            setup.language_source_region("0005000e10143600")
        with self.assertRaisesRegex(setup.SetupError, "not the European or Japanese"):
            setup.language_source_region("000500001010ec00")
        with self.assertRaisesRegex(setup.SetupError, "not the European or Japanese"):
            setup.language_source_region(None)

    def test_kind(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(setup.language_source_kind(d), "folder")
        self.assertEqual(setup.language_source_kind("/x/eu.WUA"), "archive")
        self.assertEqual(setup.language_source_kind("/x/eu.wux"), "image")
        with self.assertRaises(setup.SetupError):
            setup.language_source_kind("/x/eu.zip")

    def test_folder_european(self):
        with tempfile.TemporaryDirectory() as d:
            src, data = os.path.join(d, "eur"), os.path.join(d, "data")
            _game_folder(src, "0005000010143600", EU_PACKS + ["permanent_2d_EuRussian.pack"],
                         extra=[("code/cking.rpx", b"synthetic code"), ("content/Common/Pack/permanent_3d.pack", b"SARC 3d"),
                                ("content/Common/Layout/Title_00.szs", b"synthetic")])
            os.makedirs(data)
            (m,) = setup.add_language_source(("folder", src), data)
            self.assertEqual(m["region"], "EU")
            self.assertEqual([p["language"] for p in m["packs"]], ["English", "French", "German", "Italian", "Spanish"])
            dst = os.path.join(data, "game-lang", "EU")
            taken = sorted(os.path.relpath(os.path.join(dp, f), dst).replace(os.sep, "/")
                           for dp, _, fs in os.walk(dst) for f in fs)
            self.assertEqual(taken, sorted(["content/Common/Pack/" + n for n in EU_PACKS] +
                                           ["language-source.json", "meta/meta.xml"]))  # nothing else, no code
            german = [p for p in m["packs"] if p["language"] == "German"][0]
            self.assertEqual(german["sha256"], hashlib.sha256(b"SARC synthetic permanent_2d_EuGerman.pack").hexdigest())
            self.assertFalse(os.path.exists(os.path.join(data, "game-lang", "EU.partial")))
            self.assertEqual([x["region"] for x in setup.language_sources(data)], ["EU"])
            # again: replaces the earlier one
            setup.add_language_source(("folder", os.path.join(src, "content")), data)  # a subfolder is taken up
            self.assertEqual(len(setup.language_sources(data)), 1)
            setup.remove_language_source(data, "eu")
            self.assertEqual(setup.language_sources(data), [])
            with self.assertRaises(setup.SetupError):
                setup.remove_language_source(data, "EU")
            with self.assertRaises(setup.SetupError):
                setup.remove_language_source(data, "../game")

    def test_folder_japanese_any_case(self):
        with tempfile.TemporaryDirectory() as d:
            src, data = os.path.join(d, "jpn"), os.path.join(d, "data")
            _game_folder(src, "0005000010143400", [])
            os.rename(os.path.join(src, "content", "Common"), os.path.join(src, "content", "COMMON"))
            with open(os.path.join(src, "content", "COMMON", "Pack", "PERMANENT_2D_JPJAPANESE.PACK"), "wb") as f:
                f.write(b"SARC synthetic jp")
            os.makedirs(data)
            (m,) = setup.add_language_source(("folder", src), data)
            self.assertEqual((m["region"], [p["language"] for p in m["packs"]]), ("JP", ["Japanese"]))

    def test_folder_refused(self):
        with tempfile.TemporaryDirectory() as d:
            data = os.path.join(d, "data")
            os.makedirs(data)
            usa = os.path.join(d, "usa")
            _game_folder(usa, "0005000010143500", ["permanent_2d_UsEnglish.pack"])
            with self.assertRaisesRegex(setup.SetupError, "USA game"):
                setup.add_language_source(("folder", usa), data)
            empty = os.path.join(d, "empty")
            _game_folder(empty, "0005000010143600", ["permanent_2d_UsEnglish.pack"])
            with self.assertRaisesRegex(setup.SetupError, "no language packs"):
                setup.add_language_source(("folder", empty), data)
            bad = os.path.join(d, "bad")
            _game_folder(bad, "0005000010143600", [])
            with open(os.path.join(bad, "content", "Common", "Pack", "permanent_2d_EuGerman.pack"), "wb") as f:
                f.write(b"not a pack")
            with self.assertRaisesRegex(setup.SetupError, "not a language pack"):
                setup.add_language_source(("folder", bad), data)
            self.assertEqual(os.listdir(os.path.join(data, "game-lang")), [])  # nothing left behind

    def test_image_and_archive_take_only_language_files(self):
        """The extractor is asked for the language files only (wwhd-extract --only), for the right title."""
        calls = []

        def fake_extract(image, keys, out, title=None, only=None):
            calls.append((image, title, list(only or [])))
            _game_folder(out, "0005000010143600" if title != "0005000010143400_v0" else "0005000010143400",
                         EU_PACKS[:2] if title != "0005000010143400_v0" else ["permanent_2d_JpJapanese.pack"])

        def fake_archive_info(path, title=setup.SUPPORTED_TITLE):
            self.assertIsNone(title)
            return "wrong_title", "", {"titles": [
                {"id": "0005000010143500", "version": 0, "folder": "0005000010143500_v0", "files": 1, "bytes": 1},
                {"id": "0005000010143600", "version": 0, "folder": "0005000010143600_v0", "files": 1, "bytes": 1},
                {"id": "0005000010143400", "version": 0, "folder": "0005000010143400_v0", "files": 1, "bytes": 1}]}
        saved = setup.run_extract, setup.archive_info
        setup.run_extract, setup.archive_info = fake_extract, fake_archive_info
        try:
            with tempfile.TemporaryDirectory() as d:
                ms = setup.add_language_source(("image", os.path.join(d, "eu.wux")), d, keys=setup.Keys(),
                                               info={"title_id": "0005000010143600"})
                self.assertEqual(ms[0]["region"], "EU")
                self.assertEqual(calls[-1], (os.path.join(d, "eu.wux"), None, setup.LANGUAGE_SOURCE_FILES))
                with self.assertRaisesRegex(setup.SetupError, "USA game"):
                    setup.add_language_source(("image", "usa.wux"), d, keys=setup.Keys(),
                                              info={"title_id": "0005000010143500"})
                ms = setup.add_language_source(("archive", os.path.join(d, "both.wua")), d)
                self.assertEqual([m["region"] for m in ms], ["EU", "JP"])  # the USA title is left alone
                self.assertEqual([c[1] for c in calls[-2:]], ["0005000010143600_v0", "0005000010143400_v0"])
                self.assertEqual([m["region"] for m in setup.language_sources(d)], ["EU", "JP"])
        finally:
            setup.run_extract, setup.archive_info = saved
        self.assertEqual(setup.LANGUAGE_SOURCE_FILES, ["content/Common/Pack/permanent_2d_*.pack", "meta/meta.xml"])


class CodeModsBuild(unittest.TestCase):
    def test_option_default_and_override(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertFalse(setup.code_mods.hooks_option())
        with mock.patch.dict(os.environ, {"WWHD_CODE_MODS": "1"}):
            self.assertTrue(setup.code_mods.hooks_option())
            self.assertFalse(setup.code_mods.hooks_option("0"))
        with self.assertRaises(ValueError):
            setup.code_mods.hooks_option("yes")

    def test_setup_choice_question_remembered_and_overrides(self):
        from pathlib import Path
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as d, mock.patch.dict(os.environ, {}, clear=True):
            ctx = SimpleNamespace(data_dir=d, args=SimpleNamespace(code_mods=None),
                                  state=lambda: {})
            ui = mock.Mock(interactive=True)
            ui.yesno.side_effect = lambda question, default: default
            self.assertFalse(setup.setup_code_mods(ctx, ui))
            ui.yesno.assert_called_with("Build with code-mod support?", False)
            Path(d, "code-mods-active.json").write_text(json.dumps({"state": "ready", "hooks": True}))
            self.assertTrue(setup.setup_code_mods(ctx, ui))
            ui.yesno.assert_called_with("Build with code-mod support?", True)
            ui.reset_mock()
            ctx.args.code_mods = "0"
            self.assertFalse(setup.setup_code_mods(ctx, ui))
            ui.yesno.assert_not_called()
            ctx.args.code_mods = None
            with mock.patch.dict(os.environ, {"WWHD_CODE_MODS": "0"}):
                self.assertFalse(setup.setup_code_mods(ctx, ui))
                ui.yesno.assert_not_called()
            Path(d, "code-mods-active.json").write_text("broken")
            ctx.state = lambda: {"code_mods": True}
            self.assertTrue(setup.setup_code_mods(ctx, setup.UI(False)))

    def test_setup_writes_both_host_settings_preserving_other_options(self):
        from pathlib import Path
        import plistlib
        with tempfile.TemporaryDirectory() as d:
            for mac in (False, True):
                filename = "display.plist" if mac else "settings.ini"
                path = Path(d, "user", filename)
                path.parent.mkdir(exist_ok=True)
                path.write_bytes(plistlib.dumps({"other": "keep"}) if mac else b"other=keep\ncode-mods=0\n")
                with mock.patch.multiple(setup, IS_MAC=mac, PORTABLE=True), \
                     mock.patch.dict(os.environ, {}, clear=True):
                    for hooks in (True, False):
                        setup.write_code_mods_setting(d, hooks)
                        values = plistlib.loads(path.read_bytes()) if mac else dict(
                            line.split("=", 1) for line in path.read_text().splitlines())
                        self.assertEqual(values["other"], "keep")
                        self.assertEqual(values["code-mods"], "1" if hooks else "0")

    def test_setup_host_setting_locations_and_overrides(self):
        from pathlib import Path
        import plistlib
        with tempfile.TemporaryDirectory() as d, mock.patch.dict(os.environ, {
                "HOME": d, "APPDATA": d, "XDG_CONFIG_HOME": d}, clear=True):
            for mac, win, suffix in ((True, False, "Library/Application Support/wwhd/display.plist"),
                                     (False, True, "WWHD/settings.ini"),
                                     (False, False, "wwhd/settings.ini")):
                with mock.patch.multiple(setup, IS_MAC=mac, IS_WIN=win, PORTABLE=False):
                    setup.write_code_mods_setting(d, True)
                    self.assertTrue(Path(d, suffix).is_file())
                    override = Path(d, "override.plist" if mac else "override.ini")
                    with mock.patch.dict(os.environ, {
                            "WWHD_DISPLAY_SETTINGS" if mac else "WWHD_SETTINGS": str(override)}):
                        setup.write_code_mods_setting(d, False)
                    values = plistlib.loads(override.read_bytes()) if mac else dict(
                        line.split("=", 1) for line in override.read_text().splitlines())
                    self.assertEqual(values["code-mods"], "0")

    def test_setup_install_publishes_choice_settings_and_variant(self):
        from pathlib import Path
        from types import SimpleNamespace
        import plistlib
        package_test = os.environ.get("WWHD_SETUP_MOD_PACKAGE_TEST")
        with tempfile.TemporaryDirectory() as d, mock.patch.dict(os.environ, {}, clear=True):
            root = Path(d)
            game = root / "game"
            (game / "code").mkdir(parents=True)
            (game / "code/cking.rpx").write_bytes(b"synthetic fingerprint input")
            (root / "sdk").mkdir()
            data = root / "data"
            data.mkdir()
            tc = SimpleNamespace(cc=["test-compiler"], env={}, desc="fixture")
            args = SimpleNamespace(code_mods="1", jobs=4, shortcuts=False, keep_work=False)
            ctx = SimpleNamespace(data_dir=str(data), game_dir=str(game), args=args,
                                  manifest={"platform": "fixture", "toolchain": {}, "exe": "fixture"},
                                  exe=str(data / "bin/fixture"), exe_dir=str(data / "bin"),
                                  version="test", state=lambda: {})
            def translate(game, gen, hooks=False):
                Path(gen).mkdir()
                Path(gen, "mode.txt").write_text("on" if hooks else "off")
                return 1
            def link(tc, manifest, objs, work, target):
                Path(target).write_bytes(b"synthetic executable")
            with mock.patch.multiple(setup, PKG=root, PORTABLE=True, IS_MAC=True), \
                 mock.patch.object(setup, "valid_game_folder", return_value=True), \
                 mock.patch.object(setup, "game_folder_title", return_value=None), \
                 mock.patch.object(setup, "check_game_version"), \
                 mock.patch.object(setup, "get_toolchain", return_value=tc), \
                 mock.patch.object(setup, "run_logged", return_value="fixture compiler"), \
                 mock.patch.object(setup, "free_space", return_value=20 << 30), \
                 mock.patch.object(setup, "recompile", side_effect=translate) as recomp, \
                 mock.patch.object(setup, "compile_gamecode", return_value=[]), \
                 mock.patch.object(setup, "link_game", side_effect=link), \
                 mock.patch.object(setup, "write_guest_build_config"):
                for mode in ("1", "0", "1"):
                    args.code_mods = mode
                    state = setup.install(ctx, ("installed", str(game)))
                    on = mode == "1"
                    self.assertEqual(state["code_mods"], on)
                    self.assertEqual(recomp.call_args.kwargs["hooks"], on)
                    values = plistlib.loads((data / "user/display.plist").read_bytes())
                    self.assertEqual(values["code-mods"], mode)
                    active = json.loads((data / "code-mods-active.json").read_text())
                    ready = json.loads((Path(active["exe"]).parents[1] / "ready.json").read_text())
                    self.assertEqual(active["hooks"], on)
                    self.assertEqual(ready["hooks"], on)
                    self.assertEqual(active["fingerprint"], ready["fingerprint"])
                    self.assertTrue(Path(active["exe"]).is_file())
                    self.assertEqual(active["user_dir"], str((data / "user").resolve()))
                    if package_test:
                        with mock.patch.object(setup, "IS_MAC", False):
                            setup.write_code_mods_setting(data, on)
                        import subprocess
                        subprocess.run([package_test,
                                        "--setup-code-mods", str(data)], check=True)


    def test_fingerprint_tracks_build_inputs_and_mode(self):
        from pathlib import Path
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "game/code").mkdir(parents=True)
            (root / "game/code/cking.rpx").write_bytes(b"synthetic test input")
            (root / "sdk").mkdir()
            source = root / "sdk/runtime.o"
            source.write_bytes(b"runtime fixture")
            def key(hooks=False, compiler="clang", manifest=None):
                return setup.code_mods.fingerprint(root, manifest or {"exe": "game"},
                                                   root / "game", compiler, hooks)
            first = key()
            self.assertEqual(first, key())
            self.assertNotEqual(first, key(True))
            self.assertNotEqual(first, key(compiler="new clang"))
            self.assertNotEqual(first, key(manifest={"exe": "other"}))
            source.write_bytes(b"updated runtime fixture")
            self.assertNotEqual(first, key())

    def test_cache_modes_corruption_and_failed_link(self):
        from pathlib import Path
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "game/code").mkdir(parents=True)
            (root / "game/code/cking.rpx").write_bytes(b"synthetic input")
            (root / "sdk").mkdir()
            data = root / "data"
            ctx = SimpleNamespace(data_dir=str(data), game_dir=str(root / "game"),
                                  manifest={"toolchain": {}, "exe": "fixture"},
                                  args=SimpleNamespace(jobs=4))
            mode = []
            def translate(game, gen, hooks=False):
                Path(gen).mkdir(); mode[:] = [hooks]
            def compile_code(tc, manifest, gen, obj, jobs, **kwargs):
                Path(obj).mkdir(); kwargs["cancel"](); kwargs["progress"](1, 1)
                return []
            def link(tc, manifest, objs, work, target):
                Path(target).write_bytes(b"hooks on" if mode[0] else b"hooks off")
            tc = SimpleNamespace(cc=["fixture compiler"], env={})
            with mock.patch.multiple(setup, PKG=root, PORTABLE=False), \
                 mock.patch.object(setup, "get_toolchain", return_value=tc), \
                 mock.patch.object(setup, "run_logged", return_value="fixture compiler version"), \
                 mock.patch.object(setup, "free_space", return_value=20 << 30), \
                 mock.patch.object(setup, "recompile", side_effect=translate) as recomp, \
                 mock.patch.object(setup, "compile_gamecode", side_effect=compile_code), \
                 mock.patch.object(setup, "link_game", side_effect=link) as linker:
                data.mkdir()
                initial = root / "initial.exe"
                initial.write_bytes(b"hooks off")
                generated = root / "initial-gen"
                generated.mkdir()
                setup.code_mods.remember_installed(setup, ctx, tc, False, initial, generated, [])
                active = json.loads((data / "code-mods-active.json").read_text())
                self.assertFalse(active["hooks"])
                self.assertTrue(Path(active["exe"]).is_file())
                off = setup.code_mods.rebuild(setup, ctx, False)
                self.assertTrue(off["cached"])
                on = setup.code_mods.rebuild(setup, ctx, True)
                self.assertNotEqual(off["fingerprint"], on["fingerprint"])
                self.assertTrue(Path(off["exe"]).is_file())
                again = setup.code_mods.rebuild(setup, ctx, False)
                self.assertTrue(again["cached"])
                self.assertEqual(recomp.call_count, 1)
                # Bad metadata rebuilds rather than trusting an unrelated executable.
                ready = Path(on["exe"]).parents[1] / "ready.json"
                ready.write_text("broken json")
                previous = (data / "code-mods-active.json").read_bytes()
                linker.side_effect = setup.SetupError("synthetic link failure")
                with self.assertRaisesRegex(setup.SetupError, "link failure"):
                    setup.code_mods.rebuild(setup, ctx, True)
                self.assertEqual((data / "code-mods-active.json").read_bytes(), previous)
                self.assertTrue(Path(off["exe"]).is_file())
                self.assertFalse(list(data.glob("*.partial")))

    def test_build_lock_excludes_concurrent_writer_and_reopens(self):
        from pathlib import Path
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "code-build.lock"
            first = setup.code_mods.BuildLock(path, setup.SetupError)
            try:
                with self.assertRaisesRegex(setup.SetupError, "Another code-mod rebuild"):
                    setup.code_mods.BuildLock(path, setup.SetupError)
            finally:
                first.close()
            second = setup.code_mods.BuildLock(path, setup.SetupError)
            second.close()

    def test_low_space_evicts_only_inactive_owned_cache(self):
        from pathlib import Path
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            active, inactive = "a" * 64, "b" * 64
            for key in (active, inactive):
                cache = root / "code-builds" / key
                cache.mkdir(parents=True)
                (cache / "ready.json").write_text(json.dumps({"fingerprint": key}))
            unrelated = root / "code-builds" / "unrecognized"
            unrelated.mkdir()
            (root / "code-mods-active.json").write_text(json.dumps({"fingerprint": active}))
            with mock.patch.object(setup, "free_space", side_effect=[0, 3 << 30]):
                setup.code_mods.make_build_space(setup, root)
            self.assertTrue((root / "code-builds" / active).is_dir())
            self.assertFalse((root / "code-builds" / inactive).exists())
            self.assertTrue(unrelated.is_dir())
            with mock.patch.object(setup, "free_space", return_value=0):
                with self.assertRaisesRegex(setup.SetupError, "previous build retained"):
                    setup.code_mods.make_build_space(setup, root)
            self.assertTrue((root / "code-builds" / active).is_dir())

    def test_cancel_preserves_selection_and_releases_lock(self):
        from pathlib import Path
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            active = root / "code-mods-active.json"
            active.write_text('{"previous": true}')
            cancel = root / "cancel"
            cancel.touch()
            status = root / "status.json"
            ctx = SimpleNamespace(data_dir=d)
            with self.assertRaisesRegex(setup.SetupError, "cancelled"):
                setup.code_mods.rebuild(setup, ctx, True, status, cancel)
            self.assertEqual(active.read_text(), '{"previous": true}')
            with self.assertRaisesRegex(setup.SetupError, "cancelled"):
                setup.code_mods.rebuild(setup, ctx, True, status, cancel)
            self.assertEqual(json.loads(status.read_text())["state"], "error")


class Download(unittest.TestCase):
    """setup.download: retries, resume with range requests, the player's own copy (issue #113)."""
    DATA = bytes(range(256)) * 4096  # 1 MiB of fixture bytes
    SHA = hashlib.sha256(DATA).hexdigest()

    class Response:
        def __init__(self, data, status, fail_after=None):
            self.data, self.status, self.fail_after, self.pos = data, status, fail_after, 0
            self.headers = {"Content-Length": str(len(data))}
        def __enter__(self): return self
        def __exit__(self, *a): return False
        def read(self, n):
            if self.fail_after is not None and self.pos >= self.fail_after:
                raise ConnectionResetError("connection reset by peer")
            end = min(len(self.data), self.pos + n, self.fail_after if self.fail_after is not None else len(self.data))
            chunk = self.data[self.pos:end]; self.pos = end
            return chunk

    def serve(self, fail_first):
        calls = []
        def urlopen(req, timeout=None):
            rng = req.get_header("Range")
            calls.append(rng)
            start = int(rng.split("=")[1].rstrip("-")) if rng else 0
            body = self.DATA[start:]
            fail = 300000 if (fail_first and len(calls) == 1) else None
            return self.Response(body, 206 if rng else 200, fail)
        return urlopen, calls

    def test_resumes_after_interruption(self):
        urlopen, calls = self.serve(fail_first=True)
        with tempfile.TemporaryDirectory() as d, mock.patch.object(setup.urllib.request, "urlopen", urlopen), \
                mock.patch.object(setup, "PKG", d), mock.patch.object(setup, "HERE", d):
            dst = os.path.join(d, "out", "tc.zip"); os.makedirs(os.path.dirname(dst))
            setup.download("https://example.invalid/tc.zip", dst, self.SHA, len(self.DATA), "test", wait=0)
            with open(dst, "rb") as f:
                self.assertEqual(f.read(), self.DATA)
            self.assertEqual(calls, [None, "bytes=300000-"])

    def test_gives_up_with_hint(self):
        def urlopen(req, timeout=None):
            raise setup.urllib.error.URLError("timed out")
        with tempfile.TemporaryDirectory() as d, mock.patch.object(setup.urllib.request, "urlopen", urlopen), \
                mock.patch.object(setup, "PKG", d), mock.patch.object(setup, "HERE", d):
            with self.assertRaises(setup.SetupError) as e:
                setup.download("https://example.invalid/tc.zip", os.path.join(d, "tc.zip"), self.SHA, 1, "test",
                               attempts=2, wait=0)
            self.assertIn("put tc.zip in the Wind Waker HD folder", str(e.exception))

    def test_certificate_error_names_security_software(self):
        def urlopen(req, timeout=None):
            raise setup.urllib.error.URLError("[SSL: CERTIFICATE_VERIFY_FAILED] certificate verify failed")
        with tempfile.TemporaryDirectory() as d, mock.patch.object(setup.urllib.request, "urlopen", urlopen), \
                mock.patch.object(setup, "PKG", d), mock.patch.object(setup, "HERE", d):
            with self.assertRaises(setup.SetupError) as e:
                setup.download("https://example.invalid/tc.zip", os.path.join(d, "tc.zip"), self.SHA, 1, "test", wait=0)
            self.assertIn("antivirus or security program", str(e.exception))

    def test_certificate_error_falls_back_to_windows_curl(self):
        def urlopen(req, timeout=None):
            raise setup.urllib.error.URLError("[SSL: CERTIFICATE_VERIFY_FAILED] unable to get local issuer certificate")
        def curl_download(curl, url, tmp):
            with open(tmp, "wb") as f:
                f.write(self.DATA)
            return True
        with tempfile.TemporaryDirectory() as d, mock.patch.object(setup.urllib.request, "urlopen", urlopen), \
                mock.patch.object(setup, "PKG", d), mock.patch.object(setup, "HERE", d), \
                mock.patch.object(setup, "windows_curl", lambda: "curl.exe"), \
                mock.patch.object(setup, "curl_download", curl_download):
            dst = os.path.join(d, "tc.zip")
            setup.download("https://example.invalid/tc.zip", dst, self.SHA, len(self.DATA), "test", wait=0)
            self.assertEqual(setup.file_sha256(dst), self.SHA)

    def test_uses_players_own_copy(self):
        def urlopen(req, timeout=None):
            raise AssertionError("must not download")
        with tempfile.TemporaryDirectory() as d, mock.patch.object(setup.urllib.request, "urlopen", urlopen), \
                mock.patch.object(setup, "PKG", d), mock.patch.object(setup, "HERE", d):
            with open(os.path.join(d, "tc.zip"), "wb") as f:
                f.write(self.DATA)
            dst = os.path.join(d, "toolchain", "tc.zip"); os.makedirs(os.path.dirname(dst))
            setup.download("https://example.invalid/tc.zip", dst, self.SHA, len(self.DATA), "test", wait=0)
            self.assertEqual(setup.file_sha256(dst), self.SHA)


if __name__ == "__main__":
    unittest.main(verbosity=2)
