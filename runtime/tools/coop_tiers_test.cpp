// SPDX-License-Identifier: MPL-2.0
// Online co-op's distance tiers on the client (GAPS C13), the pure parts: the cheap puppet's gait by speed (with its
// hysteresis) and the sea radar's dots (runtime/src/mods/coop_emotes.h, coop_radar.h).
#include <cmath>
#include <cstdio>

#include "mods/coop_emotes.h"
#include "mods/coop_radar.h"

using namespace mods::coop;

static int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                               \
        }                                                           \
    } while (0)
static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

int main() {
    using emotes::gait_for;
    // the gaits by speed, and no flicker around a step: up at 40 and 330, down below 25 and 290
    CHECK(gait_for(0, emotes::kWait) == emotes::kWait && gait_for(39, emotes::kWait) == emotes::kWait);
    CHECK(gait_for(41, emotes::kWait) == emotes::kWalk && gait_for(30, emotes::kWalk) == emotes::kWalk && gait_for(20, emotes::kWalk) == emotes::kWait);
    CHECK(gait_for(331, emotes::kWalk) == emotes::kDash && gait_for(300, emotes::kDash) == emotes::kDash && gait_for(280, emotes::kDash) == emotes::kWalk);
    CHECK(gait_for(1000, emotes::kWait) == emotes::kDash);
    // a speed wobbling across a step changes the gait once, not every time
    emotes::Gait g = emotes::kWait;
    int changes = 0;
    for (int i = 0; i < 100; i++) {
        const emotes::Gait n = gait_for(i % 2 ? 36.f : 44.f, g);
        changes += n != g, g = n;
    }
    CHECK(changes == 1);

    // the radar: up is the camera's view, right its right; beyond the range on the rim
    const float me[3] = {1000, 0, 2000};
    // camera looking +z (yaw 0): a player 12500 ahead (+z) is half way up; one 12500 to +x is to the camera's left
    auto d = radar::place(me, 0, {{1000, 14500, false}, {13500, 2000, true}});
    CHECK(d.size() == 2 && near(d[0].x, 0) && near(d[0].y, 0.5f) && !d[0].rim);
    CHECK(near(d[1].x, -0.5f) && near(d[1].y, 0) && d[1].party);
    // camera looking +x (yaw 90 degrees): the +x player is ahead now
    d = radar::place(me, 3.14159265f / 2, {{13500, 2000, false}});
    CHECK(near(d[0].x, 0) && near(d[0].y, 0.5f));
    // far out: on the rim, its direction kept
    d = radar::place(me, 0, {{1000, 2000 + 100000, false}});
    CHECK(d[0].rim && near(d[0].y, 1) && near(d[0].x, 0));

    std::printf(g_fail ? "coop_tiers: %d failed\n" : "coop_tiers: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
