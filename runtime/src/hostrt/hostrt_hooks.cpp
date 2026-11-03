// SPDX-License-Identifier: MPL-2.0
// Decomp build only (cmake/Decomp.cmake; docs/decomp-build.md): routes the game's calls of the port-owned
// runtime roots (tools/recomp/decomp_hostrt_hooks.txt, read by recomp.py --decomp-hooks) to the host
// replacements (hostrt_impl.h). The normal build keeps the recompiled originals and has none of this.
//
// WWHD_HOSTRT=0      runs the recompiled originals instead (A/B comparisons).
// WWHD_HOSTRT_CHECK=1 runs both for every call of a register-only root and logs the first differences
//                    (the original runs first on the thread's CPU state, then the registers are reset
//                    and the replacement runs; these roots write no guest memory except the stack below
//                    SP and the LR save word, which both write alike).
// A root a guest mod hooks or replaces (mod-hook builds, docs/mod-sdk-v2.md) always runs its original,
// where the mod's check sits.
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

#include "../runtime.h"
#include "hostrt_impl.h"
#include "recomp_table.h"

extern uint8_t* g_mod_hook_flags;  // mods/guest_mods.cpp: per-function flags in mod-hook builds

namespace hostrt {
namespace {

enum Out { R3 = 1, R4 = 2, F1 = 4, F2 = 8 };

struct Root {
    uint32_t addr;     // canonical address
    PpcFunc self;      // f_X: finds the function's ordinal in this build's table
    const char* name;
    int outs;          // result registers a caller reads (tools/host_runtime/callsite_regs.tsv)
    int ordinal = -2;  // -2: not looked up yet, -1: not in the table
    std::atomic<uint32_t> mismatches{0};
};

int mode() {  // 0 off, 1 on, 2 on + check
    static const int m = [] {
        const char* e = getenv("WWHD_HOSTRT");
        if (e && !strcmp(e, "0")) return 0;
        const char* k = getenv("WWHD_HOSTRT_CHECK");
        return k && strcmp(k, "0") ? 2 : 1;
    }();
    return m;
}

int ordinal_of(PpcFunc fn) {
    for (unsigned i = 0; i < g_recomp_func_count; i++)
        if (g_recomp_funcs[i].fn == fn) return (int)i;
    return -1;
}

bool use_host(Root& r) {
    if (!mode()) return false;
    if (g_mod_hook_flags) {
        if (r.ordinal == -2) r.ordinal = ordinal_of(r.self);  // a benign race: every thread computes the same
        if (r.ordinal >= 0 && g_mod_hook_flags[r.ordinal]) return false;
    }
    return true;
}

bool same(const Cpu& a, const Cpu& b, int outs) {
    if ((outs & R3) && a.r[3] != b.r[3]) return false;
    if ((outs & R4) && a.r[4] != b.r[4]) return false;
    if ((outs & F1) && (memcmp(&a.f[1], &b.f[1], sizeof a.f[1]))) return false;
    if ((outs & F2) && (memcmp(&a.f[2], &b.f[2], sizeof a.f[2]))) return false;
    return true;
}

void run(Root& r, Cpu* c, void (*host)(Cpu*), PpcFunc orig) {
    if (!use_host(r)) return orig(c);
    if (mode() == 2) {
        // the original runs on the guest thread's own CPU state (a copy would confuse the scheduler if
        // it yields at a function entry), its results are kept aside, the registers go back, and the
        // replacement runs; only the replacement's results stay
        const Cpu entry = *c;
        orig(c);
        Cpu o;
        memcpy(o.r, c->r, sizeof o.r);
        memcpy(o.f, c->f, sizeof o.f);
        memcpy(c->r, entry.r, sizeof c->r);
        memcpy(c->f, entry.f, sizeof c->f);
        memcpy(c->cr, entry.cr, sizeof c->cr);
        c->lr = entry.lr, c->ctr = entry.ctr, c->fpscr = entry.fpscr;
        c->xer_so = entry.xer_so, c->xer_ov = entry.xer_ov, c->xer_ca = entry.xer_ca;
        host(c);
        if (!same(o, *c, r.outs) && r.mismatches.fetch_add(1) < 20)
            LOG("[hostrt] CHECK %s (%08X) differs: in r3 %08X r4 %08X r5 %08X r6 %08X f1 %016llX f2 %016llX -> original r3 %08X r4 %08X f1 %016llX/%016llX, "
                "host r3 %08X r4 %08X f1 %016llX/%016llX (lr %08X)",
                r.name, r.addr, entry.r[3], entry.r[4], entry.r[5], entry.r[6], (unsigned long long)f64_as_u64(entry.f[1].ps0),
                (unsigned long long)f64_as_u64(entry.f[2].ps0), o.r[3], o.r[4], (unsigned long long)f64_as_u64(o.f[1].ps0),
                (unsigned long long)f64_as_u64(o.f[1].ps1), c->r[3], c->r[4], (unsigned long long)f64_as_u64(c->f[1].ps0),
                (unsigned long long)f64_as_u64(c->f[1].ps1), entry.lr);
        return;
    }
    host(c);
}

// roots that write memory or call game code: no CHECK run (the original cannot run on a copy); the
// replacement may hand an input back to the original (false)
void run_calls(Root& r, Cpu* c, bool (*host)(Cpu*), PpcFunc orig) {
    if (!use_host(r) || !host(c)) orig(c);
}

}  // namespace
}  // namespace hostrt

