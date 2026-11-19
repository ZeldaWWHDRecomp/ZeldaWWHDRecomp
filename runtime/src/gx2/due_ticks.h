// due_ticks.h: the render thread's "is the held frame due?" check before each draw/clear/copy, without a clock call.
//
// execute_one asks render::present_due() before every draw, clear and copy (thousands per logic frame) and, while a
// frame is held, reads steady_clock each time. On the phone (Xiaomi 14) that clock read is __kernel_clock_gettime at
// ~0.6 ms per logic frame (simpleperf p1-s2c2f, 2026-11-19). WWHD_PRESENT_DUE_TICKS=1 (aarch64 only; default 0 = the
// steady_clock check as before) converts the due time once per held frame into the CPU's virtual counter
// (CNTVCT_EL0, the counter the monotonic clock itself is derived from) and then compares one register read per op.
// The conversion error over the few ms of a held frame is below a microsecond (counter and clock run at the same rate).
#pragma once
#include <cstdint>
#include <cstdlib>

namespace gx2 {
namespace due_ticks {

inline bool enabled() {
#if defined(__aarch64__)
    static const bool on = [] {
        const char* e = std::getenv("WWHD_PRESENT_DUE_TICKS");
        return e && *e && *e != '0';
    }();
    return on;
#else
    return false;
#endif
}

#if defined(__aarch64__)
inline uint64_t counter() {
    uint64_t v;
    asm volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}
inline uint64_t frequency() {
    uint64_t v;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}
#endif

// due: render::present_due() (non-zero); now_ns: the steady_clock reader (called only when `due` is new)
inline uint64_t g_converted = 0;  // due times converted (held frames seen; the preflight's proof the path runs)
template <class NowNs>
inline bool reached(int64_t due, NowNs now_ns) {
#if defined(__aarch64__)
    static int64_t s_for = 0;    // the due time the tick deadline below was made for
    static uint64_t s_tick = 0;  // its counter value
    if (due == s_for) return counter() >= s_tick;
    const uint64_t c = counter();
    const int64_t now = now_ns();
    s_for = due;
    ++g_converted;
    if (now >= due) {
        s_tick = c;
        return true;
    }
    static const uint64_t freq = frequency();
    s_tick = c + uint64_t((unsigned __int128)uint64_t(due - now) * freq / 1000000000u);
    return false;
#else
    return now_ns() >= due;
#endif
}

}  // namespace due_ticks
}  // namespace gx2
