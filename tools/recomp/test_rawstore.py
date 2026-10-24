#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Unit tests for rawstore.py (lfs -> stfs copies store the loaded word): python3 test_rawstore.py

Which stores take the raw path (and which must not), checked on ppc2c statements: the real
translation of lfs / stfs / psq / arithmetic words through ppc2c.translate, in one function body.
That the raw word and the single-result conversion equal ppc.h's ppc_stfs_bits for every value they
can hold is checked in C (runtime/tools/ppc_f32_exact_test.c: the lfs -> stfs round trip and the
single-result case over all 2^32 words).
"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ppc2c  # noqa: E402
import rawstore  # noqa: E402


class Ctx:
    """the parts of recomp.py's context ppc2c.translate uses for these instructions"""
    imm_override = {}

    def call(self, addr, target, link):
        return "f_%08X(c);" % target


def lfs(d, a, off):   # lfs fD, off(rA)
    return (48 << 26) | (d << 21) | (a << 16) | (off & 0xFFFF)


def stfs(s, a, off):  # stfs fS, off(rA)
    return (52 << 26) | (s << 21) | (a << 16) | (off & 0xFFFF)


def fadds(d, a, b):   # fadds fD, fA, fB
    return (59 << 26) | (d << 21) | (a << 16) | (b << 11) | (21 << 1)


def fadd(d, a, b):    # fadd fD, fA, fB (double: not a single result)
    return (63 << 26) | (d << 21) | (a << 16) | (b << 11) | (21 << 1)


def fmr(d, b):        # fmr fD, fB
    return (63 << 26) | (d << 21) | (b << 11) | (72 << 1)


def frsp(d, b):
    return (63 << 26) | (d << 21) | (b << 11) | (12 << 1)


def psq_l(d, a, off, i=0):
    return (56 << 26) | (d << 21) | (a << 16) | (i << 12) | (off & 0xFFF)


def psq_st(s, a, off, i=0):
    return (60 << 26) | (s << 21) | (a << 16) | (i << 12) | (off & 0xFFF)


def bl(rel):
    return (18 << 26) | (rel & 0x3FFFFFC) | 1


def run(words, labels=(), sites=()):
    ctx = Ctx()
    body = []
    for i, w in enumerate(words):
        a = 0x02000000 + 4 * i
        s = "f_%08X(c);" % (a + 0x100) if w >> 26 == 18 else ppc2c.translate(a, w, ctx)  # bl: a call statement
        body.append((a, w, s))
    out, raws = rawstore.transform(body, {0x02000000 + 4 * i for i in labels}, {0x02000000 + 4 * i for i in sites})
    return [s for _, _, s in out], raws


def kinds(stmts):
    """per statement: R (raw store), S (single-result store), P (plain stf32), L (raw lfs), - other"""
    k = []
    for s in stmts:
        if "st32(ea, fw" in s:
            k.append("R")
        elif "st32(ea, f32_as_u32((float)" in s:
            k.append("S")
        elif "stf32(" in s:
            k.append("P")
        elif "ld32(ea); double v = ppc_lfs_value(w)" in s:
            k.append("L")
        else:
            k.append("-")
    return "".join(k)


class RawStore(unittest.TestCase):
    def test_copy(self):
        st, raws = run([lfs(1, 3, 8), stfs(1, 4, 0)])
        self.assertEqual(kinds(st), "LR")
        self.assertEqual(raws, {1})
        self.assertIn("st32(ea, fw1);", st[1])
        self.assertIn("fw1 = w;", st[0])

    def test_copy_twice_and_other_register_between(self):
        st, _ = run([lfs(1, 3, 8), lfs(2, 3, 12), stfs(1, 4, 0), stfs(1, 4, 4), stfs(2, 4, 8)])
        self.assertEqual(kinds(st), "LLRRR")

    def test_modified_in_between(self):
        st, _ = run([lfs(1, 3, 8), fadd(1, 1, 2), stfs(1, 4, 0)])
        self.assertEqual(kinds(st), "--P")
        st, _ = run([lfs(1, 3, 8), fmr(1, 2), stfs(1, 4, 0)])
        self.assertEqual(kinds(st), "--P")
        st, _ = run([lfs(1, 3, 8), lfs(1, 3, 12), stfs(1, 4, 0)])  # the second load is stored
        self.assertEqual(kinds(st), "-LR")
        self.assertIn("0x0000000Cu", st[1])

    def test_read_does_not_end_the_copy(self):
        st, _ = run([lfs(1, 3, 8), fadds(2, 1, 1), stfs(1, 4, 0)])
        self.assertEqual(kinds(st), "L-R")

    def test_block_boundary(self):
        st, _ = run([lfs(1, 3, 8), stfs(1, 4, 0)], labels=[1])
        self.assertEqual(kinds(st), "-P")

    def test_instruction_hook(self):
        st, _ = run([lfs(1, 3, 8), stfs(1, 4, 0)], sites=[1])
        self.assertEqual(kinds(st), "-P")

    def test_call(self):
        st, _ = run([lfs(1, 3, 8), bl(0x100), stfs(1, 4, 0)])
        self.assertEqual(kinds(st), "--P")

    def test_paired_single(self):
        st, _ = run([psq_l(1, 3, 8), stfs(1, 4, 0)])      # psq_l: never a raw source
        self.assertEqual(kinds(st), "-P")
        st, _ = run([lfs(1, 3, 8), psq_l(1, 3, 16), stfs(1, 4, 0)])
        self.assertEqual(kinds(st), "--P")
        st, _ = run([lfs(1, 3, 8), psq_st(1, 4, 0)])      # psq_st: never a raw store
        self.assertEqual(kinds(st), "--")
        self.assertIn("psq_store(c, 1,", st[1])

    def test_single_result(self):
        st, _ = run([fadds(1, 2, 3), stfs(1, 4, 0)])
        self.assertEqual(kinds(st), "-S")
        st, _ = run([frsp(1, 2), stfs(1, 4, 0)])
        self.assertEqual(kinds(st), "-S")
        st, _ = run([fadd(1, 2, 3), stfs(1, 4, 0)])       # a double result keeps the exact store
        self.assertEqual(kinds(st), "-P")
        st, _ = run([fadds(1, 2, 3), fmr(1, 2), stfs(1, 4, 0)])
        self.assertEqual(kinds(st), "--P")
        st, _ = run([fadds(1, 2, 3), stfs(1, 4, 0)], labels=[1])
        self.assertEqual(kinds(st), "-P")

    def test_no_store_no_change(self):
        words = [lfs(1, 3, 8), fadds(2, 1, 1)]
        ctx = Ctx()
        body = [(0x02000000 + 4 * i, w, ppc2c.translate(0x02000000 + 4 * i, w, ctx)) for i, w in enumerate(words)]
        out, raws = rawstore.transform(body, set(), set())
        self.assertEqual(out, body)
        self.assertEqual(raws, set())


if __name__ == "__main__":
    unittest.main()
