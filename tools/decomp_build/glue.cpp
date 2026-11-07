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

gabi::Candidate::Candidate(u32 a, const char* n, EntryFn f, RetKind r) : addr(a), name(n), fn(f), ret(r), next(nullptr) {
    decomp_register(DECOMP_UNIT, a, n, f, decomp_run);
}
