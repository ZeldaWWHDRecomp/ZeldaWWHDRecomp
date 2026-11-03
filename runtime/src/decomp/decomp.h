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

#include "ppc.h"

namespace decomp {
struct Slot {
    uint32_t addr;
    uint16_t unit;
    const char* name;
    PpcFunc orig;                          // the recompiled function (f_X_orig)
    PpcFunc cand = nullptr;                // the decompiled function's gabi entry, registered by its unit
    void (*run)(Cpu*, PpcFunc) = nullptr;  // the unit's wrapper (tools/decomp_build/glue.cpp)
    uint8_t on = 0;
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

// sets up the slots (environment, guest mods, frames); main.cpp, after the game's code is loaded and
// the guest mods are registered, before any guest thread runs
void init();

// the decompiled function whose code runs on this thread (0: none). gmem_call sets LR for a guest call
// from it to the return address of the original's call instruction (decomp.cpp call_lr).
extern thread_local uint32_t t_running;

// debug only (0 unless WWHD_DECOMP_STATS / WWHD_DECOMP_GUARD are set)
extern int g_debug;
void call_debug(Cpu* c, Slot& s);

inline void call(Cpu* c, int i) {
    Slot& s = g_slots[i];
    if (__builtin_expect(g_debug, 0)) return call_debug(c, s);
    if (s.on) {
        const uint32_t outer = t_running;
        t_running = s.addr;
        s.run(c, s.cand);
        t_running = outer;
    } else {
        s.orig(c);
    }
}

// the LR a guest call from decompiled function `caller` to `target` (kind 1: through CTR) gets: the return
// address after the original's call instruction when the original has exactly one such call, else `lr`
// (unchanged); the scan of the original's code is call_lr.h
uint32_t call_lr(uint32_t caller, uint32_t target, int kind, uint32_t lr);
}  // namespace decomp
