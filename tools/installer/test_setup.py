#!/usr/bin/env python3
"""Unit tests for the installer's helpers (no game files, no network): python3 test_setup.py"""
import hashlib
import json
import os
import re
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup  # noqa: E402

KEY_HEX = "0011223344556677" "8899aabbccddeeff"  # made-up test value


class MacSdkFallback(unittest.TestCase):
    # the linker output from a Mac whose beta SDK is newer than its Command Line Tools' linker
    MISMATCH = ("linking the game failed (exit code 1):\n"
                "/Library/Developer/CommandLineTools/SDKs/MacOSX27.0.sdk/usr/lib/libz.1.tbd:4:20: error: unknown architecture\n"
                "arm64e.x1-macos, arm64e.x1-maccatalyst ]\n"
                "in '/Library/Developer/CommandLineTools/SDKs/MacOSX27.0.sdk/usr/lib/libz.tbd'; tapi error: malformed file")
    SDKS = [((27, 0), "/sdk/MacOSX27.0.sdk"), ((26, 1), "/sdk/MacOSX26.1.sdk"),
            ((15, 5), "/sdk/MacOSX15.5.sdk"), ((13, 3), "/sdk/MacOSX13.3.sdk")]

    def test_mismatch_pattern(self):
        self.assertTrue(setup.MAC_SDK_MISMATCH.search(self.MISMATCH))
        self.assertFalse(setup.MAC_SDK_MISMATCH.search("ld: symbol(s) not found for architecture arm64"))

    def test_sdk_versions_and_order(self):
        self.assertEqual(setup.mac_sdk_version("/x/MacOSX27.0.sdk"), (27, 0))
        self.assertEqual(setup.mac_sdk_version("/x/MacOSX26.sdk/"), (26, 0))
        self.assertIsNone(setup.mac_sdk_version("/x/MacOSX.sdk"))
        # older SDKs only, newest first, never below the game's deployment target (14.0)
        self.assertEqual(setup.mac_fallback_sdks("/sdk/MacOSX27.0.sdk", self.SDKS),
                         ["/sdk/MacOSX26.1.sdk", "/sdk/MacOSX15.5.sdk"])
        # an unversioned current path (Xcode's MacOSX.sdk) gets its version from the list
        sdks = [((26, 1), "/clt/MacOSX26.1.sdk"), ((26, 0), "/xcode/MacOSX.sdk"), ((15, 4), "/clt/MacOSX15.4.sdk")]
        self.assertEqual(setup.mac_fallback_sdks("/xcode/MacOSX.sdk", sdks), ["/clt/MacOSX15.4.sdk"])

    def _link(self, works_with, env=None):
        calls = []
        def run_logged(cmd, env=None, what=""):
            sdk = (env or {}).get("SDKROOT")
            calls.append(sdk)
            if sdk != works_with:
                raise setup.SetupError(self.MISMATCH)
            return ""
        with mock.patch.object(setup, "IS_MAC", True), \
             mock.patch.object(setup, "run_logged", side_effect=run_logged), \
             mock.patch.object(setup, "mac_sdks", return_value=self.SDKS), \
             mock.patch.object(setup, "mac_current_sdk", return_value="/sdk/MacOSX27.0.sdk"), \
             mock.patch.object(setup, "say"):
            setup.link_with_sdk_fallback(["clang++"], env)
        return calls

    def test_retries_with_an_older_sdk(self):
        self.assertEqual(self._link("/sdk/MacOSX15.5.sdk", {}), [None, "/sdk/MacOSX26.1.sdk", "/sdk/MacOSX15.5.sdk"])

    def test_clear_message_when_no_sdk_works(self):
        with self.assertRaises(setup.SetupError) as e:
            self._link("/none", {})
        self.assertIn("Update the Command Line Tools", str(e.exception))

    def test_other_errors_and_explicit_sdkroot_are_not_retried(self):
        with mock.patch.object(setup, "IS_MAC", True), \
             mock.patch.object(setup, "run_logged", side_effect=setup.SetupError("ld: symbol(s) not found")) as run:
            with self.assertRaises(setup.SetupError):
                setup.link_with_sdk_fallback(["clang++"], {})
            self.assertEqual(run.call_count, 1)
        with mock.patch.object(setup, "IS_MAC", True), \
             mock.patch.object(setup, "run_logged", side_effect=setup.SetupError(self.MISMATCH)) as run:
            with self.assertRaises(setup.SetupError):
                setup.link_with_sdk_fallback(["clang++"], {"SDKROOT": "/chosen.sdk"})  # the player chose the SDK
            self.assertEqual(run.call_count, 1)
class ShaderPreparationBoundary(unittest.TestCase):
    def test_unexpected_preparation_error_does_not_abort_setup(self):
        import shader_prebuild
        with mock.patch.object(shader_prebuild, "prepare", side_effect=RuntimeError("unexpected failure")), \
                mock.patch.object(setup.LOG, "write") as log:
            result = setup.prepare_shaders_nonfatal("game", "template", "exe", "cache", "package")
        self.assertEqual(result, dict(status="failed", message="unexpected failure"))
        self.assertIn("The game can still run", log.call_args.args[0])


class ShaderPreparationProgress(unittest.TestCase):
    def test_gui_bar_is_indeterminate_until_fractions_then_determinate(self):
        class Gui:
            def __init__(self): self.events = []
            def emit(self, event): self.events.append(event)
        gui = Gui()
        with mock.patch.object(setup, "GUI", gui), mock.patch.object(setup.LOG, "write") as log:
            message = setup.shader_progress_callback(setup.Progress("Preparing shaders"))
            message("Resolving shaders from your game files")
            message("Resolving shaders from your game files: 3 of 818 game archives", fraction=0.1)
            message("Resolving shaders from your game files: 9 of 818 game archives", fraction=0.2)
            message("Compiling shaders and pipelines for this computer", fraction=0.35)
        bars = [(e["done"], e["total"], e["detail"]) for e in gui.events if e["event"] == "progress"]
        self.assertEqual(bars[0], (0, 0, "Resolving shaders from your game files"))
        self.assertEqual(bars[1], (100, 1000, "Resolving shaders from your game files: 3 of 818 (10%)"))
        self.assertEqual(bars[-1][2], "Compiling shaders and pipelines for this computer (35%)")
        self.assertEqual(bars[-1][:2], (350, 1000))
        self.assertEqual([c.args[0] for c in log.call_args_list],
                         ["Resolving shaders from your game files", "Compiling shaders and pipelines for this computer"])

    def test_terminal_line_shows_percentage(self):
        import io
        out = io.StringIO()
        with mock.patch.object(setup, "GUI", None), mock.patch.object(setup.LOG, "write"), \
                mock.patch.object(setup.sys, "stdout", out):
            message = setup.shader_progress_callback(setup.Progress("Preparing shaders"))
            message("Resolving shaders from your game files: 400 of 818 game archives", fraction=0.42)
        self.assertIn("42% Resolving shaders from your game files: 400 of 818 game archives", out.getvalue())


