// Vulkan guest buffer cache (design: buffer_cache_core.h). On by default on macOS, off elsewhere:
//   WWHD_VK_BUFFER_CACHE=0|1       vertex arrays, index arrays and uniform blocks come from persistent
//                                  GPU copies, re-uploaded only when their pages were written
//   WWHD_VK_BUFFER_CACHE_VERIFY=1  (implies the cache) compares every hit with guest memory and logs
//                                  mismatches; slow, for validation runs
//   WWHD_VK_BUFFER_CACHE_MB=n      budget of resident GPU copies (default 256)
//   WWHD_VK_BUFFER_CACHE_HINTS=0   ignore DCFlushRange/DCStoreRange and GX2Invalidate (faults only)
// Needs page write tracking (write_watch.h); without it the cache stays off.
#pragma once
#include <cstdint>
#include <vector>
#include "buffer_cache_core.h"

namespace gfxvk {
struct UploadSlice;

bool buffer_cache_enabled();
bool buffer_cache_verify();
bufcache::Cache& buffer_cache();
// the entry's region as a draw binding of size bytes
UploadSlice buffer_cache_slice(const bufcache::Entry& entry, uint64_t size);

// Raw guest bytes [addr, addr + size) (vertex arrays, uniform blocks): true and out set, or false (dynamic
// range, too large, no memory): copy through the upload arena. uploadKind: rprof::Upload.
bool cached_guest_range(uint32_t addr, uint32_t size, int uploadKind, UploadSlice& out,
                         bufcache::Entry** retained = nullptr);
// Native (little-endian) index data: like cached_guest_range, plus the entry, whose shadow holds the
// same bytes on the CPU (extent scans never read GPU memory).
bool cached_native_indices(uint32_t addr, uint32_t size, UploadSlice& out, bufcache::Entry*& entry);
// Verify mode: report a mismatch found by Cache::verify
void buffer_cache_mismatch(const char* what, const bufcache::Entry& entry, uint32_t size, uint32_t firstDiff);

void buffer_cache_end_frame();                       // render thread, once per swap
void buffer_cache_invalidate_all();                  // save-state load (any thread)
void buffer_cache_guest_invalidate(uint32_t flags, uint32_t addr, uint32_t size);  // GX2Invalidate
void buffer_cache_free(const std::vector<bufcache::Region>& regions);  // their submission completed
void buffer_cache_report(double frames);             // periodic statistics line
}  // namespace gfxvk
