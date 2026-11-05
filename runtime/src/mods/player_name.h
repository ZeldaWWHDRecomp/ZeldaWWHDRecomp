// Link's name on the save file being played: "Change Link's name" on the settings page's Game tab and
// in the Settings window's Saves tab (issue #328). Up to 16 characters: the game keeps the first 8 (its file
// select shows those, an unmodded game reads them); the whole name is kept in the story block (long_name.h).
// The game keeps one name per save file in SaveMgr's card info (getCardName 0x02720154; the file being
// played: 0x02720170). Its talks, the file select and the save all read that one, so a new name shows
// at once and goes into cking.sav with the player's next save, like any other progress.
#pragma once
#include <string>

struct Cpu;

namespace player_name {

constexpr int kMaxLen = 16;  // the whole name (long_name.h); the game itself keeps 8 (its name screen, cking.sav)
constexpr int kGameLen = 8;

// Any thread: whether a save file is being played, and its name (UTF-8, "" when none)
bool loaded();
std::string current();
// Main thread: asks for a new name (the on-screen text prompt, overlay/text_entry.h)
void ask();
// Guest thread (VPADRead, every controller read): puts an accepted name in
void service(Cpu* c);

}  // namespace player_name
