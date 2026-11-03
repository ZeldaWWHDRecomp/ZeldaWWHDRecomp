// SPDX-License-Identifier: MPL-2.0
// nn static wrapper replacements: the intrusive doubly linked list of nn::ut (a node is {next, prev}; the
// list keeps an element count in its first word), as measured on the recompiled originals
// (tools/host_runtime/difftest/calltest.cpp checks every one). The LR save word is written where the
// original writes it.
#include <cstring>

#include "guest_addr.h"
#include "hostrt_impl.h"

namespace hostrt {
namespace {

// erase `node`: its neighbours are linked, the count drops, its own links are cleared; returns next
inline uint32_t erase(uint32_t list, uint32_t node) {
    const uint32_t next = ld32(node), prev = ld32(node + 4);
    st32(next + 4, prev);
    st32(prev, next);
    st32(list, ld32(list) - 1);
    st32(node + 4, 0);
    st32(node, 0);
    return next;
}

// erase from `first` up to (not including) `end`; returns end
inline uint32_t erase_range(uint32_t list, uint32_t first, uint32_t end) {
    for (uint32_t n = first; n != end;) {
        const uint32_t next = ld32(n);
        erase(list, n);
        n = next;
    }
    return end;
}

}  // namespace

// insert `node` before the node the iterator points at; the count grows; r3 = node
void list_insert_0286DA70(Cpu* c) {
    const uint32_t list = c->r[3], pos = ld32(c->r[4]), node = c->r[5];
    const uint32_t prev = ld32(pos + 4);
    st32(node, pos);
    st32(node + 4, prev);
    st32(pos + 4, node);
    st32(prev, node);
    st32(list, ld32(list) + 1);
    c->r[3] = node;
}

// erase a node; r3 = the node after it
void list_erase_0286DA9C(Cpu* c) { c->r[3] = erase(c->r[3], c->r[4]); }

// erase [first, end) (both given as iterators); r3 = r4 = end
void list_erase_range_0286DACC(Cpu* c) {
    st32(c->r[1] + 4, c->lr);
    const uint32_t end = erase_range(c->r[3], ld32(c->r[4]), ld32(c->r[5]));
    c->r[3] = c->r[4] = end;
}

// clear: erase from the node the iterator points at up to the one after it ... as the original does:
// [ [it], [[it]] ); r3 = r4 = that end
void list_clear_0286DB18(Cpu* c) {
    st32(c->r[1] + 4, c->lr);
    const uint32_t first = ld32(c->r[4]);
    const uint32_t end = erase_range(c->r[3], first, ld32(first));
    c->r[3] = c->r[4] = end;
}

namespace {
// an export thunk: r12 and CTR get the table entry and the export runs with the caller's registers and
// LR (a tail call), as measured
template <uint32_t kSlot>
inline void export_thunk(Cpu* c) {
    static const uint32_t slot = GD(kSlot);  // canonical address -> this build's
    const uint32_t t = ld32(slot);
    c->r[12] = t;
    c->ctr = t;
    c->pc = t;
    ppc_dispatch(c);
}
}  // namespace

void export_thunk_0286CA30(Cpu* c) { export_thunk<0x104B6774u>(c); }
void export_thunk_0286CA40(Cpu* c) { export_thunk<0x104B6790u>(c); }
void export_thunk_0286CB2C(Cpu* c) { export_thunk<0x104B6748u>(c); }
void export_thunk_0286CB3C(Cpu* c) { export_thunk<0x104B6764u>(c); }
void export_thunk_0286CB4C(Cpu* c) { export_thunk<0x104B6784u>(c); }
void export_thunk_0286CB5C(Cpu* c) { export_thunk<0x104B6780u>(c); }
void export_thunk_0286CB6C(Cpu* c) { export_thunk<0x104B6778u>(c); }
void export_thunk_0286CBB4(Cpu* c) { export_thunk<0x104B6770u>(c); }
void export_thunk_0286D270(Cpu* c) { export_thunk<0x104B67E4u>(c); }
void export_thunk_0286D2D4(Cpu* c) { export_thunk<0x104B67F8u>(c); }
void export_thunk_0286D2E4(Cpu* c) { export_thunk<0x104B67A8u>(c); }
void export_thunk_0286D3C0(Cpu* c) { export_thunk<0x104B67ECu>(c); }
void export_thunk_0286D3D0(Cpu* c) { export_thunk<0x104B67F0u>(c); }
void export_thunk_0286D3E0(Cpu* c) { export_thunk<0x104B67A4u>(c); }
void export_thunk_0286D3F0(Cpu* c) { export_thunk<0x104B67C4u>(c); }
void export_thunk_0286D400(Cpu* c) { export_thunk<0x104B67D8u>(c); }

void work_size_0286CBC4(Cpu* c) { c->r[3] = 0x01600000u; }
void empty_0286D5F0(Cpu*) {}

// ---- small wrappers. Measured protocol for each (calltest compares every call, its registers, LR and
// stack, the memory and globals): the caller's LR save word is written; a frame of the original's size
// is opened (back chain to the entry SP) while the calls run; the registers the original sets before a
// call are set; afterwards the registers come back as the original leaves them for its callers (r3/r4 as
// the last call left them).
namespace {
inline uint32_t table(uint32_t canon) { return ld32(GD(canon)); }
inline void call_export(Cpu* c, uint32_t canon_slot, uint32_t canon_ret) {
    const uint32_t t = table(canon_slot);
    c->r[12] = t, c->ctr = t, c->pc = t, c->lr = GC(canon_ret);
    ppc_dispatch(c);
}
// a frame of `size` bytes below the entry SP (back chain), LR save word in the caller's frame
inline void open_frame(Cpu* c, uint32_t sp, uint32_t lr, uint32_t size) {
    st32(sp + 4, lr);
    st32(sp - size, sp);
    c->r[1] = sp - size;
}
inline void finish(Cpu* c, const Cpu& e) {
    const uint32_t r3 = c->r[3], r4 = c->r[4];
    memcpy(c->r, e.r, sizeof c->r);
    memcpy(c->f, e.f, sizeof c->f);
    memcpy(c->cr, e.cr, sizeof c->cr);
    c->lr = e.lr, c->ctr = e.ctr, c->xer_ca = e.xer_ca, c->xer_so = e.xer_so, c->xer_ov = e.xer_ov;
    c->r[3] = r3, c->r[4] = r4;
}
// A(); if it returned 0: B(), and if that returned 0 too, stop; then C(arg)
inline void check_then_call(Cpu* c, uint32_t frame, uint32_t a_slot, uint32_t a_ret, uint32_t b_slot, uint32_t b_ret, uint32_t c_slot,
                            uint32_t c_ret, bool with_arg) {
    const Cpu e = *c;
    open_frame(c, e.r[1], e.lr, frame);
    if (with_arg) c->r[31] = e.r[3];
    call_export(c, a_slot, a_ret);
    bool go = c->r[3] != 0;
    if (!go) {
        call_export(c, b_slot, b_ret);
        go = c->r[3] != 0;
    }
    if (go) {
        if (with_arg) c->r[3] = e.r[3];
        call_export(c, c_slot, c_ret);
    }
    finish(c, e);
}
}  // namespace

void wrap_0286CA50(Cpu* c) { check_then_call(c, 0x10, 0x104B6774u, 0x0286CA68u, 0x104B6790u, 0x0286CA74u, 0x104B674Cu, 0x0286CA90u, true); }
void wrap_0286CAA4(Cpu* c) { check_then_call(c, 0x08, 0x104B6774u, 0x0286CAB4u, 0x104B6790u, 0x0286CAC0u, 0x104B676Cu, 0x0286CAD8u, false); }
void wrap_0286CAE8(Cpu* c) { check_then_call(c, 0x08, 0x104B6774u, 0x0286CAF8u, 0x104B6790u, 0x0286CB04u, 0x104B6768u, 0x0286CB1Cu, false); }
void wrap_0286D280(Cpu* c) { check_then_call(c, 0x10, 0x104B67E0u, 0x0286D298u, 0x104B67E4u, 0x0286D2A4u, 0x104B67B0u, 0x0286D2C0u, true); }
void wrap_0286D2F4(Cpu* c) { check_then_call(c, 0x08, 0x104B67E0u, 0x0286D304u, 0x104B67E4u, 0x0286D310u, 0x104B67CCu, 0x0286D328u, false); }
void wrap_0286D338(Cpu* c) { check_then_call(c, 0x08, 0x104B67E0u, 0x0286D348u, 0x104B67E4u, 0x0286D354u, 0x104B67C8u, 0x0286D36Cu, false); }

// calls export 104B6758 with r3 = the address of a local copy of the word at r3 (entry SP - 0x10)
void wrap_0286CB7C(Cpu* c) {
    const Cpu e = *c;
    open_frame(c, e.r[1], e.lr, 0x18);
    st32(e.r[1] - 0x10, ld32(e.r[3]));
    c->r[3] = e.r[1] - 0x10;
    call_export(c, 0x104B6758u, 0x0286CBA4u);
    finish(c, e);
}

// swkbd work size: 0x1980000, less 0xA0000 with option bit 0 and 0x120000 with bit 1
void work_size_0286CD54(Cpu* c) {
    uint32_t less = 0;
    if (c->r[3] & 1) less += 0xA0000;
    if (c->r[3] & 2) less += 0x120000;
    c->r[3] = 0x1980000u - less;
}

// erreula's allocator: MEMAllocFromExpHeapEx(heap at 104B6798, size, align); *out = the block; r3 = 0, or -1
// without a block
void heap_alloc_0286C590(Cpu* c) {
    const Cpu e = *c;
    open_frame(c, e.r[1], e.lr, 0x10);
    c->r[31] = e.r[5];
    c->r[3] = table(0x104B6798u), c->r[4] = e.r[3], c->r[5] = e.r[4];
    c->lr = GC(0x0286C5BCu);
    imp_coreinit_MEMAllocFromExpHeapEx(c);
    st32(e.r[5], c->r[3]);
    const uint32_t res = c->r[3] ? 0 : 0xFFFFFFFFu;
    finish(c, e);
    c->r[3] = res;
}

// erreula destroy: export 104B6760, OSDynLoad_Release(104B679C), and when the heap flag (101FC898) is set,
// MEMDestroyExpHeap(104B6798) and the flag cleared
void erreula_destroy_0286C9CC(Cpu* c) {
    const Cpu e = *c;
    open_frame(c, e.r[1], e.lr, 0x10);
    call_export(c, 0x104B6760u, 0x0286C9ECu);
    c->r[3] = table(0x104B679Cu);
    c->lr = GC(0x0286C9F8u);
    imp_coreinit_OSDynLoad_Release(c);
    const uint32_t flag = GD(0x101FC898u);
    c->r[31] = flag;
    if (ld8(flag)) {
        c->r[3] = table(0x104B6798u);
        c->lr = GC(0x0286CA10u);
        imp_coreinit_MEMDestroyExpHeap(c);
        st8(flag, 0);
    }
    finish(c, e);
}

// swkbd destroy: export 104B67BC, OSDynLoad_Release(104B6830), MEMDestroyExpHeap(104B682C)
void swkbd_destroy_0286D37C(Cpu* c) {
    const Cpu e = *c;
    open_frame(c, e.r[1], e.lr, 0x08);
    call_export(c, 0x104B67BCu, 0x0286D398u);
    c->r[3] = table(0x104B6830u);
    c->lr = GC(0x0286D3A4u);
    imp_coreinit_OSDynLoad_Release(c);
    c->r[3] = table(0x104B682Cu);
    c->lr = GC(0x0286D3B0u);
    imp_coreinit_MEMDestroyExpHeap(c);
    finish(c, e);
}

}  // namespace hostrt
