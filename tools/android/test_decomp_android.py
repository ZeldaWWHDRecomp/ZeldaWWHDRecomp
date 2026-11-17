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

TEST_ROOTS = (
    """-----BEGIN CERTIFICATE-----
MIIBjTCCATOgAwIBAgIUI1tPTzuL+YXA4nJ2Q0vdgK8xK5cwCgYIKoZIzj0EAwIw
GzEZMBcGA1UEAwwQd3doZCB0ZXN0IHJvb3QgMTAgFw0yNjExMTcxMDQxMjdaGA8y
MTI2MTAyNDEwNDEyN1owGzEZMBcGA1UEAwwQd3doZCB0ZXN0IHJvb3QgMTBZMBMG
ByqGSM49AgEGCCqGSM49AwEHA0IABGLnlDgV7IoeUqV0GCLg/UaX/utEnVK0Up9L
lWguy7YLoTS9DvImCCS9Y0L9x4w6FTUYC8kP9GBGV3iUUo569yqjUzBRMB0GA1Ud
DgQWBBQttAQbiWlMy0XcVLHxeBa+KznoZzAfBgNVHSMEGDAWgBQttAQbiWlMy0Xc
VLHxeBa+KznoZzAPBgNVHRMBAf8EBTADAQH/MAoGCCqGSM49BAMCA0gAMEUCIQDc
pYeFNZ9k2rEC8FFZJC4c6kK1gJqmNoegfvoWXu0HxQIgJEJH83NQdOG/J4ZjzGA0
LHceG098lLB348DUx/E1dBY=
-----END CERTIFICATE-----
""",
    """-----BEGIN CERTIFICATE-----
MIIBjDCCATOgAwIBAgIUYNMFHPELdHpejNNgr/KuQpHHFIkwCgYIKoZIzj0EAwIw
GzEZMBcGA1UEAwwQd3doZCB0ZXN0IHJvb3QgMjAgFw0yNjExMTcxMDQxMjdaGA8y
MTI2MTAyNDEwNDEyN1owGzEZMBcGA1UEAwwQd3doZCB0ZXN0IHJvb3QgMjBZMBMG
ByqGSM49AgEGCCqGSM49AwEHA0IABAOEdOa3soUJnWW1SbSkUXz/GCI4bRvaVMgp
MWrzOMmSzHpukz9VeNkCzAoeubZw71onJBeHb7QhEoI+r93YyNajUzBRMB0GA1Ud
DgQWBBT4QWR3HXZNkY1peBFM9TiP825HtTAfBgNVHSMEGDAWgBT4QWR3HXZNkY1p
eBFM9TiP825HtTAPBgNVHRMBAf8EBTADAQH/MAoGCCqGSM49BAMCA0cAMEQCIGXB
owkEnQQN3wF+JPTGUqdMf8okHEdktRABIWaGtqDKAiAff/VGYI18Sl4zYOXI8Q7o
6chu4kuNvcKfNXH2GwEMFQ==
-----END CERTIFICATE-----
""",
)  # two throwaway self-signed test roots


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

    def test_object_cache_keys_through_the_bridge_see_whole_sources(self):
        # (phone run 2026-11-17: the bridge kept 4 MiB of the preprocessor's stdout, every decomp code_NNN.c got
        # the same key and so the first one's object; now the preprocessor writes a file, hashed in full)
        import object_cache

        def native(cmd, env=None, cancel=None, separate=False, input=None):
            assert "-E" in cmd
            text = Path(cmd[cmd.index("-P") + 1]).read_bytes()
            if "-o" in cmd:
                Path(cmd[cmd.index("-o") + 1]).write_bytes(text)
                text = b""
            return 0, text[:4 << 20], b""  # (the bridge's bound before it became an error)
        host = decomp_android.Host(FakeSetup(), self.root, COMPILER, TOOLS, native, self.root / "lib.so", self.log)
        shared = b"int f_02000000(void);\n" * 220000  # 4.8 MB, as funcs.h
        for name, tail in (("code_003.c", b"int a;"), ("code_015.c", b"int b;")):
            (self.root / name).write_bytes(shared + tail)
        cache = object_cache.Cache(self.root / "code-objects", "cc", ["-O3"], {})
        saved = object_cache.subprocess
        object_cache.subprocess = SimpleNamespace(run=host.subprocess_run, PIPE=subprocess.PIPE,
                                                  STDOUT=subprocess.STDOUT, DEVNULL=subprocess.DEVNULL)
        try:
            keys = [cache.key(["/apk/clang"], ["-O3"], str(self.root / n)) for n in ("code_003.c", "code_015.c")]
        finally:
            object_cache.subprocess = saved
        self.assertTrue(all(keys))
        self.assertNotEqual(keys[0], keys[1])

    def test_bridge_overflow_is_not_swallowed(self):
        def native(cmd, **_):
            raise RuntimeError("native tool output exceeds 4 MiB (stdout); write it to a file instead")
        host = decomp_android.Host(FakeSetup(), self.root, COMPILER, TOOLS, native, self.root / "lib.so", self.log)
        with self.assertRaisesRegex(RuntimeError, "exceeds 4 MiB"):
            host.subprocess_run(["clang", "-E", "big.c"], capture_output=True)

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


class CertificateStoreTests(unittest.TestCase):
    def test_android_store_files_are_loaded_whatever_their_names(self):
        # Android's cacerts: one PEM per file, named by the old subject hash, with a text dump after the block
        with tempfile.TemporaryDirectory() as folder:
            for i, pem in enumerate(TEST_ROOTS):
                Path(folder, "0000000%d.0" % i).write_text(pem + "Certificate:\n    Data:\n        Version: 3 (0x2)\n")
            Path(folder, "README").write_text("not a certificate\n")
            ctx = decomp_android.android_ssl_context(folder)
            self.assertEqual(ctx.cert_store_stats()["x509_ca"], 2)

    def test_an_empty_store_is_an_error(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(OSError):
                decomp_android.android_ssl_context(folder)


if __name__ == "__main__":
    unittest.main()
