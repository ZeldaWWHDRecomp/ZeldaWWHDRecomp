// WWHD_TEST_FIXED_SEED: the switch and the scripted-frame anchor (fixed_seed.h). The guest writes
// are in savestate.cpp (ss::service, at frame boundaries).
#include "fixed_seed.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace fixed_seed {
namespace {
std::atomic<int64_t> g_boot_tv{-1};
}

bool enabled() {
    static const bool on = enabled_value(getenv("WWHD_TEST_FIXED_SEED"));
    return on;
}

void set_boot_frame(uint64_t tv) { g_boot_tv.store(int64_t(tv), std::memory_order_relaxed); }

uint64_t script_frame(uint64_t tv) {
    if (!enabled()) return tv;
    return rebase(tv, g_boot_tv.load(std::memory_order_relaxed));
}
}  // namespace fixed_seed
