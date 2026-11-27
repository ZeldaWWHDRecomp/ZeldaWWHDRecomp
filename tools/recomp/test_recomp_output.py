#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""recomp.py's output files are LF on every platform: python3 test_recomp_output.py

The decompilation's funcdb.py indexes the translated files in characters and reads them back with seek(), which uses
byte offsets; a CRLF file (text mode on Windows) shifts every function after the first line and fails the decomp build
in funcdb.Cfg. A file left as CRLF by an older run is rewritten, not taken as unchanged.
"""
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import recomp  # noqa: E402


class OutputNewlines(unittest.TestCase):
    def test_written_with_lf(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = os.path.join(temporary, "out.cpp")
            with recomp._Output(path) as f:
                f.write("void a() {\n}\nvoid b() {\n}\n")
            with open(path, "rb") as f:
                self.assertEqual(f.read(), b"void a() {\n}\nvoid b() {\n}\n")

    def test_a_crlf_file_is_rewritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = os.path.join(temporary, "out.cpp")
            with open(path, "wb") as f:
                f.write(b"void a() {\r\n}\r\n")
            with recomp._Output(path) as f:
                f.write("void a() {\n}\n")
            with open(path, "rb") as f:
                self.assertEqual(f.read(), b"void a() {\n}\n")


if __name__ == "__main__":
    unittest.main()