class BracketsInInstallFolder(unittest.TestCase):
    # issue #136: an install folder such as "LoZ - The Wind Waker [WiiU]" made glob treat "[WiiU]" as a
    # character class, so setup found none of the generated files ("the recompiler wrote no code").
    def test_recompile_counts_generated_code_in_a_folder_with_brackets(self):
        with tempfile.TemporaryDirectory() as temporary:
            gen_dir = os.path.join(temporary, "LoZ - The Wind Waker [WiiU]", "data", "work", "gen")

            def run_logged(cmd, env=None, what=""):
                os.makedirs(gen_dir, exist_ok=True)
                for name in ("code_000.c", "code_001.c"):
                    open(os.path.join(gen_dir, name), "w").close()

            with mock.patch.object(setup, "run_logged", side_effect=run_logged):
                self.assertEqual(setup.recompile(os.path.join(temporary, "game"), gen_dir), 2)


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


    def appimage_fixture(self, directory):
        """A fake AppImage mount (/tmp/.mount_XXXXXX) holding a release, and a data folder outside it."""
        mount = os.path.join(directory, ".mount_AAA")
        for sub in (("tools", "guestmod"), ("tools", "installer"), ("sdk", "include")):
            os.makedirs(os.path.join(mount, *sub))
        data = os.path.join(directory, "home", ".local", "share", "wwhd")
        os.makedirs(os.path.join(data, "toolchain"))
        env = {"APPIMAGE": os.path.join(directory, "WWHD.AppImage"), "APPDIR": mount}
        return mount, data, env

    def test_appimage_stores_release_marker(self):
        # issue #230: an AppImage is mounted somewhere else on every start; paths inside it are stored as
        # "$RELEASE/...", the data folder's own paths stay absolute
        with tempfile.TemporaryDirectory() as directory:
            mount, data, env = self.appimage_fixture(directory)
            compiler = os.path.join(data, "toolchain", "zig")
            python = os.path.join(data, "python", "bin", "python3")
            tc = setup.Toolchain([compiler, "cc"], [], [], env={"ZIG_GLOBAL_CACHE_DIR": os.path.join(data, "zig-cache")})
            with mock.patch.dict(os.environ, env), mock.patch.multiple(setup, PKG=mount, PORTABLE=False), \
                    mock.patch.object(setup, "ensure_setup_python", return_value=python):
                setup.write_guest_build_config(data, tc)
            with open(os.path.join(data, "guest-sdk.json"), encoding="utf-8") as f:
                config = json.load(f)
            self.assertEqual(config["format_version"], 3)
            self.assertEqual(config["builder"], "$RELEASE/tools/guestmod/build_guest_mod.py")
            self.assertEqual(config["include"], "$RELEASE/sdk/include")
            self.assertEqual(config["setup"], "$RELEASE/tools/installer/setup.py")
            self.assertEqual(config["python"], [python])
            self.assertEqual(config["compiler"], [compiler, "cc"])
            self.assertEqual(config["data_dir"], os.path.abspath(data))
            self.assertEqual(config["zig_cache"], os.path.join(data, "zig-cache"))
            self.assertNotIn(".mount_", json.dumps(config))

    def test_appimage_marker_needs_release_inside_appdir(self):
        with tempfile.TemporaryDirectory() as directory:
            mount, data, env = self.appimage_fixture(directory)
            tc = setup.Toolchain(["cc"], [], [])
            elsewhere = os.path.join(directory, "source-tree")
            with mock.patch.dict(os.environ, env), mock.patch.multiple(setup, PKG=elsewhere, PORTABLE=False), \
                    mock.patch.object(setup, "ensure_setup_python", return_value="python3"):
                setup.write_guest_build_config(data, tc)
            with open(os.path.join(data, "guest-sdk.json"), encoding="utf-8") as f:
                config = json.load(f)
            self.assertEqual(config["builder"], os.path.join(elsewhere, "tools", "guestmod", "build_guest_mod.py"))

    def test_appimage_repair_replaces_stale_mount_paths(self):
        # a format 2 setup from an earlier start of the image: the repair stores the marker
        with tempfile.TemporaryDirectory() as directory:
            mount, data, env = self.appimage_fixture(directory)
            stale = os.path.join(directory, ".mount_OLD")
            path = os.path.join(data, "guest-sdk.json")
            with open(path, "w", encoding="utf-8") as f:
                json.dump({"format_version": 2, "python": ["old"], "compiler": ["cc"],
                           "builder": os.path.join(stale, "tools", "guestmod", "build_guest_mod.py"),
                           "include": os.path.join(stale, "sdk", "include"),
                           "setup": os.path.join(stale, "tools", "installer", "setup.py"), "data_dir": data}, f)
            with mock.patch.dict(os.environ, env), mock.patch.multiple(setup, PKG=mount, PORTABLE=False), \
                    mock.patch.object(setup, "ensure_setup_python", return_value="python3"):
                setup.repair_guest_python(data)
            with open(path, encoding="utf-8") as f:
                config = json.load(f)
            self.assertEqual(config["format_version"], 3)
            self.assertEqual(config["builder"], "$RELEASE/tools/guestmod/build_guest_mod.py")
            self.assertEqual(config["setup"], "$RELEASE/tools/installer/setup.py")
            self.assertEqual(config["data_dir"], data)

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
            self.assertEqual(config["format_version"], 3)
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


class AtomicReplace(unittest.TestCase):
    """Publishing files the running game polls (issue #282: WinError 5 replacing code-mods-status.json)."""
    cm = setup.code_mods

    def test_windows_uses_posix_rename_first(self):
        with mock.patch.object(self.cm.os, 'name', 'nt'), \
             mock.patch.object(self.cm, '_posix_rename', return_value=True) as posix, \
             mock.patch.object(self.cm.os, 'replace') as replace:
            setup.atomic_replace('source', 'destination')
        posix.assert_called_once_with('source', 'destination')
        replace.assert_not_called()

    def test_unsupported_posix_rename_falls_back_to_os_replace(self):
        with mock.patch.object(self.cm.os, 'name', 'nt'), \
             mock.patch.object(self.cm, '_posix_rename', return_value=False), \
             mock.patch.object(self.cm.os, 'replace') as replace:
            setup.atomic_replace('source', 'destination')
        replace.assert_called_once_with('source', 'destination')

    def test_other_platforms_never_use_the_windows_rename(self):
        with mock.patch.object(self.cm.os, 'name', 'posix'), \
             mock.patch.object(self.cm, '_posix_rename') as posix, \
             mock.patch.object(self.cm.os, 'replace') as replace:
            setup.atomic_replace('source', 'destination')
        posix.assert_not_called()
        replace.assert_called_once_with('source', 'destination')

    def test_transient_windows_readers(self):
        # a reader without delete sharing (a virus scanner) blocks even the POSIX rename for a moment
        with mock.patch.object(self.cm.os, 'name', 'nt'), \
             mock.patch.object(self.cm, '_posix_rename', side_effect=[PermissionError(), False, True]) as posix, \
             mock.patch.object(self.cm.os, 'replace', side_effect=[PermissionError()]) as replace, \
             mock.patch.object(self.cm.time, 'sleep') as sleep:
            setup.atomic_replace('source', 'destination')
        self.assertEqual(posix.call_count, 3)
        self.assertEqual(replace.call_count, 1)
        self.assertEqual(sleep.call_count, 2)

    def test_persistent_denial(self):
        error = PermissionError('locked')
        with mock.patch.object(self.cm.os, 'name', 'nt'), \
             mock.patch.object(self.cm, '_posix_rename', return_value=False), \
             mock.patch.object(self.cm.os, 'replace', side_effect=error) as replace, \
             mock.patch.object(self.cm.time, 'monotonic', side_effect=[0, 0, 1, 6]), \
             mock.patch.object(self.cm.time, 'sleep'):
            with self.assertRaises(PermissionError) as caught:
                setup.atomic_replace('source', 'destination')
        self.assertIs(caught.exception, error)
        self.assertEqual(replace.call_count, 3)

    def test_other_errors_and_platforms_do_not_retry(self):
        for platform, error in [('nt', FileNotFoundError()), ('posix', PermissionError())]:
            with self.subTest(platform=platform, error=error), \
                 mock.patch.object(self.cm.os, 'name', platform), \
                 mock.patch.object(self.cm, '_posix_rename', return_value=False), \
                 mock.patch.object(self.cm.os, 'replace', side_effect=error) as replace:
                with self.assertRaises(type(error)):
                    setup.atomic_replace('source', 'destination')
                replace.assert_called_once()

    def test_windows_errors_map_like_os_replace(self):
        self.assertIsInstance(self.cm._windows_error(5, 'a', 'b'), PermissionError)
        self.assertIsInstance(self.cm._windows_error(32, 'a', 'b'), PermissionError)
        other = self.cm._windows_error(3, 'a', 'b')
        self.assertNotIsInstance(other, PermissionError)
        self.assertIsInstance(other, OSError)

    def test_extended_paths(self):
        if os.name != 'nt':
            self.skipTest('Windows path forms')
        self.assertEqual(self.cm._extended_path('C:\\a\\b.json'), '\\\\?\\C:\\a\\b.json')
        self.assertEqual(self.cm._extended_path('C:/a/b.json'), '\\\\?\\C:\\a\\b.json')
        self.assertEqual(self.cm._extended_path('\\\\server\\share\\b.json'), '\\\\?\\UNC\\server\\share\\b.json')
        self.assertEqual(self.cm._extended_path('\\\\?\\C:\\a'), '\\\\?\\C:\\a')

    @unittest.skipUnless(os.name == 'nt', 'Windows rename semantics')
    def test_replaces_a_file_the_game_holds_open(self):
        """The real Windows behaviour: a reader that allows delete sharing (the game's) blocks os.replace,
        not the POSIX-semantics rename, and keeps reading the contents it opened."""
        import ctypes
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                         wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        kernel32.CreateFileW.restype = wintypes.HANDLE
        kernel32.ReadFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD,
                                      ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        with tempfile.TemporaryDirectory(prefix='wwhd-282, ') as d:
            target = os.path.join(d, 'code-mods-status.json')
            with open(target, 'w') as f:
                f.write('old')
            # GENERIC_READ, FILE_SHARE_READ|WRITE|DELETE, OPEN_EXISTING: runtime/src/platform/shared_read.h
            handle = kernel32.CreateFileW(target, 0x80000000, 7, None, 3, 0, None)
            self.assertNotEqual(handle, ctypes.c_void_p(-1).value, ctypes.get_last_error())
            try:
                with open(target + '.tmp', 'w') as f:
                    f.write('new')
                with self.assertRaises(PermissionError):  # the root cause of #282
                    os.replace(target + '.tmp', target)
                self.assertTrue(self.cm._posix_rename(target + '.tmp', target),
                                'FileRenameInfoEx POSIX rename unavailable on this Windows/volume')
                with open(target) as f:
                    self.assertEqual(f.read(), 'new')
                self.assertFalse(os.path.exists(target + '.tmp'))
                buffer, count = ctypes.create_string_buffer(16), wintypes.DWORD()
                self.assertTrue(kernel32.ReadFile(handle, buffer, 16, ctypes.byref(count), None))
                self.assertEqual(buffer.raw[:count.value], b'old')
            finally:
                kernel32.CloseHandle(handle)
            # and through the public helper, with nothing open
            with open(target + '.tmp', 'w') as f:
                f.write('newer')
            setup.atomic_replace(target + '.tmp', target)
            with open(target) as f:
                self.assertEqual(f.read(), 'newer')

    def test_progress_publication_never_fails_the_build(self):
        """A progress update that still loses to a reader is skipped; the final status is published."""
        from pathlib import Path
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "game/code").mkdir(parents=True)
            (root / "game/code/cking.rpx").write_bytes(b"synthetic input")
            (root / "sdk").mkdir()
            data = root / "data"
            ctx = SimpleNamespace(data_dir=str(data), game_dir=str(root / "game"),
                                  manifest={"toolchain": {}, "exe": "fixture"}, args=SimpleNamespace(jobs=1))
            status = data / "code-mods-status.json"
            real = self.cm.atomic_json

            def contended(path, value):
                if Path(path) == status and value.get('state') == 'building':
                    raise PermissionError(13, 'Access is denied', str(path))
                real(path, value)

            def compile_code(tc, manifest, gen, obj, jobs, **kwargs):
                Path(obj).mkdir(); kwargs["progress"](1, 1)
                return []
            tc = SimpleNamespace(cc=["fixture compiler"], env={})
            with mock.patch.multiple(setup, PKG=root, PORTABLE=False), \
                 mock.patch.object(setup, "get_toolchain", return_value=tc), \
                 mock.patch.object(setup, "run_logged", return_value="fixture compiler version"), \
                 mock.patch.object(setup, "free_space", return_value=20 << 30), \
                 mock.patch.object(setup, "recompile", side_effect=lambda game, gen, hooks=False: Path(gen).mkdir()), \
                 mock.patch.object(setup, "compile_gamecode", side_effect=compile_code), \
                 mock.patch.object(setup, "link_game", side_effect=lambda *a: Path(a[-1]).write_bytes(b"exe")), \
                 mock.patch.object(self.cm, "atomic_json", side_effect=contended):
                result = self.cm.rebuild(setup, ctx, True, status)
            self.assertEqual(result["state"], "ready")
            self.assertEqual(json.loads(status.read_text())["state"], "ready")


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


