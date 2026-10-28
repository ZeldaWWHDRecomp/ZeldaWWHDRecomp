#pragma once
// Main-thread profile (WWHD_MAIN_SAMPLER=1, also from Android's env.txt) and its pure report maths.
//
// The game's main thread builds every frame; where its time goes decides the frame rate on phones.
// While on, the main thread names what it is in: the runtime call (each HLE function, by name), a
// wait inside one ("wait: event", "wait: message receive"...), or waiting for its emulated core
// ("core wait"); nothing named = the game's own (recompiled) code. A sampler thread reads that name
// 1,000 times a second and every 5 s logs, per game step (logic step: 30 a second at full speed):
// wall time, the main thread's CPU time, the time it held its core, indirect calls (ppc_dispatch),
// OSSendMessage calls and queue-full waits, then the time per step of guest code, waits, GX2 calls,
// file calls (FS*) and the busiest runtime calls. Off: one predictable branch per runtime call.
// Idea from SwitchWakerHD (thanks @Pulpparty / @depende3000: HleMark, its main-thread sampler).
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "ppc.h"
#include <cstddef>

// main_thread sits in former padding: the layout save states store is unchanged
static_assert(offsetof(Cpu, main_thread) == 172 && offsetof(Cpu, f) == 176, "Cpu layout changed");

namespace mainprof {

// set once when the main thread starts (before it runs guest code), never cleared
inline bool g_on = false;
// what the main thread is in (only it writes; the sampler reads)
inline std::atomic<const char*> g_where{nullptr};
// counters of the main thread (only it writes)
inline std::atomic<uint64_t> g_dispatches{0}, g_sends{0}, g_send_full{0}, g_send_full_ns{0};

inline bool on(const Cpu* c) { return __builtin_expect(g_on, 0) && c && c->main_thread; }

struct Mark {
    const char* prev = nullptr;
    bool active;
    Mark(const Cpu* c, const char* name) : active(on(c)) {
        if (active) {
            prev = g_where.load(std::memory_order_relaxed);
            g_where.store(name, std::memory_order_relaxed);
        }
    }
    ~Mark() {
        if (active) g_where.store(prev, std::memory_order_relaxed);
    }
    Mark(const Mark&) = delete;
    Mark& operator=(const Mark&) = delete;
};
inline void bump(std::atomic<uint64_t>& a, uint64_t v = 1) {
    a.store(a.load(std::memory_order_relaxed) + v, std::memory_order_relaxed);  // single writer
}

// the main thread starts: marks its Cpu, reads the switch, starts the sampler (threads.cpp)
void start(Cpu* main);
// the main thread's CPU time so far and the time it held its core, as of its last core release
inline std::atomic<uint64_t> g_held_ns{0}, g_cpu_ns{0};
inline uint64_t own_cpu_ns() {
    // (MinGW's winpthreads provides CLOCK_THREAD_CPUTIME_ID too: no <windows.h> in this header)
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
// the main thread gives up its core after holding it held_ns (threads.cpp core_release)
inline void note_release(uint64_t held_ns) {
    bump(g_held_ns, held_ns);
    g_cpu_ns.store(own_cpu_ns(), std::memory_order_relaxed);
}

// ---- report maths (no clocks, no threads: runtime/tools/main_profile_test.cpp)
struct Window {
    double seconds = 0;
    uint64_t steps = 0;           // game logic steps in the window
    uint64_t cpu_ns = 0;          // main thread's CPU time (0: unknown)
    uint64_t held_ns = 0;         // time it held its emulated core
    uint64_t dispatches = 0, sends = 0, send_full = 0, send_full_ns = 0;
    uint32_t samples = 0;         // sampler ticks in the window
    std::vector<std::pair<std::string, uint32_t>> where;  // samples per name ("" = guest code); sums to samples
};
struct Line { std::string name; double ms; };
struct Report {
    bool per_step = false;  // false: no step in the window (paused, loading): per second instead
    double unit_div = 1;    // steps, or seconds
    double wall_ms = 0, cpu_ms = 0, held_ms = 0;
    double dispatches = 0, sends = 0, send_full = 0, send_full_ms = 0;
    double guest_ms = 0, wait_ms = 0, gx2_ms = 0, file_ms = 0, other_ms = 0;
    std::vector<Line> top;  // busiest named entries (waits and runtime calls), ms per unit
};
inline bool starts(const std::string& s, const char* p) { return s.compare(0, strlen(p), p) == 0; }
// waits: the profile's own wait names, and the GX2 calls that wait for the GPU or the display
inline bool is_wait(const std::string& s) {
    return starts(s, "wait") || s == "core wait" || (starts(s, "GX2") && (s.find("Wait") != std::string::npos || s == "GX2DrawDone"));
}
inline Report summarize(const Window& w, size_t top_n = 8) {
    Report r;
    r.per_step = w.steps > 0;
    r.unit_div = r.per_step ? double(w.steps) : std::max(w.seconds, 1e-9);
    const double wall_ms = w.seconds * 1000.0;
    r.wall_ms = wall_ms / r.unit_div;
    r.cpu_ms = w.cpu_ns / 1e6 / r.unit_div;
    r.held_ms = w.held_ns / 1e6 / r.unit_div;
    r.dispatches = w.dispatches / r.unit_div;
    r.sends = w.sends / r.unit_div;
    r.send_full = w.send_full / r.unit_div;
    r.send_full_ms = w.send_full_ns / 1e6 / r.unit_div;
    if (!w.samples) return r;
    const double per_sample = r.wall_ms / w.samples;  // ms per unit that one sample stands for
    for (const auto& [name, n] : w.where) {
        const double ms = n * per_sample;
        if (name.empty()) r.guest_ms += ms;
        else if (is_wait(name)) r.wait_ms += ms;
        else if (starts(name, "GX2")) r.gx2_ms += ms;
        else if (starts(name, "FS")) r.file_ms += ms;
        else r.other_ms += ms;
        if (!name.empty()) r.top.push_back({name, ms});
    }
    std::sort(r.top.begin(), r.top.end(), [](const Line& a, const Line& b) { return a.ms > b.ms || (a.ms == b.ms && a.name < b.name); });
    if (r.top.size() > top_n) r.top.resize(top_n);
    return r;
}

}  // namespace mainprof
