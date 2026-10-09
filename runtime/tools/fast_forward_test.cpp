#include "game_clock.h"
#include "mods/fast_forward.h"
#include <cassert>
#include <cstdint>
#include <initializer_list>
int main() {
    game_clock::Clock clock;
    // Exact old clock when disabled, including fractional-vsync nanoseconds.
    for (int64_t t = 0; t < 1000000000; t += 123457) assert(clock.now(t) == t);
    int64_t host = 16683333 / 3;
    for (unsigned rate : {2u, 3u, 4u, 1u, 4u, 2u, 1u}) {
        auto before = clock.now(host);
        clock.set_rate(host, rate);
        assert(clock.now(host) == before);
        assert(clock.now(host + 1001) - before == 1001 * rate);
        assert(clock.delay(host, before + 1001 * rate) == 1001);
        assert(clock.delay(host, before - 1) == 0);
        host += 1001;
    }
    auto before = clock.now(host);
    clock.set_rate(host, 0); clock.set_rate(host, 5);
    assert(clock.rate() == 1 && clock.now(host) == before);
    // Ten thousand changes at non-vsync boundaries: no cumulative phase loss.
    for (unsigned i = 0; i < 10000; ++i) {
        const auto value = clock.now(host);
        const unsigned r = i % 4 + 1;
        clock.set_rate(host, r);
        assert(clock.now(host) == value);
        assert(clock.now(host + 37) == value + 37 * r);
        host += 37;
    }
    using namespace mods;
    for (unsigned mode = 0; mode < 256; ++mode)
        for (unsigned bits = 0; bits < 32; ++bits) {
            bool enabled = bits & 1, play = bits & 2, menu = bits & 4, blocked = bits & 8, held = bits & 16;
            bool want = enabled && play && !menu && !blocked && held && mode >= 1 && mode <= 3;
            assert(fast_forward_gate(enabled, mode, play, menu, blocked, held ? 0x40 : 0, 0x40) == want);
        }
    for (uint32_t b : {0x40000u, 0x20000u, 0x20u, 0x10u, 0x80u, 0x40u}) assert(valid_fast_forward_button(b));
    for (uint32_t b : {0u, 0x8000u, 0x4000u, 0x60u, UINT32_MAX}) assert(!valid_fast_forward_button(b));
}