class AppImagePortableHome(unittest.TestCase):
    """The AppImage's portable mode is the runtime's: "<image>.home" next to the file becomes $HOME
    (issue #55). With the XDG variables unset, every folder setup derives on Linux (game and code,
    compiler, settings, shader caches, launcher and menu entry) must then sit under that $HOME: nothing
    may come from the password database or another fixed place. Runs on Linux and macOS (Linux rules forced)."""

    @unittest.skipIf(sys.platform.startswith("win"), "~ is %USERPROFILE% there, not $HOME")
    def test_every_folder_follows_home(self):
        import shader_prebuild
        from pathlib import Path
        with tempfile.TemporaryDirectory() as t:
            image = os.path.join(t, "Wind Waker [HD].AppImage")
            home = image + ".home"
            os.makedirs(home)
            env = {k: v for k, v in os.environ.items() if not k.startswith("XDG_") and k != "WWHD_USER_DIR"}
            env.update(HOME=home, APPIMAGE=image)
            with mock.patch.dict(os.environ, env, clear=True), \
                    mock.patch.object(setup, "IS_MAC", False), mock.patch.object(setup, "IS_WIN", False), \
                    mock.patch.object(setup, "IS_LINUX", True), mock.patch.object(setup, "PORTABLE", False), \
                    mock.patch.object(shader_prebuild.os.sys, "platform", "linux"):
                data = setup.default_data_dir()
                self.assertEqual(data, os.path.join(home, ".local", "share", "wwhd"))
                self.assertEqual(setup.toolchain_dir(data), os.path.join(data, "toolchain"))
                self.assertEqual(setup.legacy_config_dirs(), [os.path.join(home, ".config", "wwhd")])
                self.assertEqual(shader_prebuild.default_cache_root(data),
                                 Path(home, ".config", "wwhd", "shader-prebuild"))
                os.makedirs(data)
                self.assertEqual(setup.linux_launchers(data, os.path.join(data, "wwhd")), os.path.join(data, "play.sh"))
                self.assertTrue(os.path.isfile(os.path.join(home, ".local", "share", "applications", "wwhd.desktop")))
                # the runtime's "<image>.config" (--appimage-portable-config) moves only the settings
                with mock.patch.dict(os.environ, {"XDG_CONFIG_HOME": image + ".config"}):
                    self.assertEqual(setup.legacy_config_dirs(), [os.path.join(image + ".config", "wwhd")])
                    self.assertEqual(setup.default_data_dir(), data)
            for root, _dirs, files in os.walk(t):
                for name in files:  # nothing was written outside the portable home
                    self.assertTrue(root.startswith(home), (root, name))

    def test_v0213_data_folder_only_gets_a_hint(self):
        # v0.2.13's "<image>.data" is no longer read: a hint (same text as AppRun and the game), no paths
        with tempfile.TemporaryDirectory() as t:
            image = os.path.join(t, "Wind Waker [HD].AppImage")
            self.assertEqual(setup.legacy_appimage_data_notice(image), "")
            self.assertEqual(setup.legacy_appimage_data_notice(""), "")
            os.makedirs(image + ".data")
            self.assertEqual(setup.legacy_appimage_data_notice(image),
                             "Found Wind Waker [HD].AppImage.data: this folder is no longer used. Move its contents to "
                             "Wind Waker [HD].AppImage.home (see the changelog) to keep your saves and settings.")
            env = {k: v for k, v in os.environ.items() if not k.startswith("XDG_")}
            env["APPIMAGE"] = image
            with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(setup, "PORTABLE", False):
                self.assertTrue(setup.legacy_appimage_data_notice().startswith("Found "))
                self.assertNotIn(".data", setup.default_data_dir())  # the folders do not come from it
                self.assertNotIn(".data", setup.legacy_config_dirs()[0])
            env.pop("APPIMAGE")
            with mock.patch.dict(os.environ, env, clear=True):
                self.assertEqual(setup.legacy_appimage_data_notice(), "")  # not an AppImage


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
        self.assertEqual(setup.plan_steps("archive"), ["archive", "compiler", "extract", "translate", "compile", "app", "shaders"])
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

    def test_windows_arm64_pins(self):
        tcs = setup.load_toolchains()
        tc = tcs["toolchains"]["llvm-mingw-20260922-aarch64"]
        self.assertEqual(tc["kind"], "llvm-mingw")
        self.assertEqual(tc["triple"], "aarch64-w64-mingw32")
        self.assertIn("ucrt-aarch64", tc["url"])
        self.assertNotIn("triple", tcs["toolchains"]["llvm-mingw-20260922"])  # x86-64: the default triple
        self.assertIn("embed-arm64", tcs["python"]["windows-arm64"]["url"])
        files = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "release", "python-windows-arm64-files.json")
        with open(files) as f:
            expected = json.load(f)
        self.assertEqual(expected["sha256"], tcs["python"]["windows-arm64"]["sha256"])
        self.assertEqual(expected["url"], tcs["python"]["windows-arm64"]["url"])
        for name in ("python.exe", "pythonw.exe", "python3.dll", "LICENSE.txt"):
            self.assertIn(name, expected["files"])


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

    def test_guard_picks_the_release_python(self):
        self.assertEqual(self.guard.python_files("dist/WindWakerHD-v1-windows-arm64.zip"), self.guard.PYTHON_FILES_ARM64)
        self.assertEqual(self.guard.python_files(os.path.join("x", "WindWakerHD-v1-windows-arm64", "")), self.guard.PYTHON_FILES_ARM64)
        self.assertEqual(self.guard.python_files("dist/WindWakerHD-v1-windows-x86_64.zip"), self.guard.PYTHON_FILES)

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


