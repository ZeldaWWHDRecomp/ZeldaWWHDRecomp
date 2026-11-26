// SPDX-License-Identifier: MPL-2.0
// Link's appearance: the colours a player chose for Link's hair, skin, eyes, tunic and its details, per save
// file. Only colours: Link's own textures, as the player's game has them, are copied in memory and their
// colour parts moved to the chosen colours (link_masks.h finds the parts on the player's machine). Nothing
// chosen: nothing is copied and the game's memory is never touched.
//
// Pure parts here (link_appearance_test); the port's parts in link_appearance_port.cpp:
//   - the look shown: the save file played's, or the colour page's preview while it is open;
//   - Link.bfres found in the game's memory (by its first bytes, as player_models.cpp does) and the texture
//     objects of the textures with masks pointed at recoloured copies (the game's own bytes stay as they are;
//     the pointers go back when the look is cleared, a save state is loaded or the file is let go);
//   - kept in the save: a record in the story block of cking.sav (story_block.h, kind
//     story_mail::kRecordAppearance, one area per save file), written when the game saves, as the long names;
//   - the colour page (link_appearance_page.h), the Mod SDK services (wwhd_appearance_*) and the story
//     action {"action": "appearance"} that opens the page after a character's talk.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Cpu;
namespace input { struct PadState; }
namespace link_masks { struct Mask; }

namespace link_appearance {

// What can be coloured. Each slot moves one or more mask parts (parts_of()).
enum class Slot : uint8_t { kHair, kSkin, kEyes, kTunic, kUndershirt, kPants, kBoots, kBuckle, kCount };
inline constexpr int kSlots = int(Slot::kCount);
const char* slot_id(Slot s);     // "hair", "skin", "eyes", "tunic", "undershirt", "pants", "boots", "buckle"
const char* slot_label(Slot s);  // "Hair", "Skin", "Eyes", "Tunic and hat", ...
bool slot_of(const std::string& id, Slot& out);

// A look: a colour (0xRRGGBB) per slot, or none (the game's own colour).
struct Look {
    std::array<uint32_t, kSlots> rgb{};
    uint16_t set = 0;  // bit per slot
    bool has(Slot s) const { return set >> int(s) & 1; }
    uint32_t colour(Slot s) const { return rgb[size_t(s)]; }
    void put(Slot s, uint32_t c) { rgb[size_t(s)] = c & 0xFFFFFF, set |= uint16_t(1u << int(s)); }
    void clear(Slot s) { rgb[size_t(s)] = 0, set &= uint16_t(~(1u << int(s))); }
    bool empty() const { return set == 0; }
    bool operator==(const Look& o) const;
    bool operator!=(const Look& o) const { return !(*this == o); }
};

// ---- the record (the save's story block, the Mod SDK, online play): a few bytes ----
// Version 1: [1][set: u16 big-endian][3 bytes RGB for each set slot, in slot order]; 3..27 bytes. An empty look
// is the 3 bytes {1, 0, 0}. Unknown slot bits or a short record are refused (false).
inline constexpr uint8_t kRecordVersion = 1;
inline constexpr size_t kMaxRecord = 3 + 3 * kSlots;
std::vector<uint8_t> encode(const Look& l);
bool decode(const uint8_t* data, size_t size, Look& out);
// "hair=FFEF10,tunic=E52B36" (tests, logs, WWHD_LINK_APPEARANCE); unknown names or bad colours: false
std::string to_text(const Look& l);
bool from_text(const std::string& text, Look& out);

// ---- the presets the colour page offers (index 0 of each slot is "Original": no colour) ----
struct Preset { const char* name; uint32_t rgb; };
const std::vector<Preset>& presets(Slot s);
// the preset index of a look's slot: 0 none, else 1.. a preset's, -1 a colour that is no preset's
int preset_index(const Look& l, Slot s);

// ---- the colour parts (link_masks.h) a slot moves ----
// Tunic: the hero's tunic and hat, and the island shirt; Pants and Boots: also the island clothes'; Hair:
// also the island clothes' hair piece; Eyes: the irises.
std::vector<uint8_t> parts_of(Slot s);  // link_masks::Part values

// ---- the story block record (story_mail::Book of one save file's area) ----
// The look of an area (an empty look when it has none) and the area's records of a look (an empty look erases
// them). False when the area has no room or holds data this port did not write.
Look read_area(const uint8_t* area);
bool write_area(uint8_t* area, const Look& l);

// ---- the port (link_appearance_port.cpp) ----
// story_store.cpp: cking.sav was read (its story block's areas, null: none) / is being written (the areas to
// keep; the looks of the save files are put in)
void loaded(const uint8_t* payload);
void written(uint8_t* areas);
// interp.cpp, frame start on the game thread: Link's textures as the look shown wants them
void service();
// interp.cpp, every logic pass (`logic_pass`: a full one): the colour page's event and camera
void logic_service(Cpu* c, bool logic_pass);
// VPADRead (system_stubs.cpp): the colour page takes the controller while open; KPADReadEx (padscore.cpp): mute()
void pad(input::PadState& p, Cpu* c);
void mute(input::PadState& p);
bool page_open();

// The save file played (0..2), -1 none
int current_file();
// The look of the save file played (kept in the save when the game saves next) and the look on Link now (the
// page's preview while it is open). Any thread.
Look current();
Look shown();
// Set the look of the save file played; false (and why) with no file loaded
bool set_current(const Look& l, std::string& why);
// Ask for the colour page (any thread); it opens on the game thread once Link can stand still for it (within
// a few seconds, else it is dropped with a log line). False (and why) when it can't be asked now.
bool request_page(std::string& why);

// Link's textures with colour masks as the game has them (online co-op: each player's Link in his own look).
// want_textures() (game thread, each frame while needed) keeps them found even with no look of this game's own;
// game_textures() lists them once found (empty before): the texture object (its FTEX in Link.bfres), the game's
// own image and mip bytes (not this game's recoloured copies) and the mask. Game thread.
struct GameTexture {
    uint32_t tex = 0, image = 0, mips = 0;
    const link_masks::Mask* mask = nullptr;
};
void want_textures();
std::vector<GameTexture> game_textures();

// the store's parts for the port (link_appearance_store.cpp)
namespace store {
// once per frame: the save file played (-1 none) and whether it is a new game; returns the look to show
Look frame(int file, bool fresh_game);
// the colour page's look, shown while `on`
void preview(bool on, const Look& l);
}  // namespace store

}  // namespace link_appearance
