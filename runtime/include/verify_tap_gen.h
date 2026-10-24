/* Included by build/gen/code_tap.c (tools/verify/mktap.py): the tapped copies' own memory
 * accesses and calls go through the recorder (runtime/src/verify_tap.cpp). */
#pragma once
#include "ppc.h"
#include "verify_tap.h"

#ifdef __cplusplus
extern "C" {
#endif
int tap_begin(Cpu* c, uint32_t func);
void tap_end(Cpu* c);
void tap_call(Cpu* c, PpcFunc fn, uint32_t target);
void tap_dispatch(Cpu* c);
void tap_access(int type, uint32_t ea, int size, uint64_t value);
#ifdef __cplusplus
}
#endif

static inline uint8_t tap_ld8(uint32_t ea) { uint8_t v = ld8(ea); tap_access(TAP_READ, ea, 1, v); return v; }
static inline uint16_t tap_ld16(uint32_t ea) { uint16_t v = ld16(ea); tap_access(TAP_READ, ea, 2, v); return v; }
static inline uint32_t tap_ld32(uint32_t ea) { uint32_t v = ld32(ea); tap_access(TAP_READ, ea, 4, v); return v; }
static inline uint64_t tap_ld64(uint32_t ea) { uint64_t v = ld64(ea); tap_access(TAP_READ, ea, 8, v); return v; }
static inline void tap_st8(uint32_t ea, uint8_t v) { st8(ea, v); tap_access(TAP_WRITE, ea, 1, v); }
static inline void tap_st16(uint32_t ea, uint16_t v) { st16(ea, v); tap_access(TAP_WRITE, ea, 2, v); }
static inline void tap_st32(uint32_t ea, uint32_t v) { st32(ea, v); tap_access(TAP_WRITE, ea, 4, v); }
static inline void tap_st64(uint32_t ea, uint64_t v) { st64(ea, v); tap_access(TAP_WRITE, ea, 8, v); }
static inline double tap_ldf32(uint32_t ea) { return ppc_lfs_value(tap_ld32(ea)); /* lfs: bit-exact, ppc.h */ }
static inline double tap_ldf64(uint32_t ea) { return u64_as_f64(tap_ld64(ea)); }
static inline void tap_stf32(uint32_t ea, double d) { tap_st32(ea, ppc_stfs_bits(d)); /* stfs: bit-exact, ppc.h */ }
static inline void tap_stf64(uint32_t ea, double d) { tap_st64(ea, f64_as_u64(d)); }
static inline void tap_psq_load(Cpu* c, int fd, uint32_t ea, int w, int i) {
    uint32_t g = c->gqr[i], type = (g >> 16) & 7, scale = (g >> 24) & 0x3F;
    int sz = (type == 4 || type == 6) ? 1 : (type == 5 || type == 7) ? 2 : 4;
    uint32_t d0 = sz == 1 ? tap_ld8(ea) : sz == 2 ? tap_ld16(ea) : tap_ld32(ea);
    c->f[fd].ps0 = psq_dequant(d0, type, scale);
    if (w) c->f[fd].ps1 = 1.0;
    else {
        uint32_t d1 = sz == 1 ? tap_ld8(ea + 1) : sz == 2 ? tap_ld16(ea + 2) : tap_ld32(ea + 4);
        c->f[fd].ps1 = psq_dequant(d1, type, scale);
    }
}
static inline void tap_psq_store(Cpu* c, int fs, uint32_t ea, int w, int i) {
    uint32_t g = c->gqr[i], type = g & 7, scale = (g >> 8) & 0x3F;
    int sz = (type == 4 || type == 6) ? 1 : (type == 5 || type == 7) ? 2 : 4;
    uint32_t d0 = psq_quant(c->f[fs].ps0, type, scale);
    if (sz == 1) tap_st8(ea, d0); else if (sz == 2) tap_st16(ea, d0); else tap_st32(ea, d0);
    if (!w) {
        uint32_t d1 = psq_quant(c->f[fs].ps1, type, scale);
        if (sz == 1) tap_st8(ea + 1, d1); else if (sz == 2) tap_st16(ea + 2, d1); else tap_st32(ea + 4, d1);
    }
}
static inline void tap_dcbz(uint32_t ea) { ea &= ~31u; for (int i = 0; i < 32; i += 4) tap_st32(ea + i, 0); }
static inline uint32_t tap_lwarx(Cpu* c, uint32_t ea) { uint32_t v = ppc_lwarx(c, ea); tap_access(TAP_READ, ea, 4, v); return v; }

#define ld8 tap_ld8
#define ld16 tap_ld16
#define ld32 tap_ld32
#define ld64 tap_ld64
#define st8 tap_st8
#define st16 tap_st16
#define st32 tap_st32
#define st64 tap_st64
#define ldf32 tap_ldf32
#define ldf64 tap_ldf64
#define stf32 tap_stf32
#define stf64 tap_stf64
#define psq_load tap_psq_load
#define psq_store tap_psq_store
#define ppc_dcbz tap_dcbz
#define ppc_lwarx tap_lwarx