class ModelSweepOption(unittest.TestCase):
    """"Preparing shaders: models (experimental)": opt-in, off by default on every platform."""

    def ctx(self, state=None, **args):
        from types import SimpleNamespace
        values = dict(model_sweep=None, skip_shader_prebuild=False)
        values.update(args)
        return SimpleNamespace(args=SimpleNamespace(**values), state=lambda: dict(state or {}))

    def test_off_by_default_on_every_platform(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            for mac in (False, True):
                with mock.patch.object(setup, "IS_MAC", mac):
                    self.assertFalse(setup.model_sweep_wanted(self.ctx()))
                    self.assertFalse(setup.model_sweep_wanted(self.ctx(), setup.UI(False)))

    def test_question_marked_experimental_defaults_to_the_last_choice(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            ui = mock.Mock(interactive=True)
            ui.yesno.side_effect = lambda question, default: default
            self.assertFalse(setup.model_sweep_wanted(self.ctx(), ui))
            ui.yesno.assert_called_with("Also prepare model shaders (experimental)?", False)
            self.assertTrue(setup.model_sweep_wanted(self.ctx({"model_sweep": True}), ui))
            ui.yesno.assert_called_with("Also prepare model shaders (experimental)?", True)
            ui.reset_mock()
            self.assertFalse(setup.model_sweep_wanted(self.ctx(skip_shader_prebuild=True), ui))
            ui.yesno.assert_not_called()
        self.assertIn("Experimental", setup.MODEL_SWEEP_TEXT)
        self.assertIn("20 minutes", setup.MODEL_SWEEP_TEXT)

    def test_flag_window_choice_and_environment_override(self):
        ui = mock.Mock(interactive=True)
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertTrue(setup.model_sweep_wanted(self.ctx(model_sweep=True), ui))
            self.assertFalse(setup.model_sweep_wanted(self.ctx({"model_sweep": True}, model_sweep=False), ui))
            ui.yesno.assert_not_called()
        with mock.patch.dict(os.environ, {"WWHD_SETUP_MODEL_SWEEP": "1"}):
            self.assertTrue(setup.model_sweep_wanted(self.ctx(model_sweep=False), ui))
        with mock.patch.dict(os.environ, {"WWHD_SETUP_MODEL_SWEEP": "0"}):
            self.assertFalse(setup.model_sweep_wanted(self.ctx({"model_sweep": True}, model_sweep=True), ui))
        ui.yesno.assert_not_called()


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
                "HOME": d, "USERPROFILE": d, "APPDATA": d, "XDG_CONFIG_HOME": d}, clear=True):
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
        # the test program needs the host's PATH (Windows: its DLLs) although setup runs in a clean environment
        host_env = {k: v for k, v in os.environ.items() if k.upper() in ("PATH", "SYSTEMROOT", "WINDIR")}
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
                                        "--setup-code-mods", str(data)], check=True, env=host_env)

                import shader_prebuild
                with mock.patch.object(shader_prebuild, "prepare", side_effect=PermissionError("late scan failure")):
                    state = setup.install(ctx, ("installed", str(game)))
                self.assertEqual(state["shader_prebuild"]["status"], "failed")
                self.assertTrue(Path(state["exe"]).is_file())
                self.assertEqual(setup.read_state(str(data))["shader_prebuild"]["status"], "failed")


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


class MacAppIcon(unittest.TestCase):
    """The installed macOS app gets the game's own icon, made from the player's game files."""

    def _game(self, d):
        import struct
        meta = os.path.join(d, "game", "meta")
        os.makedirs(meta)
        w = h = 4
        header = bytes([0, 0, 2]) + bytes(9) + struct.pack("<HH", w, h) + bytes([32, 8])
        with open(os.path.join(meta, "iconTex.tga"), "wb") as f:
            f.write(header + bytes([10, 20, 30, 255]) * (w * h))

    def test_icns_holds_the_png(self):
        import struct
        with tempfile.TemporaryDirectory() as d:
            self._game(d)
            png = setup.game_icon_png(d)
            icns = setup.mac_icns(png)
            self.assertEqual(icns[:4], b"icns")
            self.assertEqual(struct.unpack(">I", icns[4:8])[0], len(icns))
            self.assertEqual(icns[8:12], b"ic07")
            self.assertEqual(icns[16:], png)

    @unittest.skipUnless(sys.platform == "darwin", "codesign")
    def test_mac_app_icon_only_with_game_icon(self):
        import plistlib
        with tempfile.TemporaryDirectory() as d:
            exe = os.path.join(d, "exe")
            with open(exe, "w") as f:
                f.write("#!/bin/sh\n")
            for with_icon in (False, True):
                data = os.path.join(d, "data%d" % with_icon)
                os.makedirs(data)
                if with_icon:
                    self._game(data)
                app = os.path.join(d, "Apps%d" % with_icon, "Wind Waker HD.app")
                setup.mac_app(app, exe, data, "v0.2.11")
                with open(os.path.join(app, "Contents", "Info.plist"), "rb") as f:
                    info = plistlib.load(f)
                self.assertEqual(info.get("CFBundleIconFile"), "AppIcon" if with_icon else None)
                self.assertEqual(os.path.isfile(os.path.join(app, "Contents", "Resources", "AppIcon.icns")), with_icon)


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



class DecompRelease(unittest.TestCase):
    """The decomp release (docs/decomp-build.md): sdk/decomp_hooks.txt reaches the recompiler."""

    def run_recompile(self, pkg, build_name="USA"):
        calls = []

        def run_logged(cmd, cwd=None, env=None, what=""):
            calls.append(cmd)
            os.makedirs(cmd[3], exist_ok=True)
            open(os.path.join(cmd[3], "code_000.c"), "w").close()

        build = mock.Mock(canonical=build_name == "USA")
        build.name = build_name
        with mock.patch.object(setup, "PKG", pkg), mock.patch.object(setup, "run_logged", run_logged), \
                mock.patch.object(setup.game_builds, "identify", return_value=build):
            setup.recompile(os.path.join(pkg, "game"), os.path.join(pkg, "gen"))
        return calls[0]

    def test_regular_release_has_no_list(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(setup.decomp_hooks_file())
            self.assertNotIn("--decomp-hooks", self.run_recompile(d))

    def test_decomp_release_passes_its_list(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "sdk"))
            hooks = os.path.join(d, "sdk", "decomp_hooks.txt")
            with open(hooks, "w") as f:
                f.write("02000020\n")
            cmd = self.run_recompile(d)
            self.assertEqual(cmd[cmd.index("--decomp-hooks") + 1], hooks)
            with self.assertRaises(setup.SetupError) as e:
                self.run_recompile(d, "EUR")
            self.assertIn("USA game only", str(e.exception))

    def test_manifest_and_list_must_agree(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "sdk"))
            plat = {"darwin": "macos", "win32": "windows"}.get(sys.platform, "linux")
            m = {"platform": plat, "variant": "decomp"}
            with open(os.path.join(d, "sdk", "manifest.json"), "w") as f:
                json.dump(m, f)
            with mock.patch.object(setup, "PKG", d), mock.patch.object(setup, "IS_LINUX", False):
                with self.assertRaises(setup.SetupError):
                    setup.load_manifest()
                open(os.path.join(d, "sdk", "decomp_hooks.txt"), "w").close()
                self.assertEqual(setup.load_manifest()["variant"], "decomp")


