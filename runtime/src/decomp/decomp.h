// Decomp build (CMake WWHD_DECOMP=ON, docs/decomp-build.md): the game functions of the decompilation
// run as compiled C++ in place of the recompiled code. Every decompiled function has a slot (generated
// hooks.cpp, tools/decomp_build/gen.py); its hook_X entry runs the decompiled code or, when the slot is
// off, the recompiled original f_X_orig.
//
// By default every slot is on, except functions a guest mod hooks (the mod's check sits in the
// recompiled original) and the ones the recompiler kept (g_recomp_decomp_kept). Debug switches, all off
// by default (decomp.cpp): WWHD_DECOMP, WWHD_DECOMP_FUNCS, WWHD_DECOMP_EXCLUDE, WWHD_DECOMP_STATS,
// WWHD_DECOMP_GUARD, WWHD_DECOMP_FRAMES.
#pragma once
#include <cstdint>
#include <cstring>

#include "decomp/call_lr.h"
#include "ppc.h"  // g_mod_hook_flags

namespace decomp {
struct Slot {
    uint32_t addr;
    uint16_t unit;
    const char* name;
    PpcFunc orig;                          // the recompiled function (f_X_orig)
    PpcFunc cand = nullptr;                // the decompiled function's gabi entry, registered by its unit
    void (*run)(Cpu*, PpcFunc) = nullptr;  // the unit's wrapper (tools/decomp_build/glue.cpp)
    uint8_t on = 0;
    uint8_t frame_exact = 0;               // 1: kFrameExact lists it (no direct calls into or from it)
    uint32_t ordinal = 0;                  // in the direct table (direct_init): its DirectEntry::ordinal
    uint8_t hooked = 0;                    // fast mode (gen.py --hooked-bodies): the runtime hooks it; entered only
                                           // behind its hook_X (hookorig_X), never directly or natively
};
struct UnitAlias {
    const char* name;  // a verify unit
    int unit;          // its game unit
};
// generated (hooks.cpp)
extern const char* const kUnits[];
extern const unsigned kNumUnits;
extern const UnitAlias kAliases[];
extern const unsigned kNumAliases;
extern Slot g_slots[];
extern const unsigned kNumSlots;
// the decompiled functions whose guest frames must be exactly the original's (a `framexact FUNC RET` line in
// a unit spec of the decompilation: cross-build save states compare the parked threads' chains)
extern const uint32_t kFrameExact[];
extern const unsigned kNumFrameExact;

// sets up the slots (environment, guest mods, frames); main.cpp, after the game's code is loaded and
// the guest mods are registered, before any guest thread runs
void init();

}  // namespace decomp
// the decompiled function whose code runs on a Cpu is Cpu::decomp_running (ppc.h): its slot index + 1 (0: none; its
// Slot::frame_exact: its guest calls take the dispatch table). gmem_call sets LR for a guest call from it to the return
// address of the original's call instruction (decomp.cpp call_lr). Every entry into a decompiled body sets it on the
// Cpu that body then runs on (decomp::call, call_debug, gmem_call's direct slot, decomp_lcall, gabi.h NativeScope) and
// restores the caller's value from a local on exit; it is read only inside such a body, through that same Cpu.
// native calls (gabi.h WWHD_GLUE_FUNC, game build): per slot, 1 while the slot may be entered natively (on,
// routed to its f_X, not frame-exact, not hooked by a guest mod, no debug switch, WWHD_DECOMP_NATIVE not 0);
// null: no native calls. Cleared, never set again, while the game runs (decomp::mod_hooked).
extern "C" __attribute__((visibility("default"))) uint8_t* g_decomp_native_ok;
// real native calls (gabi.h NativeReg / lcall): per slot a register-exact function's native entry and its signature
// (0: not entered that way; cleared by decomp_mod_hooked); null: none. g_decomp_exact bit 0: register-exact native
// entries pass no guest registers, bit 1: they poison them (WWHD_DECOMP_POISON=1).
struct GabiNativeEntry {
    void* fn;
    uint32_t sig;
    uint32_t reserved;
};
extern "C" __attribute__((visibility("default"))) GabiNativeEntry* g_decomp_native_entry;
#ifdef WWHD_DECOMP_FAST
// fast mode (gabi.h GABI_FAST, the same layout): the enabled native entries by guest address, for call_ptr / vtables
struct GabiPtrEntry {
    uint32_t addr;
    uint32_t sig;
    void* fn;
};
extern "C" __attribute__((visibility("default"))) const GabiPtrEntry* g_decomp_ptr_tab;
extern "C" __attribute__((visibility("default"))) uint32_t g_decomp_ptr_mask;
extern "C" __attribute__((visibility("default"))) const GabiPtrEntry* g_decomp_call_tab;
extern "C" __attribute__((visibility("default"))) const PpcFunc* g_decomp_hle_tab;
#ifdef WWHD_DECOMP_FAST_MUTEX
using GabiHleDirect = uint32_t (*)(Cpu* c, uint32_t r3);  // gabi.h GABI_FAST_MUTEX
extern "C" __attribute__((visibility("default"))) const GabiHleDirect* g_decomp_hle_direct;
#endif
#endif
extern "C" __attribute__((visibility("default"))) uint8_t g_decomp_exact;
#ifdef WWHD_DECOMP_FAST_A3_FN
extern "C" __attribute__((visibility("default"))) uint8_t g_decomp_a3_off;  // gabi.h GABI_A3_FN (decomp.cpp)
#endif
#ifdef WWHD_DECOMP_FAST_LEAF
extern "C" __attribute__((visibility("default"))) uint8_t g_decomp_leaf_off;  // gabi.h GABI_LEAF (decomp.cpp)
#endif
#ifdef WWHD_DECOMP_FAST_NSTATS
extern "C" __attribute__((visibility("default"))) uint64_t* g_decomp_nstats;  // gabi.h GABI_NSTATS (decomp.cpp)
#endif
#ifdef WWHD_DECOMP_FAST_PTR_SUB
extern "C" __attribute__((visibility("default"))) uint8_t g_decomp_ptr_sub;  // gabi.h GABI_PTR_SUB (decomp.cpp)
#endif
// a guest mod or the port hooks the function with recompiled-table ordinal `ordinal` (every g_mod_hook_flags
// write that sets a flag): it is no longer entered natively
extern "C" void decomp_mod_hooked(uint32_t ordinal);
namespace decomp {

// debug only (0 unless WWHD_DECOMP_STATS / WWHD_DECOMP_GUARD are set)
extern int g_debug;
void call_debug(Cpu* c, Slot& s);

inline void call(Cpu* c, int i) {
    Slot& s = g_slots[i];
    if (__builtin_expect(g_debug, 0)) return call_debug(c, s);
    if (s.on) {
        const uint16_t outer = c->decomp_running;
        c->decomp_running = uint16_t(&s - g_slots + 1);
        s.cand(c);  // the gabi entry (Entry::run: EntryScope keeps cpu, depth, LR and Activation::current itself)
        c->decomp_running = outer;
    } else {
        s.orig(c);
    }
}

// Direct calls (gmem_call, decomp.cpp direct_init): a guest call from decompiled code to a function whose
// slot is on enters that function's unit wrapper straight away instead of going through the dispatch table
// (ppc_dispatch -> f_X -> ppc_host_call -> hook_X -> call), with the same guest state: registers, r1, LR and
// memory are what the decompiled callee sees either way, and an exception still reaches the outer
// ppc_host_call boundary. Kept on the dispatch table: frame-exact functions (kFrameExact: every call into
// one and every guest call made by one, unless WWHD_DECOMP_DIRECT=all, a measurement switch), slots that are off, debug runs (call_debug), an
// unsealed dispatch table, an entry the table no longer routes to f_X, and a function a guest mod or the
// port hooks after start-up (its g_mod_hook_flags byte).
struct DirectEntry {
    uint32_t addr = 0;      // 0: empty
    uint32_t ordinal = 0;   // index in g_recomp_funcs (g_mod_hook_flags)
    Slot* slot = nullptr;
};
constexpr uint32_t kDirectSize = 1u << 17;  // open addressing, power of two, > 3x the slot count
extern DirectEntry* g_direct;                // null: no direct calls
inline uint32_t direct_hash(uint32_t addr) { return ((addr >> 2) * 0x9E3779B1u) >> (32 - 17); }
inline const DirectEntry* direct_entry(uint32_t addr) {  // direct_slot's entry, whatever the mod flags say
    const DirectEntry* t = g_direct;
    if (!t) return nullptr;
    for (uint32_t h = direct_hash(addr);; h = (h + 1) & (kDirectSize - 1)) {
        if (t[h].addr == addr) return t[h].addr ? &t[h] : nullptr;
        if (!t[h].addr) return nullptr;
    }
}
inline Slot* direct_slot(uint32_t addr) {
    const DirectEntry* t = g_direct;
    if (!t) return nullptr;
    for (uint32_t h = direct_hash(addr);; h = (h + 1) & (kDirectSize - 1)) {
        const DirectEntry& e = t[h];
        if (e.addr == addr) {
            if (__builtin_expect(g_mod_hook_flags && g_mod_hook_flags[e.ordinal], 0)) return nullptr;
            return e.slot;
        }
        if (!e.addr) return nullptr;
    }
}

// the LR a guest call from decompiled function `caller` to `target` (kind 1: through CTR) gets: the return
// address after the original's call instruction when the original has exactly one such call, else `lr`
// (unchanged); the scan of the original's code is call_lr.h, made once per function at start-up (init)
struct LrSites {
    const uint32_t* keys = nullptr;  // call_lr_sites keys, ascending
    const uint32_t* rets = nullptr;
    uint32_t n = 0;
};
extern LrSites* g_lr_sites;  // per slot (g_slots order)
inline uint32_t call_lr(uint32_t caller, uint32_t target, int kind, uint32_t lr) {  // caller: slot index
    const LrSites& l = g_lr_sites[caller];
    return call_lr_find(l.keys, l.rets, l.n, target, kind, lr);
}

// call_lr and direct_slot by call site (call_lr.h CallSite; decomp.cpp sites_init): per slot, the targets of the
// original's calls with both answers, made once at init after direct_init; null: none (WWHD_DECOMP_SITES=0, or
// before init), every guest call asks call_lr and direct_slot
extern CallerSites* g_callers;
// a guest call from slot index `caller` to `target` (kind 1: through CTR): the LR it gets (`lr` when call_lr leaves it
// unchanged) and the slot it enters directly (null: the dispatch table), as gmem_call takes them
struct CallTo {
    uint32_t lr;
    Slot* s;
};
inline CallTo call_to_lookup(uint32_t caller, uint32_t target, int kind, uint32_t lr) {  // call_lr + direct_slot
    return {call_lr(caller, target, kind, lr), g_slots[caller].frame_exact ? nullptr : direct_slot(target)};
}
inline CallTo call_to(uint32_t caller, uint32_t target, int kind, uint32_t lr) {
    CallerSites* cs = g_callers;
    if (!cs) return call_to_lookup(caller, target, kind, lr);
    const CallerSites& k = cs[caller];
    if (kind == 1) {
        // the direct table's answer for the caller's last bctrl target ({target, slot + 1}; one relaxed word: threads
        // that race only miss); the mod flag through the slot's ordinal, as direct_slot checks the entry's
        if (k.frame_exact) return {k.bctrl_lr != 1u ? k.bctrl_lr : lr, nullptr};
        const uint64_t m = __atomic_load_n(&cs[caller].ind, __ATOMIC_RELAXED);
        uint32_t s1;
        if (uint32_t(m >> 32) == target && m) {
            s1 = uint32_t(m);
        } else {
            const DirectEntry* d = direct_entry(target);
            s1 = d ? uint32_t(d->slot - g_slots) + 1 : 0;
            __atomic_store_n(&cs[caller].ind, uint64_t(target) << 32 | s1, __ATOMIC_RELAXED);
        }
        Slot* s = s1 && !(g_mod_hook_flags && g_mod_hook_flags[g_slots[s1 - 1].ordinal]) ? g_slots + (s1 - 1) : nullptr;
        return {k.bctrl_lr != 1u ? k.bctrl_lr : lr, s};
    }
    const CallSite* e = site_find(k, target);
    if (!e) return call_to_lookup(caller, target, kind, lr);
    Slot* s = nullptr;
    if (e->slot1 && !k.frame_exact && !(g_mod_hook_flags && g_mod_hook_flags[e->ordinal])) s = g_slots + (e->slot1 - 1);
    return {e->lr != 1u ? e->lr : lr, s};
}
inline uint32_t call_lr_by_site(uint32_t caller, uint32_t target, uint32_t lr) {  // call_lr(caller, target, 0, lr)
    const CallerSites* cs = g_callers;
    const CallSite* e = cs ? site_find(cs[caller], target) : nullptr;
    if (!e) return call_lr(caller, target, 0, lr);
    return e->lr != 1u ? e->lr : lr;
}
}  // namespace decomp
