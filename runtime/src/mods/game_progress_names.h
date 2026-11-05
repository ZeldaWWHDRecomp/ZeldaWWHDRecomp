// SPDX-License-Identifier: MPL-2.0
// The names Game progress shows (game_progress.h entries, game_progress_places.h places): the game's own words in the
// game's language where it has them, read at run time from the player's 2D pack (message files, by label: nothing of
// the game ships), else English. Pure: game_progress_test.
//
// Where the game has them (labels from GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60, MPL-2.0, GameText.java):
//   an item's name: the coloured part of its "You got ..." message, message.msbt label 00101 + item id;
//   a song's name: the coloured part of "You conducted the ...", label 01453 + melody;
//   a figurine's name: the first line of message4.msbt label 14301 + figurine number;
//   the dungeons: DungeonMap_00.msbt T_DungeonName_00_01..06; the sea's squares: MenuMiiverseContent_00.msbt
//   Topic_Tag_place00..48.
// The treasure charts' printed numbers by their save number: TWWHD Randomizer's item data (SuperDude88 and
// contributors, MIT; runtime/third_party/twwhd_randomizer/NOTICE.md), the same as the fork's table.
#pragma once
#include <cstdio>
#include <map>
#include <string>

#include "game_progress.h"
#include "game_progress_places.h"

namespace game_progress_names {

struct Texts {
    std::map<std::string, std::string> message, message4, dungeons, places;  // msbt label -> text (msbt::texts marks)
};

inline const char* const kDungeonEn[6] = {"Forsaken Fortress", "Dragon Roost Cavern", "Forbidden Woods", "Tower of the Gods",
                                          "Earth Temple", "Wind Temple"};
// the sea's squares (sea rooms 1..49)
inline const char* const kSquareEn[49] = {
    "Forsaken Fortress", "Star Island", "Northern Fairy Island", "Gale Isle", "Crescent Moon Island", "Seven-Star Isles",
    "Overlook Island", "Four-Eye Reef", "Mother and Child Isles", "Spectacle Island", "Windfall Island", "Pawprint Isle",
    "Dragon Roost Island", "Flight Control Platform", "Western Fairy Island", "Rock Spire Isle", "Tingle Island",
    "Northern Triangle Island", "Eastern Fairy Island", "Fire Mountain", "Star Belt Archipelago", "Three-Eye Reef",
    "Greatfish Isle", "Cyclops Reef", "Six-Eye Reef", "Tower of the Gods", "Eastern Triangle Island", "Thorned Fairy Island",
    "Needle Rock Isle", "Islet of Steel", "Stone Watcher Island", "Southern Triangle Island", "Private Oasis", "Bomb Island",
    "Bird's Peak Rock", "Diamond Steppe Island", "Five-Eye Reef", "Shark Island", "Southern Fairy Island", "Ice Ring Isle",
    "Forest Haven", "Cliff Plateau Isles", "Horseshoe Island", "Outset Island", "Headstone Island", "Two-Eye Reef",
    "Angular Isles", "Boating Course", "Five-Star Isles"};
inline const char* const kSongEn[6] = {"Wind's Requiem", "Ballad of Gales", "Command Melody", "Earth God's Lyric",
                                       "Wind God's Aria", "Song of Passing"};
// how a place is found (GP_S_*)
inline const char* const kWayEn[GP_S_UNKNOWN + 1] = {"Chest", "Submarine", "Sunken treasure", "Big Octo", "Gunboat", "On the ground",
                                                     "Reward", "Auction", "Minigame", "Letter", "Shop", "Boss", "Fish man",
                                                     "Great Fairy", ""};
inline const char* const kPartEn[GP_PARTS] = {"Dungeons", "Hearts", "Items", "Equipment", "Upgrades", "Songs",
                                              "Pearls and Triforce", "Treasure charts", "Sea chart", "Nintendo Gallery"};
// the treasure charts' printed numbers by save number (1..51; 0: not a numbered treasure chart); the Triforce Charts
// 1, 2, 3 are save numbers 1, 3, 5 (TWWHD Randomizer item_data.yaml: Game Item Id = 0xFF - save number)
inline constexpr unsigned char kChartNo[52] = {0,  0,  42, 0,  43, 0,  44, 45, 46, 11, 15, 30, 20, 5,  23, 31, 33, 2,
                                               38, 39, 24, 6,  12, 35, 1,  29, 34, 18, 16, 28, 4,  3,  40, 10, 14, 0,
                                               0,  9,  22, 36, 17, 25, 37, 8,  26, 41, 19, 32, 13, 21, 27, 7};

// some English item names, for a pack that has no message file (tests, a missing language)
inline std::string item_en(unsigned id) {
    switch (id) {
    case 0x07: return "Piece of Heart";
    case 0x08: return "Heart Container";
    case 0x20: return "Telescope";
    case 0x21: return "Tingle Bottle";
    case 0x22: return "Wind Waker";
    case 0x23: return "Picto Box";
    case 0x24: return "Spoils Bag";
    case 0x25: return "Grappling Hook";
    case 0x26: return "Deluxe Picto Box";
    case 0x27: return "Hero's Bow";
    case 0x28: return "Power Bracelets";
    case 0x29: return "Iron Boots";
    case 0x2A: return "Magic Armor";
    case 0x2C: return "Bait Bag";
    case 0x2D: return "Boomerang";
    case 0x2F: return "Hookshot";
    case 0x30: return "Delivery Bag";
    case 0x31: return "Bombs";
    case 0x33: return "Skull Hammer";
    case 0x34: return "Deku Leaf";
    case 0x38: return "Hero's Sword";
    case 0x39: return "Master Sword";
    case 0x3B: return "Hero's Shield";
    case 0x3C: return "Mirror Shield";
    case 0x43: return "Hero's Charm";
    case 0x4C: return "Dungeon Map";
    case 0x4D: return "Compass";
    case 0x4E: return "Big Key";
    case 0x50: return "Empty Bottle";
    case 0x61: return "Triforce Shard";
    case 0x69: return "Nayru's Pearl";
    case 0x6A: return "Din's Pearl";
    case 0x6B: return "Farore's Pearl";
    case 0x77: return "Swift Sail";
    case 0x78: return "Sail";
    case 0xAB: return "Wallet";
    case 0xAC: return "Wallet";
    case 0xAD: case 0xAE: return "Bomb Bag";
    case 0xAF: case 0xB0: return "Quiver";
    case 0xCC: return "Treasure Chart";
    default: return "";
    }
}

class Names {
public:
    Texts t;

