// SPDX-License-Identifier: MPL-2.0
// Decomp build, fast mode only (cmake WWHD_DECOMP_FAST; docs/decomp-build.md): native entries of host runtime roots.
//
// In fast mode a decompiled caller reaches a decompiled callee's native entry through the runtime's address table
// (gabi.h GABI_FAST: gabi::call by address, call_ptr, and lcall to a callee that is not a game-unit function all probe
// g_decomp_call_tab / g_decomp_ptr_tab first). The host runtime roots (decomp_hostrt_hooks.txt) are not decompiled,
// so without this every call of one took the whole guest path (ArgPack, do_call_g, gmem_call, call_lr, dispatch,
// hook_X, Root::use_host) to reach a host function. Here the roots whose replacement only computes registers or
// writes the memory its arguments name get a native entry of their register shape (gabi.h NEntryT: one u32 per
// integer argument register, one double per float register, the result as u32 / u64 (r3:r4) / double (f1)), which
// the runtime adds to the address table: the caller calls the host function directly, as C++.
//
// Each entry runs the same replacement as hook_X on the calling thread's Cpu (gabi's thread block, glue.cpp
// gabi_fast_cpu): the argument registers set as the guest call sets them, the result registers left as the
// replacement writes them, and returned. The registers matter: decompiled code may read them after a call declared
// void (m_Do_ext_packed_blend.cpp angleWord: `truncateUnsigned64(angle); return cpu->r[4];`; with a scratch Cpu
// Link's cliff animation changed, ds/hooktest.sh). Roots that make guest calls, use the stack pointer or may hand
// back to their original keep the guest path.
//
// Not added (the guest path as before, hook_X decides): WWHD_HOSTRT=0 (the originals), WWHD_HOSTRT_CHECK=1 (both,
// compared), WWHD_HOSTRT_FAST=0 (bisecting: no native entries for the roots), a root a guest mod hooks.
#ifdef WWHD_DECOMP_FAST
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../decomp/decomp.h"
#include "hostrt_impl.h"
#include "recomp_table.h"

extern uint8_t* g_mod_hook_flags;  // mods/guest_mods.cpp

namespace hostrt {
namespace {

// gabi.h ptr_call_sig: integer registers | float registers << 4 | result kind << 8 (1 u32, 2 u64 r3:r4, 3 double)
// | 0x8000
constexpr uint32_t sig(unsigned ni, unsigned nf, unsigned rk) { return ni | nf << 4 | rk << 8 | 0x8000u; }

inline uint64_t r34(const Cpu& k) { return (uint64_t)k.r[3] << 32 | k.r[4]; }
}  // namespace
}  // namespace hostrt
extern "C" Cpu* gabi_fast_cpu();  // tools/decomp_build/glue.cpp: this thread's gabi block's Cpu
namespace hostrt {
namespace {

// f1 in, f1 out (floorf, ceilf, floor): an input the replacement leaves in place comes back unchanged
template <void (*F)(Cpu*)> double ne_f_f(double f0) {
    Cpu& k = *gabi_fast_cpu();
    k.f[1].ps0 = k.f[1].ps1 = f0;
    F(&k);
    return k.f[1].ps0;
}
// f1 in, r3:r4 out (float -> s64 / u64)
template <void (*F)(Cpu*)> uint64_t ne_f_rr(double f0) {
    Cpu& k = *gabi_fast_cpu();
    k.f[1].ps0 = k.f[1].ps1 = f0;
    F(&k);
    return r34(k);
}
// r3, r4 in, f1 out (u64 -> float)
template <void (*F)(Cpu*)> double ne_rr_f(uint32_t a0, uint32_t a1) {
    Cpu& k = *gabi_fast_cpu();
    k.r[3] = a0, k.r[4] = a1;
    F(&k);
    return k.f[1].ps0;
}
// r3, r4 in, r3:r4 out (div)
template <void (*F)(Cpu*)> uint64_t ne_rr_rr(uint32_t a0, uint32_t a1) {
    Cpu& k = *gabi_fast_cpu();
    k.r[3] = a0, k.r[4] = a1;
    F(&k);
    return r34(k);
}
// r3, r4 in, r3 out (bzero, zero words, strcpy, list erase)
template <void (*F)(Cpu*)> uint32_t ne_rr_r(uint32_t a0, uint32_t a1) {
    Cpu& k = *gabi_fast_cpu();
    k.r[3] = a0, k.r[4] = a1;
    F(&k);
    return k.r[3];
}
// r3, r4, r5 in, r3 out (list insert)
template <void (*F)(Cpu*)> uint32_t ne_rrr_r(uint32_t a0, uint32_t a1, uint32_t a2) {
    Cpu& k = *gabi_fast_cpu();
    k.r[3] = a0, k.r[4] = a1, k.r[5] = a2;
    F(&k);
    return k.r[3];
}
// r3 in, nothing out (register global object)
template <void (*F)(Cpu*)> void ne_r_v(uint32_t a0) {
    Cpu& k = *gabi_fast_cpu();
    k.r[3] = a0;
    F(&k);
}

struct FastRoot {
    uint32_t addr;
    PpcFunc self;  // f_X, for the guest-mod check
    uint32_t sig;
    void* fn;
};

}  // namespace
}  // namespace hostrt

