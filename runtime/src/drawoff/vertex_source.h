// The renderers' vertex reads at the original guest address (drawoff/drawoff.h, stage c2): with c2 a display list's
// dynamic vertex ranges are read from the copy taken when the list was called, everywhere else from guest memory.
// Outside the decomp build's fast mode the macros are the plain reads they replace (identical preprocessed text).
#pragma once
#ifdef WWHD_DECOMP_FAST
#include <cstdint>
namespace drawoff {
const uint8_t* vertex_bytes(uint32_t addr, uint32_t size);
extern bool g_vertex_on;  // c2 is on (the render thread's lookups are skipped otherwise)
inline const void* vertex_ptr(uint32_t addr, uint32_t size) {
    if (g_vertex_on)
        if (const uint8_t* b = vertex_bytes(addr, size)) return b;
    return mem::ptr(addr);
}
inline bool vertex_cache_ok(uint32_t addr, uint32_t size) { return !g_vertex_on || !vertex_bytes(addr, size); }
}  // namespace drawoff
// the bytes of the vertex range [addr, addr + size)
#define WWHD_VERTEX_PTR(addr, size) ::drawoff::vertex_ptr((addr), (size))
// the buffer cache (keyed by guest address and its writes) may serve the range
#define WWHD_VERTEX_CACHE_OK(addr, size) (buffer_cache_enabled() && ::drawoff::vertex_cache_ok((addr), (size)))
#else
#define WWHD_VERTEX_PTR(addr, size) mem::ptr(addr)
#define WWHD_VERTEX_CACHE_OK(addr, size) buffer_cache_enabled()
#endif
