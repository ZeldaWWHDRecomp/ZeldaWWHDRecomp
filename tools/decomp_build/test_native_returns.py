#!/usr/bin/env python3
"""tools/decomp_build/native_returns.py on small glued sources."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import native_returns  # noqa: E402

RET = "wwhd_native_scope_.ret<wwhd_ret_t_>"


class NativeReturnsTest(unittest.TestCase):
    def rw(self, src):
        return native_returns.rewrite(src)

    def test_scalar(self):
        out, n, left = self.rw("u32 f(u32 a) { WWHD_GLUE_FUNC(0x02000000, u32, a);\n"
                               "    if (a) { return a + 1; }\n    return g(a, \"return x;\"), 2; // return y;\n}\n")
        self.assertEqual((n, left), (1, 0))
        self.assertIn("WWHD_GLUE_FUNC_R(0x02000000, u32, a)", out)
        self.assertIn("return %s((a + 1));" % RET, out)
        self.assertIn("return %s((g(a, \"return x;\"), 2)); // return y;" % RET, out)  # strings and comments untouched

    def test_pointer_and_braced(self):
        out, n, _ = self.rw("const Foo * f() { WWHD_GLUE_FUNC(0x02000010, const Foo *); return nullptr; }\n"
                            "Pair32 g() { WWHD_GLUE_FUNC(0x02000020, Pair32); return {1, 2}; }\n")
        self.assertEqual(n, 2)
        self.assertIn("return %s((nullptr));" % RET, out)
        self.assertIn("return %s({1, 2});" % RET, out)

    def test_left_alone(self):
        srcs = [
            "void f() { WWHD_GLUE_FUNC(0x02000000, void); return; }",                        # void: nothing to hand over
            "S f() { WWHD_GLUE_FUNC(0x02000000, S); return S{}; }",                           # not a scalar
            "int f() { WWHD_GLUE_FUNC(0x02000000, int); auto l = [](int x) { return x; }; return l(1); }",  # lambda
            "int f() { WWHD_GLUE_FUNC(0x02000000, int); struct L { int g() { return 1; } }; return L().g(); }",
            "int f(int s) { WWHD_GLUE_FUNC(0x02000000, int, s); CHECK(s, return -1); return 0; }",     # macro argument
            "int f() { WWHD_GLUE_FUNC(0x02000000, int);\n#define R return 1;\n return 0; }",          # #define
            "#define FAIL(x) \\\n  if (x) return -1;\nint f(int s) { WWHD_GLUE_FUNC(0x02000000, int, s); FAIL(s); return 0; }",
        ]
        for i, s in enumerate(srcs):
            out, n, left = self.rw(s)
            self.assertEqual(out, s, i)
            self.assertEqual((n, left), (0, 0 if i == 0 else 1), i)

    def test_scalar_type(self):
        for t in ("BOOL", "u32", "const char*", "J3DModelData *", "cPhs_State", "Pair32"):
            self.assertTrue(native_returns.scalar_type(t), t)
        for t in ("cXyz", "sead::SafeString", "u64"):
            self.assertFalse(native_returns.scalar_type(t), t)


if __name__ == "__main__":
    unittest.main()
