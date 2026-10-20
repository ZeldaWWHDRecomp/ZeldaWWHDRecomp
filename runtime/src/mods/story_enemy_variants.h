// The declared variants of the game's enemies a mod can place (Mod SDK v3 "parameters": {"variant": ...}):
// one table shared by the manifest validator, the runtime (the actor parameters and their checks) and
// the world editor (list_actor_types "variants", add_actor options.variant). A mod never writes raw
// parameters: each variant is a fixed, audited parameter word. The first variant of a kind is its
// default (no "variant" in the manifest), so manifests from before keep their meaning.
//
// Bokoblin variants keep their weapon an owned child of the placed copy (story_enemy_children):
// the weapon a variant creates is the only one its child route admits.
//
// ChuChu (d_a_cc): parameters are behaviour (bits 0..7), colour (8..15), notice range (16..23) and
// the spawn switch (24..31). Every variant is the ground behaviour, the default notice range and no
// switch (0xFF: the game's switch bits are never read); only the colour differs. Colour logic is local
// to the actor (red and yellow jump at Link, blue and yellow spark when hit, black turns to stone);
// its native drops are replaced by the mod's like the green one's. Pure data, unit-tested in
// runtime/tools/story_world_manifest_test.cpp.
#pragma once
#include <cstdint>
#include <string>

