// State the draw threads keep per thread. Ours, MPL-2.0 like the rest of the runtime.
//
// Unless a switch in draw_thread_switches.h allows a draw thread this session, only the render thread records, so a
// value lives in one ordinary global and reading it costs a test of a constant. Otherwise each thread has its own
// thread_local copy.
#pragma once
#include "gfx/vulkan/draw_thread_switches.h"

namespace draw_thread_state {
inline const bool kSplit = gfxvk::draw_switches::any_thread();
template <class Key, class T> inline T shared_value{};
template <class Key, class T> inline thread_local T own_value{};
// The thread_local copy is reached only through this call. Written inline (`kSplit ? own_value : shared_value`), the
// compiler computes both addresses and selects one, so the thread_local's address was resolved at every access even
// with no draw thread; libmain's TLS is dynamic (a dlopen'ed library on Android), so each of those went through the
// linker's TLS descriptor resolver (~40 per draw: about 0.3 ms of the render thread a frame at the cliff, Xiaomi 14).
template <class Key, class T> [[gnu::noinline, gnu::cold]] T& own_value_slow() { return own_value<Key, T>; }
// the calling thread's T for Key (value-initialised on first use)
template <class Key, class T> inline T& value() {
    if (kSplit) [[unlikely]] return own_value_slow<Key, T>();
    return shared_value<Key, T>;
}
}  // namespace draw_thread_state

// In a function, in place of `static T name;` (a memo or scratch the draw path keeps): `DRAW_THREAD_STATIC(name, T);`
// (T last: it may hold commas). Value-initialised, so a non-zero start value goes in a struct's member initialiser.
#define DRAW_THREAD_STATIC(name, ...) \
    struct name##_key_;               \
    __VA_ARGS__& name = draw_thread_state::value<name##_key_, __VA_ARGS__>()
// At namespace scope, in place of a global `T name;`: `DRAW_THREAD_VAR(name, T)` declares the accessor name(), the
// calling thread's T.
#define DRAW_THREAD_VAR(name, ...) \
    struct name##_key_;            \
    inline __VA_ARGS__& name() { return draw_thread_state::value<name##_key_, __VA_ARGS__>(); }
