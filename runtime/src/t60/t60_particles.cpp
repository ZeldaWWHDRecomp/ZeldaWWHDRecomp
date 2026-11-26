// Particles on a general step length (t60_particles.h). Decomp build only.
#include "t60/t60_particles.h"

#include <cstdio>
#include <cstdlib>

#include "runtime.h"
#include "t60/t60.h"
#include "t60/t60_draw.h"
#include "t60/t60_world.h"

namespace t60::particles {
namespace {
constexpr uint32_t kParticles = 1u << 9;  // decomp d/d_s_play_t60.h kT60W_Particles
const bool g_switch = [] {
    const char* e = getenv("WWHD_T60_PTCL");
    return !e || atoi(e) != 0;
}();
}  // namespace
}  // namespace t60::particles
// the decompilation's world block reads the switch (decomp d/d_s_play_t60.h t60_world_converted): 0 = the particles
// stay an unconverted world system (the 30 Hz particle rules)
extern "C" __attribute__((visibility("default"))) int wwhd_t60_ptcl = getenv("WWHD_T60_PTCL") ? atoi(getenv("WWHD_T60_PTCL")) : 1;
namespace t60::particles {
namespace {
const bool g_stats = getenv("WWHD_T60_PTCL_STATS") != nullptr;
uint32_t g_n[3];
uint64_t g_t0 = 0;
}  // namespace

bool steps_here() {
    return g_switch && active() && (sub_step() || tick30()) && (world::converted_mask() & kParticles) != 0;
}
int draw_lag() { return draw::align_on() ? selected_n() - 1 : 0; }
uint64_t step_stamp() { return wwhd_t60_step_counter; }
void count(int what) {
    if (!g_stats || what < 0 || what > 2) return;
    g_n[what]++;
    const uint64_t t = timebase::now();
    if (!g_t0) g_t0 = t;
    if (t - g_t0 < 5 * timebase::kTicksPerSec) return;
    LOG("[t60] particles per 5 s: step passes %u, drawn from history %u, born after W %u", g_n[0], g_n[1], g_n[2]);
    g_n[0] = g_n[1] = g_n[2] = 0;
    g_t0 = t;
}
}  // namespace t60::particles
