// SPDX-License-Identifier: MPL-2.0
// Compiler helper replacements: 64-bit integer arithmetic and conversions, as C defines them, with the
// cases C leaves open stated as measured (tools/host_runtime/difftest/abisurvey.cpp).
#include "hostrt_impl.h"

#include <cmath>

namespace hostrt {

// float -> 64-bit integer, truncating toward zero. Measured beyond C: the magnitude is converted with
// saturation (a NaN's magnitude counts as infinite) and then the sign bit is applied, so -NaN gives the
// most negative s64, and for u64 a negative value is the negated magnitude modulo 2^64.
uint64_t cvt_f_s64_value(float x) {
    const bool neg = std::signbit(x);
    const float m = std::isnan(x) ? INFINITY : std::fabs(x);
    if (m >= 9223372036854775808.0f) return neg ? 0x8000000000000000ull : 0x7FFFFFFFFFFFFFFFull;
    const uint64_t v = (uint64_t)m;
    return neg ? 0 - v : v;
}
uint64_t cvt_f_u64_value(float x) {
    const bool neg = std::signbit(x);
    const float m = std::isnan(x) ? INFINITY : std::fabs(x);
    const uint64_t v = m >= 18446744073709551616.0f ? 0xFFFFFFFFFFFFFFFFull : (uint64_t)m;
    return neg ? 0 - v : v;
}

// the argument as the original reads it (stfs of f1: a single in all the game's calls)
static inline float arg_f(const Cpu* c) { return u32_as_f32(ppc_stfs_bits(c->f[1].ps0)); }

void cvt_f_s64_028F5680(Cpu* c) {
    const uint64_t v = cvt_f_s64_value(arg_f(c));
    c->r[3] = (uint32_t)(v >> 32);
    c->r[4] = (uint32_t)v;
}
void cvt_f_u64_028F5678(Cpu* c) {
    const uint64_t v = cvt_f_u64_value(arg_f(c));
    c->r[3] = (uint32_t)(v >> 32);
    c->r[4] = (uint32_t)v;
}

// u64 -> float, rounded once to nearest (C's conversion); the FPR gets the single in both lanes
void cvt_u64_f_028F543C(Cpu* c) {
    const uint64_t v = (uint64_t)c->r[3] << 32 | c->r[4];
    c->f[1].ps0 = c->f[1].ps1 = (double)(float)v;
}

// u64 % u64; a remainder by zero is the dividend (measured). Like the original, it saves LR in the
// caller's frame (the EABI LR save word at SP+4).
void umod64_028F597C(Cpu* c) {
    st32(c->r[1] + 4, c->lr);
    const uint64_t a = (uint64_t)c->r[3] << 32 | c->r[4];
    const uint64_t b = (uint64_t)c->r[5] << 32 | c->r[6];
    const uint64_t r = b ? a % b : a;
    c->r[3] = (uint32_t)(r >> 32);
    c->r[4] = (uint32_t)r;
}

}  // namespace hostrt
