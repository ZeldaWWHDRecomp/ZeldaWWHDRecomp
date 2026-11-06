// Cheats (Gameplay menu): edits of the live save data, applied by the game's main thread at the top
// of the frame (hook_0203593C in interp.cpp), like the save states. One-shot cheats run once per
// click; the infinite health / magic / ammo switches top up every frame.
//
// The save data is dSv_info_c of the GameCube decompilation (zeldaret/tww include/d/d_save.h),
// unchanged in WWHD as far as used here. Each cheat does what the game's own item_func_*
// (d_item.cpp) does for that item. Resolve the live area through the mapped save global;
// heap allocation addresses are not part of the executable address map.
//
// Note: the game itself takes the sword away in Forsaken Fortress (first visit) and the bow in the
// Tower of the Gods when those stages load (d_s_play.cpp), cheat or not.
//
// Test switch: WWHD_CHEAT=items,sword,stats,songs,triforce,dungeon,key,bags,figurines applies those once a save
// file is loaded; WWHD_CHEAT_INFINITE=health,magic,ammo,rupees,bags,breath switches those on.
//
// Infinite rupees / bait and spoils / swimming and "Fill the bags": idea and offsets from
// GreenNaugahyde/ZeldaWWHDRecompAndroid (MPL-2.0).
// WWHD_CHEAT_SAVE_ADDR=hex overrides the address (another game version).
// Explicit story-grant test aid: WWHD_STORY_ITEM=nayru,din,farore,sword requires
// WWHD_STORY_ITEM_ACK=1; WWHD_STORY_ITEM_AT=logic_step delays it until a portable load settles.
// It uses the same mandatory backup as the UI and never sets story event/placement flags.
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "guest_addr.h"
#include "input.h"
#include "moon_jump_policy.h"
#include "mods.h"
#include "runtime.h"
#include "savestate.h"
#include "save_backup.h"

