#include "perf_metrics.h"
#include <cassert>
#include <limits>
#include <thread>
#include <iostream>
static uint64_t cpu = 1000000, reads = 0;
namespace rprof { uint64_t thread_cpu_ns() { ++reads; return cpu; } }
int main() {
    perf::Metric m;
    assert(m.average() == -1);
    m.add(-1); m.add(std::numeric_limits<double>::quiet_NaN());
    assert(m.count == 0);
    for (int i = 1; i <= 121; ++i) m.add(i);
    assert(m.count == 120 && m.current == 121 && m.average() == 61.5);
    assert(perf::format({}).find("GPU interval: n/a") != std::string::npos);
    perf::set_demand(false, false);
    auto before = perf::counters();
    for (int i = 0; i < 10000; ++i) {
        perf::game_frame(); perf::render_frame();
        assert(perf::gpu_begin(i) == 0);
        perf::gpu_advance(i + 1);
    }
    assert(reads == 0 && perf::counters().cpu_reads == before.cpu_reads);
    assert(perf::counters().gpu_submissions == before.gpu_submissions);
    perf::set_demand(true, false);
    perf::game_frame(); cpu += 3000000; perf::game_frame();
    assert(perf::snapshot().game.current == 3);
    // A separate thread owns the render CPU baseline, just as in the game.
    std::thread render([] { perf::render_frame(); cpu += 5000000; perf::render_frame(); }); render.join();
    assert(perf::snapshot().render.current == 5);
    auto a = perf::gpu_begin(10), b = perf::gpu_begin(10);
    perf::gpu_complete(10, b, 2000000);
    perf::gpu_advance(11);
    assert(perf::snapshot().gpu.count == 0); // incomplete frame never publishes a partial sum
    perf::gpu_complete(10, a, 1000000);
    assert(perf::snapshot().gpu.current == 3);
    auto token = perf::gpu_begin(11);
    perf::gpu_complete(11, token, 7000000);
    assert(perf::snapshot().gpu.current == 3); // at least one frame of delay
    perf::gpu_advance(12);
    assert(perf::snapshot().gpu.current == 7 && perf::snapshot().gpu.average() == 5);
    std::string report = perf::summary();
    assert(report.find("Game CPU: 3.00 ms (avg 3.00)") != std::string::npos);
    assert(report.find("Render CPU: 5.00 ms (avg 5.00)") != std::string::npos);
    assert(report.find("GPU interval: 7.00 ms (avg 5.00)") != std::string::npos);
    auto old = perf::gpu_begin(12);
    perf::set_demand(false, false);
    cpu += 1000000000;
    perf::set_demand(false, true);
    perf::game_frame(); // old CPU time must not leak into new session
    assert(perf::snapshot().game.count == 0);
    perf::gpu_complete(12, old, 999000000); perf::gpu_advance(13);
    assert(perf::snapshot().gpu.count == 0); // late callbacks don't leak either
    token = perf::gpu_begin(13);
    perf::gpu_complete(13, token, 0); perf::gpu_advance(14);
    assert(perf::snapshot().gpu.count == 0); // emulated/all-zero timestamps are unavailable
    token = perf::gpu_begin(14);
    auto token2 = perf::gpu_begin(78); // bounded slot reuse, safely discard late frame 14
    perf::gpu_complete(14, token, 1000000);
    perf::gpu_complete(78, token2, 2000000); perf::gpu_advance(79);
    assert(perf::snapshot().gpu.current == 2);
    old = perf::gpu_begin(79);
    perf::reset();
    perf::gpu_complete(79, old, 99000000); perf::gpu_advance(80);
    perf::game_frame();
    assert(perf::snapshot().gpu.count == 0 && perf::snapshot().game.count == 0);
    perf::set_demand(false, false);
    before = perf::counters();
    perf::game_frame(); perf::render_frame(); assert(perf::gpu_begin(80) == 0);
    assert(perf::counters().cpu_reads == before.cpu_reads);
    assert(perf::counters().gpu_submissions == before.gpu_submissions);
    std::cout << "perf_metrics: formatting, rolling averages, deferred GPU sums, reactivation, hidden counters PASS\n";
}
