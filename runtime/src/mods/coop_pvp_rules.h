// SPDX-License-Identifier: MPL-2.0
// Online co-op, PvP at sea (GAPS C28): the game-independent rules the game side keeps (coop_pvp.cpp; ctest coop_pvp).
// The server referees (proto/pvp.h): it validates every hit, keeps protection, the safe zones and the knockouts'
// rupee loss. The game decides what it can see: what of its own weapons hit whom, whether a hit relayed to it is
// plausible here, and what a hit does to its own Link.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mods::coop::pvp {

// the weapons (proto PvpKind)
enum Kind : uint8_t { kCannon = 1, kArrow = 2, kBomb = 3, kBoomerang = 4, kSword = 5 };
inline const char* kind_name(uint8_t k) {
    switch (k) {
    case kCannon: return "cannon";
    case kArrow: return "arrow";
    case kBomb: return "bomb";
    case kBoomerang: return "boomerang";
    case kSword: return "sword";
    default: return "?";
    }
}

// the AT types (dCcD_GObjAt's type, cCcD) a puppet's hit area takes: sword, bomb (and the boat's cannon ball, a bomb
// too), boomerang, the four arrows. Nothing else: enemies' attacks, wind, water and light pass through
constexpr uint32_t kAtSword = 0x2, kAtBomb = 0x20, kAtBoomerang = 0x40, kAtArrow = 0x4000, kAtFireArrow = 0x40000,
                   kAtIceArrow = 0x80000, kAtLightArrow = 0x100000;
constexpr uint32_t kTgTypes = kAtSword | kAtBomb | kAtBoomerang | kAtArrow | kAtFireArrow | kAtIceArrow | kAtLightArrow;

// the weapon of a hit on a puppet from its AT type and what made it: `link` (this game's Link), `cannon` (this game's
// boat's ball), else one of Link's items (`item`: its process kind as coop_items keeps them, 0 none). 0: not ours
constexpr int16_t kProcBomb = 0x126, kProcBoomerang = 0x1B0, kProcArrow = 0x1D8;
inline uint8_t weapon(uint32_t at_type, bool link, bool cannon, int16_t item) {
    if (cannon && (at_type & kAtBomb)) return kCannon;
    if (link && (at_type & kAtSword)) return kSword;
    if (item == kProcBomb && (at_type & kAtBomb)) return kBomb;
    if (item == kProcBoomerang && (at_type & kAtBoomerang)) return kBoomerang;
    if (item == kProcArrow && (at_type & (kAtArrow | kAtFireArrow | kAtIceArrow | kAtLightArrow))) return kArrow;
    return 0;
}
// the damage in quarter hearts: as the game's weapons hurt Link's foes, a little kinder (a fight lasts more than a hit)
// and capped: sword 2 (a spin or jump cut 4), arrow 3 (fire, ice, light 4), boomerang 1, bomb 6, cannon 8
inline uint8_t damage(uint8_t kind, uint32_t at_type, bool big_cut) {
    switch (kind) {
    case kSword: return big_cut ? 4 : 2;
    case kArrow: return (at_type & (kAtFireArrow | kAtIceArrow | kAtLightArrow)) ? 4 : 3;
    case kBoomerang: return 1;
    case kBomb: return 6;
    case kCannon: return 8;
    default: return 0;
    }
}

// the sea: 7x7 squares of 100,000 units from the north-west corner (the server's default world.sea_origin)
constexpr float kSeaOrigin = -350000.0f, kSquare = 100000.0f;
inline int square_of(float x, float z) {
    const int col = (int)std::floor((x - kSeaOrigin) / kSquare), row = (int)std::floor((z - kSeaOrigin) / kSquare);
    return col < 0 || col > 6 || row < 0 || row > 6 ? -1 : row * 7 + col;
}

struct Zone { int square; float x, z, r; };  // a safe zone: its centre in the world, its radius
// how far `x, z` is outside the nearest safe zone's edge (negative: inside it by that much), and that zone
inline float zone_edge(const std::vector<Zone>& zones, float x, float z, const Zone** nearest = nullptr) {
    float best = 1e30f;
    for (const Zone& s : zones) {
        const float d = std::hypot(x - s.x, z - s.z) - s.r;
        if (d < best) {
            best = d;
            if (nearest) *nearest = &s;
        }
    }
    return best;
}

// what a hit relayed to this game does here. The server checked it; this game checks what only it can see: that the
// attacker's puppet is near enough (`attacker_dist`: from its puppet here to this Link, < 0 when not shown) and the hit
// point is at this Link (`hit_dist`), and that it is not busy (a cutscene, talk or a shop started since)
enum class Take { kRefused, kHurt, kKnockout };
inline Take take(float attacker_dist, float hit_dist, float max_hit_dist, bool busy, int quarters, int life) {
    if (busy || quarters <= 0 || life <= 0) return Take::kRefused;
    if (attacker_dist < 0 || attacker_dist > max_hit_dist + 600 || hit_dist > 700) return Take::kRefused;
    return quarters >= life ? Take::kKnockout : Take::kHurt;
}

// the rupees a knockout costs (the server's answer is what counts; this is its rule, for the page and the tests)
inline int ko_rupees(int wallet, int percent, int most) {
    if (wallet <= 0 || percent <= 0) return 0;
    return std::min(wallet * std::clamp(percent, 0, 50) / 100, std::max(most, 0));
}

}  // namespace mods::coop::pvp
