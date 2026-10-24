// SPDX-License-Identifier: MPL-2.0
// Calm water around mod islands (story_world_calm.h): the sea's flat factor with the mods' zones.
//
// daSea_packet_c::CalcFlatInterTarget (0246B848, d_a_sea) gives the factor the sea's wave height is scaled
// by this frame (0 still, 1 full waves) for the player's position; CalcFlatInter eases the sea towards it
// as before. The hook runs the game's function and lowers its answer to what the zones around that position
// allow (the smallest). On any stage but the sea, or before any package declared a zone, the answer is the
// game's, untouched.
#include "story_world_calm.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "guest_addr.h"
#include "runtime.h"
#include "story_actors.h"

extern "C" void f_0246B848_orig(Cpu* c);  // daSea_packet_c::CalcFlatInterTarget(this, cXyz* pos) -> f1

namespace {
bool on_sea() {
    static const uint32_t stage = GD(0x1046F0B0) + 0x5134;  // dComIfGp start stage name
    return std::strncmp(reinterpret_cast<const char*>(mem::ptr(stage)), "sea", 8) == 0;
}

double g_game_ns = 0, g_zone_ns = 0;
uint64_t g_calls = 0;
// test aid: WWHD_CALM_TRACE=n logs every n-th call (once per logic step on the sea): where the player is, the
// game's target and the one used, the sea's flat factor and wave scale, the water height of the sea grid at the
// player, Link's procedure and height and the boat's height
void trace(Cpu* c, uint32_t sea, uint32_t pos, float game, float used) {
    static const int every = std::getenv("WWHD_CALM_TRACE") ? std::max(1, std::atoi(std::getenv("WWHD_CALM_TRACE"))) : 0;
    static unsigned n = 0;
    if (!every || n++ % unsigned(every)) return;
    static const uint32_t play = GD(0x1046F0B0);
    const uint32_t link = ld32(play + 0x5B34), ship = ld32(play + 0x5B3C), table = ld32(sea + 0x20C);
    const float x = float(ldf32(pos)), z = float(ldf32(pos + 8));
    float grid = 0;  // the nearest point of the 65x65 height grid (800 units apart) to the player
    if (table) {
        const int gx = int(std::lround((x - float(ldf32(sea + 0x1FC))) / 800)), gz = int(std::lround((z - float(ldf32(sea + 0x200))) / 800));
        if (gx >= 0 && gx < 65 && gz >= 0 && gz < 65) grid = float(ldf32(table + 4 * uint32_t(gz * 65 + gx)));
    }
    LOG("[calm] pos %.0f %.0f game %.4f used %.4f flat %.4f scale %.3f grid_y %.2f link %.0f %.2f %.0f proc %u ship_y %.2f ns %.0f/%.0f", x, z, game,
        used, ldf32(sea + 0x130), ldf32(sea + 0xF0 + 0x24), grid, link ? ldf32(link + 0x314) : 0.0, link ? ldf32(link + 0x318) : 0.0,
        link ? ldf32(link + 0x31C) : 0.0, link ? ld32(link + 0x65F0) : 0u, ship ? ldf32(ship + 0x318) : 0.0, g_calls ? g_game_ns / double(g_calls) : 0.0,
        g_calls ? g_zone_ns / double(g_calls) : 0.0);
    (void)c;
}
}  // namespace

extern "C" void hook_0246B848(Cpu* c) {
    static const bool timing = std::getenv("WWHD_CALM_TRACE") != nullptr;
    const uint32_t sea = c->r[3], pos = c->r[4];
    const auto t0 = timing ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    f_0246B848_orig(c);
    const auto t1 = timing ? std::chrono::steady_clock::now() : t0;
    const float game = float(c->f[1].ps0);
    float used = game;
    if (story_world_calm::declared() && on_sea()) {
        story_world_calm::Zone zones[story_world_calm::kMaxZones];
        const size_t n = story_actors::calm_zones(zones, story_world_calm::kMaxZones);
        if (n) {
            used = story_world_calm::apply(game, zones, n, float(ldf32(pos)), float(ldf32(pos + 8)));
            if (used != game) c->f[1].ps0 = double(used);
        }
    }
    if (timing) {  // the game's function and what the zones add, in ns per call (WWHD_CALM_TRACE)
        const auto t2 = std::chrono::steady_clock::now();
        g_game_ns += double(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        g_zone_ns += double(std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count());
        g_calls++;
    }
    trace(c, sea, pos, game, used);
}
