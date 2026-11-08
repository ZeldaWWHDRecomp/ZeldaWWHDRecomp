#!/usr/bin/env python3
"""tools/decomp_build/site_lr.py: call expression extents, the k-th call matching and the IR scan (no game files)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import site_lr  # noqa: E402


def wrap(text, name, nth=0):
    """call_extent at the nth occurrence of `name` in text -> the wrapped expression text or None"""
    t = site_lr.blank_text(text)
    p = -1
    for _ in range(nth + 1):
        p = text.index(name, p + 1)
    e = site_lr.call_extent(t, p, {"MACRO"})
    return None if e is None else text[e[0]:e[1]]


class Extent(unittest.TestCase):
    def test_plain_calls(self):
        self.assertEqual(wrap("    foo(a, b(c));", "foo"), "foo(a, b(c))")
        self.assertEqual(wrap("    foo(a, b(c));", "b("), "b(c)")
        self.assertEqual(wrap("x = ns::Cls::foo(1);", "foo"), "ns::Cls::foo(1)")
        self.assertEqual(wrap("x = ::foo(1);", "foo"), "::foo(1)")
        self.assertEqual(wrap("x = ns::Cls::foo(1);", "ns"), "ns::Cls::foo(1)")  # the location names the first part
        self.assertEqual(wrap("x = Cls<int>::foo(1);", "Cls"), "Cls<int>::foo(1)")
        self.assertIsNone(wrap("x = ns::MACRO(1);", "ns"))
        self.assertEqual(wrap("x = get<int, 2>(1);", "get"), "get<int, 2>(1)")
        self.assertEqual(wrap("return foo();", "foo"), "foo()")
        self.assertEqual(wrap("if (!foo(\")\")) {}", "foo"), "foo(\")\")")

    def test_member_calls(self):
        self.assertEqual(wrap("a.b->c[2].m(x);", "m("), "a.b->c[2].m(x)")
        self.assertEqual(wrap("field_0x2060.get()->m(8);", "get"), "field_0x2060.get()->m(8)"[:18])
        self.assertEqual(wrap("field_0x2060.get()->m(8);", "m("), "field_0x2060.get()->m(8)")
        self.assertEqual(wrap("y = f(x)->m();", "m("), "f(x)->m()")
        self.assertEqual(wrap("y = ((T*)p)->m();", "m("), "((T*)p)->m()")
        self.assertEqual(wrap("y = (T*)p->m();", "m("), "p->m()")
        self.assertEqual(wrap("y = *p->m();", "m("), "p->m()")
        self.assertEqual(wrap("y = static_cast<A*>(n)->m();", "m("), "static_cast<A*>(n)->m()")
        self.assertEqual(wrap("y = this->m();", "m("), "this->m()")
        self.assertEqual(wrap("y = x.get<int>()->m();", "m("), "x.get<int>()->m()")
        self.assertEqual(wrap("return (x)->m();", "m("), "(x)->m()")

    def test_not_wrapped(self):
        self.assertIsNone(wrap("cXyz pos(a, b);", "pos"))      # a declaration
        self.assertIsNone(wrap("Foo* p(x);", "p("))            # a declaration
        self.assertIsNone(wrap("T<U> v(3);", "v("))            # a declaration
        self.assertIsNone(wrap("p = new Foo(1);", "Foo"))      # a new-expression
        self.assertIsNone(wrap("new (self) Foo();", "Foo"))    # a placement new-expression
        self.assertEqual(wrap("x = (T*)foo();", "foo"), "foo()")
        self.assertIsNone(wrap("MACRO(x);", "MACRO"))          # a function-like macro
        self.assertIsNone(wrap("x = y + z;", "y"))             # not a call
        self.assertIsNone(wrap("p->~T();", "T("))              # a destructor
        self.assertIsNone(wrap("if (a) b();", "if"))           # a keyword
        self.assertIsNone(wrap("x = foo(a, {1, 2});", "foo"))  # a braced argument: no plain extent


class Matching(unittest.TestCase):
    F = 0x02000000

    def test_kth_call(self):
        # F calls T at two places (source order 10:5 then 12:5) and U once; the original has bl T twice, bl U once
        T, U = 0x02100000, 0x02200000
        fns = {"F": [["g", T, [[12, 5, self.F]]], ["g", U, [[11, 5, self.F]]], ["g", T, [[10, 5, self.F]]],
                     ["g", self.F, [[9, 1, self.F], [691, 5, 0]]]]}  # the body's own guest entry: no call
        ok, bad = site_lr.match_sites(fns, {"F": self.F}, self.F, [(T, 0x02000010), (U, 0x02000020), (T, 0x02000030)])
        self.assertEqual(ok[T], [(((10, 5),), 0x02000010), (((12, 5),), 0x02000030)])
        self.assertEqual(ok[U], [(((11, 5),), 0x02000020)])
        self.assertEqual(bad, {})

    def test_count_mismatch_stays(self):
        T = 0x02100000
        fns = {"F": [["g", T, [[10, 5, self.F]]]]}
        ok, bad = site_lr.match_sites(fns, {"F": self.F}, self.F, [(T, 0x02000010), (T, 0x02000030)])
        self.assertEqual(ok, {})
        self.assertEqual(bad, {T: (2, 1)})

    def test_wrappers_and_callees(self):
        # a header wrapper W (not inlined) calls T twice; an inlined decompiled callee G is one call of G, not of what
        # G calls; a call of a decompiled function by name is a call of its address
        T, G, H = 0x02100000, 0x02300000, 0x02400000
        fns = {"F": [["d", "W", [[10, 5, self.F]]], ["g", T, [[11, 3, self.F], [50, 2, G], [691, 5, 0]]],
                     ["d", "H", [[12, 7, self.F]]]],
               "W": [["g", T, [[100, 1, 0]]], ["g", T, [[101, 1, 0]]]]}
        glue = {"F": self.F, "Hm": H}
        glue["H"] = H
        ok, bad = site_lr.match_sites(fns, glue, self.F, [(T, 0x10), (T, 0x20), (G, 0x30), (H, 0x40)])
        self.assertEqual(ok[T], [(((10, 5), (100, 1)), 0x10), (((10, 5), (101, 1)), 0x20)])
        self.assertEqual(ok[G], [(((11, 3),), 0x30)])
        self.assertEqual(ok[H], [(((12, 7),), 0x40)])


IR = r'''
define hidden i32 @_Z1Fv() #0 !dbg !10 {
  call void @_ZN4gabi9do_call_gEP3CpuRNS_7ArgPackEPNS_8FrameCtxEjijPy(ptr %1, ptr %2, ptr %3, i32 noundef 34603008, i32 noundef 0, i32 noundef 0, ptr null), !dbg !20
  invoke void @_Z1Wv(ptr %1)
          to label %4 unwind label %5, !dbg !21
  call void @_ZN4gabi9do_call_gEP3CpuRNS_7ArgPackEPNS_8FrameCtxEjijPy(ptr %1, ptr %2, ptr %3, i32 %9, i32 noundef 1, i32 noundef 0, ptr null), !dbg !21
}
!1 = !DIFile(filename: "a.cpp", directory: "/")
!10 = distinct !DISubprogram(name: "F", scope: !1, file: !1, line: 2, type: !3, spFlags: DISPFlagDefinition, unit: !0)
!11 = distinct !DISubprogram(name: "w", scope: !1, file: !1, line: 9, type: !3, spFlags: DISPFlagDefinition, unit: !0)
!12 = distinct !DILexicalBlock(scope: !10, file: !1, line: 3, column: 3)
!19 = distinct !DILocation(line: 4, column: 7, scope: !12)
!20 = !DILocation(line: 9, column: 20, scope: !11, inlinedAt: !19)
!21 = !DILocation(line: 5, column: 3, scope: !10)
'''
SRC = ['#line 1 "a.cpp"', "void x();", "int F() { WWHD_GLUE_FUNC(0x02000000, int);", "  {", "      w();", "  W();", "}",
       "", "", "inline void w() { gabi::call(0x02100000); }"]


class Scan(unittest.TestCase):
    def test_scan_ir(self):
        f = site_lr.scan_ir(IR, SRC)["_Z1Fv"]
        self.assertEqual(f["a"], 0x02000000)
        # the inlined wrapper's guest call: outermost the call in F (physical line 5), then w's line; bctrl dropped
        self.assertEqual(f["e"], [["g", 0x02100000, [[5, 7, 0x02000000], [9, 20, 0]]],
                                  ["d", "_Z1Wv", [[6, 3, 0x02000000]]]])


if __name__ == "__main__":
    unittest.main()
