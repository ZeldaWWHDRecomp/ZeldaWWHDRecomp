// SPDX-License-Identifier: MPL-2.0
// Online co-op: the save data's layout, what of it the players share and what stays each player's own (GAPS C18-C20).
//
// dSv_save_c as the HD game keeps it in memory (0x778 bytes, at dSv_info_c +0). The offsets are layout facts from
// zeldaret/tww include/d/d_save.h (CC0) and this project's WWHD decompilation (wwhd_src include/d/d_save.h, whose
// WWHD_OFFSET/WWHD_SIZE checks hold them in the decomp build); no game data. Everything here is plain data and
// functions on a byte image of dSv_save_c, so the merge, the local-state list and the save check are tested without the
// game (runtime/tools/coop_layout_test.cpp).
//
// The local-state list (C19, Anchor's way: it keeps a place's temporary state, e.g. water levels, local): every byte of
// dSv_save_c is either in a shared field or in the list, and inside a shared bit field single bits can be local too.
// What is local is never sent, never put in from another player and never kept by a server's team store.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mods::coop::layout {

constexpr uint16_t kSaveSize = 0x778;  // sizeof(dSv_save_c)

// dSv_save_c members
namespace sv {
constexpr uint16_t kStatusA = 0x000, kStatusB = 0x018, kReturnPlace = 0x030, kItem = 0x03C, kGetItem = 0x051,
                   kItemRecord = 0x066, kItemMax = 0x06E, kBagItem = 0x076, kGetBagItem = 0x090, kBagItemRecord = 0x09C,
                   kCollect = 0x0B4, kMap = 0x0C4, kInfo = 0x148, kConfig = 0x1A4, kPriest = 0x1AC, kStatusC = 0x1BC,
                   kMemory = 0x380, kOcean = 0x5C0, kEvent = 0x624, kReserve = 0x724;
// dSv_player_status_a_c
constexpr uint16_t kMaxLife = 0x00, kLife = 0x02, kRupee = 0x04, kSelectItem = 0x09, kSelectEquip = 0x0E,
                   kWalletSize = 0x12, kMaxMagic = 0x13, kMagic = 0x14;
// dSv_player_item_max_c: mItemMax2 (reserved, arrows, bombs)
constexpr uint16_t kItemMax2 = 0x00;
// dSv_player_item_c slots 14-17: the bottles (dInvSlot_*)
constexpr uint16_t kItemSlots = 21, kFirstBottle = 14, kLastBottle = 17;
// dSv_memory_c (dSv_memBit_c) of each of the 16 stages
constexpr uint16_t kMemSize = 0x24, kStages = 16;
constexpr uint16_t kTbox = 0x00, kSwitch = 0x04, kMemItem = 0x14, kVisitedRoom = 0x18, kKeyNum = 0x20, kDungeonItem = 0x21;
// dSv_event_c: bytes 0x00..0x78 hold event bits (both games use 0x00..0x41), 0x79..0xFF the event registers
// (dSv_event_c::setEventReg: byte reg >> 8, mask reg & 0xFF; zeldaret/tww d_save_event_flag.inc)
constexpr uint16_t kEventSize = 0x100, kEventRegs = 0x79;
}  // namespace sv

constexpr uint16_t memory(int stage, uint16_t member) { return (uint16_t)(sv::kMemory + stage * sv::kMemSize + member); }
constexpr uint16_t event_byte(uint16_t flag) { return (uint16_t)(sv::kEvent + (flag >> 8)); }

// the union merge (Anchor's way, DESIGN.md Q6): kBits only the changed bits (a join's team state: set bits only),
// kMax the larger, kFill only where this game has none (0xFF) or the same kind's better one, kSet as it comes (a shared
// count: small keys), kBottle a new bottle as an empty one
enum Mode : uint8_t { kBits, kSet, kMax, kBottle, kFill };
struct Field { uint16_t off, size; Mode mode; const char* name; };
// mask 0xFF: whole bytes (never inside a shared field); other masks: bits inside a shared bit field
struct Local { uint16_t off, size; uint8_t mask; const char* why; };

const std::vector<Field>& fields();
const std::vector<Local>& local_state();
// the shared field holding off..off+size (nullptr: none)
const Field* field_of(uint16_t off, uint8_t size);
// the bits of a byte of dSv_save_c that are shared (0: all local)
uint8_t shared_bits(uint16_t off);
// "event[0x13]", "memory[3].switch" ...: for traces and the save check
std::string name_of(uint16_t off);
// the merge a server's team store gives the shared field at off..off+size (X3): 0 bits (OR), 1 a larger value, 2 filled
// where empty; -1 for none it keeps (local, or a count as it comes: small keys)
int store_mode(uint16_t off, uint8_t size);

// net::kProgress messages: records {u16 offset (little end), u8 size, the value, and for bit fields the changed bits}.
// Another game's message into the image `save` (kSaveSize bytes), each record by its field's mode, local bits left
// alone. A malformed message (a record outside the shared fields, a size not its field's, cut short) changes nothing:
// false. `trace`: the fields as they are now (" 04A=50 66B=03", each record's last byte)
bool merge(uint8_t* save, const uint8_t* m, size_t n, std::string* trace = nullptr);
// this game's changes from `was` to `now` as one message (shared bits only); `changed` the fields changed, `said` their
// offsets (" 051 6A4", at most 64 characters)
std::string diff(const uint8_t* was, const uint8_t* now, int* changed = nullptr, std::string* said = nullptr);
// the image's shared progress as messages of at most 1000 bytes (cut between records), for a player who joins: every
// shared field as a change from nothing; `counts` false leaves out kSet counts (only the leader's at a join)
std::vector<std::string> team_state(const uint8_t* now, bool counts = true);

// C20: every kProgress message carries a CRC-32 of its records (4 bytes, little end, after them): seal adds it, open
// checks and removes it (false: damaged, `m` unchanged); a damaged message is refused whole
std::string seal(const std::string& m);
bool open(std::string& m);

// C20: a Quest Log's dSv_save_c from a cking.sav (3 files of 0xA94 bytes, each the card's packed save data first:
// dSv_info_c::memory_to_card, no padding, checked by its byte sum pair at +0xA8C); false: not a cking.sav or file `n`
// (0-2) damaged
bool from_card(const std::vector<uint8_t>& sav, int n, std::vector<uint8_t>& save);
// the bytes where two images differ outside the shared bits ("offset field" lines): empty when `after` is `before` plus
// shared progress only
std::vector<std::string> local_changes(const uint8_t* before, const uint8_t* after);

}  // namespace mods::coop::layout
