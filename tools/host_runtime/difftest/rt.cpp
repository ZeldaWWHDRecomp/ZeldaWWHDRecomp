// SPDX-License-Identifier: MPL-2.0
// Minimal runtime for the host-runtime difference tests: guest memory at PPC_MEM_BASE with the RPX image
// loaded, the symbols the recompiled originals need, and a log of the calls they make outside their
// closure (imports and other game functions), so the tests can compare those too.
#include "rt.h"

#include <sys/mman.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

extern "C" void hostrt_test_outside(Cpu* c, uint32_t addr);

extern "C" {
int g_ppc_trace = 0;
volatile int g_core_preempt[3] = {0, 0, 0};
uint8_t* g_mod_hook_flags = nullptr;
int wwhd_snan_on = 0;
void ppc_trace_enter(uint32_t) {}
void ppc_preempt(Cpu*) {}
void ppc_mod_run(Cpu*) { abort(); }
typedef struct { uint32_t addr; void (*fn)(Cpu*); } ClosureEntry;
extern const ClosureEntry hostrt_test_closure[] __attribute__((weak));
// indirect call: a function of the closure under test runs, anything else is an outside call
void ppc_dispatch(Cpu* c) {
    if (hostrt_test_closure)
        for (const ClosureEntry* e = hostrt_test_closure; e->fn; e++)
            if (e->addr == c->pc) return e->fn(c);
    hostrt_test_outside(c, c->pc);
}
void ppc_unimplemented(Cpu*, uint32_t addr, uint32_t insn) {
    fprintf(stderr, "difftest: unimplemented instruction %08X at %08X\n", insn, addr);
    abort();
}
void ppc_trap(Cpu*, uint32_t addr) {
    fprintf(stderr, "difftest: trap at %08X\n", addr);
    abort();
}
uint64_t ppc_timebase(void) { return 0; }
void ppc_cr_poisoned(Cpu*, int, uint32_t) { abort(); }
}

namespace difftest {

thread_local CallLog* t_log = nullptr;
thread_local uint32_t t_entry_sp = 0;

Call make_call(Cpu* c, uint32_t addr, const char* import) {
    Call k{};
    k.addr = addr, k.import = import;
    for (int i = 0; i < 11; i++) k.r[i] = c->r[i];
    k.r[1] = t_entry_sp - c->r[1];
    for (int i = 0; i < 9; i++) k.f[i] = f64_as_u64(c->f[i].ps0);
    for (int i = 0; i < 4; i++) k.stack[i] = ld32(c->r[1] + 8 + 4 * i);
    k.lr = c->lr;
    k.back = t_entry_sp - ld32(c->r[1]);
    return k;
}

void load_image(const char* path) {
    void* p = mmap(PPC_MEM_BASE, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED | MAP_NORESERVE, -1, 0);
    if (p != PPC_MEM_BASE) {
        perror("difftest: mmap guest memory");
        exit(1);
    }
    FILE* f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    uint32_t hdr[2];
    while (fread(hdr, 4, 2, f) == 2) {  // little-endian (addr, size) + bytes, tools/verify/mkimage.py
        if (fread(PPC_MEM_BASE + hdr[0], 1, hdr[1], f) != hdr[1]) {
            fprintf(stderr, "difftest: short image %s\n", path);
            exit(1);
        }
    }
    fclose(f);
}

void reset(Cpu* c, int slot) {
    memset(c, 0, sizeof *c);
    c->r[1] = stack_top(slot);
    c->lr = 0x00000004u;  // never returned to: the host function returns
    c->r[2] = 0x10200000u;  // SDA2 base is not used by the runtime library; any fixed value
    c->r[13] = 0x10200000u;
}

}  // namespace difftest

using namespace difftest;

