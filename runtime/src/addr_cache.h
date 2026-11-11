// addr_cache.h: guest address -> host object, read without a lock (decomp build fast mode, WWHD_DECOMP_FAST_MUTEX).
//
// threads.cpp's ObjTable looks every OSMutex up in an unordered_map under one process-wide mutex, on every
// OSLockMutex / OSUnlockMutex / OSTryLockMutex. Host mutexes are never deleted or replaced (OSInitMutex re-initialises
// the same object in place), so the address -> object pair, once made, holds for the life of the process. This cache
// keeps those pairs for readers that take no lock:
//  - open addressing, linear probe over at most PROBE entries; key 0 = empty (no OSMutex lives at guest address 0;
//    a lookup of 0 always misses and takes the locked path);
//  - one writer at a time (the caller holds the table's mutex): the object pointer is stored first, then the key with
//    release order; a reader loads the key with acquire order and only when it equals its address loads the object,
//    so it never sees a key without its object. Entries are never changed or removed once written;
//  - a full probe window or a full table only means the locked path for that address (never a wrong object).
#pragma once
#include <atomic>
#include <cstdint>

template <class T, unsigned LOG2N = 12, unsigned PROBE = 16>
struct AddrCache {
    static constexpr unsigned N = 1u << LOG2N;
    struct Entry {
        std::atomic<uint32_t> key{0};
        std::atomic<T*> obj{nullptr};
    };
    Entry e[N];
    static unsigned home(uint32_t a) { return (unsigned)(((a >> 2) * 0x9E3779B1u) >> (32 - LOG2N)); }
    // the object cached for A, or null (not cached)
    T* find(uint32_t a) const {
        if (!a) return nullptr;
        unsigned i = home(a);
        for (unsigned p = 0; p < PROBE; p++, i = (i + 1) & (N - 1)) {
            const uint32_t k = e[i].key.load(std::memory_order_acquire);
            if (k == a) return e[i].obj.load(std::memory_order_relaxed);
            if (!k) return nullptr;
        }
        return nullptr;
    }
    // with the owner's lock held: remember A -> O (O must stay A's object for the life of the process)
    void insert(uint32_t a, T* o) {
        if (!a || !o) return;
        unsigned i = home(a);
        for (unsigned p = 0; p < PROBE; p++, i = (i + 1) & (N - 1)) {
            const uint32_t k = e[i].key.load(std::memory_order_relaxed);
            if (k == a) return;
            if (!k) {
                e[i].obj.store(o, std::memory_order_relaxed);
                e[i].key.store(a, std::memory_order_release);
                return;
            }
        }
    }
};
