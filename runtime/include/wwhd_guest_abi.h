/* WWHD guest mod module ABI v1 (Mod SDK v2, docs/mod-sdk-v2.md).
 *
 * A guest mod is PowerPC code. On install, tools/guestmod/build_guest_mod.py translates it to C
 * (tools/guestmod/guestmod.py) and compiles that with the player's local compiler into a module
 * (.dylib/.so/.dll). This header is the contract between such a module and the runtime
 * (runtime/src/mods/guest_mods.cpp). The module imports nothing from the executable: every runtime
 * entry point reaches it through WWHDGuestHostV1, so modules stay valid across game rebuilds and
 * work the same on every platform. Mod authors never include this file; they use
 * runtime/guest/include/wwhd_guest.h.
 */
#pragma once
#include "ppc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WWHD_GUEST_ABI_VERSION 2
/* ABI 2: imported service calls set Cpu::pc to their originating mod instruction. */

enum { WWHD_GUEST_REPLACE = 1, WWHD_GUEST_HOOK_ENTRY = 2, WWHD_GUEST_HOOK_RETURN = 3 };

typedef struct WWHDGuestHostV1 {
    uint32_t size, abi_version;
    void (*dispatch)(Cpu* c);                             /* call guest address c->pc */
    void (*unimplemented)(Cpu* c, uint32_t addr, uint32_t insn);
    void (*trap)(Cpu* c, uint32_t addr);
    uint64_t (*timebase)(void);
    double (*fres)(double);
    double (*frsqrte)(double);
    void (*preempt)(Cpu* c);
    volatile int* core_preempt;                           /* g_core_preempt[3] */
    void (*call_original)(Cpu* c, uint32_t func);         /* the game's code of func, below all mods */
    PpcFunc (*service)(const char* name);                 /* host service by name, or null */
} WWHDGuestHostV1;

typedef struct { uint32_t addr; PpcFunc fn; } WWHDGuestFunc;
typedef struct { uint32_t kind, target, func, flags; PpcFunc fn; } WWHDGuestHook;

typedef struct WWHDGuestModuleV1 {
    uint32_t size, abi_version;
    const char* translator;          /* translator version that produced the module */
    uint32_t mem_base, mem_size;     /* guest memory the mod occupies (code, data, bss) */
    const uint8_t* image;            /* initial bytes of [mem_base, mem_base + image_size), relocated */
    uint32_t image_size;
    const WWHDGuestFunc* funcs;      /* every mod function: callable through guest pointers */
    uint32_t func_count;
    const WWHDGuestHook* hooks;
    uint32_t hook_count;
} WWHDGuestModuleV1;

/* exported by every module; returns null when the host is incompatible */
typedef const WWHDGuestModuleV1* (*WWHDGuestInitV1)(const WWHDGuestHostV1* host);
#define WWHD_GUEST_INIT_SYMBOL "wwhd_guest_module_v1"

#ifdef __cplusplus
}
#endif
