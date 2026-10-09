// Presentation history for the shared, 30 Hz minigame countdown. No guest state is changed.
#pragma once
#include <algorithm>
#include <cstdint>

namespace countdown {
struct Tick {
    uint32_t object = 0, remaining = 0;
    uint64_t step = 0;
    bool advanced = false;

    int display_ms(int native_ms, uint32_t timer, uint32_t frames, uint64_t logic_step,
                   float fraction, bool running, bool interpolating) const {
        if (!interpolating || !running || !advanced || timer != object || frames != remaining)
            return native_ms;
        double shown;
        if (logic_step == step) shown = double(frames) + 1.0 - fraction;
        // Some layouts update before the timer's draw callback. Predict only the next tick
        // of the same running timer; a reset, pause, load or a missed update discards history.
        else if (logic_step == step + 1) shown = double(frames) - fraction;
        else return native_ms;
        return int(std::max(0.0, shown) * 1000.0 / 30.0);
    }
};
}  // namespace countdown

struct Cpu;
namespace countdown { void refresh(Cpu*); void reset(); }
