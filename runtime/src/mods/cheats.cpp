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
// Test switch: WWHD_CHEAT=items,sword,stats,songs,triforce,dungeon,key applies those once a save
// file is loaded; WWHD_CHEAT_INFINITE=health,magic,ammo switches those on.
// WWHD_CHEAT_SAVE_ADDR=hex overrides the address (another game version).
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>

#include "guest_addr.h"
#include "mods.h"
#include "runtime.h"

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
constexpr uint32_t kTact = 0xBD, kTriforce = 0xBE;  // song bits (6), Triforce shard bits (8)
// dSv_info_c::mMemory: the current stage's dSv_memBit_c (copied back to its save table on leaving)
constexpr uint32_t kStageKeys = 0x778 + 0x20, kStageDungeonItems = 0x778 + 0x21;  // bits: map, compass, boss key

uint32_t save_addr() {
    const char* override_addr = getenv("WWHD_CHEAT_SAVE_ADDR");
    return override_addr ? (uint32_t)strtoul(override_addr, nullptr, 16) : ld32(GD(0x101F84DC));
}

const uint32_t kStageName = GD(0x1046F0B0) + 0x5134;  // current stage, as in savestate.cpp

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
    st8(s + kCollect + 0, 0x0F);  // hero's sword .. full-power Master Sword
    st8(s + kSelectEquip + 0, 0x3E);
    st8(s + kCollect + 1, 0x03);  // hero's + mirror shield
    st8(s + kSelectEquip + 1, 0x3C);
}

void max_stats(uint32_t s) {
    st16(s + kMaxLife, 80);  // 20 hearts
    st16(s + kLife, 80);
    st8(s + kWallet, 2);     // 5000
    st16(s + kRupee, 5000);
    st8(s + kMaxMagic, 32);  // double magic
    st8(s + kMagic, 32);
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
                                                    {"dungeon", kCheatDungeon}, {"key", kCheatKey}})};
std::atomic<int> g_infinite{parse_env("WWHD_CHEAT_INFINITE", {{"health", kInfHealth}, {"magic", kInfMagic}, {"ammo", kInfAmmo}})};
}  // namespace

void request_cheat(int which) { g_pending |= which; }
bool infinite(int which) { return g_infinite.load(std::memory_order_relaxed) & which; }
void set_infinite(int which, bool on) {
    if (on) g_infinite |= which;
    else g_infinite &= ~which;
    LOG("[cheats] infinite %s %s", which == kInfHealth ? "health" : which == kInfMagic ? "magic" : "ammo", on ? "on" : "off");
}

void cheats_service() {
    int inf = g_infinite.load(std::memory_order_relaxed);
    if (!g_pending.load(std::memory_order_relaxed) && !inf) return;
    uint32_t s = save_addr();
    if (!save_loaded(s)) return;  // stays pending until a file is loaded
    // ponytail: topped up once per frame, so a single hit bigger than your whole health still kills
    if (inf & kInfHealth) st16(s + kLife, ld16(s + kMaxLife));
    if (inf & kInfMagic) st8(s + kMagic, ld8(s + kMaxMagic));
    if (inf & kInfAmmo) {
        st8(s + kArrowNum, ld8(s + kArrowMax));
        st8(s + kBombNum, ld8(s + kBombMax));
    }
    if (!g_pending.load(std::memory_order_relaxed)) return;
    int w = g_pending.exchange(0);
    if (w & kCheatItems) all_items(s);
    if (w & kCheatSword) best_sword(s);
    if (w & kCheatStats) max_stats(s);
    if (w & kCheatSongs) st8(s + kTact, 0x3F);  // Wind's Requiem .. Song of Passing
    if (w & kCheatTriforce) st8(s + kTriforce, 0xFF);
    if (w & kCheatDungeon) st8(s + kStageDungeonItems, ld8(s + kStageDungeonItems) | 7);
    if (w & kCheatKey && ld8(s + kStageKeys) < 99) st8(s + kStageKeys, ld8(s + kStageKeys) + 1);
    LOG("[cheats] applied %d in %s: life %u/%u, rupees %u, magic %u, items %02X..%02X, collect %02X %02X, songs %02X, triforce %02X, keys %u, dungeon items %02X",
        w, stage().c_str(), ld16(s + kLife), ld16(s + kMaxLife), ld16(s + kRupee), ld8(s + kMagic), ld8(s + kItems), ld8(s + kItems + 20),
        ld8(s + kCollect), ld8(s + kCollect + 1), ld8(s + kTact), ld8(s + kTriforce), ld8(s + kStageKeys), ld8(s + kStageDungeonItems));
}

}  // namespace mods
