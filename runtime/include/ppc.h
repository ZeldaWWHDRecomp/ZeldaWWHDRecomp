/* Espresso (Wii U PowerPC) CPU state and helpers used by recompiled code.
 *
 * Guest memory is a 4 GiB window mapped at a fixed host address, so a guest
 * effective address converts to a host pointer with a single add.
 */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__ANDROID__) || (defined(__linux__) && defined(__aarch64__))
/* arm64 Linux kernels (Android, Raspberry Pi OS and other 4K-page configurations) often have a 39-bit
   user address space (512 GiB), where 32 TiB is out of reach: stay well below it (64 GiB) */
#define PPC_MEM_BASE ((uint8_t*)0x1000000000ull)
#else
#define PPC_MEM_BASE ((uint8_t*)0x200000000000ull)
#endif

typedef struct Cpu {
    uint32_t r[32];
    uint32_t lr, ctr;
    uint8_t cr[32];         /* one byte per CR bit; bit 4n+0 = crN.lt, +1 gt, +2 eq, +3 so */
    uint8_t xer_so, xer_ov, xer_ca;
    uint8_t xer_bc;
    struct { double ps0, ps1; } f[32];
    uint32_t fpscr;
    uint32_t gqr[8];
    uint32_t res_addr, res_val; /* lwarx/stwcx. reservation */
    uint32_t pc;               /* target for indirect dispatch */
    uint32_t core;             /* host-side: which emulated core this thread runs on */
    uint32_t mod_skip;         /* guest mods: the next entry of this function runs its original code
                                  (fills former padding: sizeof(Cpu) and save states are unchanged) */
    void* thread;              /* host-side: owning guest thread object */
} Cpu;

typedef void (*PpcFunc)(Cpu*);

/* runtime entry points */
void ppc_host_call(Cpu* c, PpcFunc fn);           /* guarded native hook/site entry */
void ppc_dispatch(Cpu* c);                       /* call/jump to c->pc */
void ppc_unimplemented(Cpu* c, uint32_t addr, uint32_t insn);
void ppc_trap(Cpu* c, uint32_t addr);
uint64_t ppc_timebase(void);
double ppc_fres(double x);
double ppc_frsqrte(double x);

#define MUSTTAIL __attribute__((musttail))

/* optional guest function-entry trace (runtime switch, see runtime/src/trace.cpp) */
extern int g_ppc_trace;
void ppc_trace_enter(uint32_t addr);
/* per-core scheduling: a higher-priority thread on this core is ready, yield at the next function entry */
extern volatile int g_core_preempt[3];
void ppc_preempt(Cpu* c);
#define PPC_ENTER(a) do {                                                     \
        if (__builtin_expect(g_ppc_trace, 0)) ppc_trace_enter(a);             \
        if (__builtin_expect(g_core_preempt[c->core], 0)) ppc_preempt(c);     \
    } while (0)

/* guest mods (docs/mod-sdk-v2.md; game code generated with recomp.py --mod-hooks): every function body
   checks its flag byte; a set flag means a mod hooks or replaces it, and ppc_mod_run (c->pc = the
   function) runs the mods' hooks and the replacement or the original. The mod runtime calls the
   original code by setting c->mod_skip first. Code without --mod-hooks emits no check or hook metadata. */
#if defined(__GNUC__) && !defined(_WIN32)
__attribute__((visibility("hidden")))
#endif
extern uint8_t* g_mod_hook_flags;
void ppc_mod_run(Cpu* c);
/* Do not mark this branch unlikely: Apple clang 17 can outline a cold hook
   return into an i1-returning helper, invalidating the void musttail call.
   Keep the entry branch ordinary so musttail stays in its original function. */
#define PPC_MOD_HOOK(i, a) do {                                               \
        if (g_mod_hook_flags[i]) {                                           \
            if (c->mod_skip != (a)) { c->pc = (a); MUSTTAIL return ppc_mod_run(c); } \
            c->mod_skip = 0;                                                  \
        }                                                                     \
    } while (0)

