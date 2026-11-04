/* Fixed low-address storage for host fixtures that exercise 32-bit guest fields.
 * Test-only: never bundled into a player seed package. */
#pragma once
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

static inline void *test_map_guest(uint32_t address, size_t bytes) {
    void *requested = (void *)(uintptr_t)address;
#ifdef _WIN32
    void *mapped = VirtualAlloc(requested, bytes, MEM_RESERVE | MEM_COMMIT,
                               PAGE_READWRITE);
#else
    void *mapped = mmap(requested, bytes, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#endif
    assert(mapped == requested);
    return mapped;
}
