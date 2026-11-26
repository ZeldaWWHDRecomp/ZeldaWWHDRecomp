// drawoff GPU cap: see gpu_cap.h.
#include "drawoff/gpu_cap.h"
#include "drawoff/drawoff.h"
#include "cheapib/render_replay.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include "gfx/renderer.h"
#include "runtime.h"

namespace drawoff::gpu_cap {
namespace {
std::atomic<int64_t> g_frame_ns{0};  // GPU busy time per frame (ns; 0: not measured yet)
std::atomic<uint64_t> g_capped{0}, g_steps{0};

int fill_percent() {
    static const int v = [] { const char* e = getenv("WWHD_DRAWOFF_GPU_FILL"); return e ? std::clamp(atoi(e), 50, 200) : 100; }();
    return v;
}
}  // namespace

bool on() {
    static const bool v = [] {
        const char* e = cheapib::render_replay::env("WWHD_DRAWOFF_GPU_CAP");  // (the setting's set when not set)
        const bool want = e && atoi(e) != 0;
        if (want && !c2()) LOG("[drawoff] GPU cap (WWHD_DRAWOFF_GPU_CAP=1): off, needs c2 (WWHD_DRAWOFF=2)");
        const bool r = want && c2();
        if (r)
            LOG("[drawoff] GPU cap on (WWHD_DRAWOFF_GPU_CAP=1): a step plans at most the in-betweens the GPU draws with its logic "
                "frame in the step ((n + 1) x GPU frame <= %d %% of the step; command buffers timed)", fill_percent());
        return r;
    }();
    return v;
}

bool timing_wanted() { return on(); }

void gpu_interval(double ns) {  // render thread
    if (!on()) return;
    using clk = std::chrono::steady_clock;
    static clk::time_point window{};
    static double busy = 0;
    static uint64_t frames0 = 0;
    const clk::time_point now = clk::now();
    const uint64_t frames = render::frames_completed();
    if (window == clk::time_point{}) {
        window = now;
        frames0 = frames;
        busy = 0;
        return;
    }
    busy += ns;
    if (now - window < std::chrono::milliseconds(250) || frames <= frames0) return;
    // a window's busy time per completed frame; the estimate follows it at 1/2 (a few windows for a lasting change)
    const int64_t sample = int64_t(busy / double(frames - frames0));
    const int64_t old = g_frame_ns.load(std::memory_order_relaxed);
    g_frame_ns.store(old ? (old + sample) / 2 : sample, std::memory_order_relaxed);
    window = now;
    frames0 = frames;
    busy = 0;
}

int cap(int n, int64_t step) {  // game thread
    if (!on()) return n;
    const int64_t g = g_frame_ns.load(std::memory_order_relaxed);
    const int64_t room = step * fill_percent() / 100;
    const int fit = g > 0 ? int(std::max<int64_t>(1, room / g - 1)) : n;  // frames the GPU draws in the step, less the logic frame
    const int v = std::min(n, fit);
    const uint64_t s = g_steps.fetch_add(1, std::memory_order_relaxed) + 1;
    if (v < n) g_capped.fetch_add(1, std::memory_order_relaxed);
    if (s % 300 == 0) {
        LOG("[drawoff] GPU cap per 300 planned steps: %llu capped; GPU %.2f ms per frame, step %.2f ms (fill %d %%): at most %d "
            "in-betweens", (unsigned long long)g_capped.exchange(0), g / 1e6, step / 1e6, fill_percent(), fit);
    }
    return v;
}
}  // namespace drawoff::gpu_cap