/* loop back-edge (every backward branch inside a function): a compiler barrier. Guest memory is
   shared with the other guest threads, but the generated loads are plain loads, and Cpu is
   __restrict, so without it the compiler may load a guest word once before a call-free loop and spin
   on the stale value forever: games busy-wait on locks and flags (`while (*lock == 1);`) that another
   core changes (issue #62: LLVM 16/17, e.g. Apple clang 16 of Xcode 16, turn such a wait into
   `b .`). The barrier only forces guest memory to be read again on the next iteration; the register
   file stays in host registers (it is __restrict and not an operand). */
#define PPC_LOOP() __asm__ __volatile__("" ::: "memory")

/* ---- memory ---- */
static inline uint8_t* ppc_ptr(uint32_t ea) { return PPC_MEM_BASE + ea; }
static inline uint8_t ld8(uint32_t ea) { return *ppc_ptr(ea); }
static inline uint16_t ld16(uint32_t ea) { uint16_t v; memcpy(&v, ppc_ptr(ea), 2); return __builtin_bswap16(v); }
static inline uint32_t ld32(uint32_t ea) { uint32_t v; memcpy(&v, ppc_ptr(ea), 4); return __builtin_bswap32(v); }
static inline uint64_t ld64(uint32_t ea) { uint64_t v; memcpy(&v, ppc_ptr(ea), 8); return __builtin_bswap64(v); }
static inline void st8(uint32_t ea, uint8_t v) { *ppc_ptr(ea) = v; }
static inline void st16(uint32_t ea, uint16_t v) { v = __builtin_bswap16(v); memcpy(ppc_ptr(ea), &v, 2); }
static inline void st32(uint32_t ea, uint32_t v) { v = __builtin_bswap32(v); memcpy(ppc_ptr(ea), &v, 4); }
static inline void st64(uint32_t ea, uint64_t v) { v = __builtin_bswap64(v); memcpy(ppc_ptr(ea), &v, 8); }

