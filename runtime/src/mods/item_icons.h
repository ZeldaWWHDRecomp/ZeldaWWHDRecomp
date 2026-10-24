// Mod items: the extended item icon archive. The game draws its bag and button icons from the layout
// archive BtnItemIcon_00 (in the 2D language pack, cached into the resident layout heap at start): per
// icon frame a 128 x 128 icon (Icon128_nn^l) and its glow (Float128_nn^t), chosen by the animation
// IconChange. The port serves the pack with that archive extended by one frame per mod item, built on the
// player's machine from the player's own pack (only this code ships, never the game's file or a copy of
// it) and kept in a cache next to the other built archives.
//
// Frames: the game's own are 0-54; 55 is a dummy (the game's "no item" value: a real icon there would
// never show); the mod items follow from 56 in the order set_items() gives them. The Python twin is
// tools/story_mods/lyt/item_icons.py (same bytes; test_lyt.py runs item_icons_test against it).
#pragma once
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

struct Cpu;

namespace item_icons {

using Bytes = std::vector<uint8_t>;
inline constexpr int kGameFrames = 55, kDummyFrame = 55, kFirstFrame = 56, kIconSize = 128;

// ---- pure parts (unit-tested) ----
// The glow picture (RGBA, white with the softened outline as alpha) of a 128 x 128 RGBA icon.
Bytes glow(const Bytes& rgba);
// IconChange with `count` more frames (keys and texture names appended; the game's keys unchanged).
Bytes extend_animation(const Bytes& flan, unsigned count);
// The archive's files with the dummy frame and the icons (RGBA 128 x 128, frames 56...) added.
std::map<std::string, Bytes> extend(const std::map<std::string, Bytes>& files, const std::vector<Bytes>& icons);
// A SARC as the game's layout archives are laid out: each texture aligned as its .bflim asks, other files
// to 4 bytes (keeps the archive in memory about as small as the game's own).
Bytes archive(const std::map<std::string, Bytes>& files);
// SARC reading and Yaz0 decoding (throw std::runtime_error on bad data).
std::map<std::string, Bytes> read_sarc(const Bytes& sarc);
Bytes yaz0_decode(const Bytes& data);
// Yaz0 with a greedy LZ search (item_icons.py yaz0_compress: the same bytes).
Bytes yaz0_compress(const Bytes& data);
// The 2D language pack with BtnItemIcon_00.szs extended (Yaz0); the other members as they were.
Bytes extend_pack(const Bytes& pack, const std::vector<Bytes>& icons);

// ---- the port ----
struct Item {
    uint16_t key = 0;       // the item (story_items key; 0 the notebook)
    std::string icon_png;   // its picture (any size; scaled to 128 x 128)
};
// The mod items with an icon, in frame order (frame 56 + index). Before the game opens its 2D pack.
// `spares`: that many more frames after them with an empty picture, for items added while the game runs
// (the editor launch mode, live_icon()).
void set_items(const std::vector<Item>& items, const std::filesystem::path& cache_dir, unsigned spares = 0);
// For the game's read of a 2D language pack (host path of the file it would read): the extended pack's
// path (built once and cached), or "" to read the original (no items, or the build failed: logged).
std::string pack_for(const std::string& guest, const std::string& host_path);
// The icon frame of an item in the served pack, or -1 (not served: the caller falls back).
int frame(uint16_t key);
// ---- live icons (the world editor, #270) ----
// The editor launch mode starts with kSpareFrames spare frames: eight, one Delivery Bag page of new items per
// session, for 8 x 80 KB (a 128 x 128 RGBA8 icon and its BC5 glow) of the resident layout heap.
inline constexpr unsigned kSpareFrames = 8;
// The icon of `key` drawn from `png` while the game runs: an item with a frame gets its picture again, a new
// one takes a free spare frame. The textures are written by service() on the game thread. Its frame, or -1
// (the pack isn't served, or no spare frame is free).
int live_icon(uint16_t key, const std::string& png);
bool has_frame(uint16_t key);  // the item has a frame in the served pack (from the start, or a spare)
unsigned free_spares();
bool live_icons();  // the served pack has spare frames (live icons work this session)
void served_for_tests();  // tests: as if the game read the extended pack
// game thread: the pending icon pictures into the game's BtnItemIcon_00 in memory (its layout resource
// cache), through hdLayoutResMgr_c::find; kept pending while the archive isn't there
// `archive_name`: a sead SafeString "BtnItemIcon_00" in guest memory (story_lyt_screen::safe_string)
void set_game_calls(uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>), uint32_t archive_name);
void service(Cpu* c);
// Writes the image of a .bflim (as lyt_build::texture makes it) over the texture `file` of the SARC at
// `sarc` in guest memory, when its size, format and tiling are the same. False otherwise.
bool write_texture(uint32_t sarc, const std::string& file, const Bytes& flim);

// (item_icons_game.cpp's view of the state)
std::vector<std::pair<int, std::string>> pending_icons();  // frame, picture
bool icon_written(int frame, uint64_t hash);
void icon_done(int frame, const std::string& png, uint64_t hash, bool ok);
uint64_t picture_hash(const Bytes& px);
std::string icon_file(int frame);  // "timg/Icon128_NN^l.bflim"
std::string glow_file(int frame);

// An item's picture as the icons are built from it: 128 x 128 RGBA, scaled to fit and centred. Throws
// std::runtime_error (the file can't be read or isn't a PNG).
Bytes picture(const std::string& png_path);

}  // namespace item_icons
