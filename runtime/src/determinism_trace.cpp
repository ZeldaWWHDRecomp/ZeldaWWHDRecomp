// Diagnostic: WWHD_DTRACE=path writes one line per game swap (game thread) with a digest of the
// game state: g_Counter.mTimer (ctr, counts gameplay frames) and mCounter0 (tick, counts every
// game frame including loading screens), cM_rnd's seeds, the environment clock, the sky-cloud
// packet, the wind lines' slots, the live particle count and the particle system's emitter seed.
// Runs that start from the same state with the same seeds and counters (WWHD_TEST_FIXED_SEED,
// fixed_seed.h) write the same lines; the first differing line shows where two runs part.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "guest_addr.h"
#include "runtime.h"

namespace dtrace {
void on_swap(uint64_t swap, uint64_t steps) {
    static FILE* f = [] {
        const char* p = getenv("WWHD_DTRACE");
        return p ? fopen(p, "w") : nullptr;
    }();
    if (!f) return;
    const uint32_t env = GD(0x10475A68);
    const uint32_t packet = ld32(env + 0xA94);
    uint64_t h = 1469598103934665603ull;
    if (packet)
        for (uint32_t a = packet + 0xA4; a < packet + 0xA4 + 0x2C * 100; a += 4) h = (h ^ ld32(a)) * 1099511628211ull;
    // wind lines (rainWindMove): the 30 slots' states, the custom flag; live particles (its spawn gate)
    const uint32_t wind = ld32(env + 0xAAC);
    uint32_t wind_states = 0, wind_custom = 0;
    if (wind) {
        for (uint32_t i = 0; i < 30; i++) wind_states |= (ld32(wind + i * 0x34 + 0x24) ? 1u : 0u) << i;
        wind_custom = ld8(wind + 0x764);
    }
    const uint32_t proot = ld32(GD(0x1047B2D4));
    const uint32_t particles = proot ? ld32(proot + 0x24) - ld32(proot + 8) : 0;
    fprintf(f, "%llu %llu ctr %08X tick %08X rnd %08X %08X %08X time %08X clouds %016llx wind %08X %u particles %u pseed %08X\n",
            (unsigned long long)swap, (unsigned long long)steps, ld32(GD(0x101FF560)), ld32(GD(0x101FF558)), ld32(GD(0x101FF9D4)),
            ld32(GD(0x101FF9D4) + 4), ld32(GD(0x101FF9D4) + 8), ld32(env + 0x1020), (unsigned long long)h, wind_states, wind_custom,
            particles, ld32(GD(0x104B5730)));
    fflush(f);
}
}  // namespace dtrace
