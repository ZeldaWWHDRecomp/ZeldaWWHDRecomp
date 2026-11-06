// SPDX-License-Identifier: MPL-2.0
// Online co-op's PvP at sea on the client (GAPS C28), the pure parts (runtime/src/mods/coop_pvp_rules.h): which hits on a
// puppet are this game's and with what, the damage, the sea squares and safe zones, whether a relayed hit is taken, the
// knockout's rupees; and the radar's zone rings (coop_radar.h).
#include <cmath>
#include <cstdio>

#include "mods/coop_pvp_rules.h"
#include "mods/coop_radar.h"

using namespace mods::coop;
using namespace mods::coop::pvp;

static int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                               \
        }                                                           \
    } while (0)

int main() {
    // whose and which weapon: only this game's Link, its items and its cannon ball count
    CHECK(weapon(kAtSword, true, false, 0) == kSword);
    CHECK(weapon(kAtSword, false, false, 0) == 0);                 // (someone else's sword)
    CHECK(weapon(kAtBomb, false, true, 0) == kCannon);
    CHECK(weapon(kAtBomb, false, false, kProcBomb) == kBomb);
    CHECK(weapon(kAtBomb, false, false, 0) == 0);                  // (an enemy's bomb or cannon)
    CHECK(weapon(kAtBoomerang, false, false, kProcBoomerang) == kBoomerang);
    CHECK(weapon(kAtArrow, false, false, kProcArrow) == kArrow && weapon(kAtLightArrow, false, false, kProcArrow) == kArrow);
    CHECK(weapon(kAtArrow, false, false, kProcBomb) == 0);         // (type and maker must agree)
    CHECK(weapon(0x8000, true, false, 0) == 0);                    // (not a type the area takes)
    CHECK((kTgTypes & 0x800000) == 0 && (kTgTypes & kAtSword) && (kTgTypes & kAtBomb));  // (no light beam, no wind)
    // damage in quarter hearts
    CHECK(damage(kSword, kAtSword, false) == 2 && damage(kSword, kAtSword, true) == 4);
    CHECK(damage(kArrow, kAtArrow, false) == 3 && damage(kArrow, kAtFireArrow, false) == 4);
    CHECK(damage(kCannon, kAtBomb, false) == 8 && damage(kBomb, kAtBomb, false) == 6 && damage(kBoomerang, kAtBoomerang, false) == 1);
    CHECK(damage(0, 0, false) == 0);
    // the squares: 0 in the north-west, row by row; the sea_boat state's place is square 17 (room 18)
    CHECK(square_of(-349999, -349999) == 0 && square_of(349999, 349999) == 48);
    CHECK(square_of(1371.6f, -118371.6f) == 17);
    CHECK(square_of(-350001, 0) == -1 && square_of(0, 350001) == -1);
    // the safe zones: how far outside the nearest edge
    const std::vector<Zone> zones = {{17, 0, -100000, 15000}, {10, -200000, -150000, 12000}};
    const Zone* z = nullptr;
    CHECK(std::fabs(zone_edge(zones, 0, -100000, &z) + 15000) < 1 && z && z->square == 17);
    CHECK(std::fabs(zone_edge(zones, 0, -80000, &z) - 5000) < 1 && z->square == 17);
    CHECK(zone_edge({}, 0, 0) > 1e20f);
    // a relayed hit: taken, a knockout, or refused here
    CHECK(take(500, 50, 3000, false, 2, 12) == Take::kHurt);
    CHECK(take(500, 50, 3000, false, 12, 12) == Take::kKnockout && take(500, 50, 3000, false, 8, 4) == Take::kKnockout);
    CHECK(take(-1, 50, 3000, false, 2, 12) == Take::kRefused);    // (the attacker is not drawn here)
    CHECK(take(4000, 50, 3000, false, 2, 12) == Take::kRefused);  // (too far)
    CHECK(take(500, 900, 3000, false, 2, 12) == Take::kRefused);  // (the hit point is not at this Link)
    CHECK(take(500, 50, 3000, true, 2, 12) == Take::kRefused);    // (busy: a cutscene, a shop)
    CHECK(take(500, 50, 3000, false, 0, 12) == Take::kRefused);
    // the knockout's rupees: a share of the wallet, at most the cap, the share at most 50 %
    CHECK(ko_rupees(500, 10, 100) == 50 && ko_rupees(5000, 10, 100) == 100 && ko_rupees(15, 10, 100) == 1);
    CHECK(ko_rupees(0, 10, 100) == 0 && ko_rupees(100, 0, 100) == 0 && ko_rupees(100, 90, 1000) == 50);
    // the radar's zone rings: in radar units, only those reaching the radar
    const float me[3] = {0, 0, -80000};
    const auto rings = radar::rings(me, 0, {{0, -100000, false}, {200000, 0, false}}, {15000, 10000});
    CHECK(rings.size() == 1 && std::fabs(rings[0].r - 15000 / radar::kRange) < 1e-4f);
    if (g_fail) std::printf("%d failed\n", g_fail);
    else std::printf("coop_pvp: all passed\n");
    return g_fail ? 1 : 0;
}
