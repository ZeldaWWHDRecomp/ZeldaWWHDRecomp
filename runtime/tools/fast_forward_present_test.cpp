// SPDX-License-Identifier: MPL-2.0
// Exercise the real Vulkan mode selection, including release and FIFO-only
// surfaces. No window, Vulkan device, or game files are needed.
#include "gfx/vulkan/settings.h"
#include "game_clock.h"
#include "gx2/vsync_pacing.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
namespace { bool boost = false, benchmark = false; }
namespace mods { bool fast_forward_active() { return boost; } }
namespace gx2 { bool uncapped() { return benchmark; } }
namespace interp { void toggle_fps(int) {} int mode() { return 0; } void set_mode(int) {} }
namespace gfxvk { float requested_res_scale() { return 1; } void set_res_scale(float) {} }
void log_msg(const char*, ...) {}
int main() {
    using namespace gfxvk;
    for (unsigned offered : {1u, 3u, 5u, 7u}) {
        set_present_modes_offered(offered);
        for (int saved = 0; saved < kPresentModes; ++saved) {
            set_present_mode(saved);
            for (bool uncapped : {false, true}) {
                benchmark = uncapped;
                boost = false;
                const int normal = effective_present_mode();
                assert(present_acquire_timeout(false) == UINT64_MAX);
                assert(present_acquire_timeout(true) == 0);
                boost = true;
                const int during = effective_present_mode();
                assert(present_acquire_timeout(false) == 0);
                assert(present_acquire_timeout(true) == 0);
                assert(during == ((offered & 4) ? kPresentImmediate : (offered & 2) ? kPresentMailbox : kPresentFifo));
                assert(present_mode() == saved);  // no persistent setting changes
                boost = false;
                assert(effective_present_mode() == normal);
            }
        }
    }
    set_present_modes_offered(7);
    set_present_mode(kPresentFifo);
    boost = true;
    benchmark = false;
    for (int hz : {60, 120}) {
        for (unsigned rate : {2u, 3u, 4u}) {
            // A 1 ms render pass, with physical presentation completing on the
            // next refresh when effective_present_mode() selects FIFO.
            constexpr vsync_pacing::Clock grid{16'683'333, 4};
            game_clock::Clock clock;
            clock.set_rate(0, rate);
            int64_t host = 0, shift = 0;
            uint64_t tick = 0;
            for (unsigned i = 0; i < 1200; ++i) {
                host += 1'000'000;
                if (effective_present_mode() == kPresentFifo) {
                    const int64_t refresh = 1'000'000'000 / hz;
                    host = (host / refresh + 1) * refresh;
                }
                const auto due = tick + 8;
                host += clock.delay(host, grid.time(due, shift));
                tick = vsync_pacing::flip_executed(grid, shift, due, clock.now(host), 4);
            }
            const double steps = 1200e9 / double(host);
            std::printf("simulated %d Hz capped %ux: %.3f logic steps/s\n", hz, rate, steps);
            assert(std::abs(steps - rate * 1e9 / (2 * grid.period)) < .01);
        }
    }
    // Virtual flips still follow the accelerated 59.94 Hz clock, even when
    // host presentation is nonblocking. Each frame is one original 30 Hz step.
    constexpr vsync_pacing::Clock vsync{16'683'333, 4};
    for (unsigned rate : {2u, 3u, 4u}) {
        game_clock::Clock clock;
        clock.set_rate(0, rate);
        int64_t host = 0;
        for (uint64_t step = 1; step <= 600; ++step) {
            const auto due = vsync.time(step * 8, 0);
            host += clock.delay(host, due);
            assert(clock.now(host) >= due);
            assert(clock.now(host) - due < rate);
        }
        const double steps = 600e9 / double(host);
        assert(std::abs(steps - rate * 1e9 / (2 * vsync.period)) < 0.001);
        std::printf("%ux virtual clock: %.3f logic steps/s\n", rate, steps);
        const auto before = clock.now(host);
        clock.set_rate(host, 1);
        assert(clock.now(host) == before);
        assert(clock.delay(host, before + 2 * vsync.period) == 2 * vsync.period);
    }
}
