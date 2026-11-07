#!/usr/bin/env python3
"""tools/decomp_build/native_rules.py on a synthetic decomp build output (no game files)."""
import json
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
            got[int(a, 16)] = (callee, caller, why.strip())
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

    def test_unit_header(self):
        lines = native_rules.rules(self.src, self.out, self.gen)
        native_rules.unit_headers(self.out, lines)
        h = open(self.out + "/native/0.h").read()
        self.assertIn("#define GABI_NATIVE_TABLE 1", h)
        self.assertIn("{0x%08Xu, 5u, 7u}," % self.F(6), h)   # slot 5 (functions.tsv order), native callee + caller ok
        self.assertIn("{0x%08Xu, 8u, 4u}," % self.F(9), h)   # Cpu access: neither (listed: 4)
        self.assertIn("{0x%08Xu, 4u, 5u}," % self.F(5), h)   # footprint 0: callee native, its calls stay guest
        hb = open(self.out + "/native/1.h").read()
        self.assertIn("{0x%08Xu, 5u, 4u}," % self.F(6), hb)  # unit b's lean call into unit a: slot only
        self.assertNotIn("0x0300000C", hb)                   # not decompiled: no entry


if __name__ == "__main__":
    unittest.main()
