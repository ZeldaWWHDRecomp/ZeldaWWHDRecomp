// SPDX-License-Identifier: MPL-2.0
// resmgr_fit.h (issue #281): an archive copied out of a loaded pack is loaded only when its heap holds the game's
// three allocations (the buffer, the archive resource, its holder); the probe gives every allocation back, so the
// game's own allocations that follow land where the probe's did.
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include "resmgr_fit.h"

namespace {
// A first-fit heap of `size` bytes from 0x10000000, with alignment, like the load task's ExpHeap
struct Heap {
    uint32_t base = 0x10000000, size;
    std::map<uint32_t, uint32_t> used;  // address -> bytes
    std::vector<uint32_t> calls;        // sizes asked for, in order
    explicit Heap(uint32_t s) : size(s) {}
    uint32_t alloc(uint32_t bytes, uint32_t align) {
        calls.push_back(bytes);
        uint32_t at = base;
        for (;;) {
            at = (at + align - 1) & ~(align - 1);
            if (uint64_t(at) + bytes > uint64_t(base) + size) return 0;
            bool clash = false;
            for (const auto& [a, n] : used)
                if (at < a + n && a < at + bytes) {
                    at = a + n;
                    clash = true;
                    break;
                }
            if (!clash) return used[at] = bytes, at;
        }
    }
    void release(uint32_t a) { assert(used.erase(a) == 1); }
};
resmgr_fit::Fit probe(Heap& h, uint32_t size) {
    return resmgr_fit::probe([&](uint32_t bytes, uint32_t heap, uint32_t align) { assert(heap == 7); return h.alloc(bytes, align); },
                             [&](uint32_t a) { h.release(a); }, 7, size);
}
}  // namespace

int main() {
    using resmgr_fit::Fit;
    assert(resmgr_fit::buffer_bytes(1) == 32 && resmgr_fit::buffer_bytes(32) == 32 && resmgr_fit::buffer_bytes(33) == 64);
    {  // room for all three: fits, and nothing stays allocated
        Heap h(0x100000);
        assert(probe(h, 0x8000) == Fit::Fits && h.used.empty());
        // the game's order: the buffer (rounded to 32 bytes), the archive resource, the holder
        assert((h.calls == std::vector<uint32_t>{0x8000, resmgr_fit::kResourceBytes, resmgr_fit::kHolderBytes}));
        // the game's own allocations afterwards succeed where the probe's did
        assert(h.alloc(0x8000, 0x2000) && h.alloc(0x44, 0x2000) && h.alloc(0x54, 4));
    }
    {  // the reported crash: the buffer does not fit (the game wrote the archive to address 0)
        Heap h(0x4000);
        assert(probe(h, 0x8000) == Fit::NoBuffer && h.used.empty() && h.calls.size() == 1);
    }
    {  // the buffer fits, the archive resource (alignment 0x2000) does not: setBuffer would write through null
        Heap h(0x8020);
        assert(probe(h, 0x8000) == Fit::NoResource && h.used.empty());
    }
    {  // only the holder is missing
        Heap h(0x8044);
        assert(probe(h, 0x8000) == Fit::NoHolder && h.used.empty());
        Heap g(0x8044 + 0x54);
        assert(probe(g, 0x8000) == Fit::Fits && g.used.empty());
    }
    std::puts("resmgr_fit_test: ok");
    return 0;
}