    // the first coloured part of a message ("You got the *Telescope*!"), "" if none
    static std::string coloured(const std::string& msg) {
        const size_t a = msg.find('*'), b = a == std::string::npos ? a : msg.find('*', a + 1);
        if (b == std::string::npos) return "";
        std::string s;
        bool space = false;
        for (size_t i = a + 1; i < b; i++) {
            const char c = msg[i] == '\n' ? ' ' : msg[i];
            if (c == ' ') { space = !s.empty(); continue; }
            if (space) s += ' ', space = false;
            s += c;
        }
        return s;
    }
    static std::string label(unsigned n) {
        char b[8];
        std::snprintf(b, sizeof b, "%05u", n);
        return b;
    }
    static std::string capital(std::string s) {
        if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = char(s[0] - 'a' + 'A');
        return s;
    }
    static std::string one_line(std::string s) {
        for (char& c : s)
            if (c == '\n') c = ' ';
        return s;
    }

    std::string item(unsigned id) const {
        auto it = t.message.find(label(101 + id));
        std::string n = it == t.message.end() ? "" : capital(coloured(it->second));
        // (the Deluxe Picto Box's message names only the Picto Box in some languages)
        if (id == 0x26 && (n.empty() || n == item(0x23))) return "Deluxe Picto Box";
        return n.empty() ? item_en(id) : n;
    }
    std::string dungeon(unsigned n) const {  // 1..6
        if (n < 1 || n > 6) return "";
        char k[32];
        std::snprintf(k, sizeof k, "T_DungeonName_00_%02u", n);
        auto it = t.dungeons.find(k);
        return it == t.dungeons.end() || it->second.empty() ? kDungeonEn[n - 1] : one_line(it->second);
    }
    std::string square(unsigned room) const {  // 1..49
        if (room < 1 || room > 49) return "";
        char k[32];
        std::snprintf(k, sizeof k, "Topic_Tag_place%02u", room - 1);
        auto it = t.places.find(k);
        return it == t.places.end() || it->second.empty() ? kSquareEn[room - 1] : one_line(it->second);
    }
    std::string song(unsigned m) const {
        if (m > 5) return "";
        auto it = t.message.find(label(1453 + m));
        const std::string n = it == t.message.end() ? "" : coloured(it->second);
        return n.empty() ? kSongEn[m] : n;
    }
    std::string figurine(unsigned n) const {
        auto it = t.message4.find(label(14301 + n));
        if (it != t.message4.end()) {
            const std::string first = it->second.substr(0, it->second.find('\n'));
            if (!first.empty()) return first;
        }
        return "Figurine " + std::to_string(n + 1);
    }
    std::string chart(unsigned n) const {  // by save number
        if (n == 1 || n == 3 || n == 5) return "Triforce Chart " + std::to_string(n / 2 + 1);
        const std::string base = item(0xCC);
        return n < 52 && kChartNo[n] ? base + " " + std::to_string(kChartNo[n]) : base;
    }
    std::string upgrade(unsigned id) const {
        static const char* const amount[] = {"1000", "5000", "60", "99", "60", "99"};
        return id >= 0xAB && id <= 0xB0 ? item(id) + " (" + amount[id - 0xAB] + ")" : "";
    }

