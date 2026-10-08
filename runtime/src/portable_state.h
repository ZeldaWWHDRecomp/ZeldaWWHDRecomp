// Portable save states: a small text file (a few KB) with the player's progress and position only,
// for bug reports. It holds no game code and no game data: the save data of the current Quest Log
// exactly as the game's own save writes it into cking.sav (dSv_info_c::memory_to_card plus the HD
// per-file sections), the stage/room/layer, Link's position and angle and a header. Loading puts the
// save data into the running game and enters the recorded stage with Link at the recorded position
// (savestate.cpp). Not an exact snapshot: enemies, running cutscenes and actor state start fresh.
//
// This file is the format only (no game or runtime dependencies; runtime/tools/portable_state_test.cpp).
//
// File (UTF-8 text, one "key = value" per line, '#' comments):
//   # Wind Waker HD portable save state ...
//   format = 1
//   title_id = 0005000010143500
//   ...
//   savedata = <hex>            0xA94 bytes: one cking.sav file block (0x768 bytes of save data,
//                               zero up to 0xA8C, the game's byte sum and complement sum)
//   hd_player = <hex>  ...      the HD per-file sections (16, 4, 20, 220 bytes)
//   checksum = crc32:XXXXXXXX   CRC-32 of every byte before this line
//
// Safety: write() refuses files over kMaxFileSize and any field it does not know or whose size is not
// the expected one, and read() refuses the same, so a portable state can never carry a memory dump.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pstate {

constexpr int kFormatVersion = 1;
constexpr size_t kMaxFileSize = 64 * 1024;  // a real file is about 7 KB
constexpr size_t kMaxLine = 8192;           // longest line (the save data in hex is 5416 characters)
constexpr const char* kExtension = "wwstate";

// binary fields and their exact sizes (cking.sav layout: tools/savegame/README.md)
constexpr size_t kSaveDataSize = 0xA94;  // one file block of cking.sav
constexpr size_t kSaveDataUsed = 0x768;  // dSv_save_c as memory_to_card packs it
constexpr size_t kSaveChecksumAt = 0xA8C;
constexpr size_t kHdPlayerSize = 16, kHdStatusSize = 4, kHdEventSize = 20, kHdMapSize = 220;

struct State {
    // header
    int format = kFormatVersion;
    std::string title_id;       // "0005000010143500" (meta.xml)
    uint32_t title_version = 0;
    std::string game_hash;      // FNV-1a of cking.rpx, hex (which executable; not its contents)
    std::string runtime;        // "v0.2.6 (abc1234)"
    std::string created;        // local time, "2026-10-08 14:03:11"
    int file_slot = 0;          // Quest Log 0..2 (dSv_info_c::mDataNum)
    std::string player_name;    // UTF-8
    // place
    std::string stage;          // dComIfGp start stage name ("sea", "M_NewD2", ...)
    int start_point = 0, start_room = 0, layer = -1;  // how the stage was entered
    int room = 0;               // Link's room (fopAcM_GetRoomNo)
    float pos[3] = {0, 0, 0};   // Link's current.pos
    int angle_y = 0;            // shape_angle.y (s16)
    int link_proc = -1;         // daPy_lk_c::mCurProc (informational)
    bool on_ship = false;       // Link rides the boat (a daPyProc_SHIP_* procedure): he starts on it
    bool has_ship = false;      // the boat (dComIfGp_getShipActor) is in the stage: it goes back to its place
    float ship_pos[3] = {0, 0, 0};
    int ship_angle_y = 0;
    float time_of_day = 0;      // dSv_player_status_b_c::mTime (degrees, 0..360)
    int date = 0;               // mDate (day counter; day of week = date % 7)
    // save data
    std::vector<uint8_t> savedata, hd_player, hd_status, hd_event, hd_map;
};

uint32_t crc32(const void* data, size_t n, uint32_t crc = 0);
// the game's checksum of a file block (SaveMgr 02721AC4): byte sum and complement sum over 0xA8C bytes
void save_checksum(const uint8_t* block, uint32_t& sum, uint32_t& complement);
// sets the checksum pair at +0xA8C (big-endian, as in cking.sav)
void seal_savedata(std::vector<uint8_t>& block);
bool savedata_checksum_ok(const std::vector<uint8_t>& block);

// the file text; "" with `why` set when refused (wrong sizes, too large)
std::string write(const State& s, std::string& why);
// parses and checks a file: format version, checksum, field sizes, total size
bool read(const std::string& text, State& out, std::string& why);
// the guard on its own: true when `text` is small and has no binary field beyond the known ones
bool blob_check(const std::string& text, std::string& why);

}  // namespace pstate