namespace mods {
namespace {
// dSv_player_c offsets
constexpr uint32_t kMaxLife = 0x00, kLife = 0x02, kRupee = 0x04;  // u16, life in quarter hearts
constexpr uint32_t kSelectEquip = 0x0E;  // u8[4]: sword, shield, bracelet
constexpr uint32_t kWallet = 0x12, kMaxMagic = 0x13, kMagic = 0x14;
constexpr uint32_t kReturnName = 0x30;  // char[8] stage
constexpr uint32_t kItems = 0x3C;       // u8[21] item number per inventory slot (0xFF empty)
constexpr uint32_t kGetItems = 0x51;    // u8[21] "got" bits per slot (bow: 1 fire/ice, 2 light)
constexpr uint32_t kArrowNum = 0x69, kBombNum = 0x6A, kArrowMax = 0x6F, kBombMax = 0x70;
constexpr uint32_t kCollect = 0xB4;     // u8[8]: [0] swords, [1] shields, [2] bracelets
constexpr uint32_t kSymbol = 0xBF;  // Nayru, Din, Farore: bits 0, 1, 2 (itemAcquireSymbol, 025B7CC0)
constexpr uint32_t kTact = 0xBD, kTriforce = 0xBE;  // song bits (6), Triforce shard bits (8)
constexpr uint32_t kSelectItems = 0x09;  // u8[3]: the item on X / Y / R (inventory slot; bait 36..43)
// the bags (dSv_player_bag_item_c / _get_bag_item_c / _bag_item_record_c): the items in the spoils /
// bait bag (u8[8] each), their "got" flags (spoils u8 at +0x94, bait +0x95) and counts (spoils by
// dBeastIdx, bait per slot)
constexpr uint32_t kBeast = 0x76, kBait = 0x7E, kBeastFlags = 0x94, kBaitFlags = 0x95, kBeastNum = 0x9C, kBaitNum = 0xA4;
constexpr uint8_t kBirdBait = 0x82, kHyoiPear = 0x83;
// the spoils by dBeastIdx: Skull Necklace, Boko Baba Seed, Golden Feather, Knight's Crest, Red / Green
// / Blue Chu Jelly, Joy Pendant
constexpr uint8_t kSpoils[8] = {0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x1F};
// dSv_info_c::mMemory: the current stage's dSv_memBit_c (copied back to its save table on leaving)
constexpr uint32_t kStageKeys = 0x778 + 0x20, kStageDungeonItems = 0x778 + 0x21;  // bits: map, compass, boss key
// dSv_save_c::mEvent: the event flags (dSv_event_c, u8[0x100]; a flag or register is a u16, its high
// byte the index and its low byte the mask)
constexpr uint32_t kEvent = 0x624;

uint32_t save_addr() {
    const char* override_addr = getenv("WWHD_CHEAT_SAVE_ADDR");
    return override_addr ? (uint32_t)strtoul(override_addr, nullptr, 16) : ld32(GD(0x101F84DC)) + 0x20;
}

const uint32_t kStageName = GD(0x1046F0B0) + 0x5134;  // current stage, as in savestate.cpp
// g_dComIfG_gameInfo.play (GameCube dComIfG_play_c + 0x1288 here; the USA and EU builds share the layout):
//  - mItemRupeeCount (+0x48C0): rupees still to be added or taken; a shop does `-= price`. The HUD's
//    rupee counter (dMeter_rupyMove) moves it into the save and counts the shown number along.
//  - mAirMeter (+0x48C4) and its max (+0x48C8): set when Link enters deep water
//    (daPy_lk_c::setSwimTimerStartStop), counted down while he swims; at 0 he sinks.
//  - mSelectItem[5] (+0x4933): the play's copy of the X / Y / R item numbers (story_item_buttons.cpp).
const uint32_t kRupeeChange = GD(0x1046F0B0) + 0x5B48;
const uint32_t kAirMeter = GD(0x1046F0B0) + 0x5B4C;
const uint32_t kPlaySelect = GD(0x1046F0B0) + 0x5BBB;

std::string stage() { return std::string((const char*)mem::ptr(kStageName), strnlen((const char*)mem::ptr(kStageName), 8)); }

// false on the title screen (it has placeholder save data that a file load replaces), before a file
// is loaded, or if the address is wrong
bool save_loaded(uint32_t s) {
    std::string st = stage();
    if (st.empty() || st == "sea_T" || st == "Name") return false;  // title, file select
    uint16_t max = ld16(s + kMaxLife), life = ld16(s + kLife);
    if (max < 12 || max > 80 || life > max || ld16(s + kRupee) > 5000 || ld8(s + kWallet) > 2) return false;
    // the return stage: a NUL-terminated name in 8 bytes, at least 3 characters ("sea", "sea_T", "M_NewD2")
    int n = 0;
    for (; n < 8; n++) {
        uint8_t ch = ld8(s + kReturnName + n);
        if (!ch) break;
        if (ch < '0' || ch > 'z') return false;
    }
    return n >= 3;
}

void give(uint32_t s, int slot, uint8_t item, uint8_t got_bits = 1) {
    st8(s + kItems + slot, item);
    st8(s + kGetItems + slot, ld8(s + kGetItems + slot) | got_bits);
}

void all_items(uint32_t s) {
    give(s, 0, 0x20);         // telescope
    give(s, 1, 0x78);         // sail
    give(s, 2, 0x22);         // Wind Waker (songs are not given: they drive story events)
    give(s, 3, 0x25);         // grappling hook
    give(s, 4, 0x24);         // spoils bag
    give(s, 5, 0x2D);         // boomerang
    give(s, 6, 0x34);         // Deku leaf
    give(s, 8, 0x26, 3);      // deluxe picto box
    give(s, 9, 0x29);         // iron boots
    give(s, 10, 0x2A);        // magic armor
    give(s, 11, 0x2C);        // bait bag
    give(s, 12, 0x36, 7);     // bow with fire/ice and light arrows
    give(s, 13, 0x31);        // bombs
    for (int b = 14; b < 18; b++)
        if (ld8(s + kItems + b) == 0xFF) st8(s + kItems + b, 0x50);  // empty bottles, contents kept
    give(s, 18, 0x30);        // delivery bag
    give(s, 19, 0x2F);        // hookshot
    give(s, 20, 0x33);        // skull hammer
    st8(s + kCollect + 2, ld8(s + kCollect + 2) | 1);  // power bracelets
    st8(s + kSelectEquip + 2, 0x28);
    st8(s + kArrowMax, 99);
    st8(s + kArrowNum, 99);
    st8(s + kBombMax, 99);
    st8(s + kBombNum, 99);
    if (ld8(s + kMaxMagic) < 16) st8(s + kMaxMagic, 16);  // the Deku leaf needs magic
    st8(s + kMagic, ld8(s + kMaxMagic));
}

void best_sword(uint32_t s) {
    // Sword ownership is story progress: bit 2 removes Medli from Dragon Roost and the
    // Earth Temple; bit 3 removes Makar from his earlier locations. Equip the upgrades
    // without claiming those story milestones. The game's equipment refresh restores
    // the earned equipment on reload, including a portable-state load.
    st8(s + kSelectEquip + 0, 0x3E);
    st8(s + kSelectEquip + 1, 0x3C);
    ss::notice("Master Sword and Mirror Shield equipped until reload");
}

void max_stats(uint32_t s) {
    st16(s + kMaxLife, 80);  // 20 hearts
    st16(s + kLife, 80);
    st8(s + kWallet, 2);     // 5000
    st16(s + kRupee, 5000);
    st8(s + kMaxMagic, 32);  // double magic
    st8(s + kMagic, 32);
}

// Fill the bags: both bags (if missing), every kind of spoils x99, the bait bag's empty slots with
// All-Purpose Bait (first half) and Hyoi Pears; bait counts as the game gives them
// (dSv_player_bag_item_c::setBaitItem: 3). The delivery bag holds letters without counts.
void fill_bags(uint32_t s) {
    if (ld8(s + kItems + 4) == 0xFF) give(s, 4, 0x24);
    if (ld8(s + kItems + 11) == 0xFF) give(s, 11, 0x2C);
    for (uint8_t item : kSpoils) {
        bool have = false;
        for (int k = 0; k < 8; k++) have |= ld8(s + kBeast + k) == item;
        for (int k = 0; k < 8 && !have; k++)
            if (ld8(s + kBeast + k) == 0xFF) {
                st8(s + kBeast + k, item);
                have = true;
            }
    }
    for (int i = 0; i < 8; i++) st8(s + kBeastNum + i, 99);
    st8(s + kBeastFlags, 0xFF);
    for (int k = 0; k < 8; k++)
        if (ld8(s + kBait + k) == 0xFF) {
            st8(s + kBait + k, k < 4 ? kBirdBait : kHyoiPear);
            st8(s + kBaitNum + k, 3);
        }
    st8(s + kBaitFlags, ld8(s + kBaitFlags) | 3);
}

// Complete the Nintendo Gallery: every figurine Carlov makes, as daNpcMt_c::setFigure does for one. The
// figurines are bits in 17 event registers (l_figure_comp: register figure / 8, bit figure % 8); only the
// 134 that exist are set (the last register holds 6). The gallery's figures (daObjFigure_c) and
// Carlov's count (daNpcMt_c::isComp: 133 or more) read the same bits. A complete collection makes Carlov leave
// the next time his workshop loads (event 0x3D08, as in the game), so the membership he gives with the first
// figurine (event 0x3401: he took a picto to make one) could never be had afterwards: without it, nothing is set.
const uint32_t kFigureRegs = GD(0x101C2478);  // daNpcMt_c's l_figure_comp: u16[17]
constexpr int kFigures = 0x86;

void all_figurines(uint32_t s) {
    if (!(ld8(s + kEvent + 0x34) & 0x01)) {
        ss::notice("Become a member of the Nintendo Gallery first: give Carlov a color picto");
        return;
    }
    for (int i = 0; i < (kFigures + 7) / 8; i++) {
        const uint16_t reg = ld16(kFigureRegs + 2 * i);
        const int count = kFigures - 8 * i < 8 ? kFigures - 8 * i : 8;
        const uint8_t bits = (uint8_t)(((1u << count) - 1) & (reg & 0xFF));
        st8(s + kEvent + (reg >> 8), ld8(s + kEvent + (reg >> 8)) | bits);
    }
    st8(s + kEvent + 0x3A, ld8(s + kEvent + 0x3A) | 0x01);  // setFigure's event 0x3A01: a figurine was made
}

// Infinite bait and spoils, each frame: spoils counts stay at 99; bait that was used is put back. A
// Hyoi Pear (or the last bait) empties its slot and the button it was on (setBaitItemEmpty): both are
// restored, the button also in the play's copy of the item numbers.
struct BagMemory {
    uint32_t s = 0;
    uint8_t item[8], num[8], sel[3];
};
BagMemory bag_memory;

void keep_bags(uint32_t s) {
    BagMemory& m = bag_memory;
    for (int i = 0; i < 8; i++)
        if (ld8(s + kBeastNum + i) < 99) st8(s + kBeastNum + i, 99);
    if (m.s == s) {
        for (int k = 0; k < 8; k++) {
            if (m.item[k] == 0xFF) continue;
            if (ld8(s + kBait + k) == 0xFF) st8(s + kBait + k, m.item[k]);
            if (ld8(s + kBait + k) == m.item[k] && ld8(s + kBaitNum + k) < m.num[k]) st8(s + kBaitNum + k, m.num[k]);
        }
        for (int b = 0; b < 3; b++) {
            const uint8_t was = m.sel[b];
            if (was >= 36 && was < 44 && ld8(s + kSelectItems + b) == 0xFF && ld8(s + kBait + (was - 36)) != 0xFF) {
                st8(s + kSelectItems + b, was);
                st8(kPlaySelect + b, ld8(s + kBait + (was - 36)));
            }
        }
    }
    m.s = s;
    for (int k = 0; k < 8; k++) {
        m.item[k] = ld8(s + kBait + k);
        m.num[k] = ld8(s + kBaitNum + k);
    }
    for (int b = 0; b < 3; b++) m.sel[b] = ld8(s + kSelectItems + b);
}

// Infinite rupees through the HUD counter, so the shown number stays right: a charge not taken yet is
// dropped; one the counter already took (it counts down) is followed by a change back up to the full
// wallet.
void keep_rupees(uint32_t s) {
    const int full = ld8(s + kWallet) == 0 ? 200 : ld8(s + kWallet) == 1 ? 1000 : 5000;
    int32_t change = (int32_t)ld32(kRupeeChange);
    if (change < 0) st32(kRupeeChange, uint32_t(change = 0));
    if (change == 0 && ld16(s + kRupee) < full) st32(kRupeeChange, uint32_t(full - ld16(s + kRupee)));
}

// Infinite swimming: only while the meter runs (0: not swimming, the meter is hidden).
void keep_breath() {
    const int32_t left = (int32_t)ld32(kAirMeter), max = (int32_t)ld32(kAirMeter + 4);
    if (left > 0 && left < max) st32(kAirMeter, uint32_t(max));
}

int parse_env(const char* var, std::initializer_list<std::pair<const char*, int>> names) {
    const char* e = getenv(var);
    int w = 0;
    for (auto& [n, bit] : names)
        if (e && strstr(e, n)) w |= bit;
    return w;
}

std::atomic<int> g_pending{parse_env("WWHD_CHEAT", {{"items", kCheatItems}, {"sword", kCheatSword}, {"stats", kCheatStats},
                                                    {"songs", kCheatSongs}, {"triforce", kCheatTriforce},
                                                    {"dungeon", kCheatDungeon}, {"key", kCheatKey}, {"bags", kCheatBags},
                                                    {"figurines", kCheatFigurines}})};
// Explicit test/CLI requests use the same backup gate and require a separate acknowledgement.
std::atomic<int> g_story_pending{parse_env("WWHD_STORY_ITEM", {{"nayru", kStoryNayru}, {"din", kStoryDin},
                                                          {"farore", kStoryFarore}, {"sword", kStorySword}})};
std::atomic<bool> g_story_confirmed{[]{const char* e=getenv("WWHD_STORY_ITEM_ACK");return e && !strcmp(e,"1");}()};
bool g_story_backed_up = false; // game thread, this process only; never saved as an enabled cheat

void story_items(uint32_t s, int items) {
    if (!g_story_confirmed.load(std::memory_order_acquire)) {
        ss::notice("This can change or break the story's order. A save backup is made first. Confirm in Mods > More > Cheats.");
        return;
    }
    if (!g_story_backed_up) {
        std::lock_guard guard(saves::mutex());
        auto& store = saves::store();
        std::error_code error;
        const auto saved_game = store.save_dir()/"user"/"cking.sav";
        if (!store.configured() || !std::filesystem::is_regular_file(saved_game,error) ||
            std::filesystem::file_size(saved_game,error)==0 || error) {
            ss::notice("Save in the game first: story items require a save backup.");
            return;
        }
        auto result = store.create("before-mod", "story-items", "Story item cheats");
        if (!result.ok || !result.created) {
            ss::notice(result.ok ? "Save in the game first: story items require a save backup."
                                 : "Story item not granted: " + result.error);
            return;
        }
        const auto backed_up = store.root()/result.name/"save"/"user"/"cking.sav";
        if (!std::filesystem::is_regular_file(backed_up,error) ||
            std::filesystem::file_size(backed_up,error)==0 || error) {
            ss::notice("Story item not granted: the backup has no in-game save.");
            return;
        }
        g_story_backed_up = true;
    }
    // Match the individual native inventory grants; never call cutscenes or set placement events.
    uint8_t symbols = 0;
    if (items & kStoryNayru) symbols |= 1;
    if (items & kStoryDin) symbols |= 2;
    if (items & kStoryFarore) symbols |= 4;
    if (symbols) st8(s+kSymbol, ld8(s+kSymbol)|symbols);
    if (items & kStorySword) {
        // item_func_master_sword_ex sets only phase 3 (0254EFA8), not earlier phase 2.
        // Phase 2 is the Medli-removal gate (#85); unrelated ownership and event bits stay intact.
        st8(s+kCollect, ld8(s+kCollect)|8);
        st8(s+kSelectEquip, 0x3E);
    }
    LOG("[cheats] story item grant %d; symbols=%02X sword ownership=%02X (backup completed)",
        items, ld8(s+kSymbol), ld8(s+kCollect));
    ss::notice("Story item granted. Save in game to keep it. Backup: Mods > More > Save backups.");
}

std::atomic<int> g_infinite{parse_env("WWHD_CHEAT_INFINITE", {{"health", kInfHealth}, {"magic", kInfMagic}, {"ammo", kInfAmmo}, {"moonjump", kMoonJump}, {"nofall", kMoonNoFall},
                                                               {"rupees", kInfRupees}, {"bags", kInfBags}, {"breath", kInfBreath}})};
std::atomic<uint32_t> moon_buttons{0}, moon_camera{0};
std::atomic<uint64_t> moon_camera_scene{0};
std::atomic<float> moon_stick_x{0}, moon_stick_y{0};
std::atomic<bool> moon_reset_requested{false};
struct Flight {
    uint32_t link = 0;
    uint64_t scene = 0;
    bool flying = false;
    int16_t facing = 0;
    float x = 0, z = 0;
};
Flight flight;
// WWHD daPy_lk_c fields shared with climb.cpp.
constexpr uint32_t moon_proc = 0x65F0, moon_mode = 0x6A70;
constexpr uint32_t moon_speed_y = 0x340, moon_pos_y = 0x318;
constexpr uint32_t moon_angle = 0x322, moon_shape_angle = 0x32A;
constexpr uint32_t moon_ground_flags = 0x834, moon_fall_height = 0x6A48;
}  // namespace

void request_cheat(int which) { g_pending |= which; }

// what has been used in this run (applied, not only asked for): Achievements pause for the rest of the run
namespace {
std::atomic<int> g_used{0}, g_story_used{0};
}
std::vector<std::string> cheats_pausing() {
    std::vector<std::string> out;
    static const std::pair<int, const char*> kInf[] = {
        {kInfHealth, "Infinite health"}, {kInfMagic, "Infinite magic"}, {kInfAmmo, "Infinite arrows and bombs"},
        {kInfRupees, "Infinite rupees"}, {kInfBags, "Infinite bait and spoils"}, {kInfBreath, "Infinite swimming"},
        {kMoonJump, "Moon jump"}, {kMoonNoFall, "No fall damage"}};
    for (const auto& [bit, name] : kInf)
        if (infinite(bit)) out.push_back(std::string(name) + " (on)");
    static const std::pair<int, const char*> kOnce[] = {
        {kCheatItems, "Give all items"}, {kCheatSword, "Equip Master Sword and Mirror Shield"},
        {kCheatStats, "20 hearts, double magic, 5000 rupees"}, {kCheatSongs, "All songs"},
        {kCheatTriforce, "All Triforce shards"}, {kCheatDungeon, "Map, compass and boss key"}, {kCheatKey, "Add a small key"},
        {kCheatBags, "Fill the bags"}, {kCheatFigurines, "All Nintendo Gallery figurines"}};
    const int used = g_used.load(std::memory_order_relaxed);
    for (const auto& [bit, name] : kOnce)
        if (used & bit) out.push_back(std::string(name) + " (used)");
    if (g_story_used.load(std::memory_order_relaxed)) out.push_back("Story items (used)");
    return out;
}
bool story_item_confirmation_needed() { return !g_story_confirmed.load(std::memory_order_acquire); }
void request_story_item(int which, bool confirmed) {
    if (!(which & (kStoryNayru|kStoryDin|kStoryFarore|kStorySword))) return;
    if (confirmed) g_story_confirmed.store(true, std::memory_order_release);
    if (!g_story_confirmed.load(std::memory_order_acquire)) {
        ss::notice("This can change or break the story's order. A save backup is made first. Confirm in Mods > More > Cheats.");
        return;
    }
    g_story_pending.fetch_or(which & (kStoryNayru|kStoryDin|kStoryFarore|kStorySword), std::memory_order_release);
}
bool infinite(int which) { return g_infinite.load(std::memory_order_relaxed) & which; }
void set_infinite(int which, bool on) {
    if (on) g_infinite |= which;
    else g_infinite &= ~which;
    if (which == kMoonJump) moon_reset_requested.store(true, std::memory_order_release);
    LOG("[cheats] infinite %s %s", which == kInfHealth ? "health" : which == kInfMagic ? "magic" :
        which == kInfAmmo ? "ammo" : which == kMoonJump ? "moon jump" : which == kMoonNoFall ? "no fall damage" :
        which == kInfRupees ? "rupees" : which == kInfBags ? "bait and spoils" : "swimming", on ? "on" : "off");
}

void moon_jump_input(const input::PadState& pad) {
    if (!infinite(kMoonJump)) return;
    moon_stick_x.store(pad.lx, std::memory_order_relaxed);
    moon_stick_y.store(pad.ly, std::memory_order_relaxed);
    moon_buttons.store(pad.virt, std::memory_order_release);  // its own input (input_map kMoonJump), never the game's L
}

void moon_jump_camera(uint32_t camera) {
    if (!infinite(kMoonJump)) return;
    const uint64_t scene = (uint64_t(ld32(kStageName)) << 32) | ld32(kStageName + 4);
    moon_camera_scene.store(scene, std::memory_order_relaxed);
    moon_camera.store(camera, std::memory_order_release);
}

void moon_jump_reset() {
    flight = {};
    moon_camera.store(0, std::memory_order_relaxed);
    moon_camera_scene.store(0, std::memory_order_relaxed);
    moon_buttons.store(0, std::memory_order_relaxed);
}

void moon_jump_step(Cpu* cpu, uint32_t link, bool before) {
    if (!infinite(kMoonJump)) return; // stock path: no guest reads or writes
    if (before) {
        if (moon_reset_requested.exchange(false, std::memory_order_acquire)) moon_jump_reset();
        // Do not allow a malformed actor or title/file-select input to dereference fields.
        if (link < 0x10000 || link > 0xFFFF9000 || !save_loaded(save_addr())) {
            flight = {};
            moon_camera.store(0, std::memory_order_relaxed);
            moon_camera_scene.store(0, std::memory_order_relaxed);
            return;
        }
        const uint64_t scene = (uint64_t(ld32(kStageName)) << 32) | ld32(kStageName + 4);
        if (flight.link != link || flight.scene != scene) {
            // A freed camera can be reused before the new scene draws its camera.
            // Preserve the initial draw's camera, but invalidate a prior actor/scene.
            if (flight.link) moon_camera.store(0, std::memory_order_relaxed);
            flight = {}; flight.link = link; flight.scene = scene;
        }
    } else if (flight.link != link) return;
    const bool held = (moon_buttons.load(std::memory_order_acquire) & input::kVirtMoonJump) != 0;
    if (before && held) {
        if (ld32(link + moon_proc) != 0x27) {
            const Cpu saved = *cpu;
            cpu->f[1].ps0 = cpu->f[1].ps1 = 5.;
            guest_call(cpu, GC(0x023F6564), {link, 0}); // daPy_lk_c::procFall_init
            *cpu = saved;
        }
        if (!flight.flying) flight.facing = int16_t(ld16(link + moon_shape_angle));
        flight.flying = true;
        st32(link + moon_ground_flags, ld32(link + moon_ground_flags) & ~0xE0u);
    }
    if (flight.flying) {
        const uint32_t mode = ld32(link + moon_mode);
        if (!held && (!(mode & 2) || (mode & 0x40000))) flight.flying = false;
    }
    if (flight.flying) {
        if (held) stf32(link + moon_speed_y, 25.f);
        if (infinite(kMoonNoFall)) st32(link + moon_fall_height, ld32(link + moon_pos_y));
        if (before) {
            float fx = 0, fz = 0;
            const uint32_t camera = moon_camera.load(std::memory_order_acquire);
            if (moon_camera_scene.load(std::memory_order_relaxed) == flight.scene &&
                camera >= 0x10000 && camera <= 0xFFFFFE00) {
                fx = ldf32(camera + 0xE8) - ldf32(camera + 0xDC);
                fz = ldf32(camera + 0xF0) - ldf32(camera + 0xE4);
            }
            auto movement = moon_jump_policy::steer(flight.facing,
                moon_stick_x.load(std::memory_order_relaxed), moon_stick_y.load(std::memory_order_relaxed),
                fx, fz, 1.f);
            flight.facing = movement.facing; flight.x = movement.x; flight.z = movement.z;
        }
        st16(link + moon_angle, uint16_t(flight.facing));
        st16(link + moon_shape_angle, uint16_t(flight.facing));
    }
}

bool moon_jump_move(uint32_t link, float& x, float& z) {
    if (!infinite(kMoonJump) || !flight.flying || flight.link != link) return false;
    x = flight.x; z = flight.z;
    st16(link + moon_angle, uint16_t(flight.facing));
    st16(link + moon_shape_angle, uint16_t(flight.facing));
    return true;
}

void cheats_service() {
    int inf = g_infinite.load(std::memory_order_relaxed) &
              (kInfHealth | kInfMagic | kInfAmmo | kInfRupees | kInfBags | kInfBreath);
    if (!(inf & kInfBags)) bag_memory.s = 0;  // switched on again: start from the bags as they are
    int story = g_story_pending.exchange(0, std::memory_order_acq_rel);
    if (story && getenv("WWHD_STORY_ITEM")) {
        if (const char* at = getenv("WWHD_STORY_ITEM_AT"); at && step() < strtoull(at,nullptr,10)) {
            g_story_pending.fetch_or(story,std::memory_order_relaxed);
            story = 0;
        }
    }
    if (!g_pending.load(std::memory_order_relaxed) && !inf && !story) return;
    uint32_t s = save_addr();
    if (!save_loaded(s)) {
        if (story) {
            if (getenv("WWHD_STORY_ITEM")) g_story_pending.fetch_or(story, std::memory_order_relaxed);
            else ss::notice("Load a Quest Log before granting story items.");
        }
        return;
    }
    if (story) story_items(s, story), g_story_used |= story;
    // Ordinary cheats keep their existing pending-until-load behavior.
    // ponytail: topped up once per frame, so a single hit bigger than your whole health still kills
    if (inf & kInfHealth) st16(s + kLife, ld16(s + kMaxLife));
    if (inf & kInfMagic) st8(s + kMagic, ld8(s + kMaxMagic));
    if (inf & kInfAmmo) {
        st8(s + kArrowNum, ld8(s + kArrowMax));
        st8(s + kBombNum, ld8(s + kBombMax));
    }
    if (inf & kInfRupees) keep_rupees(s);
    if (inf & kInfBags) keep_bags(s);
    if (inf & kInfBreath) keep_breath();
    if (!g_pending.load(std::memory_order_relaxed)) return;
    int w = g_pending.exchange(0);
    g_used |= w;
    if (w & kCheatItems) all_items(s);
    if (w & kCheatSword) best_sword(s);
    if (w & kCheatStats) max_stats(s);
    if (w & kCheatSongs) st8(s + kTact, 0x3F);  // Wind's Requiem .. Song of Passing
    if (w & kCheatTriforce) st8(s + kTriforce, 0xFF);
    if (w & kCheatDungeon) st8(s + kStageDungeonItems, ld8(s + kStageDungeonItems) | 7);
    if (w & kCheatKey && ld8(s + kStageKeys) < 99) st8(s + kStageKeys, ld8(s + kStageKeys) + 1);
    if (w & kCheatBags) fill_bags(s);
    if (w & kCheatFigurines) all_figurines(s);
    LOG("[cheats] applied %d in %s: life %u/%u, rupees %u, magic %u, items %02X..%02X, collect %02X %02X, songs %02X, triforce %02X, keys %u, dungeon items %02X",
        w, stage().c_str(), ld16(s + kLife), ld16(s + kMaxLife), ld16(s + kRupee), ld8(s + kMagic), ld8(s + kItems), ld8(s + kItems + 20),
        ld8(s + kCollect), ld8(s + kCollect + 1), ld8(s + kTact), ld8(s + kTriforce), ld8(s + kStageKeys), ld8(s + kStageDungeonItems));
}

}  // namespace mods
