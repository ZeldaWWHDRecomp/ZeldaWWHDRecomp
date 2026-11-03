// SPDX-License-Identifier: MPL-2.0
// nn static wrapper replacements: the intrusive doubly linked list of nn::ut (a node is {next, prev}; the
// list keeps an element count in its first word), as measured on the recompiled originals
// (tools/host_runtime/difftest/calltest.cpp checks every one). The LR save word is written where the
// original writes it.
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

}  // namespace hostrt
