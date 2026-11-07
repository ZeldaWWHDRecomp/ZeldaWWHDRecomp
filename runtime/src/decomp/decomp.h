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
// the decompiled function whose code runs on this thread: its slot index + 1 (0: none; its Slot::frame_exact:
// its guest calls take the dispatch table). gmem_call sets LR for a guest call from it to the return address of
// the original's call instruction (decomp.cpp call_lr). constinit: a plain thread-local access, no wrapper
// function. C linkage, default visibility: decompiled code's native calls (gabi.h NativeScope) set it too.
extern "C" __attribute__((visibility("default"))) thread_local constinit uint32_t g_decomp_running;
// native calls (gabi.h WWHD_GLUE_FUNC, game build): per slot, 1 while the slot may be entered natively (on,
// routed to its f_X, not frame-exact, not hooked by a guest mod, no debug switch, WWHD_DECOMP_NATIVE not 0);
// null: no native calls. Cleared, never set again, while the game runs (decomp::mod_hooked).
extern "C" __attribute__((visibility("default"))) uint8_t* g_decomp_native_ok;
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
        const uint32_t outer = g_decomp_running;
        g_decomp_running = uint32_t(&s - g_slots) + 1;
        s.run(c, s.cand);
        g_decomp_running = outer;
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
}  // namespace decomp
