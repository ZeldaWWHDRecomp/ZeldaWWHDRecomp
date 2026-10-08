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
    void* thread;              /* host-side: owning guest thread object */
} Cpu;

typedef void (*PpcFunc)(Cpu*);

/* runtime entry points */
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

static inline double ldf32(uint32_t ea) { return (double)u32_as_f32(ld32(ea)); }
static inline double ldf64(uint32_t ea) { return u64_as_f64(ld64(ea)); }
static inline void stf32(uint32_t ea, double d) { st32(ea, f32_as_u32((float)d)); }
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

static inline uint32_t ppc_divw(uint32_t a, uint32_t b) {
    if (b == 0 || (a == 0x80000000u && b == 0xFFFFFFFFu)) return ((int32_t)a < 0) ? 0xFFFFFFFFu : 0;
    return (uint32_t)((int32_t)a / (int32_t)b);
}
static inline uint32_t ppc_divwu(uint32_t a, uint32_t b) { return b ? a / b : 0; }

static inline uint32_t ppc_mfcr(const Cpu* c) {
    uint32_t v = 0;
    for (int i = 0; i < 32; i++) v |= (uint32_t)(c->cr[i] & 1) << (31 - i);
    return v;
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

static inline void cr_set_f(Cpu* c, int f, double a, double b) {
    int un = isnan(a) || isnan(b);
    c->cr[4 * f + 0] = !un && a < b; c->cr[4 * f + 1] = !un && a > b;
    c->cr[4 * f + 2] = !un && a == b; c->cr[4 * f + 3] = (uint8_t)un;
    c->fpscr = (c->fpscr & ~0xF000u) | ((uint32_t)(c->cr[4 * f] << 3 | c->cr[4 * f + 1] << 2 | c->cr[4 * f + 2] << 1 | un) << 12);
}

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
static inline float psq_dequant(uint32_t data, uint32_t type, uint32_t scale) {
    if (type < 4) return u32_as_f32(data);  /* float: no scaling */
    float s = psq_pow2(-(int)((int32_t)(scale << 26) >> 26));
    switch (type) {
    case 4: return (float)(uint8_t)data * s;
    case 5: return (float)(uint16_t)data * s;
    case 6: return (float)(int8_t)data * s;
    case 7: return (float)(int16_t)data * s;
    default: return u32_as_f32(data);
    }
}
static inline uint32_t psq_quant(float v, uint32_t type, uint32_t scale) {
    if (type < 4) return f32_as_u32(v);
    float s = psq_pow2((int)((int32_t)(scale << 26) >> 26));
    switch (type) {
    case 4: v *= s; v = v < 0 ? 0 : v > 255 ? 255 : v; return (uint8_t)(uint32_t)v;
    case 5: v *= s; v = v < 0 ? 0 : v > 65535 ? 65535 : v; return (uint16_t)(uint32_t)v;
    case 6: v *= s; v = v < -128 ? -128 : v > 127 ? 127 : v; return (uint8_t)(int32_t)v;
    case 7: v *= s; v = v < -32768 ? -32768 : v > 32767 ? 32767 : v; return (uint16_t)(int32_t)v;
    default: return f32_as_u32(v);
    }
}
static inline void psq_load(Cpu* c, int fd, uint32_t ea, int w, int i) {
    uint32_t g = c->gqr[i], type = (g >> 16) & 7, scale = (g >> 24) & 0x3F;
    int sz = (type == 4 || type == 6) ? 1 : (type == 5 || type == 7) ? 2 : 4;
    uint32_t d0 = sz == 1 ? ld8(ea) : sz == 2 ? ld16(ea) : ld32(ea);
    c->f[fd].ps0 = psq_dequant(d0, type, scale);
    if (w) c->f[fd].ps1 = 1.0;
    else {
        uint32_t d1 = sz == 1 ? ld8(ea + 1) : sz == 2 ? ld16(ea + 2) : ld32(ea + 4);
        c->f[fd].ps1 = psq_dequant(d1, type, scale);
    }
}
static inline void psq_store(Cpu* c, int fs, uint32_t ea, int w, int i) {
    uint32_t g = c->gqr[i], type = g & 7, scale = (g >> 8) & 0x3F;
    int sz = (type == 4 || type == 6) ? 1 : (type == 5 || type == 7) ? 2 : 4;
    uint32_t d0 = psq_quant((float)c->f[fs].ps0, type, scale);
    if (sz == 1) st8(ea, d0); else if (sz == 2) st16(ea, d0); else st32(ea, d0);
    if (!w) {
        uint32_t d1 = psq_quant((float)c->f[fs].ps1, type, scale);
        if (sz == 1) st8(ea + 1, d1); else if (sz == 2) st16(ea + 2, d1); else st32(ea + 4, d1);
    }
}

#ifdef __cplusplus
}
#endif