extern "C" {
void f_028F4114(Cpu* __restrict c);
void f_028F41B0(Cpu* __restrict c);
void f_028F3B98(Cpu* __restrict c);
void f_028F543C(Cpu* __restrict c);
void f_028F5680(Cpu* __restrict c);
void f_028F5678(Cpu* __restrict c);
void f_028F35A8(Cpu* __restrict c);
void f_028F521C(Cpu* __restrict c);
void f_028F5914(Cpu* __restrict c);
void f_028F040C(Cpu* __restrict c);
void f_0286DA70(Cpu* __restrict c);
void f_0286DA9C(Cpu* __restrict c);
void f_028F026C(Cpu* __restrict c);
void f_028EFFD0(Cpu* __restrict c);
void f_028EFF8C(Cpu* __restrict c);
void f_028F0020(Cpu* __restrict c);
void f_028F0164(Cpu* __restrict c);
void f_028F0294(Cpu* __restrict c);
void f_028F1748(Cpu* __restrict c);
void f_028F17A8(Cpu* __restrict c);
void f_028F186C(Cpu* __restrict c);
void f_028F5C88(Cpu* __restrict c);
void f_028F597C(Cpu* __restrict c);
void f_0286DACC(Cpu* __restrict c);
void f_0286DB18(Cpu* __restrict c);
void f_028F2EE8(Cpu* __restrict c);
void f_028F3090(Cpu* __restrict c);
#ifdef WWHD_DECOMP_FAST_LIBM
void f_028F4384(Cpu* __restrict c);
void f_028F43F8(Cpu* __restrict c);
void f_028F37F0(Cpu* __restrict c);
void f_028F4BE0(Cpu* __restrict c);
void f_028F4034(Cpu* __restrict c);
#endif
}

#ifdef WWHD_DECOMP_FAST_LIBM
// cmake WWHD_DECOMP_FAST_LIBM (fast mode only; OFF: none of this is compiled): the GHS libm functions decompiled code
// calls most (cliff, Mac fast stats: sqrtf 8.9M, sinf 5.1M, sqrt 4.4M, cosf 4.0M, fmodf 2.5M generic guest calls per
// run, ~23% of all) go from gmem_call straight to their recompiled originals f_X (hostrt_fast_direct), skipping
// decomp::call_to, call_lr and the dispatch table. The guest call's own set-up stays (ArgPack, do_call_g: the stack
// pointer the callee gets below the caller's frame and Locals; these functions build a stack frame, so a native entry
// on the caller's r1 would overwrite live Locals: tried, the title sequence broke). Same code on the same registers
// and stack as before: exact by construction. (Not host runtime roots: their results differ from every host libm,
// tools/host_runtime/difftest/results/math_survey.txt.) Where a native port is proven bit-exact on every input
// (hostrt_libm.cpp, tools/host_runtime/difftest/libmport.cpp), the port runs instead of the original.
// WWHD_DECOMP_FAST_LIBM (env): 0 the guest path as before, 1 the originals only, unset / 2 the ports where there are.
namespace hostrt { void sqrtf_028F4384(Cpu* c); }  // hostrt_libm.cpp
static int fast_libm_level() {
    static const int level = [] { const char* e = getenv("WWHD_DECOMP_FAST_LIBM"); return e && *e ? atoi(e) : 2; }();
    return level;
}
static bool fast_libm_on() { return fast_libm_level() != 0; }
#endif

