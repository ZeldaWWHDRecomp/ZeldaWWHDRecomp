// Where story progress lives (docs/mod-sdk-v2.md, "Where story progress is saved"): one area per Quest
// Log, kept with the game's own save the way the game keeps its flags.
//
//   - in memory: the loaded Quest Log's area plus a copy of each Quest Log's area as last saved (the
//     game keeps the same pair for its own data: the save in memory and its card buffer). Both live in
//     guest memory (set_memory), so full save states carry them;
//   - the game's dSv_info_c::card_to_memory (loading a Quest Log, continuing after a game over) loads
//     the area from the saved copy; memory_to_card (saving) saves it; initdata_to_card (the
//     initial data of a new or erased Quest Log, which a new game then loads) empties it. Story changes are therefore
//     written with the next game save, and reloading without saving undoes them, like the game's own;
//   - in the file: the story block after the game's bytes of cking.sav and cking.sav.backup
//     (story_block.h). The game never sees it: its reads end at its own part and size queries give
//     its own size. After the game writes either file, the block is appended again (temp file +
//     rename). Without any story area in use, nothing is appended, so a vanilla save stays vanilla;
//   - a damaged block (bad checksum) is not used: the file is copied once to cking.sav.story-damaged
//     and the log says so;
//   - the 19-record form earlier builds kept in the game's reserved save bytes is moved into the
//     area when that Quest Log is loaded (story_block::migrate), and those bytes are zero again.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "story_block.h"
#include "story_mail_book.h"

struct Cpu;

namespace story_store {

inline constexpr uint32_t kMemoryBytes = (1 + story_block::kQuestLogs) * story_block::kAreaSize;

// guest memory for the areas (kMemoryBytes, zeroed by the caller); before it is set, a block read from
// the file is kept as bytes and written back unchanged
void set_memory(uint32_t guest);
bool ready();
// all kMemoryBytes of the areas (the loaded Quest Log's, then the saved copies); null before set_memory.
// The world editor's test mode keeps a copy while it runs and puts it back after.
uint8_t* memory_block();
// the loaded Quest Log's area (null before set_memory)
uint8_t* area();
// the record view of it (a null Book before set_memory)
story_mail::Book book();

// the file side (hle/fs.cpp): the game's save files that carry the block
bool is_save_file(const std::string& guest_path);
// opened for reading: reads the block (cking.sav), returns the size of the game's part
uint64_t opened_for_read(const std::string& guest_path, const std::string& host_path);
// the size of the game's part (stat)
uint64_t game_size(const std::string& host_path, uint64_t file_size);
// the game closed it after writing: append the block again
void written(const std::string& guest_path, const std::string& host_path);

// the game side
void set_game_calls(void (*original)(Cpu*, uint32_t));
void set_progress_loaded(void (*callback)());
void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
bool installed();
// while alive, the game's save copy functions don't move story areas (portable save states use
// them on scratch buffers)
struct Quiet {
    Quiet();
    ~Quiet();
};

}  // namespace story_store
