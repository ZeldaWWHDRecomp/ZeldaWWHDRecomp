// WWHD_TEST_FIXED_SEED: the switch and the scripted-frame anchor (fixed_seed.h). The guest writes
// are in savestate.cpp (ss::service, at frame boundaries).
#include "fixed_seed.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace fixed_seed {
namespace {
std::atomic<int64_t> g_boot_tv{-1};
std::atomic<uint64_t (*)()> g_steps{nullptr};
}

bool enabled() {
    static const bool on = enabled_value(getenv("WWHD_TEST_FIXED_SEED"));
    return on;
}

bool step_clock() {
    static const bool on = enabled() && enabled_value(getenv("WWHD_TEST_STEP_CLOCK"));
    return on;
}

void set_step_source(uint64_t (*steps)()) { g_steps.store(steps, std::memory_order_relaxed); }

// with the step clock the anchor is the logic step of the first gameplay frame (the tv argument is not used)
void set_boot_frame(uint64_t tv) {
    auto steps = g_steps.load(std::memory_order_relaxed);
    g_boot_tv.store(int64_t(step_clock() && steps ? steps() : tv), std::memory_order_relaxed);
}

uint64_t script_frame(uint64_t tv) {
    if (!enabled()) return tv;
    if (step_clock())
        if (auto steps = g_steps.load(std::memory_order_relaxed)) tv = steps();
    return rebase(tv, g_boot_tv.load(std::memory_order_relaxed));
}
}  // namespace fixed_seed