/* ---- bit casts ---- */
static inline double u64_as_f64(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static inline uint64_t f64_as_u64(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static inline float u32_as_f32(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint32_t f32_as_u32(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* ---- signalling-NaN counter (local diagnostic, CMake -DWWHD_SNAN_COUNT=ON, logs with WWHD_SNAN_COUNT=1):
 * counts every float load/store of a signalling NaN (which the host float<->double conversions used to quiet;
 * ldf32 / stf32 / psq_* now move it bit-exactly), per call site (snan_count.cpp, captures/snan.log).
 * Not compiled in by default: then these are no-ops and the code is unchanged. */
#ifdef WWHD_SNAN_COUNT
#ifdef __cplusplus
extern "C" {
#endif
extern int wwhd_snan_on;
void wwhd_snan_hit(int kind, uint32_t ea, uint64_t bits);  /* noinline: its return address is the call site */
#ifdef __cplusplus
}
#endif
enum { WWHD_SNAN_LFS = 0, WWHD_SNAN_STFS = 1, WWHD_SNAN_PSQ_L = 2, WWHD_SNAN_PSQ_ST = 3 };
static inline __attribute__((always_inline)) uint32_t snan_chk32(uint32_t b, uint32_t ea, int kind) {
    if (__builtin_expect(wwhd_snan_on, 0) && (b & 0x7FC00000u) == 0x7F800000u && (b & 0x003FFFFFu))
        wwhd_snan_hit(kind, ea, b);
    return b;
}
static inline __attribute__((always_inline)) double snan_chk64(double d, uint32_t ea, int kind) {
    if (__builtin_expect(wwhd_snan_on, 0)) {
        uint64_t b = f64_as_u64(d);
        if ((b & 0x7FF8000000000000ull) == 0x7FF0000000000000ull && (b & 0x0007FFFFFFFFFFFFull))
            wwhd_snan_hit(kind, ea, b);
    }
    return d;
}
#else
#define snan_chk32(b, ea, kind) (b)
#define snan_chk64(d, ea, kind) (d)
#endif

/* ---- single-precision float load/store: bit-exact PowerPC semantics ----
 * lfs/lfsx/lfsu/lfsux and stfs/stfsx/stfsu/stfsux are not arithmetic on the PowerPC: they move bits.
 * A host (double)float or (float)double conversion is arithmetic: it quiets a signalling NaN (the
 * clang optimizer folds some load->store pairs into plain copies and some not, so the host even did
 * it inconsistently), and (float)d rounds where stfs truncates.
 *
 * Load (PowerPC User Instruction Set Architecture, Book I v2.01, 4.6.2 "Floating-Point Load
 * Instructions", p. 96): WORD in single format becomes FRT as
 *   normalized (0 < WORD1:8 < 255) and zero / infinity / NaN (WORD1:8 = 255 or WORD1:31 = 0):
 *     FRT0:1 = WORD0:1, FRT2:4 = 3 x WORD1 (normalized: 3 x not WORD1), FRT5:63 = WORD2:31 || 29 zeros
 *     -> a NaN keeps sign and payload and stays signalling;
 *   denormalized (WORD1:8 = 0, WORD9:31 != 0): normalized into double format (exact value).
 * The IBM 750CL User's Manual (Espresso's paired-single 750 family), "Paired-Single Load and Store
 * Instructions" p. 108: psq_l with a float GQR type passes the value "directly to the register
 * without any conversion. This includes the case where the operand is a denorm."
 * For every non-NaN word the IEEE single->double conversion of the host is exact and gives the same
 * bits, so only NaNs need the bit move (one well-predicted branch).
 *
 * Store (Book I v2.01, 4.6.3 "Floating-Point Store Instructions", p. 100; 750CL Table 2-44 "Store
 * Floating-Point Single Behavior", p. 105):
 *   no denormalization required (FRS1:11 > 896 or FRS1:63 = 0; includes zero / infinity / NaN):
 *     WORD0:1 = FRS0:1, WORD2:31 = FRS5:34  (a plain bit selection: no rounding, the low 29 fraction
 *     bits are dropped (truncation), a NaN is not quieted; a value too large for single format gives
 *     the "well-defined value" Book I describes, not infinity);
 *   denormalization required (874 <= FRS1:11 <= 896): sign, exponent 0, and the mantissa 1.FRS12:63
 *     shifted right one bit at a time until the exponent is -126, WORD9:31 = frac1:23 (truncated);
 *   FRS1:11 < 874: Book I says "undefined"; the 750CL table says "denormalize and store" for a
 *     normalized double and "store zero" for a denormalized double, which both give a signed zero.
 * For every double that is exactly a single the host conversion gives the same bits, so the fast
 * path is one conversion plus a well-predicted "round trip equal" check (false for NaNs). */
static __attribute__((noinline, cold)) uint32_t ppc_stfs_bits_slow(uint64_t b) {
    uint32_t e = (uint32_t)(b >> 52) & 0x7FF;
    if (e > 896 || (b & 0x7FFFFFFFFFFFFFFFull) == 0)
        return (uint32_t)((b >> 32) & 0xC0000000u) | (uint32_t)((b >> 29) & 0x3FFFFFFFu);
    uint32_t sign = (uint32_t)(b >> 32) & 0x80000000u;
    if (e < 874) return sign;
    uint64_t frac = 0x0010000000000000ull | (b & 0x000FFFFFFFFFFFFFull); /* 1.FRS12:63, 53 bits */
    return sign | (uint32_t)(frac >> (897 - e + 29));                   /* shift 897-e: exp -> -126 */
}
/* stfs conversion: double in an FPR -> single-format word */
static inline uint32_t ppc_stfs_bits(double d) {
    float f = (float)d;
    if (__builtin_expect((double)f == d, 1)) return f32_as_u32(f);
    return ppc_stfs_bits_slow(f64_as_u64(d));
}
/* lfs conversion: single-format word -> double in an FPR */
static inline double ppc_lfs_value(uint32_t w) {
    if (__builtin_expect((w & 0x7FFFFFFFu) > 0x7F800000u, 0)) /* NaN: FRT0:1 = WORD0:1, FRT2:4 = 111 */
        return u64_as_f64(((uint64_t)(w & 0xC0000000u) << 32) | 0x3800000000000000ull |
                          ((uint64_t)(w & 0x3FFFFFFFu) << 29));
    return (double)u32_as_f32(w);
}

static inline double ldf32(uint32_t ea) { return ppc_lfs_value(snan_chk32(ld32(ea), ea, 0)); }
static inline double ldf64(uint32_t ea) { return u64_as_f64(ld64(ea)); }
static inline void stf32(uint32_t ea, double d) { st32(ea, ppc_stfs_bits(snan_chk64(d, ea, 1))); }
static inline void stf64(uint32_t ea, double d) { st64(ea, f64_as_u64(d)); }

/* ---- integer helpers ---- */
static inline uint32_t rotl32(uint32_t v, uint32_t sh) { sh &= 31; return sh ? (v << sh) | (v >> (32 - sh)) : v; }

static inline void cr_set_s(Cpu* c, int f, int32_t a, int32_t b) {
    c->cr[4 * f + 0] = a < b; c->cr[4 * f + 1] = a > b; c->cr[4 * f + 2] = a == b; c->cr[4 * f + 3] = c->xer_so;
}
static inline void cr_set_u(Cpu* c, int f, uint32_t a, uint32_t b) {
    c->cr[4 * f + 0] = a < b; c->cr[4 * f + 1] = a > b; c->cr[4 * f + 2] = a == b; c->cr[4 * f + 3] = c->xer_so;
}
static inline void cr0_rc(Cpu* c, uint32_t v) { cr_set_s(c, 0, (int32_t)v, 0); }
/* The recompiler's condition-register liveness pass (tools/recomp/crlive.py) stores only the bits
 * that are read later: m has bit 0 = lt, 1 = gt, 2 = eq, 3 = so of the field. With PPC_CR_CHECK, the
 * dropped bits get a poison value that ppc_cr_read() refuses (a check build of the recompiler). */
#ifdef PPC_CR_CHECK
#define PPC_CR_DEAD(c, i) ((c)->cr[i] = 0x55)
void ppc_cr_poisoned(Cpu* c, int bit, uint32_t addr);
static inline uint8_t ppc_cr_read(Cpu* c, int bit, uint32_t addr) {
    if (__builtin_expect(c->cr[bit] == 0x55, 0)) ppc_cr_poisoned(c, bit, addr);
    return c->cr[bit];
}
#else
#define PPC_CR_DEAD(c, i) ((void)0)
#endif
static inline __attribute__((always_inline)) void cr_set_s_m(Cpu* c, int f, int32_t a, int32_t b, int m) {
    if (m & 1) c->cr[4 * f + 0] = a < b; else PPC_CR_DEAD(c, 4 * f + 0);
    if (m & 2) c->cr[4 * f + 1] = a > b; else PPC_CR_DEAD(c, 4 * f + 1);
    if (m & 4) c->cr[4 * f + 2] = a == b; else PPC_CR_DEAD(c, 4 * f + 2);
    if (m & 8) c->cr[4 * f + 3] = c->xer_so; else PPC_CR_DEAD(c, 4 * f + 3);
}
static inline __attribute__((always_inline)) void cr_set_u_m(Cpu* c, int f, uint32_t a, uint32_t b, int m) {
    if (m & 1) c->cr[4 * f + 0] = a < b; else PPC_CR_DEAD(c, 4 * f + 0);
    if (m & 2) c->cr[4 * f + 1] = a > b; else PPC_CR_DEAD(c, 4 * f + 1);
    if (m & 4) c->cr[4 * f + 2] = a == b; else PPC_CR_DEAD(c, 4 * f + 2);
    if (m & 8) c->cr[4 * f + 3] = c->xer_so; else PPC_CR_DEAD(c, 4 * f + 3);
}
static inline __attribute__((always_inline)) void cr0_rc_m(Cpu* c, uint32_t v, int m) { cr_set_s_m(c, 0, (int32_t)v, 0, m); }

static inline uint32_t ppc_divw(uint32_t a, uint32_t b) {
    if (b == 0 || (a == 0x80000000u && b == 0xFFFFFFFFu)) return ((int32_t)a < 0) ? 0xFFFFFFFFu : 0;
    return (uint32_t)((int32_t)a / (int32_t)b);
}
static inline uint32_t ppc_divwu(uint32_t a, uint32_t b) { return b ? a / b : 0; }

/* eight CR bytes (0/1, little-endian host) to eight bits, the first byte in the top bit */
static inline uint32_t ppc_cr_pack8(const uint8_t* p) {
    uint64_t x;
    memcpy(&x, p, 8);
    return (uint32_t)(((x & 0x0101010101010101ull) * 0x8040201008040201ull) >> 56);
}
static inline __attribute__((always_inline)) uint32_t ppc_mfcr(const Cpu* c) {
    return ppc_cr_pack8(c->cr) << 24 | ppc_cr_pack8(c->cr + 8) << 16 | ppc_cr_pack8(c->cr + 16) << 8 | ppc_cr_pack8(c->cr + 24);
}
static inline void ppc_mtcrf(Cpu* c, uint32_t crm, uint32_t v) {
    for (int f = 0; f < 8; f++)
        if (crm & (0x80u >> f))
            for (int b = 0; b < 4; b++) c->cr[4 * f + b] = (v >> (31 - (4 * f + b))) & 1;
}
static inline uint32_t ppc_mfxer(const Cpu* c) {
    return ((uint32_t)c->xer_so << 31) | ((uint32_t)c->xer_ov << 30) | ((uint32_t)c->xer_ca << 29) | c->xer_bc;
}
static inline void ppc_mtxer(Cpu* c, uint32_t v) {
    c->xer_so = (v >> 31) & 1; c->xer_ov = (v >> 30) & 1; c->xer_ca = (v >> 29) & 1; c->xer_bc = v & 0x7F;
}

/* lwarx / stwcx. */
static inline uint32_t ppc_lwarx(Cpu* c, uint32_t ea) {
    uint32_t raw = __atomic_load_n((uint32_t*)ppc_ptr(ea), __ATOMIC_SEQ_CST);
    c->res_addr = ea; c->res_val = raw;
    return __builtin_bswap32(raw);
}
static inline void ppc_stwcx(Cpu* c, uint32_t ea, uint32_t v) {
    int ok = 0;
    if (c->res_addr == ea) {
        uint32_t expected = c->res_val;
        ok = __atomic_compare_exchange_n((uint32_t*)ppc_ptr(ea), &expected, __builtin_bswap32(v), 0,
                                         __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }
    c->res_addr = 0xFFFFFFFFu;
    c->cr[0] = 0; c->cr[1] = 0; c->cr[2] = (uint8_t)ok; c->cr[3] = c->xer_so;
}

static inline void ppc_dcbz(uint32_t ea) { memset(ppc_ptr(ea & ~31u), 0, 32); }

/* ---- floating point ---- */
static inline double round25(double d) {
    uint64_t v = f64_as_u64(d);
    v = (v & 0xFFFFFFFFF8000000ull) + (v & 0x8000000ull);
    return u64_as_f64(v);
}
static inline double to_single(double d) { return (double)(float)d; }

/* fcmpu / fcmpo. IEEE comparisons with a NaN are false, so lt/gt/eq need no NaN test; un is the
 * fourth outcome. FPSCR's FPCC field (which fcmp also sets) is not kept: only mffs and mcrfs read
 * it, and this game has neither (ppc2c warns when it translates one). */
static inline void cr_set_f(Cpu* c, int f, double a, double b) {
    c->cr[4 * f + 0] = a < b; c->cr[4 * f + 1] = a > b;
    c->cr[4 * f + 2] = a == b; c->cr[4 * f + 3] = (uint8_t)__builtin_isunordered(a, b);
}
/* cr_set_f storing only the bits in m (see cr_set_s_m) */
static inline __attribute__((always_inline)) void cr_set_f_m(Cpu* c, int f, double a, double b, int m) {
    if (m & 1) c->cr[4 * f + 0] = a < b; else PPC_CR_DEAD(c, 4 * f + 0);
    if (m & 2) c->cr[4 * f + 1] = a > b; else PPC_CR_DEAD(c, 4 * f + 1);
    if (m & 4) c->cr[4 * f + 2] = a == b; else PPC_CR_DEAD(c, 4 * f + 2);
    if (m & 8) c->cr[4 * f + 3] = (uint8_t)__builtin_isunordered(a, b); else PPC_CR_DEAD(c, 4 * f + 3);
}

/* The same compares for leaf functions that keep CR bits in C locals (tools/recomp/leaflocal.py):
 * lt, gt, eq, so point at the field's four locals. */
#ifdef PPC_CR_CHECK
#define PPC_CRL_DEAD(p) (*(p) = 0x55)
static inline uint8_t ppc_crl_read(Cpu* c, uint8_t v, int bit, uint32_t addr) {
    if (__builtin_expect(v == 0x55, 0)) ppc_cr_poisoned(c, bit, addr);
    return v;
}
#else
#define PPC_CRL_DEAD(p) ((void)0)
#endif
static inline __attribute__((always_inline)) void crl_set_s_m(uint8_t* lt, uint8_t* gt, uint8_t* eq, uint8_t* so,
                                                              const Cpu* c, int32_t a, int32_t b, int m) {
    if (m & 1) *lt = a < b; else PPC_CRL_DEAD(lt);
    if (m & 2) *gt = a > b; else PPC_CRL_DEAD(gt);
    if (m & 4) *eq = a == b; else PPC_CRL_DEAD(eq);
    if (m & 8) *so = c->xer_so; else PPC_CRL_DEAD(so);
}
static inline __attribute__((always_inline)) void crl_set_u_m(uint8_t* lt, uint8_t* gt, uint8_t* eq, uint8_t* so,
                                                              const Cpu* c, uint32_t a, uint32_t b, int m) {
    if (m & 1) *lt = a < b; else PPC_CRL_DEAD(lt);
    if (m & 2) *gt = a > b; else PPC_CRL_DEAD(gt);
    if (m & 4) *eq = a == b; else PPC_CRL_DEAD(eq);
    if (m & 8) *so = c->xer_so; else PPC_CRL_DEAD(so);
}
static inline __attribute__((always_inline)) void crl_set_f_m(uint8_t* lt, uint8_t* gt, uint8_t* eq, uint8_t* so,
                                                              const Cpu* c, double a, double b, int m) {
    (void)c;
    if (m & 1) *lt = a < b; else PPC_CRL_DEAD(lt);
    if (m & 2) *gt = a > b; else PPC_CRL_DEAD(gt);
    if (m & 4) *eq = a == b; else PPC_CRL_DEAD(eq);
    if (m & 8) *so = (uint8_t)__builtin_isunordered(a, b); else PPC_CRL_DEAD(so);
}
static inline __attribute__((always_inline)) void crl0_rc_m(uint8_t* lt, uint8_t* gt, uint8_t* eq, uint8_t* so,
                                                            const Cpu* c, uint32_t v, int m) {
    crl_set_s_m(lt, gt, eq, so, c, (int32_t)v, 0, m);
}
#define crl_set_s(lt, gt, eq, so, c, a, b) crl_set_s_m(lt, gt, eq, so, c, a, b, 15)
#define crl_set_u(lt, gt, eq, so, c, a, b) crl_set_u_m(lt, gt, eq, so, c, a, b, 15)
#define crl_set_f(lt, gt, eq, so, c, a, b) crl_set_f_m(lt, gt, eq, so, c, a, b, 15)
#define crl0_rc(lt, gt, eq, so, c, v) crl0_rc_m(lt, gt, eq, so, c, v, 15)

static inline uint64_t ppc_fctiwz(double d) {
    int32_t r;
    if (isnan(d)) r = (int32_t)0x80000000;
    else if (d >= 2147483647.0) r = 0x7FFFFFFF;
    else if (d <= -2147483648.0) r = (int32_t)0x80000000;
    else r = (int32_t)d;
    return 0xFFF8000000000000ull | (uint32_t)r;
}
static inline uint64_t ppc_fctiw(Cpu* c, double d) {
    switch (c->fpscr & 3) {
    case 0: d = nearbyint(d); break; /* default host mode is round-to-nearest-even */
    case 1: d = trunc(d); break;
    case 2: d = ceil(d); break;
    case 3: d = floor(d); break;
    }
    return ppc_fctiwz(d);
}
static inline double ppc_fsel(double a, double b, double cc) { return a >= 0.0 ? cc : b; }

/* ---- paired single quantization ---- */
/* 2^e for the 6-bit signed GQR scale, built from float bits (no libm call) */
static inline float psq_pow2(int e) { return u32_as_f32((uint32_t)(127 + e) << 23); }
/* float GQR types (0, and the reserved 1-3) move the word like lfs / stfs (750CL p. 108: "passed
 * directly to the register without any conversion"); see ppc_lfs_value / ppc_stfs_bits. */
static inline double psq_dequant(uint32_t data, uint32_t type, uint32_t scale) {
    if (type < 4) return ppc_lfs_value(data);  /* float: no scaling */
    float s = psq_pow2(-(int)((int32_t)(scale << 26) >> 26));
    switch (type) {
    case 4: return (float)(uint8_t)data * s;
    case 5: return (float)(uint16_t)data * s;
    case 6: return (float)(int8_t)data * s;
    case 7: return (float)(int16_t)data * s;
    default: return ppc_lfs_value(data);
    }
}
/* Integer GQR types: 750CL "Paired-Single Load and Store Instructions" p. 108: +Inf and a NaN with
 * the sign bit 0 give the positive overflow value, -Inf and a NaN with the sign bit 1 the negative
 * one (a NaN used to reach an undefined host float->int cast). */
static inline uint32_t psq_quant(double d, uint32_t type, uint32_t scale) {
    if (type < 4) return ppc_stfs_bits(d);
    float v = (float)d;
    if (__builtin_expect(v != v, 0)) v = (f64_as_u64(d) >> 63) ? -INFINITY : INFINITY;
    float s = psq_pow2((int)((int32_t)(scale << 26) >> 26));
    switch (type) {
    case 4: v *= s; v = v < 0 ? 0 : v > 255 ? 255 : v; return (uint8_t)(uint32_t)v;
    case 5: v *= s; v = v < 0 ? 0 : v > 65535 ? 65535 : v; return (uint16_t)(uint32_t)v;
    case 6: v *= s; v = v < -128 ? -128 : v > 127 ? 127 : v; return (uint8_t)(int32_t)v;
    case 7: v *= s; v = v < -32768 ? -32768 : v > 32767 ? 32767 : v; return (uint16_t)(int32_t)v;
    default: return ppc_stfs_bits(d);
    }
}
/* quantized formats (GQR type 4-7): out of line, so the float case below stays small and inline.
 * The _l forms take the two halves of the FPR separately (for code that keeps registers in C locals). */
static __attribute__((noinline)) void psq_load_slow_l(Cpu* c, double* p0, double* p1, uint32_t ea, int w, int i) {
    uint32_t g = c->gqr[i], type = (g >> 16) & 7, scale = (g >> 24) & 0x3F;
    int sz = (type == 4 || type == 6) ? 1 : (type == 5 || type == 7) ? 2 : 4;
    uint32_t d0 = sz == 1 ? ld8(ea) : sz == 2 ? ld16(ea) : snan_chk32(ld32(ea), ea, 2);
    *p0 = psq_dequant(d0, type, scale);
    if (w) *p1 = 1.0;
    else {
        uint32_t d1 = sz == 1 ? ld8(ea + 1) : sz == 2 ? ld16(ea + 2) : snan_chk32(ld32(ea + 4), ea + 4, 2);
        *p1 = psq_dequant(d1, type, scale);
    }
}
static __attribute__((noinline)) void psq_store_slow_l(Cpu* c, double v0, double v1, uint32_t ea, int w, int i) {
    uint32_t g = c->gqr[i], type = g & 7, scale = (g >> 8) & 0x3F;
    int sz = (type == 4 || type == 6) ? 1 : (type == 5 || type == 7) ? 2 : 4;
    uint32_t d0 = psq_quant(sz == 4 ? snan_chk64(v0, ea, 3) : v0, type, scale);
    if (sz == 1) st8(ea, d0); else if (sz == 2) st16(ea, d0); else st32(ea, d0);
    if (!w) {
        uint32_t d1 = psq_quant(sz == 4 ? snan_chk64(v1, ea + 4, 3) : v1, type, scale);
        if (sz == 1) st8(ea + 1, d1); else if (sz == 2) st16(ea + 2, d1); else st32(ea + 4, d1);
    }
}
/* paired-single loads and stores: plain floats (GQR type 0-3) are nearly all of them. The generated
 * code (funcs.h) sets bit n of PPC_GQR_STATIC_FLOAT when the game never writes GQRn, which then stays
 * 0 (plain floats): with the constant GQR index of every call the check disappears (2,082 of the
 * game's 2,106 paired loads and stores use GQR0 or GQR1). */
#ifndef PPC_GQR_STATIC_FLOAT
#define PPC_GQR_STATIC_FLOAT 0
#endif
static inline __attribute__((always_inline)) void psq_load_l(Cpu* c, double* p0, double* p1, uint32_t ea, int w, int i) {
    if (((PPC_GQR_STATIC_FLOAT >> i) & 1) || __builtin_expect(((c->gqr[i] >> 16) & 7) < 4, 1)) {
        *p0 = ppc_lfs_value(snan_chk32(ld32(ea), ea, 2));
        *p1 = w ? 1.0 : ppc_lfs_value(snan_chk32(ld32(ea + 4), ea + 4, 2));
        return;
    }
    psq_load_slow_l(c, p0, p1, ea, w, i);
}
static inline __attribute__((always_inline)) void psq_store_l(Cpu* c, double v0, double v1, uint32_t ea, int w, int i) {
    if (((PPC_GQR_STATIC_FLOAT >> i) & 1) || __builtin_expect((c->gqr[i] & 7) < 4, 1)) {
        st32(ea, ppc_stfs_bits(snan_chk64(v0, ea, 3)));
        if (!w) st32(ea + 4, ppc_stfs_bits(snan_chk64(v1, ea + 4, 3)));
        return;
    }
    psq_store_slow_l(c, v0, v1, ea, w, i);
}
static inline __attribute__((always_inline)) void psq_load(Cpu* c, int fd, uint32_t ea, int w, int i) {
    psq_load_l(c, &c->f[fd].ps0, &c->f[fd].ps1, ea, w, i);
}
static inline __attribute__((always_inline)) void psq_store(Cpu* c, int fs, uint32_t ea, int w, int i) {
    psq_store_l(c, c->f[fs].ps0, c->f[fs].ps1, ea, w, i);
}

#ifdef __cplusplus
}
#endif
