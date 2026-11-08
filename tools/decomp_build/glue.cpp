/* Decomp build: per-unit glue (tools/decomp_build/gen.py writes glue/N.cpp, which defines DECOMP_UNIT
 * and includes this file; it is compiled into every game unit).
 *
 * A unit is linked with `ld -r` and hidden visibility (CMakeLists.txt, WWHD_DECOMP), so the
 * definitions here are the unit's own: its gabi::cpu, its Activation depth (gabi.h inline variable)
 * and its Candidate constructor, which registers each VERIFY'd function with the runtime
 * (runtime/src/decomp/decomp.cpp). Nothing else of the unit is visible outside it: units never see
 * each other's classes, inline helpers or gabi state, as in the verification harness, where every
 * unit is its own program.
 */
#include "gabi.h"

extern "C" void decomp_register(const char* unit, uint32_t addr, const char* name, void (*fn)(Cpu*),
                                void (*run)(Cpu*, void (*)(Cpu*)));

/* gabi.h with GABI_GAME_BUILD has its per-thread state in one inline block (GABI_TLS_BLOCK) */
#ifndef GABI_TLS_BLOCK
namespace gabi {
thread_local Cpu* cpu;
}
#endif

/* Runs one candidate entry (gabi Entry::run). Entry::run keeps the caller's gabi::cpu, Activation
 * depth and LR itself (gabi.h EntryScope): a candidate's guest call can reach another candidate of
 * the same unit through recompiled code, unlike in the harness. */
static void decomp_run(Cpu* c, void (*fn)(Cpu*)) {
    uint32_t sp = c->r[1];
    /* the unit's function under test (WWHD_GLUE_FUNC; which `in FUNC` string a literal means): a nested
     * call of the same unit's decompiled code sets it, the harness never does, so it is restored here */
    uint32_t current = gabi::Activation::current;
    fn(c);
    gabi::Activation::current = current;
    if (c->r[1] != sp) c->r[1] = sp; /* cannot happen (gabi::Local/do_call restore r1); kept as a guard */
}

/* real native calls (gabi.h GABI_SHARED_TLS): one thread-local block for every game unit, defined by unit 0's glue
 * (gen.py: GABI_TLS_OWNER), with default visibility so that `ld -r` keeps it global */
#if defined(GABI_SHARED_TLS) && defined(GABI_TLS_OWNER)
namespace gabi {
#ifdef GABI_SITE_LR
__attribute__((visibility("default"))) thread_local constinit ThreadState tls{nullptr, 0, 0, nullptr, 0, {}};
#else
__attribute__((visibility("default"))) thread_local constinit ThreadState tls{nullptr, 0, 0, nullptr, 0};
#endif
}
#endif

/* GABI_TP_CACHE (gabi.h, gabi_tp_cache.h; level 1 step 1a): the thread-pointer table and its miss path, once (unit 0) */
#if defined(GABI_TP_CACHE) && defined(GABI_TLS_OWNER)
extern "C" __attribute__((visibility("protected"))) GabiTpSlot g_gabi_tp_slots[GABI_TP_SLOTS] = {};
namespace {
thread_local bool tp_dead = false; /* trivially destructible: still readable after the guard ran */
thread_local GabiTpGuard tp_guard;  /* clears this thread's slot at thread exit */
void tp_fork_child() { gabi_tp_fork_child(g_gabi_tp_slots); }
}
extern "C" __attribute__((visibility("default"))) gabi::ThreadState* gabi_tp_miss() {
    static const int atfork = pthread_atfork(nullptr, nullptr, tp_fork_child);
    (void)atfork;
    gabi::ThreadState* p = &gabi::tls;
    /* live blocks only (gabi_tp_cache.h): a thread inside a guest entry; a block handed out again during thread exit
     * is zero */
    if (p->cpu) gabi_tp_claim(g_gabi_tp_slots, gabi_tp_key(), p, tp_guard, tp_dead);
    return p;
}
#endif

#if defined(GABI_FAST) && defined(GABI_TLS_OWNER)
/* fast mode (runtime/src/hostrt/hostrt_cxx.cpp): may host code running for decompiled code on Cpu C (a host runtime
 * root it called) call a decompiled function's native entry now: this thread's block is inside a decompiled body on C
 * whose own calls may go native (the conditions a native call from that body itself would check) */
extern "C" __attribute__((visibility("default"))) int gabi_fast_native_ctx(Cpu* c) {
    const gabi::ThreadState& t = GABI_TLS_;
    return t.cpu == c && t.depth > 0 && t.caller_ok;
}
/* fast mode (runtime/src/hostrt/hostrt_fast.cpp): the Cpu the calling decompiled body runs on (its native call of a
 * host runtime root leaves the root's result registers there, as the guest call does) */
extern "C" __attribute__((visibility("default"))) Cpu* gabi_fast_cpu() { return GABI_TLS_.cpu; }
#endif

#ifdef GABI_GAME_BUILD
/* a register-exact function's native entry (gabi.h NativeReg; runtime/src/decomp/decomp.cpp) */
#ifdef GABI_SHARED_TLS
void gabi::native_register(u32 a, void* fn, u32 sig, u32 table_sig) { decomp_register_native(DECOMP_UNIT, a, fn, sig, table_sig); }
#else  /* a unit's own thread-local block: a call from another unit would not find its Cpu (no entry; measurement only) */
void gabi::native_register(u32 a, void*, u32 sig, u32 table_sig) { decomp_register_native(DECOMP_UNIT, a, nullptr, sig, table_sig); }
#endif
#endif

gabi::Candidate::Candidate(u32 a, const char* n, EntryFn f, RetKind r) : addr(a), name(n), fn(f), ret(r), next(nullptr) {
    decomp_register(DECOMP_UNIT, a, n, f, decomp_run);
}
