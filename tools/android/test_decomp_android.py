"""tools/android/decomp_android.py (the decomp version in the Android app) and decomp_setup.py's Android branch."""
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent / "installer")]
import decomp_android  # noqa: E402
import decomp_setup  # noqa: E402

COMPILER = {"cc": ["/apk/clang", "--driver-mode=gcc"], "cxx": ["/apk/clang", "--driver-mode=g++", "--target=x"],
            "ar": ["/apk/ar"], "ld": "/apk/ld.lld", "identity": "i"}
TOOLS = {"ld.lld": "/apk/ld.lld", "llvm-objcopy": "/apk/objcopy", "llvm-link": "/apk/link", "llvm-dis": "/apk/dis",
         "llvm-as": "/apk/as"}


class FakeSetup:
    class SetupError(Exception):
        pass

    def __init__(self):
        self.ran = []


class HostTests(unittest.TestCase):
    def setUp(self):
        self.calls = []

        def native(cmd, env=None, cancel=None, separate=False, input=None):
            self.calls.append((cmd, env, input))
            return (0, b"out\n", b"err\n") if separate else (0, b"out\nerr\n")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.log = io.StringIO()
        self.host = decomp_android.Host(FakeSetup(), self.root, COMPILER, TOOLS, native, self.root / "lib.so", self.log)

    def tearDown(self):
        self.tmp.cleanup()

    def test_clangxx_is_the_app_compiler_and_a_folder_goes_through_the_shell(self):
        r = self.host.subprocess_run(["clang++", "-fsyntax-only", "a.cpp"], cwd="/w", capture_output=True, text=True)
        cmd = self.calls[-1][0]
        self.assertEqual(cmd[:4], [decomp_android.SHELL, "-c", 'cd "$0" && exec "$@"', "/w"])
        self.assertEqual(cmd[4:], COMPILER["cxx"] + ["-include", str(self.root / "tools/decomp_build/android_compat.h"),
                                                     "-fsyntax-only", "a.cpp"])
        self.assertEqual((r.returncode, r.stdout, r.stderr), (0, "out\n", "err\n"))

    def test_unknown_tools_are_missing(self):
        with self.assertRaises(FileNotFoundError):
            self.host.subprocess_run(["git", "rev-parse", "HEAD"], capture_output=True)

    def test_python_tools_run_here_with_their_arguments_folder_environment_and_pools(self):
        script = self.root / "tool.py"
        script.write_text("import os, sys, concurrent.futures as cf\n"
                          "with cf.ProcessPoolExecutor(max_workers=2, initializer=None) as ex:\n"
                          "    total = sum(ex.map(abs, [-1, -2], chunksize=4))\n"
                          "print(sys.argv[1:], os.getcwd() == os.path.realpath(sys.argv[1]), os.environ.get('WWHD_X'), total)\n"
                          "sys.exit(3)\n")
        before = (list(sys.argv), os.getcwd(), dict(os.environ))
        r = subprocess.CompletedProcess  # noqa: F841
        res = self.host.subprocess_run([sys.executable, str(script), str(self.root)], env=dict(os.environ, WWHD_X="1"),
                                       cwd=str(self.root), capture_output=True, text=True)
        self.assertEqual(res.returncode, 3)
        self.assertIn("True 1 3", res.stdout)
        self.assertEqual((list(sys.argv), os.getcwd(), dict(os.environ)), before)
        import concurrent.futures
        self.assertIsNot(concurrent.futures.ProcessPoolExecutor, decomp_android._ThreadPool)
        self.assertIs(subprocess.run.__module__, "subprocess")

    def test_run_logged_raises_with_the_tail(self):
        script = self.root / "fail.py"
        script.write_text("print('broken here')\nraise SystemExit('stop')\n")
        with self.assertRaises(FakeSetup.SetupError) as e:
            self.host.run_logged([sys.executable, str(script)], what="the tool")
        self.assertIn("broken here", str(e.exception))


class MergeTests(unittest.TestCase):
    def s(self):
        return SimpleNamespace(IS_ANDROID=True, DECOMP_TOOLS=TOOLS, PKG="/pkg")

    def test_elf_units_are_ld_r_then_localized(self):
        tc = SimpleNamespace(cxx=COMPILER["cxx"])
        plain = decomp_setup.merge_commands(self.s(), tc, False, False, None)("u.o", ["a.o", "b.o"])
        self.assertEqual(plain, [["/apk/ld.lld", "-r", "-o", "u.o", "a.o", "b.o"], ["/apk/objcopy", "--localize-hidden", "u.o"]])
        fast = decomp_setup.merge_commands(self.s(), tc, True, False, None)("u.o", ["a.o"])
        self.assertEqual(fast[0][:4], ["/apk/ld.lld", "-r", "--lto-O2", "--thinlto-jobs=1"])
        self.assertEqual(fast[1], ["/apk/objcopy", "--localize-hidden", "u.o"])

    def test_whole_image_only_when_switched_on(self):
        old = os.environ.get("WWHD_DECOMP_WHOLE_LTO")
        try:
            os.environ["WWHD_DECOMP_WHOLE_LTO"] = "0"
            self.assertFalse(decomp_setup.whole_lto(SimpleNamespace(cxx=["x"]), self.s())[0])
            os.environ.pop("WWHD_DECOMP_WHOLE_LTO")
            self.assertFalse(decomp_setup.whole_lto(SimpleNamespace(cxx=["x"]), self.s())[0])
            os.environ["WWHD_DECOMP_WHOLE_LTO"] = "1"
            self.assertTrue(decomp_setup.whole_lto(SimpleNamespace(cxx=["x"]), self.s())[0])
            cmd = decomp_setup.merge_commands(self.s(), SimpleNamespace(cxx=COMPILER["cxx"]), True, True, None)("u.o", ["a.o"])
            self.assertEqual(len(cmd), 1)
            self.assertIn("--tool", cmd[0])
            self.assertIn("llvm-dis=/apk/dis", cmd[0])
            self.assertEqual(cmd[0][cmd[0].index("--cc") + 1], "/apk/clang")
        finally:
            if old is None:
                os.environ.pop("WWHD_DECOMP_WHOLE_LTO", None)
            else:
                os.environ["WWHD_DECOMP_WHOLE_LTO"] = old

    def test_capstone_stand_in_on_android(self):
        self.assertEqual(decomp_setup.fetch_capstone(self.s(), "/data"),
                         os.path.join("/pkg", "tools", "decomp_build", "capstone_shim"))


if __name__ == "__main__":
    unittest.main()