// one hook per root: hook_X is what the game's f_X calls (tools/recomp/recomp.py), f_X_orig the original
#define HOSTRT_ROOT(ADDR, IMPL, OUTS)                                                               \
    extern "C" void f_##ADDR(Cpu* __restrict c);                                                    \
    extern "C" void f_##ADDR##_orig(Cpu* __restrict c);                                             \
    extern "C" void hook_##ADDR(Cpu* c) {                                                           \
        static hostrt::Root root{0x##ADDR##u, f_##ADDR, #IMPL, OUTS};                               \
        hostrt::run(root, c, hostrt::IMPL##_##ADDR, f_##ADDR##_orig);                               \
    }

#define HOSTRT_CALLS_ROOT(ADDR, IMPL, OUTS)                                                         \
    extern "C" void f_##ADDR(Cpu* __restrict c);                                                    \
    extern "C" void f_##ADDR##_orig(Cpu* __restrict c);                                             \
    extern "C" void hook_##ADDR(Cpu* c) {                                                           \
        static hostrt::Root root{0x##ADDR##u, f_##ADDR, #IMPL, OUTS};                               \
        hostrt::run_calls(root, c, hostrt::IMPL##_##ADDR, f_##ADDR##_orig);                         \
    }

using namespace hostrt;

HOSTRT_ROOT(028F4114, floorf, F1)
HOSTRT_ROOT(028F41B0, ceilf, F1)
HOSTRT_ROOT(028F3B98, floor, F1)
HOSTRT_ROOT(028F543C, cvt_u64_f, F1)
HOSTRT_ROOT(028F597C, umod64, R3 | R4)
HOSTRT_ROOT(028F5680, cvt_f_s64, R3 | R4)
HOSTRT_ROOT(028F5678, cvt_f_u64, R3 | R4)
HOSTRT_CALLS_ROOT(028EFFD0, construct_array, R3 | R4)
HOSTRT_CALLS_ROOT(028EFF8C, vec_new, R3 | R4)
HOSTRT_CALLS_ROOT(028F0020, vec_new_zero, R3 | R4)
HOSTRT_CALLS_ROOT(028F0164, destroy_arr, R3 | R4)

// memory-writing roots that never hand back
namespace hostrt {
template <void (*F)(Cpu*)>
bool always(Cpu* c) {
    F(c);
    return true;
}
}  // namespace hostrt
#define HOSTRT_MEM_ROOT(ADDR, IMPL, OUTS)                                                           \
    extern "C" void f_##ADDR(Cpu* __restrict c);                                                    \
    extern "C" void f_##ADDR##_orig(Cpu* __restrict c);                                             \
    extern "C" void hook_##ADDR(Cpu* c) {                                                           \
        static hostrt::Root root{0x##ADDR##u, f_##ADDR, #IMPL, OUTS};                               \
        hostrt::run_calls(root, c, hostrt::always<hostrt::IMPL##_##ADDR>, f_##ADDR##_orig);         \
    }

HOSTRT_MEM_ROOT(028F026C, register_global_object, 0)
HOSTRT_MEM_ROOT(028F521C, bzero, R4)
HOSTRT_MEM_ROOT(028F5914, zero_words, R3)
HOSTRT_MEM_ROOT(028F040C, strcpy, R4)

// nn export thunks (calls into erreula/swkbd through the loader's table), trivial nn wrappers
HOSTRT_MEM_ROOT(0286CA30, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286CA40, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286CB2C, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286CB3C, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286CB4C, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286CB5C, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286CB6C, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286CBB4, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286D270, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286D2D4, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286D2E4, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286D3C0, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286D3D0, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286D3E0, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286D3F0, export_thunk, R3 | R4)
HOSTRT_MEM_ROOT(0286D400, export_thunk, R3 | R4)
HOSTRT_ROOT(0286CBC4, work_size, R3)
HOSTRT_ROOT(0286D5F0, empty, 0)
HOSTRT_MEM_ROOT(0286DA70, list_insert, R3)
HOSTRT_MEM_ROOT(0286DA9C, list_erase, R3)
HOSTRT_MEM_ROOT(0286DACC, list_erase_range, R3 | R4)
HOSTRT_MEM_ROOT(0286DB18, list_clear, R3 | R4)
HOSTRT_MEM_ROOT(028F5C88, va_arg, R3)

// printf family: formats with floating-point or wide conversions run the original
HOSTRT_CALLS_ROOT(028F1748, vsnprintf, R3 | R4)
HOSTRT_CALLS_ROOT(028F17A8, snprintf, R3 | R4)
HOSTRT_CALLS_ROOT(028F186C, sprintf, R3 | R4)
HOSTRT_MEM_ROOT(028EA038, memmove, 0)
HOSTRT_ROOT(028F35A8, div, R3 | R4)
HOSTRT_MEM_ROOT(028F2EE8, strtoul, R3)
HOSTRT_MEM_ROOT(028F3090, strtol, R3)
HOSTRT_ROOT(0286CD54, work_size, R3)
HOSTRT_MEM_ROOT(0286C590, heap_alloc, R3 | R4)
HOSTRT_MEM_ROOT(0286C9CC, erreula_destroy, R3 | R4)
HOSTRT_MEM_ROOT(0286D37C, swkbd_destroy, R3 | R4)
HOSTRT_MEM_ROOT(0286CA50, wrap, R3 | R4)
HOSTRT_MEM_ROOT(0286CAA4, wrap, R3 | R4)
HOSTRT_MEM_ROOT(0286CAE8, wrap, R3 | R4)
HOSTRT_MEM_ROOT(0286D280, wrap, R3 | R4)
HOSTRT_MEM_ROOT(0286D2F4, wrap, R3 | R4)
HOSTRT_MEM_ROOT(0286D338, wrap, R3 | R4)
HOSTRT_MEM_ROOT(0286CB7C, wrap, R3 | R4)
HOSTRT_MEM_ROOT(0286D620, sci_language, R3 | R4)
HOSTRT_MEM_ROOT(0286D938, sci_parental, R3 | R4)
HOSTRT_MEM_ROOT(0286D9E0, sci_parental, R3 | R4)
HOSTRT_MEM_ROOT(0286C5F8, erreula_create, R3 | R4)
HOSTRT_MEM_ROOT(0286CD7C, swkbd_create, R3 | R4)
HOSTRT_MEM_ROOT(028F0294, construct_array_arg, R3 | R4)
HOSTRT_MEM_ROOT(028EA09C, static_init, R3 | R4)
