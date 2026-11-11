// SPDX-License-Identifier: MPL-2.0
// Runtime functions referenced by code paths native_quiescence_test and fast_mutex_test never run. The Mac and Linux
// linkers drop those paths (dead-strip / gc-sections) before resolving them; the Windows linker
// resolves first, so the tests link these instead. Each one aborts if a test ever reaches it.
#include "runtime.h"
#include "savestate.h"
#include "platform/perf_hint.h"
#include "platform/thread_sampler.h"
#include "mods/fast_forward.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

static void unreachable(const char* name) {
    std::fprintf(stderr, "native_quiescence_test: unexpected call to %s\n", name);
    std::abort();
}

void fatal(const char* fmt, ...) { unreachable(fmt); }
uint32_t guest_call(Cpu*, uint32_t, std::initializer_list<uint32_t>) { unreachable("guest_call"); return 0; }
namespace mem {
uint32_t host_alloc(uint32_t, uint32_t) { unreachable("mem::host_alloc"); return 0; }
uint32_t runtime_alloc(uint32_t, uint32_t) { unreachable("mem::runtime_alloc"); return 0; }
void write_cstr(uint32_t, const std::string&, uint32_t) { unreachable("mem::write_cstr"); }
}  // namespace mem
namespace ss {
uint32_t snap_ld32(uint32_t) { unreachable("ss::snap_ld32"); return 0; }
}  // namespace ss
namespace perf_hint {
void add_current_thread() { unreachable("perf_hint::add_current_thread"); }
}  // namespace perf_hint
namespace thread_sampler {
void add_current_thread(const char*) { unreachable("thread_sampler::add_current_thread"); }
}  // namespace thread_sampler
// fast_mutex_test gives every threads.cpp import an external entry, so OSSleepTicks and the timed waits stay in its
// object; their virtual-time branch asks the Fast forward mod (the mutex calls the test makes never get there)
namespace mods {
bool fast_forward() { unreachable("mods::fast_forward"); return false; }
}  // namespace mods

// threads.cpp (included by the test) starts the main-thread profile; the test doesn't need it
#include "main_profile.h"
namespace mainprof { void start(Cpu*) {} }