    std::string entry(const gp_entry& e) const {
        switch (e.kind) {
        case GP_E_DUNGEON: return dungeon(e.arg);
        case GP_E_HEART_PIECES: return item(0x07);
        case GP_E_ITEM: return item(e.arg);
        case GP_E_FIRE_ICE: return "Fire & Ice Arrows";
        case GP_E_LIGHT: return "Light Arrows";
        case GP_E_BOTTLES: return item(0x50);
        case GP_E_MASTER_HALF: return "Master Sword (half power)";
        case GP_E_MASTER_FULL: return "Master Sword (full power)";
        case GP_E_PIRATE: return "Pirate's Charm";
        case GP_E_UPGRADE: return upgrade(e.arg);
        case GP_E_MAGIC: return "Magic meter (double)";
        case GP_E_SONG: return song(e.arg);
        case GP_E_PEARL: return item(e.arg);
        case GP_E_SHARD: return item(0x61) + " " + std::to_string(e.arg);
        case GP_E_CHARTS: return "Treasure charts found";
        case GP_E_SALVAGED: return "Sunken treasures salvaged";
        case GP_E_SQUARE: return square(e.arg);
        case GP_E_FIGURINE: return figurine(e.arg);
        default: return "";
        }
    }

    std::string place(const gp_place& p) const {
        static const unsigned equip_item[9] = {0x38, 0x39, 0, 0, 0x3B, 0x3C, 0x28, 0, 0x43};
        static const unsigned upgrade_item[6] = {0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0};
        static const unsigned pearl_item[3] = {0x69, 0x6A, 0x6B};
        switch (p.kind) {
        case GP_K_HEART: return item(0x07);
        case GP_K_CONTAINER: return item(0x08);
        case GP_K_MAP: return item(0x4C) + " \xC2\xB7 " + dungeon(p.what - 1u);
        case GP_K_COMPASS: return item(0x4D) + " \xC2\xB7 " + dungeon(p.what - 1u);
        case GP_K_BIGKEY: return item(0x4E) + " \xC2\xB7 " + dungeon(p.what - 1u);
        case GP_K_BOSS: return "Boss \xC2\xB7 " + dungeon(p.what - 1u);
        case GP_K_CHART: return chart(p.what);
        case GP_K_SALVAGE: return chart(p.what) + " (its treasure)";
        case GP_K_SQUARE: return "Sea chart square";
        case GP_K_ITEM: return item(p.what);
        case GP_K_ARROWS: return p.what == 2 ? "Fire & Ice Arrows" : "Light Arrows";
        case GP_K_BOTTLE: return item(0x50);
        case GP_K_EQUIP:
            if (p.what < 9 && equip_item[p.what]) return item(equip_item[p.what]);
            return p.what == 2 ? "Master Sword (half power)" : p.what == 3 ? "Master Sword (full power)" : "Pirate's Charm";
        case GP_K_UPGRADE: return p.what < 6 ? upgrade(upgrade_item[p.what]) : "Magic meter (double)";
        case GP_K_SONG: return song(p.what);
        case GP_K_PEARL: return p.what < 3 ? item(pearl_item[p.what]) : "";
        case GP_K_SHARD: return item(0x61) + " " + std::to_string(p.what);
        case GP_K_GALLERY: return "Nintendo Gallery figurines";
        default: return "";
        }
    }
};

}  // namespace game_progress_names