extern "C" void hostrt_test_import(Cpu* c, const char* name) {
    if (t_log) t_log->push_back(make_call(c, 0, name));
    if (!strcmp(name, "coreinit___gh_set_errno")) return;  // the port's HLE is a no-op too
    if (!strcmp(name, "coreinit_memset")) {
        memset(PPC_MEM_BASE + c->r[3], (int)(c->r[4] & 0xFF), c->r[5]);
        return;  // r3 (the destination) is returned unchanged
    }
    if (!strcmp(name, "coreinit_memcpy") || !strcmp(name, "coreinit_memmove")) {
        memmove(PPC_MEM_BASE + c->r[3], PPC_MEM_BASE + c->r[4], c->r[5]);
        return;
    }
    if (import_hook && import_hook(c, name)) return;
    fprintf(stderr, "difftest: import %s not modelled\n", name);
    abort();
}

extern "C" void hostrt_test_outside(Cpu* c, uint32_t addr) {
    if (t_log) t_log->push_back(make_call(c, addr, nullptr));
    if (outside_hook && outside_hook(c, addr)) return;
    fprintf(stderr, "difftest: call to game function %08X not modelled\n", addr);
    abort();
}

namespace difftest {
bool (*import_hook)(Cpu*, const char*) = nullptr;
bool (*outside_hook)(Cpu*, uint32_t) = nullptr;
}  // namespace difftest

// the port's memset for replacements that call it, in a closure that does not reference it
extern "C" __attribute__((weak)) void imp_coreinit_memset(Cpu* c) { hostrt_test_import(c, "coreinit_memset"); }
extern "C" __attribute__((weak)) void imp_coreinit___gh_set_errno(Cpu* c) { hostrt_test_import(c, "coreinit___gh_set_errno"); }
extern "C" __attribute__((weak)) void imp_coreinit_MEMAllocFromExpHeapEx(Cpu* c) { hostrt_test_import(c, "coreinit_MEMAllocFromExpHeapEx"); }
extern "C" __attribute__((weak)) void imp_coreinit_MEMDestroyExpHeap(Cpu* c) { hostrt_test_import(c, "coreinit_MEMDestroyExpHeap"); }
extern "C" __attribute__((weak)) void imp_coreinit_OSDynLoad_Release(Cpu* c) { hostrt_test_import(c, "coreinit_OSDynLoad_Release"); }
extern "C" __attribute__((weak)) void imp_coreinit_UCOpen(Cpu* c) { hostrt_test_import(c, "coreinit_UCOpen"); }
extern "C" __attribute__((weak)) void imp_coreinit_UCReadSysConfig(Cpu* c) { hostrt_test_import(c, "coreinit_UCReadSysConfig"); }
extern "C" __attribute__((weak)) void imp_coreinit_UCClose(Cpu* c) { hostrt_test_import(c, "coreinit_UCClose"); }
extern "C" __attribute__((weak)) void imp_coreinit_OSReport(Cpu* c) { hostrt_test_import(c, "coreinit_OSReport"); }
extern "C" __attribute__((weak)) void imp_nn_act_GetParentalControlSlotNoEx__Q2_2nn3actFPUcUc(Cpu* c) { hostrt_test_import(c, "nn_act_GetParentalControlSlotNoEx__Q2_2nn3actFPUcUc"); }
extern "C" __attribute__((weak)) void imp_coreinit_MEMCreateExpHeapEx(Cpu* c) { hostrt_test_import(c, "coreinit_MEMCreateExpHeapEx"); }
extern "C" __attribute__((weak)) void imp_coreinit_MEMGetAllocatableSizeForExpHeapEx(Cpu* c) { hostrt_test_import(c, "coreinit_MEMGetAllocatableSizeForExpHeapEx"); }
extern "C" __attribute__((weak)) void imp_coreinit_OSDynLoad_SetAllocator(Cpu* c) { hostrt_test_import(c, "coreinit_OSDynLoad_SetAllocator"); }
extern "C" __attribute__((weak)) void imp_coreinit_OSDynLoad_Acquire(Cpu* c) { hostrt_test_import(c, "coreinit_OSDynLoad_Acquire"); }
extern "C" __attribute__((weak)) void imp_coreinit_OSDynLoad_FindExport(Cpu* c) { hostrt_test_import(c, "coreinit_OSDynLoad_FindExport"); }