class LongPaths(unittest.TestCase):
    """Issue #144: a release unpacked twice into Downloads by Windows' "Extract All" (the dev build's longer
    name) put the deepest files of llvm-mingw beyond MAX_PATH. Setup's file operations use extended-length
    paths (longpath); a data folder too deep for the game and its tools stops setup before any download."""

    # the reporter's folder (a 6-letter account name) and the matching release folder of v0.2.10
    DEVEL = "C:\\Users\\player\\Downloads\\" + "\\".join(["WindWakerHD-v0.2.12-devel.a84118c0-windows-x86_64"] * 2)
    RELEASE = "C:\\Users\\player\\Downloads\\" + "\\".join(["WindWakerHD-v0.2.10-windows-x86_64"] * 2)

    def test_longpath_windows(self):
        lp = lambda p: setup.longpath(p, windows=True)
        self.assertEqual(lp("C:\\Games\\WindWakerHD\\data"), "\\\\?\\C:\\Games\\WindWakerHD\\data")
        self.assertEqual(lp("C:/Games/WindWakerHD/./x/../data/"), "\\\\?\\C:\\Games\\WindWakerHD\\data")
        self.assertEqual(lp("\\\\server\\share\\WWHD"), "\\\\?\\UNC\\server\\share\\WWHD")
        self.assertEqual(lp("\\\\?\\C:\\already"), "\\\\?\\C:\\already")  # never twice
        self.assertEqual(lp("\\\\.\\pipe\\x"), "\\\\.\\pipe\\x")  # device paths unchanged
        from pathlib import PureWindowsPath
        self.assertEqual(lp(PureWindowsPath("C:\\a\\b")), "\\\\?\\C:\\a\\b")
        # forward slashes (MSYS2's Python writes them), mixed separators, doubled ones: always backslashes
        self.assertEqual(lp("D:/a/_temp/msys64/tmp/x/code-builds/bbbb"), "\\\\?\\D:\\a\\_temp\\msys64\\tmp\\x\\code-builds\\bbbb")
        self.assertEqual(lp("D:\\a/b\\\\c//d"), "\\\\?\\D:\\a\\b\\c\\d")
        self.assertEqual(lp("//server/share/WWHD/../x"), "\\\\?\\UNC\\server\\share\\x")
        # a drive root keeps its backslash; ".." never climbs above it
        for root in ("D:/", "D:\\", "D:", "D:/a/.."):
            self.assertEqual(lp(root), "\\\\?\\D:\\")
        self.assertEqual(lp("C:/../.."), "\\\\?\\C:\\")
        for p in ("C:/x/y", "\\\\server\\share\\y"):
            self.assertNotIn("/", lp(p))
            self.assertEqual(setup.plain_path(lp(p)), setup.windows_path(p))

    def test_windows_path(self):
        self.assertEqual(setup.windows_path("C:/Games//WWHD/./data/"), "C:\\Games\\WWHD\\data")
        self.assertEqual(setup.windows_path("relative/./dir/../x"), "relative\\x")
        self.assertEqual(setup.windows_path("//srv/share"), "\\\\srv\\share\\")

    def test_msys2_python_keeps_ordinary_paths(self):
        # a Python that joins paths with "/" cannot use \\?\ paths (shutil and os.walk append with it)
        with mock.patch.object(setup, "NATIVE_WINDOWS_PATHS", False):
            p = "D:/a/_temp/msys64/tmp/x"
            self.assertIs(setup.longpath(p), p)
        self.assertEqual(setup.NATIVE_WINDOWS_PATHS, setup.IS_WIN and os.path.join("a", "b") == "a\\b")

    def test_longpath_other_platforms_unchanged(self):
        for p in ("/home/player/wwhd", "relative/dir", "C:\\x"):
            self.assertIs(setup.longpath(p, windows=False), p)
        if not setup.IS_WIN:
            p = "/Users/player/wwhd"
            self.assertIs(setup.longpath(p), p)

    def test_shader_prebuild_agrees(self):
        import shader_prebuild
        p = os.path.abspath(os.path.join("some", "cache"))
        self.assertEqual(shader_prebuild.longpath(p), setup.longpath(p))

    def test_messages_show_normal_paths(self):
        for p in ("C:\\Games\\WWHD", "\\\\server\\share\\WWHD"):
            self.assertEqual(setup.plain_path(setup.longpath(p, windows=True)), p)
        err = FileNotFoundError(2, "No such file or directory")
        err.filename = setup.longpath("C:\\Users\\player\\Downloads\\x\\data\\toolchain", windows=True)
        text = setup.plain_text(err)
        self.assertNotIn("\\\\?\\", text)
        self.assertIn("'C:\\\\Users\\\\player\\\\Downloads", text)  # repr doubles the backslashes
        with mock.patch.object(setup, "GUI", None), mock.patch("builtins.print") as out:
            setup.say("Removed \\\\?\\C:\\Users\\player\\x and \\\\?\\UNC\\srv\\s\\y")
        out.assert_called_once_with("Removed C:\\Users\\player\\x and \\\\srv\\s\\y", flush=True)

    def test_measured_depths(self):
        # the numbers the limit is made of (setup.py, "long paths on Windows")
        self.assertEqual(setup.DEEPEST_DATA_PATH, 78)
        self.assertEqual(setup.MAX_DATA_DIR_CHARS, 180)
        # the deepest names nobody here chooses: a game file and the compiler the setup starts
        game = "game\\content\\Common\\AudioRes\\stream\\Kugutu_Taraba_Intro_CK.w.32.dspadpcm.bfstm"
        self.assertEqual(len(game), setup.DEEPEST_DATA_PATH)
        for tc in setup.load_toolchains()["toolchains"].values():
            if tc["kind"] == "llvm-mingw":
                triple = tc.get("triple", "x86_64-w64-mingw32")
                for program in ("-clang.exe", "-clang++.exe"):
                    exe = "toolchain\\%s\\bin\\%s%s" % (tc["dir"], triple, program)
                    self.assertLessEqual(len(exe), setup.DEEPEST_DATA_PATH, exe)
        # every name the game and setup choose themselves: within 64 (the staging suffixes at their longest)
        import shader_prebuild  # noqa: F401  (the Python side of the shader caches)
        t8 = ".t" + "f" * 8
        own = [
            "user\\shader-prebuild\\live\\vk-%s.bin.recipes%s" % ("f" * 12, t8),       # pipeline cache recipes
            "user\\shader-prebuild\\live\\vulkan-shaders\\spirv.bin" + t8,
            "user\\shader-prebuild\\generation-abcdefgh\\manifest.json" + t8,           # also Python's mkstemp
            "user\\shader-prebuild\\generation-abcdefgh\\headstart.bin" + t8,
            "user\\shader-prebuild\\launcher.json" + t8,
            "user\\shadercache\\vk-%s.bin.recipes%s" % ("f" * 12, t8),                # without prepared shaders
            "user\\screenshots\\WindWakerHD_2026-10-11_02-33-07.png",
            "code-build-%s.partial\\obj\\code_0123.c.o" % ("f" * 16),
            "code-builds\\%s\\bin\\wwhd.exe" % ("f" * 16),
            "game-lang\\EU.tmp\\content\\Common\\Pack\\permanent_2d_euitalian.pack",
        ]
        for path in own:
            self.assertLessEqual(len(path), 64, path)
        # save backups (the runtime's mod manager) keep the game's save folder below their own: at most as
        # deep as the game files
        backup = "save-backups\\2026-10-12_03-15-00-mod-abcdefg-99\\save\\user\\cking_pic_order0.sav"
        self.assertLessEqual(len(backup), setup.DEEPEST_DATA_PATH)
        self.assertEqual(len(setup.code_mods.cache_name("f" * 64)), 16)
        self.assertEqual(os.path.basename(setup.game_staging_dir(tempfile.gettempdir())), "gnew")

    def test_short_code_build_folders(self):
        # code-builds/<16 hex digits>; the 64-digit folders of earlier releases are still recognised
        from pathlib import Path
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            active, inactive, old = "a" * 64, "b" * 64, "c" * 64
            for key, name in ((active, active[:16]), (inactive, inactive[:16]), (old, old)):
                (root / "code-builds" / name).mkdir(parents=True)
                (root / "code-builds" / name / "ready.json").write_text(json.dumps({"fingerprint": key}))
            (root / "code-mods-active.json").write_text(json.dumps({"fingerprint": active}))
            with mock.patch.object(setup, "free_space", side_effect=[0, 0, 3 << 30]):
                setup.code_mods.make_build_space(setup, root)
            self.assertEqual(sorted(p.name for p in (root / "code-builds").iterdir()), [active[:16]])

    def test_reporter_folder(self):
        # the reporter's folder: the compiler's deepest file (118 characters below unpack.tmp) is beyond MAX_PATH
        self.assertEqual(len(self.DEVEL), 125)
        self.assertGreater(len(self.DEVEL + "\\data\\toolchain\\unpack.tmp\\") + 117, setup.WINDOWS_MAX_PATH)
        self.assertLessEqual(len(self.RELEASE + "\\data\\toolchain\\unpack.tmp\\") + 117, setup.WINDOWS_MAX_PATH)
        # with longpath() setup unpacks it there, and the game and its tools stay within MAX_PATH: no stop,
        # also with a long account name
        for folder in (self.DEVEL, self.RELEASE, self.DEVEL.replace("player", "a.much.longer.account.name"),
                       "C:\\Games\\WindWakerHD"):
            self.assertIsNone(setup.path_length_problem(folder + "\\data", folder, windows=True), folder)
        # 51 characters more than the reporter's: one too many for the game's own files
        deeper = self.DEVEL.replace("player", "player" + "x" * 51)
        self.assertEqual(setup.path_length_problem(deeper + "\\data", deeper, windows=True),
                         "This folder's path is too long for Windows (176 characters, at most 175 work). Move the Wind "
                         "Waker HD folder to a shorter place such as C:\\Games\\WindWakerHD and start it again.")
        # only on Windows
        self.assertIsNone(setup.path_length_problem("/x" * 200, "/x" * 199, windows=False))

    def test_limit_boundary(self):
        pkg = "C:\\" + "p" * (setup.MAX_DATA_DIR_CHARS - len("\\data") - 3)
        self.assertEqual(len(pkg), 175)
        self.assertEqual(len(pkg + "\\data"), setup.MAX_DATA_DIR_CHARS)
        self.assertIsNone(setup.path_length_problem(pkg + "\\data", pkg, windows=True))
        self.assertIsNotNone(setup.path_length_problem(pkg + "p\\data", pkg + "p", windows=True))
        # given as an extended-length path, or with another case of the drive letter: the same folder
        self.assertIn("(176 characters, at most 175 work)",
                      setup.path_length_problem(setup.longpath(pkg + "p\\data", windows=True), "c" + pkg[1:] + "p",
                                                windows=True))

    def test_install_folder_outside_the_release(self):
        data = "D:\\" + "\\".join(["games"] * 30)
        msg = setup.path_length_problem(data, self.RELEASE, windows=True)
        self.assertIn("The install folder's path is too long for Windows (%d characters, at most 180 work)" % len(data), msg)

    def test_setup_stops_before_anything_else(self):
        with tempfile.TemporaryDirectory() as d:
            data = os.path.join(d, "x" * (setup.MAX_DATA_DIR_CHARS - len(d)), "data")
            args = mock.Mock(data_dir=data, app_dir=None)
            saved = setup.LOG.f
            try:
                with mock.patch.object(setup, "IS_WIN", True), mock.patch.object(setup, "PKG", os.path.dirname(data)), \
                        mock.patch.object(setup, "load_manifest", return_value={"version": "v0", "exe": "wwhd"}):
                    with self.assertRaises(setup.SetupError) as e:
                        setup.Ctx(args)
                self.assertTrue(str(e.exception).startswith("This folder's path is too long for Windows"))
                with open(os.path.join(data, "setup.log"), encoding="utf-8") as f:
                    self.assertIn("too long for Windows", f.read())
            finally:
                if setup.LOG.f is not saved:
                    setup.LOG.f.close()
                setup.LOG.f = saved

    def test_unpacks_a_deep_compiler_archive_into_a_deep_folder(self):
        # On Windows the member ends up far beyond MAX_PATH (about 300 characters); elsewhere the same code path runs.
        import zipfile
        top = tempfile.mkdtemp()
        self.addCleanup(lambda: __import__("shutil").rmtree(setup.longpath(top), ignore_errors=True))
        # deep only where setup uses \\?\ paths (not with MSYS2's Python, see NATIVE_WINDOWS_PATHS)
        deep = setup.NATIVE_WINDOWS_PATHS or not setup.IS_WIN
        data = os.path.join(top, "d" * max(1, (120 if deep else 20) - len(top) - 1))
        os.makedirs(data)
        member = "llvm-mingw-test/" + "/".join(["python-lib-site-packages-%02d" % i for i in range(5 if deep else 2)]) + "/deep_module.pyd"
        arc = os.path.join(top, "src.zip")
        with zipfile.ZipFile(arc, "w") as z:
            z.writestr(member, b"deep")
            z.writestr("llvm-mingw-test/bin/x86_64-w64-mingw32-clang.exe", b"")
        sha = setup.file_sha256(arc)
        pins = {"toolchains": {"test-mingw": {"kind": "llvm-mingw", "url": "https://example.invalid/llvm-mingw-test.zip",
                                              "sha256": sha, "size": os.path.getsize(arc), "dir": "llvm-mingw-test"}}}
        env = {k: v for k, v in os.environ.items() if k != "WWHD_TOOLCHAIN_DIR"}
        fetch = lambda url, dst, *a, **k: __import__("shutil").copyfile(arc, dst)
        with mock.patch.object(setup, "load_toolchains", return_value=pins), mock.patch.object(setup, "download", fetch), \
                mock.patch.dict(os.environ, env, clear=True), mock.patch.object(setup, "say"):
            tc = setup.get_toolchain("test-mingw", data, setup.UI(False))
        root = os.path.join(data, "toolchain")
        deep = os.path.join(root, *member.split("/"))
        if setup.NATIVE_WINDOWS_PATHS:
            self.assertGreater(len(os.path.join(root, "unpack.tmp", *member.split("/"))), 260)
        with open(setup.longpath(deep), "rb") as f:
            self.assertEqual(f.read(), b"deep")
        self.assertEqual(tc.cc, [os.path.join(root, "llvm-mingw-test", "bin", "x86_64-w64-mingw32-clang.exe")])
        self.assertEqual(sorted(os.listdir(root)), ["llvm-mingw-test"])  # no unpack.tmp, no archive left
        marker = os.path.join(root, "llvm-mingw-test", ".wwhd-toolchain")  # text mode: "\r\n" on Windows
        self.assertEqual(setup.folder_size(root), 4 + os.path.getsize(setup.longpath(marker)))  # the deep file and the marker
        with mock.patch.object(setup, "say"):
            setup.remove_toolchain(data)
        self.assertFalse(os.path.exists(root))


