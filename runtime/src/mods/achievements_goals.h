// SPDX-License-Identifier: MPL-2.0
// The achievements' goals (built-in mod "Achievements", achievements.cpp): each one checked on the save data as it is
// now (game_progress.h's dSv_save_c layout), plus the stage in play and the read before for the few that watch a change. Pure: achievements_test.
//
// From GreenNaugahyde/ZeldaWWHDRecompAndroid db65d60 (MPL-2.0), android/.../Achievements.java: its 105 goals, their
// keys, groups, points, titles and descriptions (its own texts) and their checks. The flags are kept only where they are
// confirmed (achievements_test, docs in achievements.cpp): by the GameCube decompilation's actor that sets the flag
// (zeldaret/tww, CC0), by the HD decompilation, or by real saves; `confirmed` says which, and the unconfirmed ones are
// left out of kAll (listed in kUnconfirmed, never checked).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

#include "game_progress.h"

namespace achievements {

enum Group : uint8_t { kStory, kItems, kSongs, kQuests, kCollect, kGallery, kGroups };
inline const char* const kGroupEn[kGroups] = {"Story", "Items and upgrades", "Songs", "Side quests", "Collections", "Nintendo Gallery"};

// what a check reads (achievements.cpp fills it once a second)
struct Read {
    const unsigned char* d = nullptr;  // GP_SAVE_LEN bytes of save data (big-endian, as in guest memory)
    char stage[9] = {};                // the stage in play
    bool hero = false;                 // the file is a Hero Mode file (HD SaveMgr's slot flags of the file in play, byte 6)
    int64_t now_ms = 0;                // a wall clock (the trees' 20 minutes)
    int64_t* trees_from = nullptr;     // the time the first withered tree was watered (kept by the caller per save game; 0 none)
    const Read* prev = nullptr;        // the read before (a second earlier), null at the first

