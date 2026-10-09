#include "perf_metrics.h"
#include "render_prof.h"
#include <atomic>
#include <cstdio>
#include <mutex>

namespace perf {
namespace {
std::atomic<bool> active{false};
std::atomic<uint64_t> epoch{1};
std::atomic<uint64_t> gpuCount{0}, cpuCount{0}, timestampCount{0}, queryReads{0}, minimumQueryAge{UINT64_MAX};
std::mutex mutex;
Snapshot data;
// Bounded storage; a very late result is dropped rather than blocking or growing memory.
struct GpuFrame {
    uint64_t frame = UINT64_MAX, token = 0;
    unsigned pending = 0, submissions = 0;
    bool invalid = false;
    double ns = 0;
};
std::array<GpuFrame, 64> gpu;
uint64_t advanced = 0;
void publish() {
    for (auto& f : gpu) if (f.frame != UINT64_MAX && f.frame < advanced && !f.pending) {
        if (!f.invalid && f.submissions) data.gpu.add(f.ns / 1e6);
        f = {};
    }
}
void sample(bool game) {
    struct Baseline { uint64_t token = 0, cpu = 0; };
    thread_local Baseline baseline;
    if (!enabled()) { baseline = {}; return; }
    const uint64_t token = generation();
    const uint64_t cpu = rprof::thread_cpu_ns();
    ++cpuCount;
    std::lock_guard<std::mutex> lock(mutex);
    if (!active || token != epoch) { baseline = {}; return; }
    if (baseline.token == token && baseline.cpu && cpu >= baseline.cpu)
        (game ? data.game : data.render).add(double(cpu - baseline.cpu) / 1e6);
    baseline = {token, cpu};
}
std::string time(const Metric& m) {
    if (!m.count) return "n/a";
    char b[100];
    snprintf(b, sizeof b, "%.2f ms (avg %.2f)", m.current, m.average());
    return b;
}
}
void set_demand(bool overlay, bool report) {
    const bool wanted = overlay || report;
    if (active.load(std::memory_order_relaxed) == wanted) return;
    std::lock_guard<std::mutex> lock(mutex);
    active = false;
    ++epoch;
    data = {};
    gpu = {};
    advanced = 0;
    active.store(wanted, std::memory_order_release);
}
bool enabled() { return active.load(std::memory_order_acquire); }
void reset() {
    std::lock_guard<std::mutex> lock(mutex);
    ++epoch; data = {}; gpu = {}; advanced = 0;
}
uint64_t generation() { return epoch.load(std::memory_order_acquire); }
void game_frame() { sample(true); }
void render_frame() { sample(false); }
uint64_t gpu_begin(uint64_t frame) {
    if (!enabled()) return 0;
    std::lock_guard<std::mutex> lock(mutex);
    if (!active) return 0;
    auto& f = gpu[frame % gpu.size()];
    if (f.frame != frame) { f = {}; f.frame = frame; f.token = epoch; }
    ++f.pending; ++f.submissions; ++gpuCount;
    return f.token;
}
void gpu_complete(uint64_t frame, uint64_t token, double ns) {
    if (!token || !enabled()) return;
    std::lock_guard<std::mutex> lock(mutex);
    auto& f = gpu[frame % gpu.size()];
    if (!active || token != epoch || f.token != token || f.frame != frame || !f.pending) return;
    --f.pending;
    if (!std::isfinite(ns) || ns <= 0) f.invalid = true; else f.ns += ns;
    publish();
}
void gpu_advance(uint64_t frame) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> lock(mutex);
    advanced = frame;
    publish();
}
Snapshot snapshot() { std::lock_guard<std::mutex> lock(mutex); return data; }
std::string format(const Snapshot& s) {
    return "Game CPU: " + time(s.game) + "\nRender CPU: " + time(s.render) +
        "\nGPU interval: " + time(s.gpu) + "\n";
}
std::string summary() { return format(snapshot()); }
void timestamp_read(uint64_t age) {
    ++queryReads;
    auto minimum = minimumQueryAge.load();
    while (age < minimum && !minimumQueryAge.compare_exchange_weak(minimum, age)) {}
}
void timestamp_write() { ++timestampCount; }
Counters counters() { return {gpuCount.load(), cpuCount.load(), timestampCount.load(), queryReads.load(), minimumQueryAge.load()}; }
}
