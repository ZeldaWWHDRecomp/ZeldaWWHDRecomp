#!/usr/bin/env python3
"""tools/decomp_build/native_rules.py on a synthetic decomp build output (no game files)."""
import json
import re
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import native_rules  # noqa: E402

BASE = 0x02000000


def bl(at, to, link=True):
    return 0x48000000 | ((to - at) & 0x03FFFFFC) | (1 if link else 0)


class NativeRulesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = self.tmp.name
        self.src, self.out, self.gen = (os.path.join(t, x) for x in ("decomp", "out", "gen"))
        for d in (self.src + "/tools/verify/units", self.out + "/verify", self.gen):
            os.makedirs(d)
        # functions every 0x40 bytes: F(i) = BASE + 0x40 * i
        F = self.F = lambda i: BASE + 0x40 * i
        code = [0x60000000] * (0x40 * 19 // 4)

        def put(i, k, w):
            code[(F(i) - BASE) // 4 + k] = w
        put(1, 0, 0x9421FFF0)              # F1: stwu r1,-16(r1); bl F2 (F2 parks: F1 may park too)
        put(1, 1, bl(F(1) + 4, F(2)))
        put(2, 0, bl(F(2), F(2) + 0x20))    # F2: bl at its own +0x20, an import call site (OSReceiveMessage)
        put(3, 0, bl(F(3), F(2), False))   # F3: tail b F2 (may park through a tail branch)
        put(4, 0, 0x9421FFE0)              # F4: stwu r1,-32(r1); lwz r3, 40(r1) (reads its caller's frame)
        put(4, 1, 0x80610028)
        put(5, 0, bl(F(5), F(6)))           # F5: plain call of F6
        put(16, 0, 0x38630010)             # F16: addi r3,r3,16; b <import stub> (sead CriticalSection::lock)
        put(16, 1, bl(F(16) + 4, F(16) + 0x30, False))
        put(18, 0, 0x4E800421)             # F18: bctrl (an indirect call: its targets are unknown)
        img = struct.pack("<II", BASE, len(code) * 4) + struct.pack(">%dI" % len(code), *code)
        open(self.out + "/verify/image.bin", "wb").write(img)
        imp = 0xC0001000
        tab = "".join("{0x%08Xu, f_%08X}," % (F(i), F(i)) for i in range(19))
        tab += '\n{0x%08Xu, 0x%08Xu, "coreinit", "OSReceiveMessage"},\n' % (imp, imp)
        tab += "const uint32_t g_recomp_decomp_import_calls[][2] = {{0x%08Xu, 0x%08Xu}, {0x%08Xu, 0x%08Xu}};\n" % (
            F(2), imp, F(16) + 4, imp)
        tab += "const uint32_t g_recomp_decomp_kept[] = {0x%08Xu};\n" % F(7)
        open(self.gen + "/table.c", "w").write(tab)
        bodies = {
            1: "void f1() { WWHD_GLUE_FUNC(0x%08X, void); }",
            2: "void f2() { WWHD_GLUE_FUNC(0x%08X, void); }",
            3: "void f3() { WWHD_GLUE_FUNC(0x%08X, void); }",
            4: "void f4() { WWHD_GLUE_FUNC(0x%08X, void); }",
            5: "void f5() { WWHD_GLUE_FUNC(0x%08X, void); f6(); }",
            6: "int f6(int a) { WWHD_GLUE_FUNC_R(0x%08X, int, a); return wwhd_native_scope_.ret<wwhd_ret_t_>((a + 1)); }",
            7: "void f7() { WWHD_GLUE_FUNC(0x%08X, void); }",
            8: "void f8() { WWHD_GLUE_FUNC(0x%08X, void); gabi::LocalOf<X> x; }",
            9: "void f9() { WWHD_GLUE_FUNC(0x%08X, void); gabi::cpu->r[3] = 1; }",
            10: "void f10() { WWHD_GLUE_FUNC(0x%08X, void); gabi::tail_ptr<void>(1); }",
            11: "void f11() { WWHD_GLUE_FUNC(0x%08X, void); }",
            12: "void f12() { WWHD_GLUE_FUNC(0x%08X, void); }",
            13: "void f13(int a, int b, int c, int d, int e, int f, int g, int h, int i) "
                "{ WWHD_GLUE_FUNC(0x%08X, void, a, b, c, d, e, f, g, h, i); }",
            14: "void f14(f32 a, f32 b, f32 c, f32 d, f32 e, f32 f, f32 g, f32 h) "
                "{ WWHD_GLUE_FUNC(0x%08X, void, a, b, c, d, e, f, g, h); }",
            16: "void f16() { WWHD_GLUE_FUNC(0x%08X, void); }",
            17: "S f17() { WWHD_GLUE_FUNC(0x%08X, S); return S{}; }",
            18: "void f18() { WWHD_GLUE_FUNC(0x%08X, void); }",
        }  # F15: listed but no body
        srcf = self.src + "/a.cpp"
        open(srcf, "w").write("\n".join(b % F(i) for i, b in bodies.items()) + "\n")
        open(self.src + "/tools/verify/units/a.txt", "w").write(
            "src a.cpp\nframexact %08X %08X\nfootprint %08X 0x20\nfootprint %08X 0\n" % (F(11), F(11) + 8, F(12), F(5)))
        open(self.out + "/functions.tsv", "w").write("".join("%08X\ta\tf%d\n" % (F(i), i) for i in range(1, 19)))
        srcb = self.src + "/b.cpp"  # unit b only calls unit a's F6 (a lean call) and something not decompiled
        open(srcb, "w").write("int g(int a) { return gabi::lcall<0x%08X, int>(a); } /* other unit (ext) */\n"
                              "void h() { return gabi::lcall<0x0300000C, void>(); } /* other unit (ext) */\n" % F(6))
        json.dump({"units": [{"name": "a", "sources": [{"path": srcf}]}, {"name": "b", "sources": [{"path": srcb}]}]},
                  open(self.out + "/units.json", "w"))

    def tearDown(self):
        self.tmp.cleanup()

    def got(self, strict):
        got = {}
        for line in native_rules.rules(self.src, self.out, self.gen, strict):
            a, callee, caller, why = line.split("#")[0].split(None, 3) if len(line.split("#")[0].split()) >= 4 else (None,) * 4
            got[int(a, 16)] = (callee, caller, " ".join(x for x in why.split() if not x.startswith("decl=")))
        return got

    def test_park_rule(self):
        F = self.F
        for strict in (False, True):
            got = self.got(strict)
            for i in (1, 2, 3, 16, 18):  # C4 through a bl, an import call, a tail b, a tail b into an import stub, bctrl
                c, k, w = got[F(i)]
                if strict:
                    self.assertEqual(c, "callee=guest", (i, w))
                    self.assertIn("may park", w.split(","), (i, w))
                else:
                    self.assertEqual(c, "callee=native", (i, w))
                    self.assertIn("(may park)", w.split(","), (i, w))
            self.assertNotIn("park", got[F(6)][2])

    def test_rules(self):
        got = self.got(False)
        F = self.F

        def ok(i, callee, caller, *why):
            c, k, w = got[F(i)]
            self.assertEqual((c, k), ("callee=" + callee, "caller=" + caller), (i, w))
            for x in why:
                self.assertIn(x, w.split(","), (i, w))
        ok(4, "guest", "ok", "orig:caller-frame-read")  # C9
        ok(5, "native", "guest", "A1 footprint")    # footprint 0: callee may go native, its calls stay guest
        ok(6, "native", "ok")
        ok(7, "guest", "ok", "kept")                # C1
        ok(8, "guest", "ok", "src:local")           # C6
        ok(9, "guest", "guest", "src:cpu", "A2 cpu")  # C7 / A2
        ok(10, "guest", "ok", "src:tail_ptr")       # C8
        ok(11, "guest", "guest", "framexact", "A1 framexact")  # C3 / A1
        ok(12, "guest", "guest", "footprint")       # C5
        ok(13, "guest", "ok", "stack args")         # C9: 9 integer arguments
        ok(14, "native", "ok")                      # 8 float arguments fit f1..f8
        ok(15, "guest", "ok", "no body")            # C11
        ok(17, "guest", "ok", "src:ret")            # C12: a result native_returns.py did not rewrite

    def test_lift(self):
        """level 1 step 2 (--lift): the category's reason no longer keeps the guest call; the line says (place) and the
        unit table sets bit 18; other reasons still decide"""
        F = self.F
        lines = native_rules.rules(self.src, self.out, self.gen, False, None, ("local", "tailptr"))
        got = {int(l.split()[0], 16): l.split("#")[0] for l in lines}
        for i in (8, 10):
            self.assertIn("callee=native", got[F(i)], i)
            self.assertIn("(place)", got[F(i)], i)
        self.assertIn("callee=guest", got[F(12)])  # footprint stays
        self.assertNotIn("(place)", got[F(6)])
        native_rules.unit_headers(self.out, lines)
        h = open(self.out + "/native/0.h").read()
        row = next(l for l in h.splitlines() if "0x%08X" % F(8) in l or "0x%08x" % F(8) in l)
        flags = int(row.replace("}", "").split(",")[-2 if row.rstrip().endswith(",") else -1].strip().rstrip("u"), 0)
        self.assertTrue(flags & (1 << 18), row)
        self.assertEqual(native_rules.rules(self.src, self.out, self.gen), native_rules.rules(self.src, self.out, self.gen, False, None, ()))

    def test_unit_header(self):
        lines = native_rules.rules(self.src, self.out, self.gen)
        native_rules.unit_headers(self.out, lines)
        h = open(self.out + "/native/0.h").read()
        self.assertIn("#define GABI_NATIVE_TABLE 1", h)
        self.assertIn("{0x%08Xu, 5u, 7u}," % self.F(6), h)   # slot 5 (functions.tsv order), native callee + caller ok
        self.assertIn("{0x%08Xu, 8u, 4u}," % self.F(9), h)   # Cpu access: neither (listed: 4)
        self.assertIn("{0x%08Xu, 4u, 5u}," % self.F(5), h)   # footprint 0: callee native, its calls stay guest
        hb = open(self.out + "/native/1.h").read()
        self.assertIn("{0x%08Xu, 5u, 5u}," % self.F(6), hb)  # unit b's lean call into unit a: its slot, native callee
        self.assertNotIn("0x0300000C", hb)                   # not decompiled: no entry


    def test_header_calls(self):
        """--hcall: a unit's calls by constant address in its sources and the headers they include (gen.py's hinc copy
        first, the including file's directory, the source's quote directory) get the unit's table rows"""
        F = self.F
        inc = self.out + "/hinc/d"
        os.makedirs(inc)
        open(inc + "/h.h", "w").write('#include "g.h"\ninline int h6(int a) { return gabi::hcall<0x%08X, int>(a); }\n' % F(6))
        open(inc + "/g.h", "w").write("inline void g9() { gabi::hcall<0x%08X, void>(); }\n" % F(9))
        os.makedirs(self.src + "/wwhd_src/include/d")  # the original header: shadowed by the copy
        open(self.src + "/wwhd_src/include/d/h.h", "w").write("inline void h1() { gabi::call<void>(0x%08X); }\n" % F(1))
        srcc = self.src + "/c.cpp"
        open(srcc, "w").write('#include "d/h.h"\nvoid k() { gabi::hcall<0x%08X, void>(); }\n' % F(5))
        uj = json.load(open(self.out + "/units.json"))
        uj["units"].append({"name": "c", "sources": [{"path": srcc}]})
        json.dump(uj, open(self.out + "/units.json", "w"))
        hc = native_rules.HeaderCalls(self.src, self.out)
        self.assertEqual(hc.of_source(srcc, None), {F(5), F(6), F(9)})
        lines = native_rules.rules(self.src, self.out, self.gen)
        native_rules.unit_headers(self.out, lines, True, hc)
        h = open(self.out + "/native/2.h").read()
        for i in (5, 6, 9):
            self.assertIn("{0x%08Xu, %du," % (F(i), i - 1), h)
        self.assertNotIn("{0x%08Xu," % F(1), h)
        native_rules.unit_headers(self.out, lines, True)  # without --hcall: no rows for them
        self.assertNotIn("{0x%08Xu," % F(9), open(self.out + "/native/2.h").read())

    def test_tail_ptr_ic_reason(self):
        """gen.py --ptr-ic writes tail_ptr as tail_ptr_ic: the source reason stays"""
        rx = dict(native_rules.SRCF)["src:tail_ptr"]
        self.assertTrue(rx.search("gabi::tail_ptr_ic<decltype([]{}), void>(1);"))
        self.assertTrue(rx.search("gabi::tail_ptr<void>(1);"))

    def test_decl_table(self):
        F = self.F
        decl = os.path.join(self.tmp.name, "handoff.tsv")
        open(decl, "w").write("# decl_lint.py hand-off table\n"
                              "%08X\texact\t1r0f\ta.cpp\tf6\t-\n" % F(6) +
                              "%08X\tshim\t0r8f\ta.cpp\tf14\te 1\n" % F(14) +
                              "%08X\tguest\t0r0f\ta.cpp\tf5\td? callee (1 sites)\n" % F(5) +
                              "%08X\texact\t9r0f\ta.cpp\tf13\t-\n" % F(13) +
                              "%08X\tshim\t0r0f\ta.cpp\tf1\te? 2\n" % F(1))
        lines = native_rules.rules(self.src, self.out, self.gen, False, decl)
        tok = {int(x.split()[0], 16): [w for w in x.split("#")[0].split() if w.startswith("decl=")][0] for x in lines}
        self.assertEqual(tok[F(6)], "decl=exact:1r0f:1")     # int result in r3
        self.assertEqual(tok[F(14)], "decl=shim:0r8f:0")     # extra arguments dropped, no result
        self.assertEqual(tok[F(5)], "decl=-")                # guest in the table: keeps the registers
        self.assertEqual(tok[F(13)], "decl=-")               # more than 8 integer registers
        self.assertEqual(tok[F(1)], "decl=shim:0r0f:0")      # extra arguments nothing reads (the table's own ruling)
        self.assertEqual(tok[F(2)], "decl=-")                # not in the table
        native_rules.unit_headers(self.out, lines)
        h = open(self.out + "/native/0.h").read()
        self.assertIn("{0x%08Xu, 5u, %du}," % (F(6), 7 | 8 | 1 << 8 | 1 << 16), h)
        self.assertIn("{0x%08Xu, 13u, %du}," % (F(14), 7 | 8 | 8 << 12), h)
        self.assertIn("{0x%08Xu, 4u, 5u}," % F(5), h)
        hb = open(self.out + "/native/1.h").read()
        self.assertIn("{0x%08Xu, 5u, %du}," % (F(6), 5 | 8 | 1 << 8 | 1 << 16), hb)  # the lean call's native entry


    def test_under_declared(self):
        """an ext line that passes fewer registers than a register-exact definition takes (a gap in the decl table)"""
        p = os.path.join(self.tmp.name, "u.cpp")
        open(p, "w").write("/* WWHD glue\next 02019424 void cMl_initFromHeapReturnRegister()\n"
                           "ext 02000100 void C::f(f32 x)\next 02000200 void g(int, ...)\n*/\n")
        decl = {0x02019424: ("exact", 1, 0), 0x02000100: ("exact", 1, 1), 0x02000200: ("exact", 2, 0)}
        q = os.path.join(self.tmp.name, "v.cpp")  # a stub only used by its address makes no call
        open(q, "w").write("void cb(int, int);\nstatic gabi::HostFn h(reinterpret_cast<const void*>(&cb), 0x02000300);\n"
                           "/* WWHD glue\next 02000300 void cb(int, int)\n*/\n")
        decl[0x02000300] = ("exact", 7, 0)
        self.assertEqual(native_rules.under_declared([q], decl), {})
        r = os.path.join(self.tmp.name, "r.cpp")  # a result the callee's definition never sets
        open(r, "w").write("void f() { u32 x = getRes(); }\n/* WWHD glue\next 02000400 u32 getRes()\n*/\n")
        decl[0x02000400] = ("shim", 0, 0)
        self.assertEqual(native_rules.under_declared([r], decl, {0x02000400: 0}), {r: [(0x02000400, (-1, -1), (0, 0), "shim")]})
        self.assertEqual(native_rules.under_declared([r], decl, {0x02000400: 1}), {})
        got = native_rules.under_declared([p], decl)
        self.assertEqual(got, {p: [(0x02019424, (0, 0), (1, 0), "exact")]})  # C::f passes this + f1; variadic: not counted

    def test_a3_precise(self):
        """--a3-fn: a source whose under-declared ext lines are all shim callees passed fewer registers keeps caller=ok"""
        p = os.path.join(self.tmp.name, "a3.cpp")
        open(p, "w").write("/* x */\n")
        self.assertEqual(native_rules.a3_precise(p, [(0x02000100, (3, 0), (4, 0), "shim")]), set())
        self.assertEqual(native_rules.a3_precise(p, [(0x02000100, (3, 0), (4, 0), "shim"), (0x02000200, (1, 0), (1, 8), "shim")]), set())
        # a result its definition never sets (the caller reads what an earlier call left): the whole source
        self.assertIsNone(native_rules.a3_precise(p, [(0x02000100, (3, 0), (4, 0), "shim"), (0x02000400, (-1, -1), (0, 0), "shim")]))
        self.assertIsNone(native_rules.a3_precise(p, [(0x02000400, (-1, -1), (0, 0), "exact")]))
        self.assertIsNone(native_rules.a3_precise(p, [(0x02000100, (0, 0), (1, 0), "exact")]))

    def test_a3_fn_flag(self):
        """unit_headers: "(a3fn)" on a caller=ok line sets NativeInfo bit 19 (gabi.h GABI_A3_FN), not on a caller=guest one"""
        lines = native_rules.rules(self.src, self.out, self.gen, False, None)
        a = int(lines[0].split()[0], 16)
        lines[0] = "%08X  callee=native caller=ok  (a3fn) decl=-  # %s" % (a, lines[0].split("#")[1].strip())
        lines[1] = lines[1].replace("caller=ok", "caller=guest", 1).replace("  -", "  (a3fn)", 1) if "caller=ok" in lines[1] else lines[1]
        native_rules.unit_headers(self.out, lines)
        h = "".join(open(os.path.join(self.out, "native", x)).read() for x in sorted(os.listdir(os.path.join(self.out, "native"))))
        self.assertIn("{0x%08Xu, 0u, %du}," % (a, 7 | 1 << 19), h)
        b = int(lines[1].split()[0], 16)
        for row in re.findall(r"\{0x%08Xu, \d+u, (\d+)u\}" % b, h):
            self.assertFalse(int(row) & (1 << 19))

    def test_leaf_rules(self):
        """--leaf: register-exact callee=native functions whose original makes no call and whose body has no call form;
        a candidate that calls (by name) a non-leaf decompiled function of its source is demoted, transitively"""
        p = os.path.join(self.tmp.name, "leaf.cpp")
        open(p, "w").write(
            "/* 02000100 */\nu32 A::get() { WWHD_GLUE_FUNC_R(0x02000100, u32, this); return wwhd_native_scope_.ret<wwhd_ret_t_>(mX); }\n"
            "/* 02000200 */\nvoid A::big() { WWHD_GLUE_FUNC(0x02000200, void, this); mX = 1; }\n"
            "/* 02000300 */\nvoid A::uses() { WWHD_GLUE_FUNC(0x02000300, void, this); big(); }\n"
            "/* 02000400 */\nvoid A::uses2() { WWHD_GLUE_FUNC(0x02000400, void, this); uses(); }\n"
            "/* 02000500 */\nvoid A::lit() { WWHD_GLUE_FUNC(0x02000500, void, this); f(\"x\"); }\n"
            "/* 02000600 */\nvoid A::gc() { WWHD_GLUE_FUNC(0x02000600, void, this); gabi::call<void>(0x02001000); }\n")
        native_rules.LEAF_CALLS.clear()
        srcf = native_rules.source_features([p])
        self.assertIn("src:nonleaf", srcf[0x02000500][0])
        self.assertIn("src:nonleaf", srcf[0x02000600][0])
        self.assertNotIn("src:nonleaf", srcf[0x02000300][0])
        exact = ("exact", 1, 0)
        rows = [(a, [], [], [], exact, 0, srcf[a][0]) for a in (0x02000100, 0x02000200, 0x02000300, 0x02000400, 0x02000500, 0x02000600)]
        calls = {0x02000200: [0x02001000]}  # big's original calls something: no leaf; uses/uses2 call it by name
        native_rules.leaf_rules(rows, calls, {}, {})
        leaves = {a for a, r, c, note, d, rk, fs in rows if "leaf" in note}
        self.assertEqual(leaves, {0x02000100})
        self.assertEqual(native_rules.LEAF_ERRORS, [])


if __name__ == "__main__":
    unittest.main()
