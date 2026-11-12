// Exact CPU-write tracking for guest memory ranges (texture change detection, Vulkan buffer cache).
//
// The game writes textures in place without always announcing it: GX2Invalidate(TEXTURE) on the exact
// range is optional on the console (the GPU reads memory directly), and the game mostly sends the
// "invalidate everything" form, which says nothing about which memory changed. Hashing every sampled
// texture every frame is too slow, and sampling it sparsely misses changes (stale textures for up to
// the periodic full check). Instead the renderers write-protect the host pages of each texture they
// have uploaded: the first CPU write to such a page (guest code, HLE memcpy, save-state restore ...)
// faults once, the handler records a write stamp for the page and makes it writable again, and the
// next lookup of a texture on that page sees the newer stamp and re-hashes the texture in full.
// Untouched textures cost one stamp comparison per page per frame and no hashing at all.
//
// Correctness of the race between arm() (render thread) and the fault handler (any thread): arm()
// reads the clock, then protects; the handler unprotects, then stamps. A write that slips in before
// the protection lands before the caller's hash (which follows arm()); a write after it faults and is
// stamped later than the clock value arm() returned. A page flagged protected is always read-only
// (the handler clears the flag only after unprotecting; arm() sets it before protecting).
//
// Kernel writes into protected pages fail instead of faulting (read(2) returns EFAULT, ReadFile
// ERROR_NOACCESS): host code that lets the kernel write guest memory brackets the call with
// HostWrite (fs.cpp's FSReadFile does).
//
// Debugging: the faults are expected. lldb stops on each one unless told otherwise
// (`process handle -p true -s false -n false SIGBUS SIGSEGV`, and on macOS
// `settings set platform.plugin.darwin.ignored-exceptions EXC_BAD_ACCESS`).
//
// Not covered: GPU writes into guest memory (neither renderer has any: render targets stay in GPU
// textures, marked gpuWritten). If page protection is unavailable, active() is false and the renderers
// keep their sampling fallback.
//
// Explicit guest signals (hints): DCFlushRange/DCStoreRange and GX2Invalidate of vertex or uniform
// buffers say "this range was written". Once a consumer calls enable_hints(), hint() stamps the pages
// in a second stamp array; changed_since() checks both arrays, written_since() (textures) only the
// fault stamps, so texture checks do not change. Hints are redundant with the faults for CPU writes
// into armed pages; they are the explicit second line (Cemu's buffer cache uses them alone).
//
// write_seq() advances after every stamp (fault, host write, hint) is stored: a consumer that saw no
// change of a range while write_seq() read S, and reads S again later, knows that nothing was stamped
// in between and can skip the page scan.
#pragma once
#include <cstddef>
#include <cstdint>

namespace wwatch {

// Tracks [base, base + size) (the guest address space). Called once; later calls are ignored.
// Returns false when write tracking cannot be used on this host.
bool init(uint8_t* base, uint64_t size);
bool active();

// Write-protects the pages of [addr, addr + size) (guest addresses) and returns the stamp to keep:
// a later written_since(..., stamp) on the same range is true iff a write may have reached it after
// this call. Hash the range only after arming it.
uint64_t arm(uint32_t addr, uint32_t size);
bool written_since(uint32_t addr, uint32_t size, uint64_t stamp);
// written_since, or a hint for one of the pages newer than stamp
bool changed_since(uint32_t addr, uint32_t size, uint64_t stamp);
uint64_t write_seq();

// Explicit guest signals (see above). hint() is a no-op until enable_hints(); any thread. Ranges of
// 256 MiB or more ("everything") are ignored, as GX2Invalidate's texture handling does.
void enable_hints();
void hint(uint32_t addr, uint32_t size);

// Brackets a kernel write (fread/ReadFile) into guest memory: the pages stay writable meanwhile and
// count as written.
void host_write_begin(uint32_t addr, uint32_t size);
void host_write_end(uint32_t addr, uint32_t size);
struct HostWrite {
    uint32_t addr, size;
    HostWrite(uint32_t a, uint32_t n) : addr(a), size(n) { host_write_begin(a, n); }
    ~HostWrite() { host_write_end(addr, size); }
};

// statistics for the renderers' periodic report: write faults and pages protected since the last call
void take_stats(uint64_t& faults, uint64_t& protectedPages);
// hints since the last call: calls and bytes covered (counted from the first call on; WWHD_WWATCH_HINT_STATS=1: always)
void take_hint_stats(uint64_t& calls, uint64_t& bytes);
// pages arm() failed to protect so far (they are reported as written on every check instead)
uint64_t protect_failures();

}  // namespace wwatch
