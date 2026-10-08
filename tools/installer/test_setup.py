#!/usr/bin/env python3
"""Unit tests for the installer's helpers (no game files, no network): python3 test_setup.py"""
import hashlib
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup  # noqa: E402

KEY_HEX = "0011223344556677" "8899aabbccddeeff"  # made-up test value


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


if __name__ == "__main__":
    unittest.main(verbosity=2)
