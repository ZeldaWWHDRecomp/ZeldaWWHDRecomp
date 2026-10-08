#pragma once
#include <cstdint>

namespace overlay {
// Cumulative rates from counter differences, independent of how often the UI is visible.
struct PerfAverage {
    bool started = false;
    double since = 0, fps = 0, logic = 0;
    uint64_t first_frames = 0, first_steps = 0, last_frames = 0, last_steps = 0;
    int renderer = 0, mode = 0, target = 0;
    float scale = 0;
    void reset() { started = false; fps = logic = 0; }
    void sample(double now, uint64_t frames, uint64_t steps, int api, int rate_mode, int rate, float resolution) {
        if (!started || api != renderer || rate_mode != mode || rate != target || resolution != scale ||
            frames < last_frames || steps < last_steps || now < since) {
            started = true; since = now; first_frames = frames; first_steps = steps;
            renderer = api; mode = rate_mode; target = rate; scale = resolution;
            fps = logic = 0;
        } else if (now > since) {
            fps = double(frames - first_frames) / (now - since);
            logic = double(steps - first_steps) / (now - since);
        }
        last_frames = frames; last_steps = steps;
    }
};
}
