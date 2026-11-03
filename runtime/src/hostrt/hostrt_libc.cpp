// SPDX-License-Identifier: MPL-2.0
// C library replacements (string / memory functions), from the C standard; register results beyond the
// standard as measured (tools/host_runtime/difftest/calltest.cpp checks every one).
#include "hostrt_impl.h"

namespace hostrt {

// bzero(p, n): memset(p, 0, n), done by the port's memset as in the original; r4 ends 0 (callers may
// read it), r3 is what memset returns (p)
void bzero_028F521C(Cpu* c) {
    const uint32_t r5 = c->r[5];
    c->r[5] = c->r[4];
    c->r[4] = 0;
    imp_coreinit_memset(c);
    c->r[5] = r5;
}

// n zero words from p (n unsigned). Measured: r3 ends at p + 4n when n is a multiple of 8, else at
// p + 4(n - 1) (p for n = 0); callers read r3.
void zero_words_028F5914(Cpu* c) {
    const uint32_t p = c->r[3], n = c->r[4];
    for (uint32_t i = 0; i < n; i++) st32(p + 4 * i, 0);
    c->r[3] = n == 0 ? p : (n & 7) == 0 ? p + 4 * n : p + 4 * (n - 1);
}

// strcpy(dst, src): bytes copied forward one at a time up to and including the terminator; r3 stays dst
// and r4 ends at the terminator's address in src (callers read it)
void strcpy_028F040C(Cpu* c) {
    const uint32_t dst = c->r[3];
    uint32_t src = c->r[4];
    for (uint32_t i = 0;; i++) {
        const uint8_t b = ld8(src + i);
        st8(dst + i, b);
        if (!b) {
            src += i;
            break;
        }
    }
    c->r[4] = src;
}

// __va_arg(ap, type): the PowerPC EABI va_list {u8 gpr, u8 fpr, u16 -, u32 overflow area, u32 register save
// area} (8 GPR words, then 8 FPR doublewords). Type 1: a word, 2: a doubleword in a GPR pair (an odd gpr is
// rounded up), 3: a double; other types are passed by reference: the word slot holds the address, which is
// returned. Registers run out at 8; then the argument comes from the overflow area (aligned to 8 for
// types 2 and 3) and the counter keeps its value (type 2: rounded up to even), as measured. r3 = the argument's address.
void va_arg_028F5C88(Cpu* c) {
    const uint32_t ap = c->r[3], type = c->r[4];
    uint32_t gpr = ld8(ap), fpr = ld8(ap + 1), ovf = ld32(ap + 4);
    const uint32_t save = ld32(ap + 8);
    uint32_t addr;
    if (type == 3) {
        if (fpr < 8) {
            addr = save + 32 + fpr * 8;
            st8(ap + 1, (uint8_t)(fpr + 1));
        } else {
            ovf = (ovf + 7) & ~7u;
            addr = ovf;
            st32(ap + 4, ovf + 8);
        }
    } else if (type == 2) {
        gpr = (uint8_t)(gpr + (gpr & 1));  // in the 8-bit counter (255 rounds to 0, measured)
        if (gpr < 8) {
            addr = save + gpr * 4;
            st8(ap, (uint8_t)(gpr + 2));
        } else {
            st8(ap, (uint8_t)gpr);  // the rounded-up count (measured)
            ovf = (ovf + 7) & ~7u;
            addr = ovf;
            st32(ap + 4, ovf + 8);
        }
    } else {
        if (gpr < 8) {
            addr = save + gpr * 4;
            st8(ap, (uint8_t)(gpr + 1));
        } else {
            addr = ovf;
            st32(ap + 4, ovf + 4);
        }
        if (type != 1) addr = ld32(addr);
    }
    c->r[3] = addr;
}

}  // namespace hostrt
