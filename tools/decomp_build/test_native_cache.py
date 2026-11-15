#!/usr/bin/env python3
"""native_cache.py: native_rules.py's per-source cache gives the outputs of an uncached run (native.txt, nentry.txt,
the units' native/*.h, the messages) after every kind of change, recomputes only what a change touches, treats a
damaged entry as a miss, shares entries between build directories at other paths and is pruned with the
declaration-table cache (decl_cache.py: the same store)"""
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import decl_cache  # noqa: E402
import native_cache  # noqa: E402
import native_rules  # noqa: E402
import test_native_rules  # noqa: E402

FAST = ["--lift", "autoframe,local,tailptr", "--fast", "--hcall", "--a3-fn", "--a3-stub", "--leaf", "--bare",
        "--bare-entry", "--unbased", "--stackarg"]


class NativeCacheTest(unittest.TestCase):
    def setUp(self):
        # test_native_rules' synthetic decomp build output, in fast mode: a declaration table (register-exact
        # functions: leaves, bare leaves, --bare-entry's name scan), a header with an hcall, a result stub
        test_native_rules.NativeRulesTest.setUp(self)
        F = self.F
        inc = os.path.join(self.src, "wwhd_src", "include")
        os.makedirs(inc)
        with open(os.path.join(inc, "h.h"), "w") as f:
            f.write("void f6x();\ninline int hc() { return gabi::hcall<0x%08X, int>(1); }\n" % F(6))
        with open(os.path.join(self.src, "a.cpp"), "a") as f:
            f.write("VERIFY(0x%08X, f6);\n" % F(6))
        # unit c: one leaf (no call in its original, beyond the image), so a bare one: --bare-entry scans c.cpp and h.h
        src = os.path.join(self.src, "c.cpp")
        with open(src, "w") as f:
            f.write('#include "h.h"\nint f19(int a) { WWHD_GLUE_FUNC_R(0x%08X, int, a); return wwhd_native_scope_.ret'
                    '<wwhd_ret_t_>((a + 2)); }\n' % F(19))
        with open(os.path.join(self.out, "functions.tsv"), "a") as f:
            f.write("%08X\tc\tf19\n" % F(19))
        uj = json.load(open(os.path.join(self.out, "units.json")))
        uj["units"].append({"name": "c", "sources": [{"path": src}]})
        json.dump(uj, open(os.path.join(self.out, "units.json"), "w"))
        with open(os.path.join(self.gen, "table.c"), "a") as f:
            f.write("{0x%08Xu, f_%08X},\n" % (F(19), F(19)))
        self.decl = os.path.join(self.tmp.name, "handoff.tsv")
        with open(self.decl, "w") as f:
            f.write("# table\n" + "".join("%08X\texact\t1r0f\n" % F(i) for i in (6, 14, 19)))
        self.cache = os.path.join(self.tmp.name, "cache")

    def tearDown(self):
        native_rules.a3_reset()
        self.tmp.cleanup()

    def run_rules(self, cache, out=None):
        """native_rules.py's main() (without the input-stamp skip) -> (stdout, {output file: text}); `cache`: the
        cache root, or None for an uncached run"""
        out = out or self.out
        code = ("import sys; sys.path.insert(0, %r); import native_rules; sys.argv = ['native_rules.py'] + %r; "
                "native_rules.main()" % (HERE, [self.src, out, self.gen, "--decl", self.decl] + FAST +
                                         (["--cache", cache] if cache else ["--no-cache"])))
        for p in glob.glob(os.path.join(out, "native", "*.h")) + [os.path.join(out, f) for f in ("native.txt", "nentry.txt")]:
            if os.path.exists(p):
                os.remove(p)  # (this test's own outputs of its previous run: each run must write all of them)
        r = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True,
                           env=dict(os.environ, WWHD_DECOMP_BUILD_CACHE="off"))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        files = {}
        for p in sorted(glob.glob(os.path.join(out, "native", "*.h")) + [os.path.join(out, f) for f in ("native.txt", "nentry.txt")]):
            with open(p) as fh:
                files[os.path.relpath(p, out)] = fh.read()
        stats = [x for x in r.stderr.splitlines() if x.startswith("decomp: native-call rules cache")]
        return r.stdout, files, (stats[0] if stats else "")

    def same_as_uncached(self, out=None):
        """runs cached and uncached; asserts identical outputs; -> the cached run's cache summary"""
        want = self.run_rules(None, out)
        got = self.run_rules(self.cache, out)
        self.assertEqual(got[:2], want[:2])
        return got[2]

    def hits(self, summary, ns):
        """(hits, lookups) of namespace ns in a cache summary line"""
        for part in summary.split(": ", 2)[2].split(" (hits")[0].split(", "):
            name, v = part.split()
            if name == ns:
                h, n = v.split("/")
                return int(h), int(n)
        return 0, 0

    def test_identical_after_each_change(self):
        a = os.path.join(self.src, "a.cpp")
        base = open(a).read()
        s = self.same_as_uncached()                                  # cold
        self.assertEqual(self.hits(s, "nsrc")[0], 0)
        s = self.same_as_uncached()                                  # no change: every entry hits
        for ns in ("nsrc", "nleaf", "nscan", "nhdr", "ncode", "nspec"):
            h, n = self.hits(s, ns)
            self.assertEqual(h, n, (ns, s))
            self.assertGreater(n, 0, (ns, s))
        before = self.run_rules(None)[1]["native.txt"]
        with open(a, "w") as f:                                      # an edit that changes the rules
            f.write(base.replace("void f8() { WWHD_GLUE_FUNC(0x%08X, void); gabi::LocalOf<X> x; }" % self.F(8),
                                 "void f8() { WWHD_GLUE_FUNC(0x%08X, void); gabi::cpu->r[3] = 1; }" % self.F(8)))
        s = self.same_as_uncached()
        self.assertNotEqual(self.run_rules(None)[1]["native.txt"], before)
        self.assertEqual(self.hits(s, "nsrc"), (2, 3))               # b.cpp, c.cpp from the cache, a.cpp recomputed
        with open(a, "w") as f:                                      # a comment-only edit
            f.write(base + "// a comment\n")
        self.same_as_uncached()
        with open(os.path.join(self.gen, "table.c"), "a") as f:      # the translation's tables (a hook-list change)
            f.write("const uint32_t g_recomp_decomp_kept2[] = {0x%08Xu};\n" % self.F(6))
        s = self.same_as_uncached()
        tab = open(os.path.join(self.gen, "table.c")).read()
        with open(os.path.join(self.gen, "table.c"), "w") as f:
            f.write(tab.replace("const uint32_t g_recomp_decomp_kept[] = {0x%08Xu};" % self.F(7),
                                "const uint32_t g_recomp_decomp_kept[] = {0x%08Xu, 0x%08Xu};" % (self.F(7), self.F(5))))
        self.same_as_uncached()
        with open(os.path.join(self.src, "wwhd_src", "include", "h.h"), "a") as f:  # a header: its hcalls
            f.write("inline int hc2() { return gabi::hcall<0x%08X, int>(2); }\n" % self.F(19))
        self.same_as_uncached()
        with open(a, "w") as f:                                      # back to the base source
            f.write(base)
        s = self.same_as_uncached()
        self.assertEqual(self.hits(s, "nsrc"), (3, 3))

    def test_damaged_entries_are_misses(self):
        self.same_as_uncached()
        for p in glob.glob(os.path.join(self.cache, "n*", "*", "*")):
            with open(p, "wb") as f:
                f.write(b"damaged")
        s = self.same_as_uncached()
        self.assertEqual(self.hits(s, "nsrc")[0], 0, s)
        s = self.same_as_uncached()
        self.assertEqual(self.hits(s, "nsrc")[0], self.hits(s, "nsrc")[1], s)

    def test_shared_by_build_directories(self):
        # another decompilation checkout and build directory at other paths with the same content: every source entry
        # hits (keys hold no path), and the outputs are those of an uncached run there
        self.same_as_uncached()
        t2 = os.path.join(self.tmp.name, "other")
        os.makedirs(t2)
        src2, out2 = os.path.join(t2, "decomp"), os.path.join(t2, "out")
        shutil.copytree(self.src, src2)
        shutil.copytree(self.out, out2)
        uj = json.load(open(os.path.join(out2, "units.json")))
        for u in uj["units"]:
            for s in u["sources"]:
                s["path"] = s["path"].replace(self.src, src2)
        json.dump(uj, open(os.path.join(out2, "units.json"), "w"))
        self.src = src2
        s = self.same_as_uncached(out2)
        self.assertEqual(self.hits(s, "nsrc")[0], self.hits(s, "nsrc")[1], s)
        self.assertEqual(self.hits(s, "nspec")[0], self.hits(s, "nspec")[1], s)

    def test_hidden_files_and_spec_key(self):
        self.same_as_uncached()
        open(os.path.join(self.src, "tools", "verify", "units", ".DS_Store"), "w").write("x")
        s = self.same_as_uncached()
        self.assertEqual(self.hits(s, "nspec"), (2, 2), s)
        with open(os.path.join(self.src, "tools", "verify", "units", "a.txt"), "a") as f:  # a spec change
            f.write("framexact %08X %08X\n" % (self.F(6), self.F(6) + 8))
        s = self.same_as_uncached()
        self.assertEqual(self.hits(s, "nspec"), (0, 2), s)

    def test_name_scan_per_name(self):
        # NameScan's entry answers the names it was asked; other names are computed alone and added
        store = native_cache.Store(self.cache)
        c = native_cache.Cache(store)
        calls = []

        def scan(text, names, multiline):
            calls.append(set(names))
            return native_rules.name_scan_text(text, names, multiline)
        text = "void a(int x);\nvoid b() { a(1); }\nvoid c(int y);\n"
        full = native_rules.name_scan_text(text, {"a", "b", "c"}, True)
        self.assertEqual(c.names(text, {"a"}, True, scan), {k: full[k] for k in ("a",)})
        self.assertEqual(c.names(text, {"a", "c"}, True, scan), {k: full[k] for k in ("a", "c")})
        self.assertEqual(calls, [{"a"}, {"c"}])
        c2 = native_cache.Cache(store)  # another run: the same names hit without a scan
        self.assertEqual(c2.names(text, {"a", "c"}, True, scan), {k: full[k] for k in ("a", "c")})
        self.assertEqual(c2.names(text, {"c"}, True, scan), {"c": full["c"]})
        self.assertEqual(len(calls), 2)
        self.assertNotEqual(native_rules.name_scan_text(text, {"a"}, False), {})

    def test_pruned_with_the_store(self):
        self.same_as_uncached()
        # a declaration-table entry and the native ones under one cap, least recently used first
        decl_cache.Store(self.cache).put("parse", "00" * 32, "x" * 1000)
        store = native_cache.Store(self.cache, cap=1)
        total, files = store.size()
        self.assertTrue(any("/nsrc/" in p.replace(os.sep, "/") for _, _, p, _ in files))
        self.assertTrue(any("/parse/" in p.replace(os.sep, "/") for _, _, p, _ in files))
        store.prune()
        self.assertEqual(store.size()[0], 0)

    def test_off(self):
        self.assertIsNone(native_cache.open_cache("off"))
        self.assertIsNotNone(native_cache.open_cache(self.cache))


if __name__ == "__main__":
    unittest.main()
