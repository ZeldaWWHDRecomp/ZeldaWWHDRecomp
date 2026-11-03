// SPDX-License-Identifier: MPL-2.0
// Host replacements of the game's compiler runtime (GHS C/C++ runtime) and nn static wrappers.
//
// Each function replaces one guest root (named by its canonical address) and is written from the C/C++
// standard and the host's own libraries; where the original's results go beyond the standard (NaNs,
// out-of-range conversions, division by zero, which FPR lane it writes), the behaviour was measured on
// the recompiled original (tools/host_runtime/difftest) and is stated here as a rule.
//
// Register contract (tools/host_runtime/callsite_regs.py): a replacement writes only its results (r3, r4,
// f1, f2 exactly as the original leaves them) and keeps every other register as it was on entry; no
// caller reads anything else the original leaves behind.
//
// These files depend only on ppc.h (guest memory and the Cpu state), so the difference tests compile
// them as they are. The hooks that route the game's calls here are in hostrt_hooks.cpp.
#pragma once
#include "ppc.h"

namespace hostrt {

// ---- math (hostrt_math.cpp): only functions measured bit-exact over their full input sets
void floorf_028F4114(Cpu* c);
void ceilf_028F41B0(Cpu* c);
void floor_028F3B98(Cpu* c);

// ---- compiler helpers (hostrt_abi.cpp)
void cvt_u64_f_028F543C(Cpu* c);  // u64 (r3:r4) -> float (f1)
void cvt_f_s64_028F5680(Cpu* c);  // float (f1) -> s64 (r3:r4)
void cvt_f_u64_028F5678(Cpu* c);  // float (f1) -> u64 (r3:r4)
uint64_t cvt_f_s64_value(float x);
uint64_t cvt_f_u64_value(float x);
void umod64_028F597C(Cpu* c);     // u64 (r3:r4) % u64 (r5:r6) -> r3:r4

}  // namespace hostrt
