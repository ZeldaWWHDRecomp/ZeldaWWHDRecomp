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

// Array construction, measured on __construct_array (028EFFD0), __vec_new (028EFF8C) and the zero-filling
// form (028F0020):
// - the allocator hook at 1020CD08 is 0 in the game; when it is set, the original runs;
// - the byte size is n*size, or 1 for an empty array; p == 0 allocates: operator new[] (0273ADAC) with
//   r3 = the byte size + 8, r4 = n, r5 = size, r6 = 8, r7 = 0,
//   r8 = ctor, r9 = dtor (0 for __construct_array), r10 = 0, LR = 028EFEB4; a null result is returned
//   as it is (nothing else happens); the array then starts 8 bytes in;
// - an 8-byte header before the array ({byte size, ~n}) when it was allocated here, and always for __vec_new;
// - the zero-filling form clears n*size bytes first through the port's memset (r3 = p, r4 = 0,
//   r5 = the byte size (n*size when the caller gave p), LR = 028EFF18);
// - each constructor call gets r3 = p + i*size (i = 0 .. n-1), r4..r10 = 0, SP = entry SP - 0x48 with the
//   back chain entry-0x48 -> entry-0x18 -> entry SP, zero stack-argument words at SP+8 and at
//   entry-0x18+8..+0x10, LR = 028EFF60, CTR = the constructor;
// - the caller's LR save word gets LR; r3 = the array, r4 = what the last call left (unchanged without
//   calls); every other register is restored (callers read only r3/r4).
namespace {
bool array_new(Cpu* c, uint32_t p, uint32_t n, uint32_t size, uint32_t ctor, uint32_t dtor, bool header, bool zero) {
    static const uint32_t hook = GD(0x1020CD08u);
    if (ld32(hook)) return false;
    const Cpu entry = *c;
    const uint32_t sp = entry.r[1];
    st32(sp + 4, entry.lr);
    st32(sp - 0x18, sp);
    st32(sp - 0x18 + 8, 0), st32(sp - 0x18 + 0xC, 0), st32(sp - 0x18 + 0x10, 0);
    st32(sp - 0x48, sp - 0x18);
    bool called = false;
    const uint32_t total = n * size ? n * size : 1;  // measured: an empty array counts one byte
    auto frame = [&] { c->r[1] = sp - 0x48; };
    bool allocated = false;
    if (!p) {
        static const uint32_t ret = GC(0x028EFEB4u), op_new = GC(0x0273ADACu);
        frame();
        c->r[3] = total + 8, c->r[4] = n, c->r[5] = size, c->r[6] = 8, c->r[7] = 0, c->r[8] = ctor, c->r[9] = dtor;
        c->r[10] = 0;
        call(c, op_new, ret);
        called = true;
        const uint32_t q = c->r[3];
        if (!q) {
            const uint32_t r4 = c->r[4];
            restore_registers(c, entry);
            c->r[3] = 0, c->r[4] = r4;
            return true;
        }
        p = q + 8;
        header = true;
        allocated = true;
    }
    if (header) st32(p - 8, total), st32(p - 4, ~n);
    if (zero) {
        static const uint32_t ret = GC(0x028EFF18u);
        frame();
        c->r[3] = p, c->r[4] = 0, c->r[5] = allocated ? total : n * size, c->r[6] = header ? 8 : 0, c->r[7] = 0, c->r[8] = ctor;
        c->r[9] = dtor, c->r[10] = 0;
        c->lr = ret;
        imp_coreinit_memset(c);
        called = true;
    }
    if (ctor && n) {
        static const uint32_t ret = GC(0x028EFF60u);
        for (uint32_t i = 0; i < n; i++) {
            frame();
            st32(sp - 0x48 + 8, 0);
            c->r[3] = p + i * size;
            for (int k = 4; k <= 10; k++) c->r[k] = 0;
            call(c, ctor, ret);
            called = true;
        }
    }
    const uint32_t r4 = called ? c->r[4] : entry.r[4];
    restore_registers(c, entry);
    c->r[3] = p, c->r[4] = r4;
    return true;
}
}  // namespace

// __construct_array(p, n, size, ctor)
bool construct_array_028EFFD0(Cpu* c) { return array_new(c, c->r[3], c->r[4], c->r[5], c->r[6], 0, false, false); }
// __vec_new(p, n, size, ctor, dtor): always with the header
bool vec_new_028EFF8C(Cpu* c) { return array_new(c, c->r[3], c->r[4], c->r[5], c->r[6], c->r[7], true, false); }
// zero-filling array new(p, n, size, ctor, dtor)
bool vec_new_zero_028F0020(Cpu* c) { return array_new(c, c->r[3], c->r[4], c->r[5], c->r[6], c->r[7], false, true); }

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