class DecompOption(unittest.TestCase):
    """Setup's optional decomp version (decomp_setup.py): gates, the bundled source check, the remembered
    option, the link line."""
    DEV = {"version": "v0.2.13-devel.0123abcd", "platform": "macos-arm64", "decomp": {"tree_sha256": ""}}

    def test_dev_build_gate(self):
        ds = setup.decomp_setup
        self.assertTrue(ds.is_dev_version("v0.2.13-devel.0123abcd"))
        self.assertTrue(ds.is_dev_version("development build"))
        for v in ("v0.2.13", "v0.2.13-rc1", "", None, "v0.2.13-devel.", "devel-0123abcd"):
            self.assertFalse(ds.is_dev_version(v), v)
        self.assertTrue(ds.offered(self.DEV, True))
        self.assertFalse(ds.offered(dict(self.DEV, version="v0.2.13"), True))   # a release
        self.assertFalse(ds.offered({k: v for k, v in self.DEV.items() if k != "decomp"}, True))  # no parts

    def test_platform_gate(self):
        self.assertFalse(setup.decomp_setup.offered(self.DEV, False))
        with mock.patch.object(setup, "IS_MAC", False):
            self.assertFalse(setup.decomp_offered(self.DEV))
        with mock.patch.object(setup, "IS_MAC", True):
            self.assertTrue(setup.decomp_offered(self.DEV))

    def wanted(self, manifest, arg, state, mac=True):
        ctx = mock.Mock(manifest=manifest, args=mock.Mock(decomp=arg))
        ctx.state.return_value = state
        with mock.patch.object(setup, "IS_MAC", mac):
            return setup.decomp_wanted(ctx)

    def test_option_default_off_and_remembered(self):
        self.assertFalse(self.wanted(self.DEV, None, {}))                 # off by default
        self.assertTrue(self.wanted(self.DEV, True, {}))                  # --decomp / the checkbox
        self.assertTrue(self.wanted(self.DEV, None, {"decomp": True}))    # a repair or update remembers it
        self.assertFalse(self.wanted(self.DEV, False, {"decomp": True}))  # --no-decomp
        self.assertFalse(self.wanted(self.DEV, True, {}, mac=False))      # never where it is not offered
        self.assertFalse(self.wanted(dict(self.DEV, version="v0.2.13"), True, {"decomp": True}))

    def test_steps(self):
        self.assertEqual(setup.plan_steps("installed", True)[-2:], ["shaders", "decomp"])
        self.assertNotIn("decomp", setup.plan_steps("installed"))
        self.assertIn("decomp", setup.STEP_TITLES)

    # the decomp source: downloaded (a GitHub archive of the pinned commit), unpacked, checked
    FILES = {"wwhd_src/a.cpp": "/* 02000020 */\nvoid f() {}\n", "tools/verify/units/a.txt": "src wwhd_src/a.cpp\n",
             "LICENSE": "CC0\n", "docs/not-needed.md": "x\n"}

    def archive(self, d, name, files, prefix="wwhd-2c1deae/"):
        import io
        import tarfile
        path = os.path.join(d, name)
        with tarfile.open(path, "w:gz") as t:
            for rel, text in files.items():
                data = text.encode()
                info = tarfile.TarInfo(prefix + rel)
                info.size = len(data)
                t.addfile(info, io.BytesIO(data))
        return path

    def expected_digest(self, d, files):
        tree = os.path.join(d, "expected")
        for rel, text in files.items():
            if setup.decomp_setup.in_source(rel):
                os.makedirs(os.path.dirname(os.path.join(tree, rel)), exist_ok=True)
                with open(os.path.join(tree, rel), "wb") as f:  # the archive's bytes ("w" writes CRLF on Windows)
                    f.write(text.encode())
        return setup.decomp_setup.tree_digest(tree)

    def test_digest_platform_independent(self):
        # the same files listed as on Windows (ntpath: '\\', another walk order) or from a \\?\ root hash the same
        import ntpath
        import posixpath
        ds = setup.decomp_setup
        h = {rel: hashlib.sha256(text.encode()).hexdigest() for rel, text in self.FILES.items()}
        posix = [(posixpath.join(*rel.split("/")), v) for rel, v in h.items()]
        windows = [(ntpath.join(*rel.split("/")), v) for rel, v in reversed(list(h.items()))]
        self.assertIn("\\", windows[0][0])
        self.assertEqual(ds.digest_entries(windows), ds.digest_entries(posix))
        # the line format the release's hash was made with: "SHA-256 PATH\n", sorted bytewise by path
        lines = "".join("%s %s\n" % (h[rel], rel) for rel in sorted(h, key=lambda r: r.encode()))
        self.assertEqual(ds.digest_entries(posix), hashlib.sha256(lines.encode()).hexdigest())
        with tempfile.TemporaryDirectory() as d:
            for rel, text in self.FILES.items():
                os.makedirs(os.path.dirname(os.path.join(d, "t", rel)), exist_ok=True)
                with open(os.path.join(d, "t", rel), "wb") as f:
                    f.write(text.encode())
            self.assertEqual(ds.tree_digest(os.path.join(d, "t")), ds.digest_entries(posix))
            self.assertEqual(ds.tree_digest(os.path.join(d, "t") + os.sep), ds.digest_entries(posix))
            # a root written another way (as longpath() writes one on Windows): only the names below count
            walk = [(r.replace(os.path.join(d, "t"), "\\\\?\\C:\\x"), dn, fn) for r, dn, fn in os.walk(os.path.join(d, "t"))]
            real_open = open
            with mock.patch.object(ds.os, "walk", lambda root: iter(walk)), \
                    mock.patch("builtins.open", lambda p, m="r": real_open(p.replace("\\\\?\\C:\\x", os.path.join(d, "t")), m)):
                self.assertEqual(ds.tree_digest("\\\\?\\C:\\x"), ds.digest_entries(posix))

    def fetch(self, data, d, source, fail=None):
        """decomp_setup.fetch_source with download() copying `source` (or raising `fail`); returns (root, urls)"""
        urls = []

        def download(url, dst, sha256, size, label):
            urls.append((url, sha256, label))
            if fail:
                raise fail
            import shutil as sh
            sh.copyfile(source, dst)
        s = mock.Mock(download=download, say=lambda *a: None, LOG=mock.Mock(), SetupError=setup.SetupError,
                      longpath=setup.longpath, plain_text=setup.plain_text)
        return setup.decomp_setup.fetch_source(s, data, d), urls

    def test_source_download_and_check(self):
        ds = setup.decomp_setup
        with tempfile.TemporaryDirectory() as d:
            src = self.archive(d, "a.tar.gz", self.FILES)
            pin = {"revision": "2c1deae" + "0" * 33, "tree_sha256": self.expected_digest(d, self.FILES),
                   "download_bytes": 8100000}
            data = os.path.join(d, "data")
            root, urls = self.fetch(data, pin, src)
            self.assertEqual(root, os.path.join(data, "decomp-src", pin["revision"][:12]))  # short name (#144)
            self.assertTrue(os.path.isfile(os.path.join(root, "wwhd_src", "a.cpp")))
            self.assertFalse(os.path.exists(os.path.join(root, "docs")))  # only what the build needs
            self.assertEqual(urls[0][0], ds.ARCHIVE_URL % pin["revision"])
            self.assertIsNone(urls[0][1])  # the unpacked tree is checked, not GitHub's archive
            self.assertEqual(urls[0][2], "Downloading the decomp source (about 8 MB)")
            self.assertEqual(os.listdir(os.path.join(data, "decomp-src")), [pin["revision"][:12]])  # no archive left
            # an update with the same pin: nothing downloaded
            root2, urls2 = self.fetch(data, pin, src)
            self.assertEqual((root2, urls2), (root, []))
            # another pin: downloaded again, the old copy removed
            pin2 = dict(pin, revision="1" * 40)
            root3, urls3 = self.fetch(data, pin2, src)
            self.assertEqual(len(urls3), 1)
            self.assertEqual(os.listdir(os.path.join(data, "decomp-src")), [pin2["revision"][:12]])
            # a damaged cached copy is downloaded again
            with open(os.path.join(root3, "LICENSE"), "a") as f:
                f.write("changed\n")
            _, urls4 = self.fetch(data, pin2, src)
            self.assertEqual(len(urls4), 1)

    def test_source_tamper_detected(self):
        with tempfile.TemporaryDirectory() as d:
            pin = {"revision": "2" * 40, "tree_sha256": self.expected_digest(d, self.FILES)}
            bad = self.archive(d, "bad.tar.gz", dict(self.FILES, **{"wwhd_src/a.cpp": "void f() { evil(); }\n"}))
            data = os.path.join(d, "data")
            with self.assertRaises(setup.SetupError) as e:
                self.fetch(data, pin, bad)
            self.assertIn("Couldn't download the decomp source", str(e.exception))
            self.assertIn("SHA-256", str(e.exception))
            self.assertFalse(os.path.exists(os.path.join(data, "decomp-src")))  # nothing kept
            unsafe = self.archive(d, "unsafe.tar.gz", {"wwhd_src/../../x": "x"})
            with self.assertRaises(setup.SetupError):
                self.fetch(data, pin, unsafe)
            self.assertFalse(os.path.exists(os.path.join(d, "x")))

    def test_source_file_operations_use_longpath(self):
        # Windows extended-length paths for setup's own unpacking, checking and moving (issue #144)
        with tempfile.TemporaryDirectory() as d:
            src = self.archive(d, "a.tar.gz", self.FILES)
            pin = {"revision": "4" * 40, "tree_sha256": self.expected_digest(d, self.FILES)}
            seen = []

            def lp(p):
                seen.append(os.path.relpath(p, d))
                return p
            with mock.patch.object(setup, "longpath", lp):
                self.fetch(os.path.join(d, "data"), pin, src)
            root = os.path.join("data", "decomp-src", "4" * 12)
            for p in (os.path.dirname(root), root + ".tmp", root):
                self.assertIn(p, seen)

    def test_source_offline(self):
        with tempfile.TemporaryDirectory() as d:
            pin = {"revision": "3" * 40, "tree_sha256": "0" * 64}
            offline = setup.SetupError("could not download https://github.com/...: [Errno 8] nodename nor servname provided")
            with self.assertRaises(setup.SetupError) as e:
                self.fetch(os.path.join(d, "data"), pin, None, fail=offline)
            msg = str(e.exception)
            self.assertTrue(msg.startswith("Couldn't download the decomp source \u2014 check your internet connection "
                                           "and try again"), msg)
            self.assertFalse(os.path.exists(os.path.join(d, "data", "decomp-src")))

    def test_digest_ignores_finder_files(self):
        ds = setup.decomp_setup
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "a.cpp"), "w") as f:
                f.write("x")
            before = ds.tree_digest(d)
            for junk in (".DS_Store", "._a.cpp"):
                open(os.path.join(d, junk), "w").close()
            self.assertEqual(ds.tree_digest(d), before)

    def test_digest_independent_of_walk_order(self):
        ds = setup.decomp_setup
        with tempfile.TemporaryDirectory() as d:
            for rel in ("b/x", "a/y", "a/z", "c"):
                os.makedirs(os.path.dirname(os.path.join(d, "t1", rel)) or d, exist_ok=True)
                os.makedirs(os.path.dirname(os.path.join(d, "t2", rel)) or d, exist_ok=True)
            for rel in ("c", "a/z", "b/x", "a/y"):
                for t in ("t1", "t2"):
                    with open(os.path.join(d, t, rel), "wb") as f:
                        f.write(rel.encode())
            self.assertEqual(ds.tree_digest(os.path.join(d, "t1")), ds.tree_digest(os.path.join(d, "t2")))

    def test_link_line_glue(self):
        calls = []
        tc = setup.Toolchain(["cc"], ["c++"], ["ar"])
        with tempfile.TemporaryDirectory() as d, \
                mock.patch.object(setup, "run_logged", lambda cmd, **kw: calls.append(cmd)), \
                mock.patch.object(setup, "link_with_sdk_fallback", lambda cmd, env: calls.append(cmd) or
                                  open(os.path.join(d, "out"), "w").close()):
            setup.link_game(tc, {"link": ["{gamecode}", "-o", "{out}"]}, ["g.o"], d, os.path.join(d, "out"),
                            recipe=["{sdk}/decomp/obj/main.o", "{decomp_glue}", "{gamecode}", "-o", "{out}"],
                            glue=["hooks.o", "u1.o"])
        link = calls[-1]
        self.assertEqual(link[1:4], [setup.fwd(os.path.join(setup.PKG, "sdk")) + "/decomp/obj/main.o", "hooks.o", "u1.o"])

    def test_response_files_are_utf8(self):
        # Windows links through response files; a folder with characters outside the code page (an emoji,
        # Asian scripts) stopped setup with "'charmap' codec can't encode" before they were written as UTF-8
        calls = []
        tc = setup.Toolchain(["cc"], ["c++"], ["ar"], rsp=True)
        with tempfile.TemporaryDirectory() as root:
            d = os.path.join(root, "\U0001f3ae Games \u6e38\u620f")
            os.makedirs(d)
            obj = os.path.join(d, "g.o")
            with mock.patch.object(setup, "run_logged", lambda cmd, **kw: calls.append(cmd)), \
                    mock.patch.object(setup, "link_with_sdk_fallback", lambda cmd, env: calls.append(cmd) or
                                      open(os.path.join(d, "out"), "w").close()):
                setup.link_game(tc, {"link": ["{gamecode}", "-o", "{out}"]}, [obj], d, os.path.join(d, "out"))
            for name in ("ar.rsp", "link.rsp"):
                with open(os.path.join(d, name), encoding="utf-8") as f:
                    self.assertIn("\U0001f3ae Games \u6e38\u620f", f.read(), name)

    def test_state_keeps_decomp_exe_relative(self):
        with tempfile.TemporaryDirectory() as d, mock.patch.object(setup, "PORTABLE", True):
            exe = os.path.join(d, "bin", "wwhd-decomp")
            setup.write_state(d, {"exe": os.path.join(d, "bin", "wwhd"), "decomp_exe": exe, "start_decomp": True})
            with open(os.path.join(d, "install.json")) as f:
                self.assertEqual(json.load(f)["decomp_exe"], "bin/wwhd-decomp")
            self.assertEqual(setup.read_state(d)["decomp_exe"], exe)

    def test_launch_chooses_version(self):
        with tempfile.TemporaryDirectory() as d:
            exe, dexe = os.path.join(d, "wwhd"), os.path.join(d, "wwhd-decomp")
            open(dexe, "w").close()
            started = []
            with mock.patch.object(setup.subprocess, "Popen", lambda args, **kw: started.append(args[0])), \
                    mock.patch.object(setup, "say", lambda *a: None), mock.patch.object(setup, "IS_MAC", False):
                st = {"exe": exe, "decomp_exe": dexe}
                setup.launch(st, d)
                setup.launch(dict(st, start_decomp=True), d)
                setup.launch(st, d, decomp=True)
                os.remove(dexe)
                setup.launch(dict(st, start_decomp=True), d)  # not built (any more): the normal game
            self.assertEqual(started, [exe, dexe, dexe, exe])


