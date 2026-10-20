// SPDX-License-Identifier: MPL-2.0
// A Wizzrobe summoner's pool (story_enemy_variants "summoner_*"): the port's own declarations its summon
// door's creates become ("<id>.summon1".."<id>.summonN"), one wave of the game's summon table each. The
// game picks a slot of the wave at random and creates its group(s); a pool holds the wave's largest count
// of each kind, so every slot of the wave fits. Summons are transient (no flag, no drops; a defeated one's
// next copy is the next wave's) and count toward the overall owned-actor capacity, not the per-room or
// per-mod actor limits.
#pragma once
#include <cstdint>
#include <string>

namespace story_enemies_summons {

struct Member {
    const char* actor = nullptr;  // the manifest's actor of the kind
    unsigned count = 0;           // the most the wave makes at once
    uint16_t native = 0;          // the native profile name the game's create asks for
};
struct Pool {
    uint16_t wave;  // the summon table's wave (the summoner's z rotation word)
    Member members[2];
};
inline constexpr Pool kPools[] = {
    {0, {{"keese", 8, 0xD7}, {"magtail", 2, 0xD8}}},
    {1, {{"keese", 8, 0xD7}, {"bubble", 2, 0xCF}}},
    {2, {{"bubble", 2, 0xCF}, {}}},
    {3, {{"morth", 20, 0xCD}, {}}},
    {4, {{"chuchu", 10, 0xCE}, {}}},
    {11, {{"morth", 10, 0xCD}, {}}},
};
inline constexpr unsigned kMaxSummons = 20;  // the game's door has 20 slots

inline const Pool* pool(uint16_t wave) {
    for (const Pool& p : kPools)
        if (p.wave == wave) return &p;
    return nullptr;
}
inline unsigned size(uint16_t wave) {
    const Pool* p = pool(wave);
    unsigned n = 0;
    if (p)
        for (const Member& m : p->members) n += m.count;
    return n;
}
// The variant of the pool's `index`-th member of a kind: ChuChus cycle through the colours the game's summons
// take (green, red, blue, yellow), Bubbles red then blue; the other kinds their default.
inline std::string variant(const std::string& actor, unsigned index) {
    static const char* chuchu[] = {"green", "red", "blue", "yellow"};
    if (actor == "chuchu") return chuchu[index % 4];
    if (actor == "bubble") return index % 2 ? "blue" : "red";
    return {};
}
// The variant the game's own word for a summon asks for ("" any): a ChuChu's colour (byte 1, its summon
// colour plus 0x0A), a Bubble's (bit 0 blue)
inline std::string wanted_variant(uint16_t native, uint32_t word) {
    if (native == 0xCE) {
        static const char* colours[] = {"green", "red", "blue", "black", "yellow"};
        const int c = int((word >> 8) & 0xFF) - 0x0A;
        return c >= 0 && c < 5 ? colours[c] : "";
    }
    if (native == 0xCF) return (word & 1) ? "blue" : "red";
    return {};
}
// a summon's id and flag suffix (ids are at most 32 bytes)
inline std::string suffix(unsigned n) { return ".summon" + std::to_string(n); }

}  // namespace story_enemies_summons