// the roots without a native entry that decompiled code calls often (decomp.cpp gmem_call, fast mode): their f_X
// directly (-> ppc_host_call -> hook_X, which still decides between the replacement and the original), without
// call_lr's answer and the dispatch table. These roots read LR only to store it into the caller's LR save word,
// which fast mode does not keep exact anyway (no prologue stores). Null: the guest path as before.
extern "C" PpcFunc hostrt_fast_direct(uint32_t addr) {
    static const bool off = [] { const char* f = getenv("WWHD_HOSTRT_FAST"); return f && (!strcmp(f, "0") || !strcmp(f, "entries")); }();
    if (off) return nullptr;
    switch (addr) {
    case 0x028EFFD0u: return f_028EFFD0;
    case 0x028EFF8Cu: return f_028EFF8C;
    case 0x028F0020u: return f_028F0020;
    case 0x028F0164u: return f_028F0164;
    case 0x028F0294u: return f_028F0294;
    case 0x028F1748u: return f_028F1748;
    case 0x028F17A8u: return f_028F17A8;
    case 0x028F186Cu: return f_028F186C;
    case 0x028F5C88u: return f_028F5C88;
    case 0x028F597Cu: return f_028F597C;
    case 0x0286DACCu: return f_0286DACC;
    case 0x0286DB18u: return f_0286DB18;
    case 0x028F2EE8u: return f_028F2EE8;
    case 0x028F3090u: return f_028F3090;
#ifdef WWHD_DECOMP_FAST_LIBM
    // GHS libm (WWHD_DECOMP_FAST_LIBM): their f_X directly, as the roots above
    case 0x028F4384u: return fast_libm_on() ? (fast_libm_level() >= 2 ? hostrt::sqrtf_028F4384 : f_028F4384) : nullptr;
    case 0x028F43F8u: return fast_libm_on() ? f_028F43F8 : nullptr;
    case 0x028F37F0u: return fast_libm_on() ? f_028F37F0 : nullptr;
    case 0x028F4BE0u: return fast_libm_on() ? f_028F4BE0 : nullptr;
    case 0x028F4034u: return fast_libm_on() ? f_028F4034 : nullptr;
#endif
    // roots with a native entry, for callers whose shape does not match it (their guest call)
    case 0x028F4114u: return f_028F4114;
    case 0x028F41B0u: return f_028F41B0;
    case 0x028F3B98u: return f_028F3B98;
    case 0x028F543Cu: return f_028F543C;
    case 0x028F5680u: return f_028F5680;
    case 0x028F5678u: return f_028F5678;
    case 0x028F35A8u: return f_028F35A8;
    case 0x028F521Cu: return f_028F521C;
    case 0x028F5914u: return f_028F5914;
    case 0x028F040Cu: return f_028F040C;
    case 0x0286DA70u: return f_0286DA70;
    case 0x0286DA9Cu: return f_0286DA9C;
    case 0x028F026Cu: return f_028F026C;
    default: return nullptr;
    }
}

// the roots' native entries for decomp.cpp's address table; ADD(addr, sig, fn) for each one that is on
extern "C" unsigned hostrt_fast_entries(void (*add)(uint32_t addr, uint32_t sig, void* fn)) {
    using namespace hostrt;
    const char* e = getenv("WWHD_HOSTRT");
    const char* k = getenv("WWHD_HOSTRT_CHECK");
    const char* f = getenv("WWHD_HOSTRT_FAST");
    if ((e && !strcmp(e, "0")) || (k && strcmp(k, "0")) || (f && (!strcmp(f, "0") || !strcmp(f, "direct")))) return 0;
    // bisecting: WWHD_HOSTRT_FAST_ONLY=ADDR,... (only these roots get native entries)
    const char* only = getenv("WWHD_HOSTRT_FAST_ONLY");
    static const FastRoot roots[] = {
        {0x028F4114u, f_028F4114, sig(0, 1, 3), (void*)&ne_f_f<floorf_028F4114>},
        {0x028F41B0u, f_028F41B0, sig(0, 1, 3), (void*)&ne_f_f<ceilf_028F41B0>},
        {0x028F3B98u, f_028F3B98, sig(0, 1, 3), (void*)&ne_f_f<floor_028F3B98>},
        {0x028F543Cu, f_028F543C, sig(2, 0, 3), (void*)&ne_rr_f<cvt_u64_f_028F543C>},
        {0x028F5680u, f_028F5680, sig(0, 1, 2), (void*)&ne_f_rr<cvt_f_s64_028F5680>},
        {0x028F5678u, f_028F5678, sig(0, 1, 2), (void*)&ne_f_rr<cvt_f_u64_028F5678>},
        {0x028F35A8u, f_028F35A8, sig(2, 0, 2), (void*)&ne_rr_rr<div_028F35A8>},
        {0x028F521Cu, f_028F521C, sig(2, 0, 1), (void*)&ne_rr_r<bzero_028F521C>},
        {0x028F5914u, f_028F5914, sig(2, 0, 1), (void*)&ne_rr_r<zero_words_028F5914>},
        {0x028F040Cu, f_028F040C, sig(2, 0, 1), (void*)&ne_rr_r<strcpy_028F040C>},
        {0x0286DA70u, f_0286DA70, sig(3, 0, 1), (void*)&ne_rrr_r<list_insert_0286DA70>},
        {0x0286DA9Cu, f_0286DA9C, sig(2, 0, 1), (void*)&ne_rr_r<list_erase_0286DA9C>},
        {0x028F026Cu, f_028F026C, sig(1, 0, 0), (void*)&ne_r_v<register_global_object_028F026C>},
    };
    unsigned n = 0;
    for (const FastRoot& r : roots) {
        if (only) {
            char a[9];
            snprintf(a, sizeof a, "%08X", r.addr);
            if (!strstr(only, a)) continue;
        }
        if (g_mod_hook_flags) {
            bool hooked = false;
            for (unsigned i = 0; i < g_recomp_func_count; i++)
                if (g_recomp_funcs[i].fn == r.self) { hooked = g_mod_hook_flags[i] != 0; break; }
            if (hooked) continue;
        }
        add(r.addr, r.sig, r.fn);
        n++;
    }
    return n;
}
#endif
