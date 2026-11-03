// SPDX-License-Identifier: MPL-2.0
// C++ runtime replacements: array construction. The calling protocol (which registers and stack words a
// constructor sees, what is written where) was measured on the recompiled original with
// tools/host_runtime/difftest (calltest.cpp checks it on every input class); it is stated here as facts.
#include <cstring>

#include "guest_addr.h"
#include "hostrt_impl.h"

namespace hostrt {
namespace {

// a guest call from a replacement: the callee sees the registers and stack prepared by the caller
inline void call(Cpu* c, uint32_t target, uint32_t lr) {
    c->lr = lr;
    c->ctr = target;
    c->pc = target;
    ppc_dispatch(c);
}

// the guest registers back to their entry values (host-side fields such as the core stay as they are now)
inline void restore_registers(Cpu* c, const Cpu& e) {
    memcpy(c->r, e.r, sizeof c->r);
    memcpy(c->f, e.f, sizeof c->f);
    memcpy(c->cr, e.cr, sizeof c->cr);
    c->lr = e.lr, c->ctr = e.ctr, c->fpscr = e.fpscr;
    c->xer_so = e.xer_so, c->xer_ov = e.xer_ov, c->xer_ca = e.xer_ca, c->xer_bc = e.xer_bc;
    memcpy(c->gqr, e.gqr, sizeof c->gqr);
}

}  // namespace

// __construct_array(p, n, size, ctor) constructing in place (p != 0), measured:
// - each constructor call gets r3 = p + i*size (i = 0 .. n-1, in order), r4..r10 = 0, SP = entry SP - 0x48
//   with the back chain entry-0x48 -> entry-0x18 -> entry SP, zero stack-argument words at SP+8..SP+0x10
//   and at entry-0x18+8..+0x10, LR = 028EFF60, CTR = the constructor;
// - the caller's LR save word (entry SP + 4) gets LR;
// - no constructor (ctor == 0) or n == 0: nothing is called;
// - the result is r3 = p; r4 is what the last constructor left in it (unchanged when none ran); every
//   other register is restored (callers read only r3/r4, tools/host_runtime/callsite_regs.tsv).
// p == 0 allocates (an 8-byte count header, operator new[]): handed back to the original.
bool construct_array_028EFFD0(Cpu* c) {
    const uint32_t p = c->r[3];
    if (!p) return false;
    const uint32_t n = c->r[4], size = c->r[5], ctor = c->r[6];
    const Cpu entry = *c;
    const uint32_t sp = entry.r[1];
    st32(sp + 4, entry.lr);
    if (ctor && n) {
        // the two frames the original has open while it calls the constructors
        st32(sp - 0x18, sp);
        st32(sp - 0x18 + 8, 0), st32(sp - 0x18 + 0xC, 0), st32(sp - 0x18 + 0x10, 0);
        st32(sp - 0x48, sp - 0x18);
        uint32_t r4 = entry.r[4];
        for (uint32_t i = 0; i < n; i++) {
            st32(sp - 0x48 + 8, 0);
            c->r[1] = sp - 0x48;
            c->r[3] = p + i * size;
            for (int k = 4; k <= 10; k++) c->r[k] = 0;
            static const uint32_t ret = GC(0x028EFF60u);  // canonical address -> this build's
            call(c, ctor, ret);
            r4 = c->r[4];
        }
        const uint32_t keep_r4 = r4;
        restore_registers(c, entry);  // the original restores its saved ones; callers read only r3/r4
        c->r[4] = keep_r4;
    }
    c->r[3] = p;
    return true;
}

// __register_global_object(record): the record's first word links the chain of destructor records whose
// head is at 104DA130 (measured): a record whose link is already set, or that is the head, is left alone;
// otherwise it becomes the head and links to the previous one. No register changes a caller reads.
void register_global_object_028F026C(Cpu* c) {
    const uint32_t rec = c->r[3];
    if (ld32(rec)) return;
    static const uint32_t chain = GD(0x104DA130u);  // canonical address -> this build's
    const uint32_t head = ld32(chain);
    if (rec == head) return;
    st32(chain, rec);
    st32(rec, head);
}

}  // namespace hostrt
