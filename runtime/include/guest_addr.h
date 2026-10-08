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
#include <stdio.h>
#include <stdlib.h>

typedef struct { uint32_t start, end; } GuestChanged;
typedef struct { uint32_t start; int32_t delta; } GuestStep;  /* a run of addresses and its shift */

#ifdef __cplusplus
extern "C" {
#endif
extern const GuestStep g_guest_code_steps[];
extern const unsigned g_guest_code_step_count;
extern const GuestStep g_guest_data_steps[];
extern const unsigned g_guest_data_step_count;
extern const uint32_t g_guest_code_lo, g_guest_code_hi, g_guest_data_lo, g_guest_data_hi;
extern const GuestChanged g_guest_changed_code[];
extern const unsigned g_guest_changed_code_count;
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

/* Zero bounds identify the canonical/placeholder identity map. Other maps must
 * stay inside the verified executable; an interior of changed code has no map. */
static inline int guest_in_bounds(uint32_t a, uint32_t lo, uint32_t hi) {
    return (!lo && !hi) || (lo <= a && a < hi);
}
static inline int guest_code_valid(uint32_t a) {
    if (!guest_in_bounds(a, g_guest_code_lo, g_guest_code_hi)) return 0;
    for (unsigned i = 0; i < g_guest_changed_code_count; i++)
        if (g_guest_changed_code[i].start < a && a < g_guest_changed_code[i].end) return 0;
    return 1;
}
static inline int guest_data_valid(uint32_t a) {
    return guest_in_bounds(a, g_guest_data_lo, g_guest_data_hi);
}
static inline void guest_bad_address(uint32_t a, const char* kind) {
    fprintf(stderr, "[%s] unmapped canonical %s address %08X\n", g_guest_build_name, kind, a);
    abort();
}

/* canonical code address -> this build's */
static inline uint32_t guest_code(uint32_t canon) {
    if (!guest_code_valid(canon)) guest_bad_address(canon, "code");
    return guest_shift(g_guest_code_steps, g_guest_code_step_count, canon);
}

/* canonical data address -> this build's */
static inline uint32_t guest_data(uint32_t canon) {
    if (!guest_data_valid(canon)) guest_bad_address(canon, "data");
    return guest_shift(g_guest_data_steps, g_guest_data_step_count, canon);
}

/* Whether a range of canonical addresses keeps its length in this build: a range is only valid
 * when both ends shift by the same amount. */
static inline int guest_code_range_ok(uint32_t canon_lo, uint32_t canon_hi) {
    if (!guest_code_valid(canon_lo) || !guest_code_valid(canon_hi)) return 0;
    return (int32_t)(guest_code(canon_hi) - guest_code(canon_lo)) == (int32_t)(canon_hi - canon_lo);
}

#define GC(a) guest_code((uint32_t)(a))
#define GD(a) guest_data((uint32_t)(a))
