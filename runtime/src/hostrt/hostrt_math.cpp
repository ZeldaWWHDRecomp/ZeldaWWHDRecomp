// SPDX-License-Identifier: MPL-2.0
// Math replacements: the C standard's floor/ceil, computed by the host. Measured behaviour of the game's
// versions beyond the standard (tools/host_runtime/difftest/mathsurvey.cpp, all 2^32 float inputs, 1e9
// double inputs plus edge values):
// - a NaN argument comes back unchanged (a signalling NaN stays signalling);
// - floorf/ceilf: for |x| >= 2^23, infinities, NaNs and zero (floorf: either zero, ceilf: +0 only) f1 is
//   returned as it came in (both FPR lanes untouched); otherwise f1 gets the single result in both lanes;
// - floor (double): only the first lane is written.
// The other math functions differ from every host candidate and stay the recompiled originals
// (tools/host_runtime/difftest/results/math_survey.txt).
#include <cmath>

#include "hostrt_impl.h"

namespace hostrt {
namespace {

// the argument as the original reads it: stfs of f1 (a single in all the game's calls)
inline uint32_t single_bits(double d) { return ppc_stfs_bits(d); }

template <bool kCeil>
inline void round_f(Cpu* c) {
    const double x = c->f[1].ps0;
    const uint32_t b = single_bits(x);
    const uint32_t e = (b >> 23) & 0xFF;
    const bool keep_zero = kCeil ? f64_as_u64(x) == 0 : x == 0;
    if (e >= 150 || keep_zero) return;  // |x| >= 2^23, inf, NaN, zero: f1 unchanged
    const float f = u32_as_f32(b);
    const double r = (double)(kCeil ? std::ceil(f) : std::floor(f));
    c->f[1].ps0 = c->f[1].ps1 = r;
}

}  // namespace

void floorf_028F4114(Cpu* c) { round_f<false>(c); }
void ceilf_028F41B0(Cpu* c) { round_f<true>(c); }

void floor_028F3B98(Cpu* c) {
    const double x = c->f[1].ps0;
    if (std::isnan(x)) return;
    c->f[1].ps0 = std::floor(x);
}

}  // namespace hostrt
