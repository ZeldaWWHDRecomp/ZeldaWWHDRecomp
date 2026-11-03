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

}  // namespace hostrt
