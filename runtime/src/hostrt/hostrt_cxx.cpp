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

// __destroy_arr(p, n, size, dtor, flags, extra), measured:
// - p == 0: only the caller's LR save word is written (r3, r4 unchanged);
// - n == 0xFFFFFFFF: the array has the 8-byte header before p (word p-4 = ~count, word p-8 = the byte size
//   of the elements); the count is ~[p-4] and the destructors see r6 = 8, otherwise the count is n, r6 = 0;
//   a header with count 0 or a byte size other than count*size makes the original report a runtime error:
//   handed back to it;
// - with a destructor: it is called for i = count-1 .. 0 with r3 = p + i*size, r4 = 2; the first call gets
//   r5 = size, r6 = 0 or 8, r7 = dtor, r8 = flags, r9 = r10 = 0, later ones what the previous left; SP = entry SP - 0x48 (back chain to entry-0x10, which
//   chains to the entry SP), LR = 028F0120;
// - flags != 0: operator delete[] (0273AFC8) with r3 = p (p - 8 with the header), r4 = 0 (the header's byte
//   size + 8), r5..r10 as for a further destructor call, LR = 028F014C;
// - the caller's LR save word gets LR; r3/r4 are what the last call left (unchanged without calls).
bool destroy_arr_028F0164(Cpu* c) {
    const uint32_t p = c->r[3];
    if (!p) {
        st32(c->r[1] + 4, c->lr);
        return true;
    }
    const Cpu entry = *c;
    const uint32_t n = c->r[4], size = c->r[5], dtor = c->r[6], flags = c->r[7];
    const uint32_t sp = entry.r[1];
    const bool header = n == 0xFFFFFFFFu;
    const uint32_t count = header ? ~ld32(p - 4) : n;
    // a header the original rejects (it then reports a runtime error and stops the game): its own path
    if (header && (count == 0 || (uint64_t)count * size != ld32(p - 8))) return false;
    const uint32_t r6 = header ? 8 : 0;
    st32(sp + 4, entry.lr);
    bool called = false;
    auto frame = [&] {
        st32(sp - 0x10, sp);
        st32(sp - 0x48, sp - 0x10);
        c->r[1] = sp - 0x48;
    };
    // r5..r10 are set once; each later call sees what the previous callee left in them (measured)
    c->r[5] = size, c->r[6] = r6, c->r[7] = dtor, c->r[8] = flags, c->r[9] = c->r[10] = 0;
    if (dtor) {
        static const uint32_t ret = GC(0x028F0120u);
        for (uint32_t i = count; i-- > 0;) {
            frame();
            c->r[3] = p + i * size, c->r[4] = 2;
            call(c, dtor, ret);
            called = true;
        }
    }
    if (flags) {
        static const uint32_t ret = GC(0x028F014Cu), del = GC(0x0273AFC8u);
        frame();
        c->r[3] = header ? p - 8 : p, c->r[4] = header ? ld32(p - 8) + 8 : 0;
        call(c, del, ret);
        called = true;
    }
    const uint32_t r3 = called ? c->r[3] : entry.r[3], r4 = called ? c->r[4] : entry.r[4];
    restore_registers(c, entry);
    c->r[3] = r3, c->r[4] = r4;
    return true;
}

}  // namespace hostrt
