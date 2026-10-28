// The DSP overload check guard (runtime/src/dsp_overload_guard.h): only the game's exact 1.1 is
// replaced, by a ratio no sub-frame timing can fall below, and WWHD_DSP_KILL keeps the game's value.
#include "dsp_overload_guard.h"

#include <cassert>
#include <cstdio>
#include <cstring>

static float as_float(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }

// updateDSP (USA 028137E0): kill when first_gap / current_gap < ratio (fcmpu, branch if not less)
static bool kills(uint32_t ratio_bits, uint32_t first_gap, uint32_t current_gap) {
    float ratio = (float)first_gap / (float)current_gap;
    return ratio < as_float(ratio_bits);
}

int main() {
    using namespace dsp_guard;
    assert(as_float(kKillRatioBits) == 1.1f);
    uint32_t out = 123;
    assert(replacement(kKillRatioBits, false, out) && out == kNeverKillBits);
    out = 123;
    assert(!replacement(kKillRatioBits, true, out) && out == 123);  // WWHD_DSP_KILL=1
    assert(!replacement(kNeverKillBits, false, out));                // already off
    assert(!replacement(0x3F800000, false, out));                    // another value: not ours to touch
    // a catch-up frame: the gap before the first sub-frame ~0.1 ms, a sub-frame 0.5 ms (OSGetTick
    // runs at 62.15 MHz): the game's ratio kills, the replaced one never does
    assert(kills(kKillRatioBits, 6215, 31075));
    assert(!kills(kNeverKillBits, 6215, 31075));
    assert(!kills(kNeverKillBits, 0, 31075));
    assert(!kills(kNeverKillBits, 0, 0));
    // an on-time frame (3 ms gap, 0.5 ms sub-frame) keeps its channels either way
    assert(!kills(kKillRatioBits, 186450, 31075));
    puts("dsp_overload_guard: ok");
}
