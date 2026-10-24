/* SPDX-License-Identifier: MPL-2.0
 *
 * PowerPC NaN propagation for the recompiled code, verification builds only
 * (tools/recomp/recomp.py --ppc-nan-propagation; off for the game port, whose code then never includes
 * this file). recomp.py copies it next to funcs.h, which includes it after ppc.h.
 *
 * With NaN operands a host float operation returns whichever NaN the host and the compiler pick (clang may
 * swap the operands of a + b or a * b; x86 also makes the default NaN negative). The PowerPC rule
 * (PowerPC Architecture Book I, floating-point invalid operation / NaN operands; Broadway/Espresso apply it
 * per slot of the paired-single instructions too):
 *   - if frA is a NaN, the result is frA; else if frB is a NaN, frB; else if frC is a NaN, frC;
 *     (fmadd family FRT = FRA*FRC +/- FRB: the order is A, B, C, not the order of the formula)
 *   - a signalling NaN operand is returned quieted (fraction bit 0x0008000000000000 set), sign and payload
 *     kept; the negating forms (fnmadd, fnmsub, ps_nmadd, ps_nmsub) and the subtracting forms do not change
 *     the sign of a propagated NaN;
 *   - an invalid operation without NaN operands (inf - inf, 0 * inf, 0 / 0, inf / inf) returns the
 *     default QNaN 0x7FF8000000000000 (positive, also for the negating forms).
 * The single-precision forms round the selected NaN like any result (to_single in the generated code:
 * the low 29 fraction bits are dropped, as frsp does).
 *
 * ppc_nan2 / ppc_nan3 take the NaN-relevant operands in that priority order plus the normal result r,
 * evaluated from the original expression, and return r unless an operand is a NaN or r is. The
 * operands are tested before r: the single-precision multiply rounds frC to 25 bits first (round25),
 * which can turn a NaN frC into an infinity, so r alone would miss it. */
#pragma once
#include <stdint.h>
#include <string.h>

#define PPC_NAN_QUIET_BIT 0x0008000000000000ull
#define PPC_NAN_DEFAULT 0x7FF8000000000000ull

static inline double ppc_nan_quiet(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    u |= PPC_NAN_QUIET_BIT;
    memcpy(&d, &u, 8);
    return d;
}

static inline double ppc_nan_default(void) {
    uint64_t u = PPC_NAN_DEFAULT;
    double d;
    memcpy(&d, &u, 8);
    return d;
}

/* two NaN-relevant operands, x before y (fadd/fsub/fdiv: A, B; fmul: A, C) */
static inline double ppc_nan2(double x, double y, double r) {
    if (__builtin_expect(x != x, 0)) return ppc_nan_quiet(x);
    if (__builtin_expect(y != y, 0)) return ppc_nan_quiet(y);
    if (__builtin_expect(r != r, 0)) return ppc_nan_default();
    return r;
}

/* the fmadd family: a = frA, b = frB, c = frC */
static inline double ppc_nan3(double a, double b, double c, double r) {
    if (__builtin_expect(a != a, 0)) return ppc_nan_quiet(a);
    if (__builtin_expect(b != b, 0)) return ppc_nan_quiet(b);
    if (__builtin_expect(c != c, 0)) return ppc_nan_quiet(c);
    if (__builtin_expect(r != r, 0)) return ppc_nan_default();
    return r;
}