namespace story_enemy_variants {

struct Variant {
    const char* kind;     // the manifest's actor: chuchu, bokoblin, darknut, ...
    const char* name;     // parameters.variant
    uint32_t params;      // the fixed actor parameters
    const char* label;    // the editor's name for it
    const char* help;     // plain words
    uint16_t angle_x = 0; // the placement's x rotation word, which some kinds read as a parameter
    uint8_t path = 0;     // the mod room path it takes ("path": "<id>"): 0 none, 1 required (a line or an area), 2 required area
    uint16_t angle_z = 0; // the placement's z rotation word, which some kinds read as a parameter (a summoner's wave)
};
inline constexpr uint8_t kNoPath = 0, kPath = 1, kAreaPath = 2;

inline constexpr Variant kVariants[] = {
    {"chuchu", "green", 0xFF000000u, "Green ChuChu", "The ordinary green ChuChu."},
    {"chuchu", "red", 0xFF000100u, "Red ChuChu", "A red ChuChu: it jumps at Link when he comes close."},
    {"chuchu", "blue", 0xFF000200u, "Blue ChuChu", "A blue ChuChu: it sparks with electricity, so a sword hit shocks Link."},
    {"chuchu", "yellow", 0xFF000400u, "Yellow ChuChu", "A yellow ChuChu: it jumps at Link and sparks with electricity."},
    {"chuchu", "black", 0xFF000300u, "Black ChuChu", "A black ChuChu: swords bounce off; light turns it to stone, then Link can lift and throw it to break it (or use the hammer)."},
    // Bokoblin (d_a_bk): type 4 (the ordinary one that waits and searches), no path, no switches; bit 0x20
    // gives it a shield and 7 health instead of 5, bit 0x40 a machete (daBoko type 1) instead of the stick.
    // Rat hole (d_a_nzg): the hole itself is always the same; its three rats are ordinary rats, bomb
    // rats (each lights a bomb and chases Link) or both, one after the other.
    {"rat_hole", "rats", 0xFF010100u, "Rat hole", "A hole three rats come out of; they run at Link from behind and steal rupees."},
    {"rat_hole", "bomb_rats", 0xFF010100u, "Rat hole (bomb rats)", "A hole three bomb rats come out of: each lights a bomb, throws it at Link and runs back."},
    {"rat_hole", "mixed", 0xFF010100u, "Rat hole (mixed)", "A hole an ordinary rat, a bomb rat and another ordinary rat come out of."},
    {"bokoblin", "stick", 0xFFFFFF04u, "Bokoblin", "The ordinary Bokoblin with a stick."},
    {"bokoblin", "shield", 0xFFFFFF24u, "Bokoblin with a shield", "A Bokoblin with a stick and a shield: it blocks, and takes more hits."},
    {"bokoblin", "machete", 0xFFFFFF44u, "Bokoblin with a machete", "A Bokoblin with a machete instead of the stick."},
    // Darknut (d_a_tn): behaviour 4 (guards its place, then fights), sight range 150 or 250 (byte 1, tens of
    // units), no path (byte 2 0xFF), no spawn switch (byte 3 0xFF); bits 4..7 the armour colour. Its
    // equipment is the x rotation word's bits 5..7 (0 armour only, 2 a shield, 3 a helmet and a shield,
    // 5 a helmet, a shield and a cape: the Mighty Darknut); a helmet doubles its health (30 instead of
    // 15). The z rotation stays 0xFF: no defeat switch. Each pair is one the game itself places.
    {"darknut", "darknut", 0xFFFF9604u, "Darknut", "The armoured knight with a big sword.", 0x0000},
    {"darknut", "shield", 0xFFFF9614u, "Darknut with a shield", "A Darknut with a shield: it blocks Link's sword.", 0x0040},
    {"darknut", "helmet", 0xFFFF9654u, "Darknut with a helmet", "A Darknut with a helmet and a shield: it takes twice the hits.", 0x0060},
    {"darknut", "mighty", 0xFFFFFA54u, "Mighty Darknut", "The Mighty Darknut: helmet, shield and cape, and it notices Link from further away.", 0x00A0},
    // Keese (d_a_ki): action 0 (waits, then attacks), the nearest notice range, no path, no switch; bit 15 is
    // the Fire Keese (its own model, a burning attack, 2 damage, immune to fire). The game has no Ice Keese.
    {"keese", "normal", 0xFFFF0000u, "Keese", "The ordinary Keese."},
    {"keese", "fire", 0xFFFF8000u, "Fire Keese", "A Fire Keese: its bite sets Link on fire for a moment; fire doesn't hurt it."},
    // Peahat (d_a_ph): type 0 the Peahat, type 1 the Seahat (bigger, over the sea; it attacks only while Link
    // sails; its kill counter, event register 0x7EFF, is kept unchanged by the adapter); default ranges.
    {"peahat", "peahat", 0xFFFFFF00u, "Peahat", "The ordinary Peahat."},
    {"peahat", "seahat", 0xFFFFFF01u, "Seahat", "A Seahat: it hovers over water and dives at Link while he sails the boat."},
    // Bubble (d_a_bl): byte 0 the colour (0 red, 1 blue; never a path or the bare skull), byte 1 no switch,
    // byte 2 no path, byte 3 on the ground (it stays at the ground's height).
    {"bubble", "red", 0xFFFFFF00u, "Red Bubble", "A Red Bubble: a flaming skull; its flames set Link on fire. Strike it, or first put its fire out with the Deku Leaf's wind or a bomb."},
    {"bubble", "blue", 0xFFFFFF01u, "Blue Bubble", "A Blue Bubble: its touch curses Link (no sword or items for a moment). Put its fire out with the Deku Leaf's wind or a bomb, then strike the skull."},
    // Mothula (d_a_gm): type 1 on the ground or type 2 with wings; no switches (story_enemies_more.h)
    {"mothula", "ground", 0xFF01FFFFu, "Mothula", "A Mothula on the ground, without wings.", 0x0000},
    {"mothula", "winged", 0xFF02FFFFu, "Winged Mothula", "A Mothula that flies and dives at Link; strike its wings off first.", 0x0000},
    // Kargaroc (d_a_bb): behaviour 0 wanders and attacks, 4 perches until Link comes near; no path, switch or rider
    {"kargaroc", "flying", 0xFFFFFF00u, "Kargaroc", "A Kargaroc that circles where it was placed and swoops at Link."},
    {"kargaroc", "perched", 0xFFFFFF04u, "Perched Kargaroc", "A Kargaroc that sits where it was placed until Link comes near."},
    // Kargaroc riders: behaviour 5 carries a Moblin, 6 a Bokoblin (created by the Kargaroc's create, the port's
    // own rider declaration "<id>.rider"); it flies its room path and lets the rider go at a point marked
    // "release" (argument 3 = 5); the rider then drops and fights as an owned enemy of its own.
    {"kargaroc", "moblin_rider", 0xFFFFFF05u, "Kargaroc with a Moblin", "A Kargaroc that flies one of the mod's paths carrying a Moblin and drops it at a release point.", 0, kPath},
    {"kargaroc", "bokoblin_rider", 0xFFFFFF06u, "Kargaroc with a Bokoblin", "A Kargaroc that flies one of the mod's paths carrying a Bokoblin and drops it at a release point.", 0, kPath},
    // Wizzrobe (d_a_wz): type 0 the red one (story_enemies_more.h); type 1 the summoner: it teleports between the
    // points of its path and casts at the points of the path's next path (a summon ball, then a summon door that
    // makes the enemies of a row of the game's summon table, its wave the z rotation word, its slot random);
    // the summons are the port's own pool declarations (story_enemies_summons.h). Path byte 3 is the mod path's
    // number; no switches (bytes 1 and 2 0xFF).
    {"wizzrobe", "red", 0xFFFFFF00u, "Wizzrobe", "The red Wizzrobe: it appears and vanishes around where it was placed and casts fire at Link."},
    {"wizzrobe", "summoner_keese", 0xFFFFFF01u, "Wizzrobe summoner (Keese and Magtails)", "A Wizzrobe that teleports along one of the mod's paths and summons Keese and Magtails at the points of its next path.", 0, kPath, 0},
    {"wizzrobe", "summoner_keese_bubbles", 0xFFFFFF01u, "Wizzrobe summoner (Keese and Bubbles)", "A summoner of Keese and Bubbles.", 0, kPath, 1},
    {"wizzrobe", "summoner_bubbles", 0xFFFFFF01u, "Wizzrobe summoner (Bubbles)", "A summoner of Bubbles.", 0, kPath, 2},
    {"wizzrobe", "summoner_morths", 0xFFFFFF01u, "Wizzrobe summoner (Morths)", "A summoner of Morths: 15 or 20 at a time, as in the game.", 0, kPath, 3},
    {"wizzrobe", "summoner_chuchus", 0xFFFFFF01u, "Wizzrobe summoner (ChuChus)", "A summoner of ChuChus: 8 or 10 at a time.", 0, kPath, 4},
    {"wizzrobe", "summoner_few_morths", 0xFFFFFF01u, "Wizzrobe summoner (fewer Morths)", "A summoner of Morths: 8 or 10 at a time.", 0, kPath, 11},
    {"big_octo", "four", 0xFFFFFF00u, "Big Octo (four eyes)", "A Big Octo to shoot from the boat: hit all four eyes. It never swallows the boat and makes no whirlpool."},
    {"big_octo", "eight", 0xFFFFFF01u, "Big Octo (eight eyes)", "A Big Octo with eight eyes to shoot from the boat. It never swallows the boat and makes no whirlpool."},
    {"big_octo", "twelve", 0xFFFFFF02u, "Big Octo (twelve eyes)", "A Big Octo with twelve eyes to shoot from the boat. It never swallows the boat and makes no whirlpool."},
    // Warship (d_a_oship): sub-mode 0 stays where it was placed; sub-mode 2 sails its room path in every idle
    // range (dLib_pathMove: always round, the last point back to the first; the sea's height), slowing to a
    // stop to fire. The path byte (16) is the mod path's number; Triforce chart, salvage and switches as the
    // anchored one (story_enemies_more.h).
    {"warship", "anchored", 0xFFFFFF00u, "Warship", "The enemy warship: it stays where it was placed and fires its cannon at Link."},
    {"warship", "route", 0xFFFFFF02u, "Warship on a route", "The enemy warship sailing one of the mod's paths round and round (a line is sailed as a loop too), firing at Link.", 0, kPath},
    // Floormaster (d_a_fm): type 0 the hole that stays, type 1 the hole that wanders along its room path
    // (mode 3, round and round) and rises when Link is near; Link the only target, no exit or switches.
    {"floormaster", "hole", 0xFFFF04FFu, "Floormaster", "A Floormaster in its dark hole: it rises when Link is near and grabs him for a moment."},
    {"floormaster", "wandering", 0xFFFF05FFu, "Wandering Floormaster", "A Floormaster whose hole glides along one of the mod's paths, round and round, and rises when Link is near.", 0, kPath},
    {"gyorg", "three", 0xFFFF0531u, "Gyorgs (three)", "Three Gyorgs that come out of the sea when Link is near and bite him; defeated when all three are."},
    {"gyorg", "one", 0xFFFF0511u, "Gyorg (one)", "One Gyorg that comes out of the sea when Link is near and bites him."},
    {"gyorg", "two", 0xFFFF0521u, "Gyorgs (two)", "Two Gyorgs that come out of the sea when Link is near; defeated when both are."},
    {"gyorg", "four", 0xFFFF0541u, "Gyorgs (four)", "Four Gyorgs that come out of the sea when Link is near; defeated when all four are."},
    {"gyorg", "five", 0xFFFF0551u, "Gyorgs (five)", "Five Gyorgs that come out of the sea when Link is near; defeated when all five are."},
};

// The native enemies of the table-driven adapter (story_enemies_more.h) that mods may place: the
// manifest, the validator and the editor accept these names like the ones they list themselves.
inline constexpr const char* kMoreEnemies[] = {"stalfos", "redead", "mothula", "morth", "magtail", "poe", "armos", "armos_knight", "kargaroc", "wizzrobe", "warship", "gyorg", "floormaster", "big_octo", "wall_cannon"};
inline bool more_enemy(const std::string& actor) {
    for (const char* name : kMoreEnemies)
        if (actor == name) return true;
    return false;
}
// the kind's variant `name` ("" is the default, the kind's first)
inline const Variant* find(const std::string& kind, const std::string& name) {
    for (const Variant& v : kVariants)
        if (kind == v.kind && (name.empty() || name == v.name)) return &v;
    return nullptr;
}
inline bool has_variants(const std::string& kind) {
    unsigned n = 0;
    for (const Variant& v : kVariants) n += kind == v.kind;
    return n > 1;
}
// the variant names of a kind, for messages: "green, red, blue, yellow or black"
inline std::string names(const std::string& kind) {
    std::string out;
    unsigned total = 0, at = 0;
    for (const Variant& v : kVariants) total += kind == v.kind;
    for (const Variant& v : kVariants) {
        if (kind != v.kind) continue;
        if (at) out += ++at == total ? " or " : ", ";
        else ++at;
        out += v.name;
    }
    return out;
}
// The mod room path a native enemy takes ("path": "<id>", story_room_path.h): kNoPath, kPath (required, a
// line or an area) or kAreaPath (required, an area); by its variant ("" the default), the wall cannon always
// an area (its firing area).
inline uint8_t room_path_rule(const std::string& actor, const std::string& variant) {
    if (actor == "wall_cannon") return kAreaPath;
    const Variant* v = find(actor, variant);
    return v ? v->path : kNoPath;
}
// whether any variant of the kind takes a room path
inline bool takes_room_path(const std::string& actor) {
    if (actor == "wall_cannon") return true;
    for (const Variant& v : kVariants)
        if (actor == v.kind && v.path) return true;
    return false;
}
// A Kargaroc rider: the kind a rider variant carries ("" none) and the rider's parameter word (Bokoblin
// type 5 / Moblin mode 5: carried, then dropped by the Kargaroc), as the Kargaroc's own create makes it
// ((bb & 0xFF000000) | 0xFFFF05 with the switch byte 0xFF).
inline constexpr uint32_t kRiderParams = 0xFFFFFF05u;
inline std::string rider_kind(const std::string& actor, const std::string& variant) {
    if (actor != "kargaroc") return {};
    return variant == "moblin_rider" ? "moblin" : variant == "bokoblin_rider" ? "bokoblin" : std::string();
}
// the rider's id and flag suffix (ids are at most 32 bytes)
inline constexpr const char* kRiderSuffix = ".rider";
// how many rats a rat hole has, and whether its rat `n` (0-based) carries a bomb
inline constexpr unsigned kRatsPerHole = 3;
inline bool rat_bomb(const std::string& variant, unsigned n) { return variant == "bomb_rats" || (variant == "mixed" && n == 1); }
// the weapon (daBoko parameter) a Bokoblin variant's parameter word creates: 1 the machete, 0 the stick
inline constexpr uint32_t bokoblin_weapon(uint32_t params) { return (params & 0x40u) ? 1u : 0u; }
// the variant of a kind by name ("" the default) with its z rotation word
inline uint16_t angle_z(const std::string& kind, const std::string& name) {
    const Variant* v = find(kind, name);
    return v ? v->angle_z : 0;
}
// a Wizzrobe summoner variant (its summons are the port's pool)
inline bool summoner(const std::string& kind, const std::string& name) { return kind == "wizzrobe" && name.rfind("summoner", 0) == 0; }
// the x rotation word of the kind's variant with parameter word `params` (0 for none)
inline uint16_t angle_x(const std::string& kind, uint32_t params) {
    for (const Variant& v : kVariants)
        if (kind == v.kind && v.params == params) return v.angle_x;
    return 0;
}
// A patrolling Bokoblin: the variant's word with type 0 (it walks its route, d_a_bk jyunkai) and the route
// number kPatrolRoute (the port's route record, never a room path).
inline constexpr uint32_t kPatrolRoute = 0xFE;
inline constexpr uint32_t bokoblin_patrol(uint32_t params) { return (params & ~0x00FF000Fu) | (kPatrolRoute << 16); }
inline constexpr bool bokoblin_patrols(uint32_t params) { return ((params >> 16) & 0xFF) == kPatrolRoute; }
// A patrolling Moblin: its ordinary word (mode 0) with the route number kPatrolRoute (d_a_mo2 mPathIndex).
inline constexpr uint32_t moblin_patrol(uint32_t params) { return (params & ~0x00FF0000u) | (kPatrolRoute << 16); }
// whether a Bokoblin's or Moblin's word names the port's route
inline constexpr bool patrol_word(uint32_t params) { return ((params >> 16) & 0xFF) == kPatrolRoute; }
// whether `params` is one of the kind's declared parameter words (a Bokoblin's also as a patrol)
inline bool declared_params(const std::string& kind, uint32_t params) {
    for (const Variant& v : kVariants)
        if (kind == v.kind && (v.params == params || (kind == "bokoblin" && (bokoblin_patrol(v.params) == params || params == kRiderParams)) ||
                               (v.path && (params | (kind == "wizzrobe" ? 0xFF000000u : 0x00FF0000u)) == v.params))) return true;  // a path variant with its path's number (byte 3 for a Wizzrobe, else byte 2)
    return false;
}

}  // namespace story_enemy_variants
