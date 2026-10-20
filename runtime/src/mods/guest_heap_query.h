// SPDX-License-Identifier: MPL-2.0
// The size and free space of a game heap (JKRHeap) for the heap diagnostics (WWHD_MOD_ACTOR_HEAPS and
// the budget checks). A heap getter can return no heap (a null pointer, or one the game has not set up
// yet), and a heap's vtable can be missing; each is a 0 answer here, never a read of guest address 0
// or a call through a null method.
#pragma once
#include <cstdint>
#include <initializer_list>
#include "runtime.h"
namespace guest_heap {
using Call = uint32_t (*)(Cpu*, uint32_t, std::initializer_list<uint32_t>);
inline constexpr uint32_t kVtable = 0xC;          // JKRHeap: its vtable
inline constexpr uint32_t kGetSize = 0x6C, kGetFreeSize = 0x74;  // JKRHeap virtual slots
// guest memory an object of `bytes` can be read from (word aligned, inside the game's memory)
inline bool readable(uint32_t a, uint32_t bytes) { return a && !(a & 3) && bytes && a >= 0x10000000u && a <= 0x50000000u - bytes; }
// a heap's virtual method `slot` called on it, or 0 without a heap, a vtable or the method
inline uint32_t query(Cpu* c, uint32_t heap, uint32_t slot, Call call) {
    if (!c || !call || !readable(heap, kVtable + 4)) return 0;
    const uint32_t vt = ld32(heap + kVtable);
    if (!readable(vt, slot + 4)) return 0;
    const uint32_t fn = ld32(vt + slot);
    return fn ? call(c, fn, {heap}) : 0;
}
inline uint32_t free_size(Cpu* c, uint32_t heap, Call call) { return query(c, heap, kGetFreeSize, call); }
inline uint32_t size(Cpu* c, uint32_t heap, Call call) { return query(c, heap, kGetSize, call); }
}  // namespace guest_heap
