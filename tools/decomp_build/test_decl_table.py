#!/usr/bin/env python3
"""decl_table.py: the decompilation's decl_lint.py runs with autoglue reading the given RPX, never <decomp>/game"""
import os
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))

AUTOGLUE = """import os
_bss = None
def bss_ranges():
    global _bss
    if _bss is None:
        open(os.path.join(os.path.dirname(__file__), '..', '..', 'game', 'code', 'cking.rpx'), 'rb')
    return _bss
"""

# port mode (--out-dir); writes the bss ranges seen by the main process and by a spawned worker (as decl_lint's
# ProcessPoolExecutor compile workers on macOS)
DECL_LINT = """import argparse, concurrent.futures, multiprocessing, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import autoglue
def worker(_):
    return repr(autoglue.bss_ranges())
def main():
    ap = argparse.ArgumentParser()
    for k in ('-j', '--gen', '--image', '--rpx', '--runtime', '--tools', '--out-dir', '--handoff'):
        ap.add_argument(k)
    a = ap.parse_args()
    with concurrent.futures.ProcessPoolExecutor(1, mp_context=multiprocessing.get_context('spawn')) as ex:
        w = list(ex.map(worker, [0]))[0]
    with open(a.handoff, 'w') as f:
        f.write('main %r\\nworker %s\\n' % (autoglue.bss_ranges(), w))
    return 0
if __name__ == '__main__':
    sys.exit(main())
"""


def tiny_rpx(path):
    """a minimal big-endian ELF: the null section, .bss (NOBITS at 0x10000000, 0x100 bytes), .shstrtab"""
    names = b"\0.bss\0.shstrtab\0"
    hdr = bytearray(0x34)
    hdr[:6] = b"\x7fELF\x01\x02"
    struct.pack_into(">I", hdr, 0x20, 0x34 + len(names))
    struct.pack_into(">HHH", hdr, 0x2E, 40, 3, 2)
    shdrs = bytes(40) + struct.pack(">10I", 1, 8, 3, 0x10000000, 0, 0x100, 0, 0, 4, 0) \
        + struct.pack(">10I", 6, 3, 0, 0, 0x34, len(names), 0, 0, 1, 0)
    with open(path, "wb") as f:
        f.write(bytes(hdr) + names + shdrs)


class DeclTableTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.tmp.name
        self.decomp, self.gen, self.out = (os.path.join(t, d) for d in ("decomp", "gen", "out"))
        os.makedirs(os.path.join(self.decomp, "tools", "verify"))
        os.makedirs(self.gen)
        for name, text in (("autoglue.py", AUTOGLUE), ("decl_lint.py", DECL_LINT)):
            with open(os.path.join(self.decomp, "tools", "verify", name), "w") as f:
                f.write(text)
        self.image, self.rpx = os.path.join(t, "image.bin"), os.path.join(t, "cking.rpx")
        open(self.image, "wb").close()
        tiny_rpx(self.rpx)

    def tearDown(self):
        self.tmp.cleanup()

    def test_bss_from_given_rpx(self):
        # the decompilation has no game/ link: autoglue must get the sections of the RPX given to decl_table.py, in
        # decl_lint's own process and in its spawned workers
        r = subprocess.run([sys.executable, os.path.join(HERE, "decl_table.py"), self.decomp, self.gen, self.image,
                            self.rpx, self.out, "--jobs", "1"], capture_output=True, text=True)
        log = os.path.join(self.out, "decl_lint.log")
        self.assertEqual(r.returncode, 0, r.stderr + (open(log).read() if os.path.isfile(log) else ""))
        with open(os.path.join(self.out, "handoff.tsv")) as f:
            self.assertEqual(f.read(), "main [(268435456, 256)]\nworker [(268435456, 256)]\n")


if __name__ == "__main__":
    unittest.main()
