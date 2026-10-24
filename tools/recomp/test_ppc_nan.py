#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Unit tests for recomp.py --ppc-nan-propagation (ppc2c.NAN_PROPAGATION, ppc_nan.h): python3 test_ppc_nan.py

The float instructions are translated by ppc2c with the option on and off, compiled with the host C
compiler against runtime/include/ppc.h and run on operand sets with quiet and signalling NaNs of both
signs, infinities, zero and normal numbers in every operand position. The expected results come from
the PowerPC rule written out here independently of ppc_nan.h:
  - the first NaN among frA, frB, frC (fmadd family: A, B, C; fmul: A, C), quieted, sign and payload kept;
  - else the default QNaN 0x7FF8000000000000 when the operation is invalid (the host result is a NaN);
  - else the result of the existing translation (option off), unchanged;
  - single-precision forms round the result to single (NaN: the low 29 fraction bits dropped, as frsp).
No game files are needed. Skipped when no C compiler is found.
"""
import itertools
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import ppc2c  # noqa: E402

D, A, B, C = 1, 2, 3, 4          # register numbers used by every test instruction
D_PS1_INIT = 0x4045000000000000   # 42.0: what a double instruction must leave in frD.ps1

QNAN = 0x7FF8000000000123         # quiet, positive
QNAN_NEG = 0xFFF80000000ABC00     # quiet, negative
SNAN_LOW = 0x7FF0000000000789     # signalling, payload only in the low 27 bits (round25 makes it infinity)
SNAN_NEG = 0xFFF4000012345000     # signalling, negative
INF, ZERO, ONE_HALF, TWO = 0x7FF0000000000000, 0x0000000000000000, 0x3FF8000000000000, 0x4000000000000000
POOL = [QNAN, QNAN_NEG, SNAN_LOW, SNAN_NEG, INF, ZERO, ONE_HALF, TWO]
NAN_POOL = [QNAN, QNAN_NEG, SNAN_LOW, SNAN_NEG]
DEFAULT_QNAN = 0x7FF8000000000000


def a_form(op, xo, d=D, a=A, b=B, c=C):
    return (op << 26) | (d << 21) | (a << 16) | (b << 11) | (c << 6) | (xo << 1)


# name, opcode, xo, single, per result slot: the NaN-relevant operands in priority order as
# (register letter, ps slot), or ("copy", (letter, slot)) for a slot the instruction copies
def _scalar(order):
    return [[(r, 0) for r in order]]


def _ps(order0, order1):
    return [order0, order1]


INSNS = [
    ("fdiv", 63, 18, False, _scalar("AB")), ("fsub", 63, 20, False, _scalar("AB")),
    ("fadd", 63, 21, False, _scalar("AB")), ("fmul", 63, 25, False, _scalar("AC")),
    ("fmsub", 63, 28, False, _scalar("ABC")), ("fmadd", 63, 29, False, _scalar("ABC")),
    ("fnmsub", 63, 30, False, _scalar("ABC")), ("fnmadd", 63, 31, False, _scalar("ABC")),
    ("fdivs", 59, 18, True, _scalar("AB")), ("fsubs", 59, 20, True, _scalar("AB")),
    ("fadds", 59, 21, True, _scalar("AB")), ("fmuls", 59, 25, True, _scalar("AC")),
    ("fmsubs", 59, 28, True, _scalar("ABC")), ("fmadds", 59, 29, True, _scalar("ABC")),
    ("fnmsubs", 59, 30, True, _scalar("ABC")), ("fnmadds", 59, 31, True, _scalar("ABC")),
    ("ps_sum0", 4, 10, True, _ps([("A", 0), ("B", 1)], ("copy", ("C", 1)))),
    ("ps_sum1", 4, 11, True, _ps(("copy", ("C", 0)), [("A", 0), ("B", 1)])),
    ("ps_muls0", 4, 12, True, _ps([("A", 0), ("C", 0)], [("A", 1), ("C", 0)])),
    ("ps_muls1", 4, 13, True, _ps([("A", 0), ("C", 1)], [("A", 1), ("C", 1)])),
    ("ps_madds0", 4, 14, True, _ps([("A", 0), ("B", 0), ("C", 0)], [("A", 1), ("B", 1), ("C", 0)])),
    ("ps_madds1", 4, 15, True, _ps([("A", 0), ("B", 0), ("C", 1)], [("A", 1), ("B", 1), ("C", 1)])),
    ("ps_div", 4, 18, True, _ps([("A", 0), ("B", 0)], [("A", 1), ("B", 1)])),
    ("ps_sub", 4, 20, True, _ps([("A", 0), ("B", 0)], [("A", 1), ("B", 1)])),
    ("ps_add", 4, 21, True, _ps([("A", 0), ("B", 0)], [("A", 1), ("B", 1)])),
    ("ps_mul", 4, 25, True, _ps([("A", 0), ("C", 0)], [("A", 1), ("C", 1)])),
    ("ps_msub", 4, 28, True, _ps([("A", 0), ("B", 0), ("C", 0)], [("A", 1), ("B", 1), ("C", 1)])),
    ("ps_madd", 4, 29, True, _ps([("A", 0), ("B", 0), ("C", 0)], [("A", 1), ("B", 1), ("C", 1)])),
    ("ps_nmsub", 4, 30, True, _ps([("A", 0), ("B", 0), ("C", 0)], [("A", 1), ("B", 1), ("C", 1)])),
    ("ps_nmadd", 4, 31, True, _ps([("A", 0), ("B", 0), ("C", 0)], [("A", 1), ("B", 1), ("C", 1)])),
]


def is_nan(u):
    return (u & 0x7FF0000000000000) == 0x7FF0000000000000 and (u & 0x000FFFFFFFFFFFFF) != 0


def quiet(u):
    return u | 0x0008000000000000


def nan_to_single(u):
    """a double NaN rounded to single and back (frsp): sign kept, fraction cut to its top 23 bits, quiet"""
    return (u & 0x8000000000000000) | 0x7FF0000000000000 | ((u >> 29) << 29 & 0x000FFFFFFFFFFFFF) | 0x0008000000000000


class _Ctx:
    imm_override = {}


def translate(w, on):
    old = ppc2c.NAN_PROPAGATION
    ppc2c.NAN_PROPAGATION = on
    try:
        return ppc2c.translate(0x02000000, w, _Ctx())
    finally:
        ppc2c.NAN_PROPAGATION = old


HARNESS = r"""
#include <stdio.h>
#include <inttypes.h>
#include "ppc.h"
#include "ppc_nan.h"
%(funcs)s
typedef void (*Op)(Cpu*);
static const Op ops[][2] = { %(table)s };
static Cpu cpu;
int main(void) {
    unsigned k;
    uint64_t v[6];
    while (scanf("%%u %%" SCNx64 " %%" SCNx64 " %%" SCNx64 " %%" SCNx64 " %%" SCNx64 " %%" SCNx64,
                 &k, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 7) {
        for (int on = 0; on < 2; on++) {
            memset(&cpu, 0, sizeof cpu);
            for (int r = 0; r < 3; r++) {
                memcpy(&cpu.f[%(A)d + r].ps0, &v[2 * r], 8);
                memcpy(&cpu.f[%(A)d + r].ps1, &v[2 * r + 1], 8);
            }
            uint64_t init = 0x%(init)016Xull;
            memcpy(&cpu.f[%(D)d].ps0, &init, 8);
            memcpy(&cpu.f[%(D)d].ps1, &init, 8);
            ops[k][on](&cpu);
            uint64_t d0, d1;
            memcpy(&d0, &cpu.f[%(D)d].ps0, 8);
            memcpy(&d1, &cpu.f[%(D)d].ps1, 8);
            printf("%%016" PRIx64 " %%016" PRIx64 "%%s", d0, d1, on ? "\n" : " ");
        }
    }
    return 0;
}
"""


def find_cc():
    for cc in (os.environ.get("CC"), "clang", "cc", "gcc"):
        if cc and shutil.which(cc):
            return cc
    return None


class PpcNanPropagation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc = find_cc()
        if cc is None:
            raise unittest.SkipTest("no C compiler")
        cls.tmp = tempfile.mkdtemp(prefix="ppc_nan_test_")
        funcs, table = [], []
        for i, (name, op, xo, _, _) in enumerate(INSNS):
            w = a_form(op, xo)
            for on in (0, 1):
                funcs.append("static void op_%d_%d(Cpu* c) { %s } /* %s */" % (i, on, translate(w, bool(on)), name))
            table.append("{ op_%d_0, op_%d_1 }" % (i, i))
        src = os.path.join(cls.tmp, "harness.c")
        with open(src, "w") as f:
            f.write(HARNESS % {"funcs": "\n".join(funcs), "table": ", ".join(table), "A": A, "D": D,
                               "init": D_PS1_INIT})
        cls.exe = os.path.join(cls.tmp, "harness.exe" if os.name == "nt" else "harness")
        # the flags the recompiled game code is built with (no FP contraction: fused multiply-adds
        # only where ppc2c writes fma)
        subprocess.run([cc, "-O2", "-ffp-contract=off", "-fno-strict-aliasing", "-w",
                        "-I", os.path.join(ROOT, "runtime", "include"), "-I", HERE, src, "-o", cls.exe, "-lm"],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_cases(self, cases):
        """cases: (insn index, [a0, a1, b0, b1, c0, c1]) -> [(off ps0, off ps1, on ps0, on ps1)]"""
        text = "".join("%d %s\n" % (k, " ".join("%x" % x for x in v)) for k, v in cases)
        out = subprocess.run([self.exe], input=text, stdout=subprocess.PIPE, text=True, check=True).stdout
        return [tuple(int(x, 16) for x in line.split()) for line in out.splitlines()]

    @staticmethod
    def expected(spec, single, regs, off):
        """the PowerPC result of one slot; regs: letter -> (ps0, ps1); off: the existing translation's result"""
        if spec[0] == "copy":
            letter, slot = spec[1]
            return regs[letter][slot]
        for letter, slot in spec:
            x = regs[letter][slot]
            if is_nan(x):
                return nan_to_single(quiet(x)) if single else quiet(x)
        if is_nan(off):
            return DEFAULT_QNAN
        return off

    def check(self, k, values_list):
        name, op, _, single, slots = INSNS[k]
        results = self.run_cases([(k, v) for v in values_list])
        self.assertEqual(len(results), len(values_list))
        for v, (off0, off1, on0, on1) in zip(values_list, results):
            regs = {"A": (v[0], v[1]), "B": (v[2], v[3]), "C": (v[4], v[5])}
            want0 = self.expected(slots[0], single, regs, off0)
            if op == 63:
                want1 = D_PS1_INIT          # double instructions leave ps1 alone
            elif op == 59:
                want1 = want0               # single instructions write the result to both slots
            else:
                want1 = self.expected(slots[1], single, regs, off1)
            msg = "%s A=%016x/%016x B=%016x/%016x C=%016x/%016x" % ((name,) + tuple(v))
            self.assertEqual((on0, on1), (want0, want1), msg)
            if not any(is_nan(x) for x in v) and not is_nan(off0) and not is_nan(off1):
                self.assertEqual((on0, on1), (off0, off1), msg + ": a result without NaNs must not change")

    def test_every_operand_combination(self):
        """every value of POOL in every operand position (both NaN in every order, one NaN, quiet and
        signalling, both signs, invalid operations), ps1 slots with the pool in another order"""
        rot = POOL[3:] + POOL[:3]
        for k in range(len(INSNS)):
            values = [(a, rot[POOL.index(a)], b, rot[POOL.index(b)], c, rot[POOL.index(c)])
                      for a, b, c in itertools.product(POOL, repeat=3)]
            self.check(k, values)

    def test_ps_slots_independent(self):
        """the paired-single slots choose their NaN independently: NaNs only in ps1, only in ps0, mixed"""
        for k, (name, op, _, _, _) in enumerate(INSNS):
            if op != 4:
                continue
            values = []
            for n0, n1 in itertools.product(NAN_POOL, repeat=2):
                values.append((ONE_HALF, n0, TWO, n1, ONE_HALF, TWO))
                values.append((n0, ONE_HALF, n1, TWO, TWO, n0))
                values.append((ONE_HALF, TWO, n0, ONE_HALF, n1, TWO))
            self.check(k, values)

    def test_documented_examples(self):
        idx = {name: i for i, (name, *_rest) in enumerate(INSNS)}
        nums = (ONE_HALF, ONE_HALF)
        # fadd: both NaN -> frA, whichever order the host would pick
        r = self.run_cases([(idx["fadd"], [QNAN_NEG, 0, QNAN, 0, 0, 0]),
                            (idx["fadd"], [QNAN, 0, QNAN_NEG, 0, 0, 0])])
        self.assertEqual(r[0][2], QNAN_NEG)
        self.assertEqual(r[1][2], QNAN)
        # fmadd = A*C + B: with C and B NaN, B wins (the order is A, B, C), quieted, sign kept
        r = self.run_cases([(idx["fmadd"], [ONE_HALF, 0, SNAN_NEG, 0, QNAN, 0])])
        self.assertEqual(r[0][2], quiet(SNAN_NEG))
        # fnmadd does not negate a propagated NaN, and its invalid result is the positive default QNaN
        r = self.run_cases([(idx["fnmadd"], [QNAN, 0, ONE_HALF, 0, ONE_HALF, 0]),
                            (idx["fnmadd"], [INF, 0, ONE_HALF, 0, ZERO, 0])])
        self.assertEqual(r[0][2], QNAN)
        self.assertEqual(r[1][2], DEFAULT_QNAN)
        # fmuls: a signalling frC with only low payload bits is still frC (round25 would make it infinity)
        r = self.run_cases([(idx["fmuls"], [ONE_HALF, 0, 0, 0, SNAN_LOW, 0])])
        self.assertEqual(r[0][2], nan_to_single(quiet(SNAN_LOW)))
        self.assertEqual(r[0][2], DEFAULT_QNAN)  # its payload is below single precision
        # ps_add per slot
        r = self.run_cases([(idx["ps_add"], [QNAN_NEG, ONE_HALF, QNAN, SNAN_NEG, nums[0], nums[1]])])
        self.assertEqual(r[0][2:], (nan_to_single(QNAN_NEG), nan_to_single(quiet(SNAN_NEG))))


    def test_multiply_add_family(self):
        """fmadd/fmsub/fnmadd/fnmsub (double, single) and ps_madd/ps_msub/ps_nmadd/ps_nmsub: first NaN in
        A, B, C order, quieted; the negating forms keep the NaN's sign; inf * 0 + x gives the positive
        default NaN (single word 7FC00000)"""
        idx = {name: i for i, (name, *_rest) in enumerate(INSNS)}
        family = ["fmadd", "fmsub", "fnmadd", "fnmsub", "fmadds", "fmsubs", "fnmadds", "fnmsubs",
                  "ps_madd", "ps_msub", "ps_nmadd", "ps_nmsub"]
        for name in family:
            single = INSNS[idx[name]][3]
            sel = (lambda u: nan_to_single(u)) if single else (lambda u: u)
            r = self.run_cases([
                (idx[name], [QNAN_NEG, QNAN_NEG, QNAN, QNAN, SNAN_NEG, SNAN_NEG]),   # A, B, C NaN: A
                (idx[name], [ONE_HALF, ONE_HALF, SNAN_NEG, SNAN_NEG, QNAN, QNAN]),   # B, C NaN: B (not C)
                (idx[name], [ONE_HALF, ONE_HALF, TWO, TWO, QNAN_NEG, QNAN_NEG]),     # C only, negative
                (idx[name], [INF, INF, ONE_HALF, ONE_HALF, ZERO, ZERO]),             # inf * 0 + x: invalid
            ])
            self.assertEqual(r[0][2], sel(QNAN_NEG), name)
            self.assertEqual(r[1][2], sel(quiet(SNAN_NEG)), name)
            self.assertEqual(r[2][2], sel(QNAN_NEG), name)       # sign kept by the negating forms too
            self.assertEqual(r[3][2], DEFAULT_QNAN, name)
            if single:
                self.assertEqual(struct.unpack(">I", struct.pack(">f", struct.unpack(">d", struct.pack(">Q", r[3][2]))[0]))[0],
                                 0x7FC00000, name)


class OptionOff(unittest.TestCase):
    def test_off_emits_no_helper(self):
        for name, op, xo, _, _ in INSNS:
            self.assertNotIn("ppc_nan", translate(a_form(op, xo), False), name)
            self.assertIn("ppc_nan", translate(a_form(op, xo), True), name)


if __name__ == "__main__":
    unittest.main()