class GameOptions(unittest.TestCase):
    """issue #266: Wind Waker HD.exe --editor (and the other launchers) pass game options on to the game"""
    SPACED = 'C:\\Users\\Jo Doe\\my "mods" dir\\'

    def test_split(self):
        own, game = setup.split_game_args(["--yes", "--editor", "--data-dir", "D", "--renderer", "vulkan",
                                           "--gui-protocol", "--trace", "--renderer=metal", "--decomp"])
        self.assertEqual(own, ["--yes", "--data-dir", "D", "--gui-protocol", "--decomp"])
        self.assertEqual(game, ["--editor", "--renderer", "vulkan", "--trace", "--renderer=metal"])
        # everything after "--" is the game's, as it is (a setup option there too)
        own, game = setup.split_game_args(["--data-dir", "D", "--", "--user-dir", self.SPACED, "--yes"])
        self.assertEqual(own, ["--data-dir", "D"])
        self.assertEqual(game, ["--user-dir", self.SPACED, "--yes"])
        self.assertEqual(setup.split_game_args(["--yes"]), (["--yes"], []))

    def test_main_parses_with_game_options(self):
        """setup's argparse never sees game options (it stopped with "unrecognized arguments")"""
        seen = {}

        def run(args, ui):
            seen["args"], seen["game"] = args, list(setup.GAME_ARGS)
            return 0
        argv = ["setup.py", "--yes", "--editor", "--data-dir", "D", "--", "--user-dir", self.SPACED]
        try:
            with mock.patch.object(sys, "argv", argv), mock.patch.object(setup, "run", run):
                self.assertEqual(setup.main(), 0)
        finally:
            setup.GAME_ARGS[:] = []
        self.assertEqual(seen["args"].data_dir, "D")
        self.assertTrue(seen["args"].yes)
        self.assertEqual(seen["game"], ["--editor", "--user-dir", self.SPACED])

    def started(self, state, d, game_args=None, mac=False, win=False):
        calls = []
        with mock.patch.object(setup.subprocess, "Popen", lambda args, **kw: calls.append(args)), \
                mock.patch.object(setup, "say", lambda *a: None), mock.patch.object(setup, "IS_MAC", mac), \
                mock.patch.object(setup, "IS_WIN", win):
            setup.launch(state, d, game_args=game_args)
        self.assertEqual(len(calls), 1)
        return calls[0]

    def test_launch_on_each_platform(self):
        with tempfile.TemporaryDirectory() as d:
            exe, app = os.path.join(d, "wwhd"), os.path.join(d, "Wind Waker HD.app")
            os.mkdir(app)
            opts = ["--editor", "--user-dir", self.SPACED]
            fixed = [exe, "--game", os.path.join(d, "game"), "--save", os.path.join(d, "save")]
            # Windows and Linux (and macOS without an app): appended to the game's own arguments, one list
            # item each, so spaces and quotes survive (subprocess quotes them for CreateProcess on Windows)
            self.assertEqual(self.started({"exe": exe}, d, opts, win=True), fixed + opts)
            self.assertEqual(self.started({"exe": exe}, d, opts), fixed + opts)
            self.assertEqual(self.started({"exe": exe, "app": app}, d, opts, mac=False), fixed + opts)
            # macOS app (non-portable install): open --args; the app's launcher passes "$@" on
            self.assertEqual(self.started({"exe": exe, "app": app}, d, opts, mac=True), ["open", app, "--args"] + opts)
            self.assertEqual(self.started({"exe": exe, "app": app}, d, [], mac=True), ["open", app])
            self.assertEqual(self.started({"exe": exe}, d, []), fixed)
            # the decomp version gets them too
            dexe = os.path.join(d, "wwhd-decomp")
            open(dexe, "w").close()
            args = self.started({"exe": exe, "decomp_exe": dexe, "start_decomp": True, "app": app}, d, ["--editor"], mac=True)
            self.assertEqual((args[0], args[-1]), (dexe, "--editor"))

    def test_launch_uses_this_runs_options(self):
        """the GUI's launch request and the terminal menu call launch(state, data_dir): main's GAME_ARGS"""
        with tempfile.TemporaryDirectory() as d:
            own, setup.GAME_ARGS[:] = setup.split_game_args(["--gui-protocol", "--data-dir", d, "--editor"])
            try:
                args = self.started({"exe": "wwhd"}, d)
            finally:
                setup.GAME_ARGS[:] = []
            self.assertEqual(args[-1], "--editor")
            self.assertNotIn("--gui-protocol", args)
            self.assertNotIn("--data-dir", args)

    def test_windows_quoting(self):
        # what CreateProcess gets for the list (CommandLineToArgvW / the C runtime read it back as one argument)
        self.assertEqual(setup.subprocess.list2cmdline(["--user-dir", self.SPACED]),
                         '--user-dir "C:\\Users\\Jo Doe\\my \\"mods\\" dir\\\\"')

    def test_same_rule_as_the_launcher(self):
        """tools/installer/gui/game_args.h (the launcher program) lists the same options"""
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "gui", "game_args.h")) as f:
            text = f.read()
        lists = {m.group(1): re.findall(r'"([^"]+)"', m.group(2))
                 for m in re.finditer(r"(kGameFlags|kGameValueOptions)\[\] = \{([^}]*)\}", text)}
        self.assertEqual(tuple(lists["kGameFlags"]), setup.GAME_FLAGS)
        self.assertEqual(tuple(lists["kGameValueOptions"]), setup.GAME_VALUE_OPTIONS)



