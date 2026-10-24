#pragma once
// A cheap identity of the calling thread, unique among live threads, without a thread_local (gx2's
// display-list recording slots look the recording thread up by it on every emit).
#include <cstdint>
#if defined(_WIN64) && (defined(__x86_64__) || defined(_M_X64))
#include <intrin.h>
#else
#include <pthread.h>
#endif

namespace gx2 {
inline uintptr_t thread_identity() {
#if defined(__aarch64__) && defined(__linux__)  // Linux and Android: TPIDR_EL0 is the thread pointer
    return (uintptr_t)__builtin_thread_pointer();
#elif defined(__x86_64__) && defined(__linux__)
    uintptr_t p;
    __asm__("mov %%fs:0, %0" : "=r"(p));  // the thread control block's self pointer
    return p;
#elif defined(_WIN64) && (defined(__x86_64__) || defined(_M_X64))
    return (uintptr_t)__readgsqword(0x30);  // the thread's TEB
#else
    // macOS: pthread_self() reads TPIDRRO_EL0 / %gs; __builtin_thread_pointer (TPIDR_EL0) is not per
    // thread there (threads share values)
    return (uintptr_t)pthread_self();
#endif
}
}  // namespace gx2
