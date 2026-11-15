#!/usr/bin/env python3
"""decl_table.py: the decompilation's decl_lint.py runs with autoglue reading the given RPX, never <decomp>/game;
decl_cache.py: its store (atomic, LRU), the table shared by build directories, and the per-unit entries (a change
recomputes only the sources it touches, results as uncached)"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import types
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import decl_cache  # noqa: E402

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
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'runs.txt'), 'a') as f:
        f.write('run\\n')
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

        self.cache = os.path.join(t, "cache")

    def tearDown(self):
        self.tmp.cleanup()

    def table(self, decomp, gen, out, *extra):
        r = subprocess.run([sys.executable, os.path.join(HERE, "decl_table.py"), decomp, gen, self.image,
                            self.rpx, out, "--jobs", "1"] + list(extra), capture_output=True, text=True)
        log = os.path.join(out, "decl_lint.log")
        self.assertEqual(r.returncode, 0, r.stderr + (open(log).read() if os.path.isfile(log) else ""))
        with open(os.path.join(out, "handoff.tsv")) as f:
            return r.stdout, f.read()

    def runs(self, decomp):
        p = os.path.join(decomp, "tools", "verify", "runs.txt")
        if not os.path.isfile(p):
            return 0
        with open(p) as f:
            return len(f.read().split())

    def test_bss_from_given_rpx(self):
        # the decompilation has no game/ link: autoglue must get the sections of the RPX given to decl_table.py, in
        # decl_lint's own process and in its spawned workers
        _, text = self.table(self.decomp, self.gen, self.out, "--no-cache")
        self.assertEqual(text, "main [(268435456, 256)]\nworker [(268435456, 256)]\n")

    def test_table_shared_by_build_directories(self):
        # the same inputs elsewhere (another checkout, another build directory) get the table from the shared cache
        with open(os.path.join(self.gen, "code_000.c"), "w") as f:
            f.write("void f_02000000(void) {}\n")
        _, first = self.table(self.decomp, self.gen, self.out, "--cache", self.cache)
        self.assertEqual(self.runs(self.decomp), 1)
        out2, d2, g2 = (os.path.join(self.tmp.name, d) for d in ("out2", "decomp2", "gen2"))
        shutil.copytree(self.decomp, d2)
        shutil.copytree(self.gen, g2)
        os.remove(os.path.join(d2, "tools", "verify", "runs.txt"))
        said, second = self.table(d2, g2, out2, "--cache", self.cache)
        self.assertIn("from the shared cache", said)
        self.assertEqual((second, self.runs(d2)), (first, 0))
        said, _ = self.table(d2, g2, out2, "--cache", self.cache)
        self.assertIn("up to date", said)
        # a changed translation runs decl_lint again
        with open(os.path.join(g2, "code_000.c"), "a") as f:
            f.write("void f_02000004(void) {}\n")
        self.table(d2, g2, out2, "--cache", self.cache)
        self.assertEqual(self.runs(d2), 1)


# the parts of decl_lint.py that read the runtime directory (as the decompilation has them)
LINT_RUNTIME = '''def compile_flags():
    f = list(CFLAGS)
    if PATHS["runtime"] != os.path.join(ROOT, "runtime"):
        f[f.index("-Iruntime/include")] = "-I" + os.path.join(PATHS["runtime"], "include")
def _import_reads(runtime=None):
    for path in glob.glob(os.path.join(runtime or os.path.join(ROOT, "runtime"), "src", "**", "*.cpp"), recursive=True):
        t = open(path, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"HLE\\((\\w+),\\s*(\\w+)\\)\\s*\\{", t):
            pass
def main():
    if a.runtime:
        IMPORT_READS.update(_import_reads(PATHS["runtime"]))
'''


class RuntimeInputsTest(unittest.TestCase):
    """decl_table.inputs_hash reads of runtime/src only the HLE bodies decl_lint reads, while decl_lint reads nothing
    else there"""

    def setUp(self):
        import decl_table
        self.dt = decl_table
        self.tmp = tempfile.TemporaryDirectory()
        t = self.tmp.name
        self.decomp, self.root, self.gen = (os.path.join(t, d) for d in ("decomp", "port", "gen"))
        for d in (os.path.join(self.decomp, "tools", "verify"), os.path.join(self.root, "runtime", "src", "os"),
                  os.path.join(self.root, "runtime", "include"), os.path.join(self.root, "tools"), self.gen):
            os.makedirs(d)
        self.lint(LINT_RUNTIME)
        self.write("runtime/src/os/hle.cpp", "static int x;\nHLE(coreinit, OSFoo) {\n    if (arg(c, 0)) { x++; }\n}\n"
                   "void helper() {}\n")
        self.write("runtime/src/render.cpp", "void draw() {}\n")
        self.write("runtime/src/render.h", "void draw();\n")
        self.image, self.rpx = os.path.join(t, "image.bin"), os.path.join(t, "cking.rpx")
        open(self.image, "wb").close()
        tiny_rpx(self.rpx)

    def tearDown(self):
        self.tmp.cleanup()

    def lint(self, text):
        with open(os.path.join(self.decomp, "tools", "verify", "decl_lint.py"), "w") as f:
            f.write(text)

    def write(self, rel, text):
        with open(os.path.join(self.root, rel), "w") as f:
            f.write(text)

    def key(self):
        return self.dt.inputs_hash(self.decomp, self.gen, self.image, self.rpx, root=self.root)

    def test_hle_bodies(self):
        self.assertEqual(self.dt.hle_bodies("a HLE(gx2, Draw) { if (x) { y(); } } b HLE(os, B){}"),
                         ["HLE(gx2, Draw) { if (x) { y(); } }", "HLE(os, B){}"])

    def test_only_hle_bodies_count(self):
        k = self.key()
        self.write("runtime/src/render.cpp", "void draw() { /* changed */ }\n")
        self.write("runtime/src/render.h", "void draw(int);\n")
        self.write("runtime/src/os/hle.cpp", "static int x, y;\nHLE(coreinit, OSFoo) {\n    if (arg(c, 0)) { x++; }\n}\n"
                   "void helper() { y++; }\n")
        self.assertEqual(self.key(), k)
        self.write("runtime/src/os/hle.cpp", "HLE(coreinit, OSFoo) {\n    if (arg(c, 1)) {}\n}\n")
        self.assertNotEqual(self.key(), k)
        k = self.key()
        self.write("runtime/src/new.cpp", "HLE(gx2, GX2Bar) { c->r[3] = 0; }\n")
        self.assertNotEqual(self.key(), k)
        k = self.key()
        self.write("runtime/include/api.h", "int api;\n")  # the sources compile against runtime/include
        self.assertNotEqual(self.key(), k)

    def test_other_runtime_reader_hashes_every_file(self):
        # a decl_lint that reads runtime/src some other way: every .cpp and .h file counts again
        self.lint(LINT_RUNTIME + 'def more():\n    open(os.path.join(PATHS["runtime"], "src", "main.cpp"))\n')
        k = self.key()
        self.write("runtime/src/render.h", "void draw(int);\n")
        self.assertNotEqual(self.key(), k)
        self.lint(LINT_RUNTIME.replace('r"HLE\\(', 'r"HLE2\\('))  # another HLE reader
        k = self.key()
        self.write("runtime/src/render.cpp", "void draw() { /* changed */ }\n")
        self.assertNotEqual(self.key(), k)


class StoreTest(unittest.TestCase):
    def test_atomic_roundtrip_and_lru_cap(self):
        with tempfile.TemporaryDirectory() as t:
            st = decl_cache.Store(t, cap=10000)
            self.assertIsNone(st.get("tu", "ab" * 32))
            st.put("tu", "ab" * 32, {"x": [1, 2]})
            self.assertEqual(st.get("tu", "ab" * 32), {"x": [1, 2]})
            self.assertEqual([f for f in os.listdir(os.path.join(t, "tu", "ab")) if ".tmp" in f], [])
            for i in range(8):  # 8 x ~3 KB (incompressible) > the 10 KB cap
                st.put_bytes("parse", "%064x" % i, os.urandom(3000))
                os.utime(st.path("parse", "%064x" % i), (1000 + i, 1000 + i))
            os.utime(st.path("parse", "%064x" % 7), (5000, 5000))
            st.get_bytes("parse", "%064x" % 0)  # used now: kept
            st.prune()
            left = {i for i in range(8) if os.path.isfile(st.path("parse", "%064x" % i))}
            self.assertLessEqual(st.size()[0], 8000)
            self.assertIn(0, left)
            self.assertIn(7, left)
            self.assertNotIn(1, left)

    def test_damaged_entry_is_a_miss(self):
        with tempfile.TemporaryDirectory() as t:
            st = decl_cache.Store(t)
            st.put_bytes("parse", "cd" * 32, b"not a pickle")
            self.assertIsNone(st.get("parse", "cd" * 32))


class HooksTest(unittest.TestCase):
    """the per-source entries on a decl_lint-shaped module: a second run computes nothing, an edited source (and the
    sources sharing a verify unit with it) only that, a header everything; results equal the uncached ones"""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.tmp.name
        self.decomp, self.gen, self.runtime = (os.path.join(t, d) for d in ("decomp", "gen", "runtime"))
        for d in ("wwhd_src/d", "wwhd_src/include", "tools/verify/include", "tools/verify/src"):
            os.makedirs(os.path.join(self.decomp, d))
        os.makedirs(os.path.join(self.runtime, "include"))
        os.makedirs(self.gen)
        self.write("tools/verify/decl_lint.py", "# fake\n")
        self.write("wwhd_src/include/h.h", "int h;\n")
        for n in ("a", "b", "c"):
            self.write("wwhd_src/d/%s.cpp" % n, "// %s\n" % n)
        with open(os.path.join(self.gen, "code_000.c"), "w") as f:
            f.write("x\n")
        self.image, self.rpx = os.path.join(t, "image.bin"), os.path.join(t, "cking.rpx")
        for p in (self.image, self.rpx):
            open(p, "wb").close()
        self.store = decl_cache.Store(os.path.join(t, "cache"))

    def tearDown(self):
        self.tmp.cleanup()
        for m in ("autoglue", "funcdb"):
            sys.modules.pop(m, None)

    def write(self, rel, text):
        with open(os.path.join(self.decomp, rel), "w") as f:
            f.write(text)

    def run_once(self):
        """-> (calls per kind, results) of one audit-shaped run with fresh fake modules"""
        calls = {"parse": [], "tu": [], "refs": []}
        root = self.decomp
        sys.modules["autoglue"] = types.SimpleNamespace(
            _spec_siblings=lambda rel: {"wwhd_src/d/a.cpp": {"wwhd_src/d/b.cpp"},
                                        "wwhd_src/d/b.cpp": {"wwhd_src/d/a.cpp"}}.get(rel, set()))

        def text(rel):
            with open(os.path.join(root, rel)) as f:
                return f.read()

        def parse_source(rel):
            calls["parse"].append(rel)
            return ([text(rel)], [])

        def compile_all(sources, jobs, scratch, cache=None):
            calls["tu"] += list(sources)
            return {s: ([[s, 1, text(s)]], "") for s in sources}

        class Audit:
            def stub_refs(self, rel, a, regs):
                calls["refs"].append(rel)
                return [(rel, a, text(rel))]

            def stub_result_used(self, rel, a, regs):
                return True
        D = types.SimpleNamespace(parse_source=parse_source, compile_all=compile_all, Audit=Audit,
                                  compile_flags=lambda: ["-I" + self.gen], PATHS={"image": self.image, "rpx": self.rpx,
                                                                                 "runtime": self.runtime})
        hooks = decl_cache.Hooks(self.store, self.decomp, self.gen, "tools")
        self.assertTrue(hooks.install_sources(D))
        srcs = ["wwhd_src/d/%s.cpp" % n for n in "abc"]
        res = ([D.parse_source(s) for s in srcs], D.compile_all(srcs, 1, None, None),
               [D.Audit().stub_refs(s, 1, (1, 0)) for s in srcs])
        hooks.save()
        decl_cache._DIGESTS.clear()
        return {k: sorted(v) for k, v in calls.items()}, res

    def test_recomputes_only_what_changed(self):
        allsrc = ["wwhd_src/d/%s.cpp" % n for n in "abc"]
        calls, first = self.run_once()
        self.assertEqual(calls, {"parse": allsrc, "tu": allsrc, "refs": allsrc})
        calls, again = self.run_once()
        self.assertEqual((calls, again), ({"parse": [], "tu": [], "refs": []}, first))
        self.write("wwhd_src/d/c.cpp", "// c changed\n")  # a source alone in its unit
        calls, res = self.run_once()
        self.assertEqual(calls, {"parse": allsrc[2:], "tu": allsrc[2:], "refs": allsrc[2:]})
        self.assertEqual(res[0][2], (["// c changed\n"], []))
        self.write("wwhd_src/d/a.cpp", "// a changed\n")  # a and b share a unit: both
        calls, _ = self.run_once()
        self.assertEqual(calls["parse"], allsrc[:2])
        self.write("wwhd_src/include/h.h", "int h2;\n")  # a header: every source
        calls, _ = self.run_once()
        self.assertEqual(calls["tu"], allsrc)


if __name__ == "__main__":
    unittest.main()
