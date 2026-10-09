// Performance overlay measurements. GPU intervals include queue stalls; they are not GPU load.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace perf {
struct Metric {
    static constexpr unsigned capacity = 120;
    std::array<double, capacity> values{};
    unsigned count = 0, next = 0;
    double current = -1, total = 0;
    void add(double ms) {
        if (!std::isfinite(ms) || ms < 0) return;
        current = ms;
        if (count == capacity) total -= values[next]; else ++count;
        total += values[next] = ms;
        next = (next + 1) % capacity;
    }
    double average() const { return count ? total / count : -1; }
};
struct Snapshot { Metric game, render, gpu; };
// Report sampling is active while the Graphics panel (which contains Copy report) is visible.
void set_demand(bool overlay, bool report);
bool enabled();
void reset();
uint64_t generation();
void game_frame();
void render_frame();
// Token is the demand generation. Completion callbacks from an old session are discarded.
uint64_t gpu_begin(uint64_t frame);
void gpu_complete(uint64_t frame, uint64_t token, double ns);
void gpu_advance(uint64_t frame);
Snapshot snapshot();
std::string format(const Snapshot&);
std::string summary();
// Debug evidence: count timestamp writes/Metal timing handlers and CPU-clock reads only when on.
void timestamp_read(uint64_t age);
void timestamp_write(); // called only at actual Vulkan timestamp writes
struct Counters { uint64_t gpu_submissions = 0, cpu_reads = 0, timestamp_writes = 0, query_reads = 0, minimum_query_age = UINT64_MAX; };
Counters counters();
} // namespace perf
