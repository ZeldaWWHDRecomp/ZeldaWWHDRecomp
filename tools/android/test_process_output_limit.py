# SPDX-License-Identifier: MPL-2.0
"""android/python/process.c (wwhd_native.run, the app's process bridge) built for this machine: captured output over
its 4 MiB bound is an error, never a silently shortened result (the decomp version's object cache hashed shortened
preprocessor output and gave every game-code file the same object, phone run 2026-11-17). Skipped without a C
compiler or Python headers. On macOS a small header supplies the Linux/bionic names process.c uses (pipe2 etc.)."""
import os
import shutil
import subprocess
import sys
import sysconfig
import tempfile
import unittest
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "android" / "python" / "process.c"
LIMIT = 4 * 1024 * 1024
SHIM = r"""
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
static inline int wwhd_test_pipe2(int fds[2], int flags) {
    if (pipe(fds) < 0) return -1;
    if (flags & O_CLOEXEC) { fcntl(fds[0], F_SETFD, FD_CLOEXEC); fcntl(fds[1], F_SETFD, FD_CLOEXEC); }
    return 0;
}
#define pipe2 wwhd_test_pipe2
"""


def build_module(out):
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    include = sysconfig.get_paths().get("include")
    if not cc or not include or not os.path.isfile(os.path.join(include, "Python.h")) or os.name == "nt":
        return None
    ext = sysconfig.get_config_var("EXT_SUFFIX") or ".so"
    lib = os.path.join(out, "wwhd_native" + ext)
    cmd = [cc, "-shared", "-fPIC", "-O1", "-I", include]
    if sys.platform == "darwin":
        shim = os.path.join(out, "shim.h")
        Path(shim).write_text(SHIM)
        cmd += ["-include", shim, "-undefined", "dynamic_lookup"]
    else:
        cmd += ["-D_GNU_SOURCE"]
    p = subprocess.run(cmd + [str(SOURCE), "-o", lib], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if p.returncode:
        return None
    spec = importlib.util.spec_from_file_location("wwhd_native", lib)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class OutputLimit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.native = build_module(cls.tmp.name)
        if cls.native is None:
            cls.tmp.cleanup()
            raise unittest.SkipTest("no C compiler with Python headers here")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def emit(self, count, stream=1):
        # (dd: exactly `count` bytes on the stream, from /dev/zero)
        return ["/bin/sh", "-c", "dd if=/dev/zero bs=65536 count=%d >&%d 2>/dev/null" % (count // 65536, stream)]

    def test_exactly_the_limit_is_returned(self):
        status, out = self.native.run(self.emit(LIMIT), timeout=30)
        self.assertEqual((status, len(out)), (0, LIMIT))

    def test_more_is_an_error_not_a_shorter_result(self):
        with self.assertRaisesRegex(RuntimeError, "exceeds 4 MiB .combined output."):
            self.native.run(self.emit(LIMIT + 65536), timeout=30)
        with self.assertRaisesRegex(RuntimeError, "exceeds 4 MiB .stdout."):
            self.native.run(self.emit(LIMIT + 65536), separate=True, timeout=30)
        with self.assertRaisesRegex(RuntimeError, "exceeds 4 MiB .stderr."):
            self.native.run(self.emit(LIMIT + 65536, 2), separate=True, timeout=30)

    def test_streamed_output_may_be_larger(self):
        # (the package extractor: on_output receives every byte; only the returned copy is bounded)
        got = []
        status, out, err = self.native.run(self.emit(LIMIT + 65536), separate=True, timeout=30,
                                           on_output=lambda stream, chunk: got.append(len(chunk)))
        self.assertEqual((status, len(out), err, sum(got)), (0, LIMIT, b"", LIMIT + 65536))


if __name__ == "__main__":
    unittest.main()
