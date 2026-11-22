// The play scene's world block on a general step length (t60_world.h). Decomp build only.
#include "t60/t60_world.h"

#include <cstdlib>

#include "t60/t60.h"

// the decompiled scene's converted world systems (d/d_s_play_t60.h, game build only; the runtime-only decomp build has
// no game units: nothing is converted there)
extern "C" __attribute__((weak)) uint32_t wwhd_t60_world_converted(void) { return 0; }
extern "C" __attribute__((weak)) uint32_t wwhd_t60_world_all(void) { return ~0u; }

namespace t60::world {
namespace {
const bool g_switch = [] {
    const char* e = getenv("WWHD_T60_WORLD");
    return !e || atoi(e) != 0;
}();
}  // namespace

uint32_t converted_mask() { return wwhd_t60_world_converted(); }
bool all_converted() {
    const uint32_t all = wwhd_t60_world_all();
    return g_switch && all != 0 && (converted_mask() & all) == all;
}
bool lift_hold() { return g_switch && sub_step() && converted_mask() != 0; }
}  // namespace t60::world