    unsigned u8(unsigned o) const { return d[o]; }
    unsigned long u32(unsigned o) const { return gp_u32(d, o); }
    bool event(unsigned flag) const { return (d[GP_EVENTS + (flag >> 8)] & (flag & 0xFF)) == (flag & 0xFF); }
    unsigned reg(unsigned b) const { return d[GP_EVENTS + b]; }
    bool item(unsigned slot) const { return d[GP_ITEMS + slot] != 0xFF; }
    unsigned stage_byte(int st, unsigned o) const { return gp_stage_byte(d, st, o); }
    bool stage_bit(int st, unsigned o, unsigned bit) const { return (stage_byte(st, o) >> bit) & 1; }
    // chest bits (mTbox, a big-endian u32): all of mask
    bool chests(int st, unsigned long mask) const {
        unsigned long v = 0;
        for (unsigned i = 0; i < 4; i++) v = v << 8 | stage_byte(st, i);
        return (v & mask) == mask;
    }
    bool song(unsigned m) const { return (d[GP_TACT] >> m) & 1; }
    bool pearl(unsigned p) const { return (d[GP_PEARLS] >> p) & 1; }
    bool chart(unsigned set, unsigned n) const { return gp_chart(d, set, n); }
    // the charts the game counts (game_progress.h): set 1 got, set 3 salvaged
    unsigned charts(unsigned set) const {
        unsigned c = 0;
        for (unsigned n = 1; n <= 51; n++)
            if (n != 35 && n != 36) c += (unsigned)gp_chart(d, set, n);
        return c;
    }
    unsigned squares() const {
        unsigned c = 0;
        for (unsigned n = 0; n < 49; n++) c += d[GP_FMAP + n] & 1;
        return c;
    }
    unsigned figurines(unsigned from, unsigned to) const {
        unsigned c = 0;
        for (unsigned f = from; f < to; f++) c += (unsigned)gp_figurine(d, f);
        return c;
    }
    unsigned puzzles() const { return d[GP_INFO + 0x57]; }  // dSv_player_info_c: the cabana's slide puzzles solved
    unsigned clear_count() const { return d[GP_INFO + 0x58]; }
    bool bow(unsigned got) const {
        return item(12) && ((d[GP_GOT_ITEMS + 12] & got) || d[GP_ITEMS + 12] >= (got == 2 ? 0x35u : 0x36u));
    }
    bool at(const char* name) const { return !std::strcmp(stage, name) && prev && !std::strcmp(prev->stage, name); }
};

// how a goal is confirmed (the evidence in achievements_test and the PR notes)
enum Confirmed : uint8_t {
    kCount,      // the same save bytes Game progress counts (items, songs, pearls, charts, squares, figurines...)
    kDecomp,     // the GameCube or HD decompilation: the actor that sets the flag, or the field's own code
    kSaves,      // real saves: set in the finished test save, not in new files, in step with the story's order
};

struct Goal {
    const char* key;
    Group group;
    uint8_t points;
    const char* title;
    const char* desc;     // "%s" gets `arg`'s name: an item (item > 0), a place (place: a sea room, or -n the dungeon n)
    int item, place;
    bool (*met)(const Read&);
    bool once;            // a goal on the change between two reads: earned at the first read it holds in
    Confirmed confirmed;
};

namespace detail {
// the eight withered trees (event register 0x9E, a bit each): all eight within 20 minutes of the first; the first one's
// time is kept, so a file loaded with some watered already doesn't count
inline bool trees(const Read& s) {
    if (!s.trees_from) return false;
    const unsigned now = s.reg(0x9E), before = s.prev ? s.prev->reg(0x9E) : 0x100;
    if (before == 0 && now && !(now & (now - 1))) *s.trees_from = s.now_ms;
    if (!now) *s.trees_from = 0;
    return now == 0xFF && *s.trees_from && s.now_ms - *s.trees_from <= 20 * 60000LL;
}
inline const unsigned kSpecialCharts[12] = {35, 36, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61};
}  // namespace detail

// places for the texts (sea rooms; negative: dungeons)
enum : int { kOutset = 44, kWindfall = 11, kDragonRoost = 13, kFlightPlatform = 14, kForestHaven = 41, kTower = -4, kFortress = -1,
             kFireMountain = 20, kPrivateOasis = 33, kIceRing = 40 };

inline const char* const kGet = "Get the %s.", *const kLearn = "Learn the %s.", *const kDungeonChests = "Open every chest in the %s.",
                         *const kRoom = "Complete the gallery room of %s.";

// clang-format off
inline const Goal kAll[] = {
    // the story
    {"set_sail", kStory, 5, "Off to Sea", "Leave %s on the pirate ship.", 0, kOutset, [](const Read& s) { return s.stage_bit(0, 0x0A, 4); }, false, kSaves},
    {"king_red_lions", kStory, 3, "A Boat That Talks", "Meet the King of Red Lions.", 0, 0, [](const Read& s) { return s.stage_bit(0, 0x06, 2); }, false, kSaves},
    {"pearl_din", kStory, 5, "Gift of the Rito", "Get Din's Pearl.", 0, 0, [](const Read& s) { return s.pearl(1); }, false, kCount},
    {"pearl_farore", kStory, 5, "Gift of the Forest", "Get Farore's Pearl.", 0, 0, [](const Read& s) { return s.pearl(2); }, false, kCount},
    {"pearl_nayru", kStory, 5, "Gift of the Sea", "Get Nayru's Pearl.", 0, 0, [](const Read& s) { return s.pearl(0); }, false, kCount},
    {"tower_rises", kStory, 3, "Rising Tower", "Raise the %s from the sea.", 0, kTower, [](const Read& s) { return s.stage_bit(0, 0x1B, 0); }, false, kSaves},
    {"bell", kStory, 10, "The Bell Tolls", "Ring the bell at the top of the %s.", 0, kTower, [](const Read& s) { return s.stage_bit(0, 0x09, 7); }, false, kSaves},
    {"master_sword", kStory, 10, "The Blade of Evil's Bane", kGet, 0x39, 0, [](const Read& s) { return (s.u8(GP_COLLECT) & 2) != 0; }, false, kCount},
    {"tetra", kStory, 10, "Pirate Princess", "Find out who Tetra really is.", 0, 0, [](const Read& s) { return s.event(0x3280); }, false, kSaves},
    {"sword_half", kStory, 5, "Half Awake", "Restore half of the Master Sword's power.", 0, 0, [](const Read& s) { return (s.u8(GP_COLLECT) & 4) != 0; }, false, kCount},
    {"sword_full", kStory, 5, "Fully Awake", "Restore the Master Sword's full power.", 0, 0, [](const Read& s) { return (s.u8(GP_COLLECT) & 8) != 0; }, false, kCount},
    {"triforce", kStory, 10, "Courage Made Whole", "Find all eight Triforce Shards.", 0, 0, [](const Read& s) { return s.u8(GP_TRIFORCE) == 0xFF; }, false, kCount},
    {"the_end", kStory, 25, "A New Land", "Beat the game and save the cleared file.", 0, 0, [](const Read& s) { return s.clear_count() > 0; }, false, kDecomp},
    {"hero_mode", kStory, 5, "No Hearts to Spare", "Play a file in Hero Mode.", 0, 0, [](const Read& s) { return s.hero; }, false, kDecomp},
    {"the_end_hero", kStory, 50, "Hero of Legend", "Beat the game in Hero Mode and save the cleared file.", 0, 0, [](const Read& s) { return s.clear_count() > 0 && s.hero; }, false, kDecomp},
    // items and upgrades
    {"bait_bag", kItems, 3, "Ready to Bait", kGet, 0x2C, 0, [](const Read& s) { return s.item(11); }, false, kCount},
    {"wind_waker", kItems, 3, "Maestro", kGet, 0x22, 0, [](const Read& s) { return s.item(2); }, false, kCount},
    {"bottle", kItems, 3, "Bottled Up", kGet, 0x50, 0, [](const Read& s) { return s.item(14) || s.item(15) || s.item(16) || s.item(17); }, false, kCount},
    {"grappling_hook", kItems, 5, "Swing Time", kGet, 0x25, 0, [](const Read& s) { return s.item(3); }, false, kCount},
    {"deku_leaf", kItems, 5, "Gone with the Wind", kGet, 0x34, 0, [](const Read& s) { return s.item(6); }, false, kCount},
    {"boomerang", kItems, 3, "Comes Back Around", kGet, 0x2D, 0, [](const Read& s) { return s.item(5); }, false, kCount},
    {"bombs", kItems, 5, "Explosive Start", kGet, 0x31, 0, [](const Read& s) { return s.item(13); }, false, kCount},
    {"tingle_bottle", kItems, 5, "Message in a Bottle", kGet, 0x21, 0, [](const Read& s) { return s.item(7); }, false, kCount},
    {"bow", kItems, 3, "Bowstring", kGet, 0x27, 0, [](const Read& s) { return s.item(12); }, false, kCount},
    {"skull_hammer", kItems, 5, "Hammer Time", kGet, 0x33, 0, [](const Read& s) { return s.item(20); }, false, kCount},
    {"fire_ice", kItems, 3, "Hot and Cold", "Get the Fire and Ice Arrows.", 0, 0, [](const Read& s) { return s.bow(2); }, false, kCount},
    {"iron_boots", kItems, 3, "Heavy Footed", kGet, 0x29, 0, [](const Read& s) { return s.item(9); }, false, kCount},
    {"power_bracelets", kItems, 5, "Strong Arms", kGet, 0x28, 0, [](const Read& s) { return (s.u8(GP_COLLECT + 2) & 1) != 0; }, false, kCount},
    {"mirror_shield", kItems, 3, "Reflections", kGet, 0x3C, 0, [](const Read& s) { return (s.u8(GP_COLLECT + 1) & 2) != 0; }, false, kCount},
    {"hookshot", kItems, 5, "Reach Out", kGet, 0x2F, 0, [](const Read& s) { return s.item(19); }, false, kCount},
    {"light_arrows", kItems, 5, "Arrow of Light", "Get the Light Arrows.", 0, 0, [](const Read& s) { return s.bow(4); }, false, kCount},
    {"magic_armor", kItems, 5, "Impenetrable", kGet, 0x2A, 0, [](const Read& s) { return s.item(10); }, false, kCount},
    {"heros_charm", kItems, 5, "Face of a Hero", kGet, 0x43, 0, [](const Read& s) { return (s.u8(GP_COLLECT + 4) & 1) != 0; }, false, kCount},
    {"deluxe_picto", kItems, 5, "Full Colour", kGet, 0x26, 0, [](const Read& s) { return s.u8(GP_ITEMS + 8) == 0x26; }, false, kCount},
    {"swift_sail", kItems, 10, "Full Speed Ahead", kGet, 0x77, 0, [](const Read& s) { return s.u8(GP_ITEMS + 1) == 0x77; }, false, kCount},
    {"bottles", kItems, 10, "Bottle Collector", "Have all four bottles.", 0, 0, [](const Read& s) { return s.item(14) && s.item(15) && s.item(16) && s.item(17); }, false, kCount},
    {"magic", kItems, 5, "Twice the Magic", "Double your magic meter.", 0, 0, [](const Read& s) { return s.u8(GP_MAX_MAGIC) >= 32; }, false, kCount},
    {"hearts", kItems, 10, "Big Hearted", "Have 20 hearts.", 0, 0, [](const Read& s) { return (s.u8(GP_MAX_LIFE) << 8 | s.u8(GP_MAX_LIFE + 1)) >= 80; }, false, kCount},
    {"wallet_1000", kItems, 5, "Pocket Money", kGet, 0xAB, 0, [](const Read& s) { return s.u8(GP_WALLET) >= 1; }, false, kCount},
    {"wallet_5000", kItems, 10, "Rolling in Rupees", kGet, 0xAC, 0, [](const Read& s) { return s.u8(GP_WALLET) >= 2; }, false, kCount},
    {"bombs_60", kItems, 5, "More Bombs", kGet, 0xAD, 0, [](const Read& s) { return s.u8(GP_BOMB_MAX) >= 60; }, false, kCount},
    {"bombs_99", kItems, 10, "Bombs Away", kGet, 0xAE, 0, [](const Read& s) { return s.u8(GP_BOMB_MAX) >= 99; }, false, kCount},
    {"arrows_60", kItems, 3, "More Arrows", kGet, 0xAF, 0, [](const Read& s) { return s.u8(GP_ARROW_MAX) >= 60; }, false, kCount},
    {"arrows_99", kItems, 5, "Arrows Galore", kGet, 0xB0, 0, [](const Read& s) { return s.u8(GP_ARROW_MAX) >= 99; }, false, kCount},
    // songs (the item id field holds the melody for the name)
    {"song_requiem", kSongs, 3, "Change of Wind", kLearn, -1000, 0, [](const Read& s) { return s.song(0); }, false, kCount},
    {"song_ballad", kSongs, 3, "Ride the Gale", kLearn, -1001, 0, [](const Read& s) { return s.song(1); }, false, kCount},
    {"song_command", kSongs, 3, "In Command", kLearn, -1002, 0, [](const Read& s) { return s.song(2); }, false, kCount},
    {"song_earth", kSongs, 3, "Song of the Earth", kLearn, -1003, 0, [](const Read& s) { return s.song(3); }, false, kCount},
    {"song_wind", kSongs, 3, "Song of the Wind", kLearn, -1004, 0, [](const Read& s) { return s.song(4); }, false, kCount},
    {"song_passing", kSongs, 3, "Day and Night", kLearn, -1005, 0, [](const Read& s) { return s.song(5); }, false, kCount},
    // side quests
    {"grandma_chest", kQuests, 2, "Grandma's Secret", "Open the chest hidden in Grandma's house.", 0, 0, [](const Read& s) { return (s.stage_byte(0x0B, 3) & 0x09) == 0x09; }, false, kSaves},
    {"grandma_soup", kQuests, 5, "Feeling Better", "Cheer Grandma up with a fairy and get her soup.", 0, 0, [](const Read& s) { return s.event(0x2A20); }, false, kSaves},
    {"black_pig", kQuests, 3, "Truffle Hunter", "Let the black pig dig up its treasure on %s.", 0, kOutset, [](const Read& s) { return s.stage_bit(0, 0x17, 2); }, false, kSaves},
    {"orca_500", kQuests, 5, "Sword Training", "Land 500 hits in Orca's training.", 0, 0, [](const Read& s) { return s.reg(0xD0) >= 3; }, false, kDecomp},
    {"orca_1000", kQuests, 10, "A Thousand Hits", "Land 1,000 hits in Orca's training.", 0, 0, [](const Read& s) { return s.reg(0xD0) >= 4; }, false, kDecomp},
    {"hurricane_spin", kQuests, 3, "Spin to Win", "Learn Orca's last sword technique.", 0, 0, [](const Read& s) { return s.event(0x0B20); }, false, kDecomp},
    {"killer_bees", kQuests, 5, "Hide and Seek", "Find all the Killer Bees on %s.", 0, kWindfall, [](const Read& s) { return s.event(0x1340); }, false, kDecomp},
    {"joy_pendant", kQuests, 3, "Birthday Gift", "Bring Mrs. Marie a birthday present.", 0, 0, [](const Read& s) { return s.reg(0xC0) != 0; }, false, kDecomp},
    {"lenzo", kQuests, 5, "Picto Assistant", "Take all three pictos Lenzo asks for.", 0, 0, [](const Read& s) { return s.reg(0xC4) >= 6; }, false, kDecomp},
    {"anton_linda", kQuests, 5, "Matchmaker", "Bring Anton and Linda together.", 0, 0, [](const Read& s) { return s.event(0x2280); }, false, kSaves},
    {"maggie_moe", kQuests, 5, "Love Letters", "Carry the letters between Maggie and Moe and take Maggie's thanks.", 0, 0, [](const Read& s) { return (s.reg(0xAC) & 3) == 3; }, false, kSaves},
    {"lighthouse", kQuests, 5, "Light the Way", "Get the lighthouse of %s working again and take the reward.", 0, kWindfall, [](const Read& s) { return s.event(0x1B20); }, false, kSaves},
    {"potions", kQuests, 5, "Potion Master", "Help Doc Bandam brew the green and the blue potion.", 0, 0, [](const Read& s) { return s.event(0x0D06); }, false, kDecomp},
    {"letter_sorting", kQuests, 5, "Sorted", "Sort 25 letters for Baito on %s.", 0, kDragonRoost, [](const Read& s) { return s.event(0x2702); }, false, kDecomp},
    {"golden_feathers", kQuests, 5, "Birds of a Feather", "Help the Rito guard win his sweetheart and get his letter.", 0, 0, [](const Read& s) { return (s.reg(0xAE) & 3) == 3; }, false, kDecomp},
    {"aryll_letter", kQuests, 1, "Letter from Home", "Read Aryll's letter.", 0, 0, [](const Read& s) { return s.event(0x8B01); }, false, kSaves},
    {"flight_record", kQuests, 5, "Up and Away", "Beat the record at the %s.", 0, kFlightPlatform, [](const Read& s) { return s.event(0x2B40); }, false, kSaves},
    {"beedle_silver", kQuests, 5, "Silver Member", "Earn the silver membership of Beedle's Shop Ship.", 0, 0, [](const Read& s) { return s.reg(0x86) >= 30; }, false, kDecomp},
    {"beedle_gold", kQuests, 10, "Gold Member", "Earn the gold membership of Beedle's Shop Ship.", 0, 0, [](const Read& s) { return s.reg(0x86) >= 60; }, false, kDecomp},
    {"trading", kQuests, 10, "Master Trader", "Finish the traveling merchants' trades and get the last reward.", 0, 0, [](const Read& s) { return s.event(0x3E04); }, false, kDecomp},
    // the Savage Labyrinth (stage table 0x0D): floor 30's chest (11); floor 50 is the Hero's Charm
    {"labyrinth_30", kQuests, 10, "Through Fire and Water", "Reach the treasure 30 floors down in the cave of trials on %s.", 0, kOutset, [](const Read& s) { return s.chests(0x0D, 1ul << 11); }, false, kSaves},
    {"puzzle_1", kQuests, 3, "Sliding Start", "Solve a slide puzzle in the cabana (%s).", 0, kPrivateOasis, [](const Read& s) { return s.puzzles() >= 1; }, false, kSaves},
    {"puzzle_8", kQuests, 5, "Sliding Adept", "Solve 8 slide puzzles in the cabana (%s).", 0, kPrivateOasis, [](const Read& s) { return s.puzzles() >= 8; }, false, kSaves},
    {"trees_fast", kQuests, 5, "Green Thumb", "Water all 8 withered trees within 20 minutes of the first.", 0, 0, detail::trees, true, kDecomp},
    // collections
    {"chests_drc", kCollect, 5, "Hot Loot", kDungeonChests, 0, -2, [](const Read& s) { return s.chests(3, 0xFCDF); }, false, kSaves},
    {"chests_fw", kCollect, 10, "Forest Finds", kDungeonChests, 0, -3, [](const Read& s) { return s.chests(4, 0xDFFF); }, false, kSaves},
    {"chests_totg", kCollect, 10, "Divine Treasures", kDungeonChests, 0, -4, [](const Read& s) { return s.chests(5, 0x8F7F); }, false, kSaves},
    {"chests_et", kCollect, 10, "Buried Treasures", kDungeonChests, 0, -5, [](const Read& s) { return s.chests(6, 0xDEFF); }, false, kSaves},
    {"chests_wt", kCollect, 10, "Gone with the Treasure", kDungeonChests, 0, -6, [](const Read& s) { return s.chests(7, 0xBFBD); }, false, kSaves},
    {"special_charts", kCollect, 10, "Cartophile", "Collect all 12 special charts (Tingle, Ghost Ship, Octo, Great Fairy, Hearts, Secret Cave, Light Ring, Platform, Beedle, Submarine and the IN-credible Chart).", 0, 0,
     [](const Read& s) { for (unsigned n : detail::kSpecialCharts) if (!s.chart(1, n)) return false; return true; }, false, kSaves},
    {"tingle_statues", kCollect, 10, "Idol Collector", "Find the Tingle Statue in each of the five dungeons.", 0, 0,
     [](const Read& s) { for (int st = 3; st <= 7; st++) if (!s.chests(st, 0x8000)) return false; return true; }, false, kSaves},
    {"submarines", kCollect, 5, "Sub Hunter", "Open the chests of all 7 submarines.", 0, 0, [](const Read& s) { return s.chests(0x0A, 0xDF); }, false, kSaves},
    {"big_octos", kCollect, 10, "Octo Slayer", "Defeat all 6 Big Octos.", 0, 0, [](const Read& s) { return (s.stage_byte(0, 0x05) & 0x07) == 0x07 && (s.stage_byte(0, 0x06) & 0xE0) == 0xE0; }, false, kSaves},
    {"charts_20", kCollect, 5, "Chart Reader", "Collect 20 charts.", 0, 0, [](const Read& s) { return s.charts(1) >= 20; }, false, kCount},
    {"charts_all", kCollect, 10, "Every Chart", "Collect all 49 charts.", 0, 0, [](const Read& s) { return s.charts(1) >= 49; }, false, kCount},
    {"salvage_all", kCollect, 10, "Sunken Riches", "Salvage the treasure of all 49 charts.", 0, 0, [](const Read& s) { return s.charts(3) >= 49; }, false, kCount},
    {"sea_half", kCollect, 5, "Half the Sea", "Fill in 25 squares of the sea chart.", 0, 0, [](const Read& s) { return s.squares() >= 25; }, false, kCount},
    {"sea_all", kCollect, 10, "Charted Waters", "Fill in all 49 squares of the sea chart.", 0, 0, [](const Read& s) { return s.squares() >= 49; }, false, kCount},
    {"progress_100", kCollect, 25, "One Hundred Percent", "Reach 100 % in Game progress.", 0, 0, [](const Read& s) { gp_result r; gp_count(s.d, &r); return gp_percent(&r) >= 100; }, false, kCount},
    // the Nintendo Gallery
    {"gallery_open", kGallery, 3, "Art Lover", "Open the way into the Nintendo Gallery.", 0, 0, [](const Read& s) { return s.stage_bit(0, 0x0C, 6); }, false, kSaves},
    {"figurine_first", kGallery, 3, "First Figurine", "Get a figurine made in the Nintendo Gallery.", 0, 0, [](const Read& s) { return s.figurines(0, GP_FIGURINES) > 0; }, false, kCount},
    {"room_outset", kGallery, 10, "Back Home", kRoom, 0, kOutset, [](const Read& s) { return s.figurines(0, 14) == 14; }, false, kCount},
    {"room_windfall", kGallery, 10, "Busy Town", kRoom, 0, kWindfall, [](const Read& s) { return s.figurines(14, 41) == 27; }, false, kCount},
    {"room_sea", kGallery, 10, "Sea Folk", "Complete the Great Sea room of the gallery.", 0, 0, [](const Read& s) { return s.figurines(41, 65) == 24; }, false, kCount},
    {"room_dragon", kGallery, 10, "Feathered Friends", kRoom, 0, kDragonRoost, [](const Read& s) { return s.figurines(65, 83) == 18; }, false, kCount},
    {"room_forest", kGallery, 10, "Little Leaves", kRoom, 0, kForestHaven, [](const Read& s) { return s.figurines(83, 97) == 14; }, false, kCount},
    {"room_enemies", kGallery, 10, "Know Your Enemy", "Complete the enemies room of the gallery.", 0, 0, [](const Read& s) { return s.figurines(97, 116) == 19; }, false, kCount},
    {"room_fortress", kGallery, 10, "Fortress Figures", kRoom, 0, kFortress, [](const Read& s) { return s.figurines(116, 134) == 18; }, false, kCount},
    {"figurines_all", kGallery, 50, "Gallery Complete", "Get all 134 figurines.", 0, 0, [](const Read& s) { return s.figurines(0, GP_FIGURINES) == GP_FIGURINES; }, false, kCount},
};
// clang-format on
inline constexpr unsigned kGoals = sizeof kAll / sizeof kAll[0];

// the fork's goals left out: their flags could not be confirmed (key, why); never checked
struct Unconfirmed { const char *key, *why; };
inline const Unconfirmed kUnconfirmed[] = {
    {"squid_hunt", "register 0xFE: the finished test save has 2 there with all three prizes found (the fork reads it two ways)"},
    {"platforms", "the finished test save lacks bits of the mask 0x0FBFFDFF in stage table 1 with every heart and chart found"},
    {"unarmed_robbery", "a challenge; no save to check the Forsaken Fortress chests 0..3 with no sword"},
    {"fire_mountain_fast", "the play timer at play + 0x5CF0 (mode 3) is not confirmed in HD"},
    {"ice_ring_fast", "the play timer at play + 0x5CF0 (mode 3) is not confirmed in HD"},
    {"puzzle_16", "that the count at info + 0x57 goes back to 0 after the 16th puzzle is not confirmed"},
};

inline unsigned total_points() {
    unsigned p = 0;
    for (const Goal& g : kAll) p += g.points;
    return p;
}

}  // namespace achievements
