/* Canonical game addresses -> the addresses of the build this port was recompiled from.
 *
 * Every game address written in the port's source is an address of the USA build: the canonical id
 * of that function or that global (see tools/recomp/builds.py). Another regional build of the same
 * game has the same code and the same globals at slightly different addresses, so an address that
 * is used as a value at run time -- a function pointer the game stores, the address of a global the
 * port reads -- has to be translated: GC() for code, GD() for data.
 *
 * The tables are emitted into the recompiled code (build/gen/table.c) because they belong to the
 * executable the code was translated from; the runtime, which ships prebuilt for every build, falls
 * back to the identity when it is linked without them (runtime/src/guest_addr_identity.c).
 *
 * Addresses that are *not* used as values need no translation: the recompiled code names its
 * functions by their canonical address (f_XXXXXXXX), and hooks and sites in tools/recomp/hooks*.txt
 * are translated by the recompiler.
 */
#pragma once
#include <stdint.h>

typedef struct { uint32_t start; int32_t delta; } GuestStep;  /* a run of addresses and its shift */

#ifdef __cplusplus
extern "C" {
#endif
extern const GuestStep g_guest_code_steps[];
extern const unsigned g_guest_code_step_count;
extern const GuestStep g_guest_data_steps[];
extern const unsigned g_guest_data_step_count;
extern const char g_guest_build_name[];     /* "USA", "EU", ... */
extern const char g_guest_build_title_id[];
#ifdef __cplusplus
}
#endif

static inline uint32_t guest_shift(const GuestStep* steps, unsigned n, uint32_t canon) {
    uint32_t out = canon;
    for (unsigned i = 0; i < n && steps[i].start <= canon; i++) out = canon + (uint32_t)steps[i].delta;
    return out;
}

/* canonical code address -> this build's */
static inline uint32_t guest_code(uint32_t canon) {
    return guest_shift(g_guest_code_steps, g_guest_code_step_count, canon);
}

/* canonical data address -> this build's */
static inline uint32_t guest_data(uint32_t canon) {
    return guest_shift(g_guest_data_steps, g_guest_data_step_count, canon);
}

/* Whether a range of canonical addresses keeps its length in this build: a range is only valid
 * when both ends shift by the same amount. */
static inline int guest_code_range_ok(uint32_t canon_lo, uint32_t canon_hi) {
    return (int32_t)(guest_code(canon_hi) - guest_code(canon_lo)) == (int32_t)(canon_hi - canon_lo);
}

#define GC(a) guest_code((uint32_t)(a))
#define GD(a) guest_data((uint32_t)(a))
