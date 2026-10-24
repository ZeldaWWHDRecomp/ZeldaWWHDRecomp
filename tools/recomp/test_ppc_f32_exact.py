#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Bit-exact lfs / stfs / psq float loads and stores (ppc.h): python3 test_ppc_f32_exact.py

Builds runtime/tools/ppc_f32_exact_test.c at -O0 and -O2 with the game code's floating-point flags and
runs it (at -O2 over every one of the 2^32 single words on load). The host float<->double conversions
ppc.h used before quieted signalling NaNs at one optimisation level and not at the other, so both
levels are checked. Skipped when no C compiler is found.
"""
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(ROOT, "runtime", "tools", "ppc_f32_exact_test.c")
CC = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")


@unittest.skipUnless(CC, "no C compiler")
class F32Exact(unittest.TestCase):
    def build_run(self, opt, *args):
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, "t.exe" if os.name == "nt" else "t")
            subprocess.run([CC, "-std=c11", opt, "-ffp-contract=off", "-fno-strict-aliasing", "-fwrapv", "-w",
                            "-I" + os.path.join(ROOT, "runtime", "include"), SRC, "-o", exe], check=True)
            r = subprocess.run([exe, *args], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_O0(self):
        self.build_run("-O0")

    def test_O2_exhaustive(self):
        self.build_run("-O2", "--exhaustive")


if __name__ == "__main__":
    unittest.main()
