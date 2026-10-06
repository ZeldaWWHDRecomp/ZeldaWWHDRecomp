// Crash log helpers: which module (executable, DLL, .so, .dylib) holds a host address, and the host
// backtrace with each frame annotated that way. A crash inside a graphics driver or an overlay's
// Vulkan layer then names it (issue #41: "C0000005 at 00007FFC5B9F2A01" alone told nothing).
// Called from the crash handlers in main.cpp; see crash_addr.cpp for what is safe to call there.
#pragma once
#include <cstddef>
#include <cstdint>

namespace crash_addr {

using Out = void (*)(int fd, const char* s, size_t n);

// snprintf's result limited to what is in the buffer (it returns the untruncated length)
inline int fit(int n, size_t cap) { return n < 0 ? 0 : (size_t)n >= cap ? (int)cap - 1 : n; }

// " in amdvlk64.dll+0x1A2A01 (base 00007FFC5B850000)" (plus " [symbol+0x12]" where dladdr knows the
// nearest exported symbol) into buf; returns its length, 0 when no loaded module holds `addr`.
// `path` (optional): the module's full file name.
int describe(char* buf, size_t cap, uintptr_t addr, char* path = nullptr, size_t path_cap = 0);

// "  host backtrace:" and one line per frame, each with describe(). `context`: the faulting thread's
// state (Windows: the exception's CONTEXT*, the walk starts at the faulting instruction; elsewhere
// unused, backtrace() starts in the signal handler and passes through the signal frame).
void host_backtrace(int fd, Out out, const void* context);

#ifndef _WIN32
// the faulting instruction from the signal handler's ucontext (0 where not known)
uintptr_t context_pc(const void* ucontext);
#endif

// once, when the crash handler is installed: does the lazy work (glibc loads libgcc_s for backtrace,
// with malloc) outside the handler
void prime();

}  // namespace crash_addr