class StoryNotebookStep(unittest.TestCase):
    """setup's story notebook step (runtime/src/mods/required.h): the game installs it once; offline is fine"""
    def run_step(self, stdout=None, raise_error=None):
        calls = []

        def runner(args, **kw):
            calls.append(args)
            if raise_error:
                raise raise_error
            return mock.Mock(returncode=0, stdout=stdout)
        with tempfile.TemporaryDirectory() as d, mock.patch.object(setup, "say") as said:
            ok = setup.install_story_notebook({"exe": "/app/wwhd", "game_dir": "/g"}, d, runner=runner)
            text = " ".join(str(c.args[0]) for c in said.call_args_list)
        return ok, calls, text

    def test_installed(self):
        ok, calls, text = self.run_step('[mods] ...\n{"ok": true, "message": "The story notebook is installed."}\n')
        self.assertTrue(ok)
        self.assertEqual(calls[0][0], "/app/wwhd")
        self.assertIn("--install-story-notebook", calls[0])
        self.assertIn("is installed", text)

    def test_offline_finishes_with_a_hint(self):
        ok, _, text = self.run_step('{"ok": false, "message": "Story notebook: not installed (offline)."}\n')
        self.assertFalse(ok)
        self.assertIn("install it later from the Mods menu", text)

    def test_no_answer_or_timeout(self):
        self.assertFalse(self.run_step("")[0])
        ok, _, text = self.run_step(raise_error=setup.subprocess.TimeoutExpired("wwhd", 600))
        self.assertFalse(ok)
        self.assertIn("Mods menu", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
