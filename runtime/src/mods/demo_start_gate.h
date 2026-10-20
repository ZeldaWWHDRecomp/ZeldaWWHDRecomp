// SPDX-License-Identifier: MPL-2.0
// Mod demos: a demo step that comes right after another waits for the previous demo's cleanup (its
// message window closing, its archives given back) instead of being refused. The gate decides, once
// per logic step, whether the waiting demo starts, keeps waiting, or gives up after a bounded time.
#pragma once
#include <cstdint>

namespace demo_start_gate {
inline constexpr uint32_t kLimitTicks = 240;  // 8 s at 30 Hz: the cleanup itself takes 10 ticks, 130 with a closing message
enum class Step { Wait, Start, GiveUp };
struct Gate {
    bool armed = false;
    uint32_t ticks = 0;
    void arm() { armed = true; ticks = 0; }
    void reset() { armed = false; ticks = 0; }
    // cleaning: the previous demo is still being cleared; busy: some other demo record is there
    Step step(bool cleaning, bool busy, uint32_t limit = kLimitTicks) {
        if (!armed) return Step::GiveUp;
        if (busy && !cleaning) { reset(); return Step::GiveUp; }
        if (!cleaning) { armed = false; return Step::Start; }
        if (++ticks > limit) { reset(); return Step::GiveUp; }
        return Step::Wait;
    }
};
}  // namespace demo_start_gate
