// SPDX-License-Identifier: MPL-2.0
// Decomp build, fast mode (cmake WWHD_DECOMP_FAST_NATIVE_MATH; OFF: not compiled): native versions of the game's hot
// GHS libm functions (hostrt_native_math.h). Each one follows its recompiled original (the game's code as the port
// runs it in every build) operation by operation: the same double / single rounding points (to_single where the
// original rounds with frsp or a single-precision instruction), the same fused and unfused multiply-adds (the
// recompiler's expressions: a single-precision fmadds is the double product and sum rounded to single, fnmsub is
// -fma), the paired-single rounding of the multiplier (round25), the same float<->integer bit moves (stfs / lfs
// semantics: ppc_stfs_bits / ppc_lfs_value), the same compares, branches and special cases. The constants and the
// sqrt guess table are read from the game's own data in guest memory at run time (nothing copied from the game).
// The file must be compiled with -ffp-contract=off, as the recompiled code is (the port's global flag).
//
// Proof: tools/host_runtime/difftest/nativemath.cpp runs each function next to its recompiled original: every one of
// the 2^32 single inputs for the one-argument functions, random doubles and an edge grid for all, random pairs and
// grids for fmod (results/native_math.txt).
#if defined(WWHD_DECOMP_FAST_NATIVE_MATH) || defined(WWHD_NATIVE_MATH_TEST)
#include "hostrt_native_math.h"

#include <cmath>

#include "ppc.h"

