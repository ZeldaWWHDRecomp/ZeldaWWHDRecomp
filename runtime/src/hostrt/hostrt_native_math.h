// SPDX-License-Identifier: MPL-2.0
// Decomp build, fast mode (cmake WWHD_DECOMP_FAST_NATIVE_MATH): native C++ versions of the game's hot GHS libm
// functions, computing exactly what their recompiled originals return (hostrt_native_math.cpp).
//
// Each function takes the argument register(s) as the guest call passes them (f1, f2: doubles, a float argument as
// its lfs value) and returns f1's value after the original returns, bit for bit. The original's other effects are
// not repeated: its stack frame below the caller's stack pointer (dead once it returns), the volatile registers and
// cr0/cr1 it leaves (the fast-mode recompiled original itself writes most of them back only on some paths). ERR gets
// the code the original passes to __gh_set_errno (0: no call); the caller makes that import call.
// Proof: tools/host_runtime/difftest/nativemath.cpp (results/native_math.txt).
#pragma once
#include <cstdint>

namespace hostrt::nmath {

double sqrtf_n(double x, uint32_t& err);           // 028F4384 sqrtf (single precision)
double sqrt_n(double x, uint32_t& err);            // 028F37F0 sqrt (double precision)
double sin_n(double x);                            // 028F43F8 sin (single precision arithmetic)
double cos_n(double x);                            // 028F4BE0 cos (single precision arithmetic)
double fmod_n(double x, double y, uint32_t& err);  // 028F4034 fmod (single precision arithmetic)

#ifdef WWHD_NATIVE_MATH_TEST
// proof tool only (tools/host_runtime/difftest/nativemath.cpp): fmod_n gives up after this many rounds of its loop
// and sets the flag (the original never returns for such arguments)
extern thread_local uint32_t g_fmod_round_cap;
extern thread_local bool g_fmod_capped;
#endif

}  // namespace hostrt::nmath
