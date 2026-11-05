# SPDX-License-Identifier: MPL-2.0
"""object_cache.py and compile_gamecode with the cache: a stand-in compiler (a Python script) that preprocesses by
copying its input with the -D values it was given and "compiles" by writing the input's hash. No game files."""
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup  # noqa: E402
import object_cache  # noqa: E402

FAKE_CC = r'''
import hashlib, os, sys
args = sys.argv[1:]
if args == ["--version"]:
    print("fake cc 1.0"); sys.exit(0)
defs = sorted(a for a in args if a.startswith("-D"))
src = [a for a in args if a.endswith(".c")][0]
text = open(src).read()
inc = [a[2:] for a in args if a.startswith("-I")]
for d in inc:  # the "headers": every .h of an -I folder is part of the preprocessed text
    for h in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        if h.endswith(".h"):
            text += open(os.path.join(d, h)).read()
pre = "\n".join(defs) + "\n" + text
if "-E" in args:
    sys.stdout.write(pre); sys.exit(0)
if "BROKEN" in text:
    print("error: broken"); sys.exit(1)
out = args[args.index("-o") + 1]
with open(os.environ["FAKE_CC_LOG"], "a") as log:
    log.write(os.path.basename(src) + "\n")
open(out, "w").write("obj " + hashlib.sha256(pre.encode()).hexdigest())
'''


class ObjectCache(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.cc = root / "cc.py"
        self.cc.write_text(FAKE_CC)
        self.log = root / "cc.log"
        self.data = root / "data"
        self.tc = SimpleNamespace(cc=[sys.executable, str(self.cc)], env=dict(os.environ, FAKE_CC_LOG=str(self.log)))
        self.manifest = {"gamecode_cflags": ["-O3", "-I{gen}", "-DX=1"]}
        self.patch = mock.patch.object(setup, "LOG", SimpleNamespace(f=None, write=lambda line: None))
        self.patch.start()

    def tearDown(self):
        self.patch.stop()
        self.tmp.cleanup()

    def gen(self, name, files, header="int h;"):
        d = Path(self.tmp.name) / name
        d.mkdir()
        for n, text in files.items():
            (d / n).write_text(text)
        (d / "funcs.h").write_text(header)
        for n in ("table.c", "imports.c"):
            if not (d / n).exists():
                (d / n).write_text(n)
        return d

    def compiled(self):
        names = self.log.read_text().split() if self.log.exists() else []
        self.log.write_text("")
        return sorted(names)

    def build(self, gen, obj):
        objs = setup.compile_gamecode(self.tc, self.manifest, str(gen), str(Path(self.tmp.name) / obj), 2,
                                      cache_dir=str(self.data))
        return {Path(o).name: Path(o).read_text() for o in objs}

    def test_second_build_compiles_only_changed_sources(self):
        a = self.gen("genA", {"code_000.c": "f0", "code_001.c": "f1", "code_002.c": "f2"})
        first = self.build(a, "objA")
        self.assertEqual(self.compiled(), ["code_000.c", "code_001.c", "code_002.c", "imports.c", "table.c"])
        b = self.gen("genB", {"code_000.c": "f0", "code_001.c": "f1 changed", "code_002.c": "f2", "table.c": "table 2"})
        second = self.build(b, "objB")
        self.assertEqual(self.compiled(), ["code_001.c", "table.c"])  # in another folder, the others came from the cache
        self.assertEqual(second["code_000.c.o"], first["code_000.c.o"])
        self.assertNotEqual(second["code_001.c.o"], first["code_001.c.o"])
        # the same objects as without a cache
        self.log.write_text("")
        cold = setup.compile_gamecode(self.tc, self.manifest, str(b), str(Path(self.tmp.name) / "objC"), 2)
        self.assertEqual({Path(o).name: Path(o).read_text() for o in cold}, second)

    def test_header_flag_and_compiler_changes_miss(self):
        a = self.gen("genA", {"code_000.c": "f0"})
        self.build(a, "o1")
        self.compiled()
        b = self.gen("genB", {"code_000.c": "f0"}, header="int other;")  # an included header changed
        self.build(b, "o2")
        self.assertEqual(self.compiled(), ["code_000.c", "imports.c", "table.c"])
        self.manifest["gamecode_cflags"] = ["-O3", "-I{gen}", "-DX=2"]  # another flag
        self.build(b, "o3")
        self.assertEqual(self.compiled(), ["code_000.c", "imports.c", "table.c"])
        self.cc.write_text(FAKE_CC.replace("fake cc 1.0", "fake cc 2.0"))  # another compiler version
        self.build(b, "o4")
        self.assertEqual(self.compiled(), ["code_000.c", "imports.c", "table.c"])

    def test_failed_compile_is_not_cached(self):
        a = self.gen("genA", {"code_000.c": "BROKEN"})
        with self.assertRaises(setup.SetupError):
            self.build(a, "o1")
        # (table.c and imports.c compiled and are kept; the broken source is not)
        self.assertEqual(len(list((self.data / "code-objects").glob("??/*.o"))), 2)

    def test_damaged_entry_and_trim(self):
        cache = object_cache.Cache(self.data / "c", "cc", ["-O3"], {})
        obj = Path(self.tmp.name) / "x.o"
        for i in range(5):
            obj.write_bytes(b"x" * 100)
            cache.store("%064x" % i, obj)
            os.utime(cache.path("%064x" % i), (1000 + i, 1000 + i))
        cache.trim(limit=250)
        left = sorted(p.name[:-2] for p in (self.data / "c").glob("??/*.o"))
        self.assertEqual(left, ["%064x" % 3, "%064x" % 4])  # the newest, within the limit
        self.assertFalse(cache.fetch("%064x" % 0, obj))
        self.assertTrue(cache.fetch("%064x" % 4, obj))
        self.assertFalse(cache.fetch(None, obj))


if __name__ == "__main__":
    unittest.main()