namespace hostrt::nmath {
#ifdef WWHD_NATIVE_MATH_TEST
thread_local uint32_t g_fmod_round_cap = 4096;
thread_local bool g_fmod_capped = false;
#endif
namespace {

// the game's constants (.rodata of the RPX in guest memory)
inline double k32(uint32_t ea) { return ldf32(ea); }
inline double k64(uint32_t ea) { return ldf64(ea); }

// 028F5E98: (double)n for a 32-bit signed n, rounded to single (the 0x43300000 bias trick, then frsp)
inline double int_to_single(uint32_t n) {
    const double d = u64_as_f64((uint64_t)0x43300000u << 32 | (n ^ 0x80000000u));
    return to_single(d - k64(0x1018AAE8u));
}

// fctiwz, then the low word of the stored double (stfd + lwz)
inline uint32_t fctiwz_word(double d) { return (uint32_t)ppc_fctiwz(d); }

// fmadds as the recompiler writes it: the double product (multiplier rounded as the 750's paired-single unit does)
// plus the addend, rounded to single
inline double madds(double a, double c, double b) { return to_single(a * round25(c) + b); }
inline double muls(double a, double c) { return to_single(a * round25(c)); }

// 028F4700 ldexpf(x, n) on the single-format word of x; errno ERANGE (0x22) on overflow
double ldexpf_n(double x, uint32_t n, uint32_t& err) {
    uint32_t w = ppc_stfs_bits(x);
    uint32_t e = w >> 23 & 0xFFu;
    const uint32_t sign = w & 0x80000000u;
    if (e == 255) return ppc_lfs_value(w);
    if (e == 0) {
        if (w == sign) return ppc_lfs_value(w);
        n = n + 1;  // (a denormal's bit 23 is clear: the original's test-first loop runs at least once too)
        do {
            n = n - 1;
            w = w << 1;
        } while (!(w & 0x00800000u));
        w = w ^ 0x00800000u;
    }
    e = e + n;
    if ((int32_t)e > 0) {
        if ((int32_t)e < 255) {
            w = w + (n << 23 & 0xFF800000u);
        } else {
            err = 0x22u;
            w = 0x7F800000u;
        }
    } else if ((int32_t)e > -23) {
        const uint32_t sh = 8 - e;
        w = w << 8 | 0x80000000u;
        w = (sh & 0x20) ? 0 : w >> (sh & 31);
        w = (w + 1) >> 1;
    } else {
        w = 0;
    }
    return ppc_lfs_value(w | sign);
}

}  // namespace

// 028F4384 sqrtf: x > 0: a bit-trick guess and two single-precision Newton steps; x == 0: the constant; otherwise
// (x < 0, NaN) __gh_set_errno(EDOM 0x21) and the constant
double sqrtf_n(double x, uint32_t& err) {
    const double zero = k32(0x1018A848u);
    if (x > zero) {
        const uint32_t w = (rotl32(ppc_stfs_bits(x), 31) & 0x7FFFFFFFu) + 0x1FBB4EAFu;
        double g = ppc_lfs_value(w);
        const double q = to_single(x / g);
        g = to_single(g + q);
        const double x2 = to_single(x / g);
        return to_single(g * round25(k32(0x1018A84Cu)) + x2);
    }
    if (!(x == zero)) err = 0x21u;
    return zero;
}

// 028F37F0 sqrt: x > 0: a guess from the exponent / mantissa table, three Newton steps (two forms by the size of
// the reciprocal); x == 0: the constant; otherwise __gh_set_errno(EDOM) and the constant
double sqrt_n(double x, uint32_t& err) {
    const double zero = k64(0x1018A5C0u);
    if (x > zero) {
        const double f0 = k64(0x1018A5C8u);
        double f12 = f0 / x;
        const uint64_t xb = f64_as_u64(x);
        const uint32_t hi = (uint32_t)(xb >> 32);
        const uint32_t t = (uint32_t)(int32_t)(int8_t)ld8(0x1018A5D8u + (rotl32(hi, 19) & 0xFFu));
        const uint32_t g = (uint32_t)((int32_t)hi >> 1) + 0x1FF75069u - (t << 9);
        double f8 = u64_as_f64((uint64_t)g << 32 | (uint32_t)xb);
        const double f13 = k64(0x1018A5D0u);
        double f9;
        if (f12 > zero) {
            f9 = f8 * f12;
            double f10 = -std::fma(f9, f8, -f13);
            f8 = f8 * f10;
            const double f11 = f8 * f12;
            f9 = -std::fma(f11, f8, -f13);
            f8 = f8 * f9;
            f10 = f8 * f12;
            f9 = f10 * f8;
        } else {
            double f11 = f8 / x;
            f12 = f11 * f8;
            f9 = -std::fma(f12, f0, -f13);
            f8 = f8 * f9;
            double f10 = f8 / x;
            f11 = f10 * f8;
            f12 = -std::fma(f11, f0, -f13);
            f8 = f8 * f12;
            f10 = f8 / x;
            f11 = f10 * f8;
            f9 = f11 * f0;
        }
        f12 = f13 - f9;
        return f8 * f12;
    }
    if (!(x == zero)) err = 0x21u;
    return zero;
}

// 028F43F8 sin: |x| <= c: x itself (NaN included); |x| < c2: odd polynomial; huge: a constant; otherwise reduction by
// n = round(x * 2/pi) and the sine or cosine polynomial of the remainder by n's low bits
double sin_n(double x) {
    double f12 = x;
    double f10 = std::fabs(f12);
    if (!(f10 > k32(0x1018A850u))) return f12;
    if (f10 < k32(0x1018A854u)) {
        f10 = f12;
        double f13 = k32(0x1018A858u);
        f12 = muls(f12, f12);
        f13 = madds(f13, f12, k32(0x1018A85Cu));
        const double f11 = madds(f13, f12, k32(0x1018A860u));
        f12 = madds(f12, f11, k32(0x1018A864u));
        return muls(f12, f10);
    }
    const double f13 = k32(0x1018A86Cu);
    f12 = muls(f12, k32(0x1018A868u));
    if (f12 > k32(0x1018A870u)) return f13;
    double f11 = f12;
    const double h = k32(0x1018A874u);
    if (f11 < f13) f11 = to_single(f11 - h);
    else f11 = to_single(f11 + h);
    const uint32_t n = fctiwz_word(f11);
    f12 = to_single(f12 - int_to_single(n));
    if (n & 1) {
        f12 = muls(f12, f12);
        double a = madds(k32(0x1018A878u), f12, k32(0x1018A87Cu));
        a = madds(a, f12, k32(0x1018A880u));
        f12 = madds(f12, a, k32(0x1018A884u));
    } else {
        f10 = f12;
        double a = k32(0x1018A888u);
        f12 = muls(f12, f12);
        a = madds(a, f12, k32(0x1018A88Cu));
        a = madds(a, f12, k32(0x1018A890u));
        f12 = madds(f12, a, k32(0x1018A894u));
        f12 = muls(f12, f10);
    }
    if (n & 2) f12 = -f12;
    return f12;
}

// 028F4BE0 cos: |x| <= c (NaN included) or huge: the constant 1; |x| < c2: even polynomial; otherwise reduction by
// n = trunc(|x| * 2/pi + 1/2) and the cosine or sine polynomial of the remainder
double cos_n(double x) {
    double f12 = std::fabs(x);
    if (!(f12 > k32(0x1018A94Cu))) return k32(0x1018A970u);
    const double f11 = k32(0x1018A950u);
    if (f12 < k32(0x1018A954u)) {
        f12 = muls(f12, f12);
        double a = madds(k32(0x1018A958u), f12, k32(0x1018A95Cu));
        a = madds(a, f12, k32(0x1018A960u));
        a = madds(a, f12, k32(0x1018A964u));
        return madds(f12, a, f11);
    }
    f12 = muls(f12, k32(0x1018A968u));
    if (f12 > k32(0x1018A96Cu)) return k32(0x1018A970u);
    const uint32_t n = fctiwz_word(to_single(f12 + k32(0x1018A974u)));
    f12 = to_single(f12 - int_to_single(n));
    if (n & 1) {
        const double r = f12;
        f12 = muls(f12, f12);
        double a = madds(k32(0x1018A978u), f12, k32(0x1018A97Cu));
        a = madds(a, f12, k32(0x1018A980u));
        f12 = madds(f12, a, k32(0x1018A984u));
        f12 = muls(f12, r);
    } else {
        f12 = muls(f12, f12);
        double a = madds(k32(0x1018A988u), f12, k32(0x1018A98Cu));
        a = madds(a, f12, k32(0x1018A990u));
        f12 = madds(f12, a, f11);
    }
    if (n & 2) f12 = -f12;
    return f12;
}

// 028F4034 fmod: by the single-format exponents of |y| and |x|: |y| zero / denormal: __gh_set_errno(EDOM) and the
// constant; x zero / denormal / inf / NaN: x; y inf / NaN: y; otherwise repeated subtraction of |y| scaled by powers of
// two (ldexpf), in single precision, and the sign of x
double fmod_n(double x, double y, uint32_t& err) {
    const double ay = std::fabs(y);
    const double zero = k32(0x1018A7E4u);
    const uint32_t ey = rotl32(ppc_stfs_bits(ay), 9) & 0x1FFu;
    double ax = std::fabs(x);
    if (ey == 0) {
        err = 0x21u;
        return zero;
    }
    const uint32_t ex = rotl32(ppc_stfs_bits(ax), 9) & 0x1FFu;
    if (ex == 0 || ex == 255) return x;
    if (ey == 255) return y;
    const uint32_t by = ey - 126;
#ifdef WWHD_NATIVE_MATH_TEST
    // proof tool only: the original's loop never ends for some arguments the game does not pass (a non-single double
    // beyond single range: inf - inf); nativemath.cpp stops after this many rounds and checks that the original
    // does not return either
    uint32_t rounds = 0;
#endif
    for (;;) {
#ifdef WWHD_NATIVE_MATH_TEST
        if (++rounds > g_fmod_round_cap) { g_fmod_capped = true; return 0.0; }
#endif
        if (ax < ay) return x < zero ? -ax : ax;
        const uint32_t e = (rotl32(ppc_stfs_bits(ax), 9) & 0x1FFu) - 126;
        double s = ldexpf_n(ay, e - by, err);
        if (s > ax) s = ldexpf_n(s, 0xFFFFFFFFu, err);
        ax = to_single(ax - s);
    }
}

}  // namespace hostrt::nmath
#endif
