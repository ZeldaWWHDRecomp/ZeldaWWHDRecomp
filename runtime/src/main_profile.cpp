// Main-thread profile (main_profile.h): the switch, the sampler thread and its log lines.
#include "main_profile.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "platform/host.h"
#include "runtime.h"

namespace interp { uint64_t logic_steps(); }

namespace mainprof {


namespace {
void sampler_thread() {
    host::set_thread_name("main sampler");
    host::boost_thread_priority();
    std::unordered_map<const char*, uint32_t> counts;
    uint32_t samples = 0;
    auto start = std::chrono::steady_clock::now();
    uint64_t steps0 = interp::logic_steps(), cpu0 = g_cpu_ns.load(), held0 = g_held_ns.load(), disp0 = g_dispatches.load(),
             sends0 = g_sends.load(), full0 = g_send_full.load(), fullns0 = g_send_full_ns.load();
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        counts[g_where.load(std::memory_order_relaxed)]++;
        samples++;
        const auto now = std::chrono::steady_clock::now();
        if (now - start < std::chrono::seconds(5)) continue;
        Window w;
        w.seconds = std::chrono::duration<double>(now - start).count();
        const uint64_t steps = interp::logic_steps(), cpu = g_cpu_ns.load(), held = g_held_ns.load(), disp = g_dispatches.load(),
                       sends = g_sends.load(), full = g_send_full.load(), fullns = g_send_full_ns.load();
        w.steps = steps - steps0;
        w.cpu_ns = cpu - cpu0;
        w.held_ns = held - held0;
        w.dispatches = disp - disp0;
        w.sends = sends - sends0;
        w.send_full = full - full0;
        w.send_full_ns = fullns - fullns0;
        w.samples = samples;
        for (const auto& [name, n] : counts) w.where.push_back({name ? name : "", n});
        steps0 = steps; cpu0 = cpu; held0 = held; disp0 = disp; sends0 = sends; full0 = full; fullns0 = fullns;
        counts.clear();
        samples = 0;
        start = now;
        const Report r = summarize(w);
        const char* unit = r.per_step ? "step" : "s";
        LOG("[main] %.1f s, %llu game steps; per %s: wall %.2f ms, main thread CPU %.2f ms, holds its core %.2f ms; "
            "%.0f indirect calls, %.1f OSSendMessage (queue full %.2f, %.3f ms)",
            w.seconds, (unsigned long long)w.steps, unit, r.wall_ms, r.cpu_ms, r.held_ms, r.dispatches, r.sends, r.send_full,
            r.send_full_ms);
        std::string top;
        for (const Line& l : r.top) {
            char item[128];
            snprintf(item, sizeof item, "%s%s %.2f", top.empty() ? "" : ", ", l.name.c_str(), l.ms);
            top += item;
        }
        LOG("[main] ms per %s: guest code %.2f, waits %.2f, GX2 calls %.2f, file calls %.2f, other runtime calls %.2f | %s",
            unit, r.guest_ms, r.wait_ms, r.gx2_ms, r.file_ms, r.other_ms, top.c_str());
    }
}
}  // namespace

void start(Cpu* main) {
    main->main_thread = 1;
    static std::once_flag once;
    std::call_once(once, [] {
        const char* e = getenv("WWHD_MAIN_SAMPLER");
        if (!e || !*e || !strcmp(e, "0")) return;
        g_on = true;
        g_cpu_ns.store(own_cpu_ns());
        LOG("[main] main-thread profile on (WWHD_MAIN_SAMPLER): every 5 s, per game step");
        std::thread(sampler_thread).detach();
    });
}

}  // namespace mainprof
