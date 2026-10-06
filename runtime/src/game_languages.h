// The console languages the installed game has text for, from its language packs. The game (cking.rpx)
// names nine 2D packs, Common/Pack/permanent_2d_<Region><Language>.pack: JpJapanese, UsEnglish,
// UsFrench, UsSpanish, EuEnglish, EuFrench, EuGerman, EuItalian, EuSpanish (a table at 0x1048DD4C,
// filled by 0x02613710). A disc carries those of its region, so the packs on disc are the languages
// it offers; the other Wii U console languages (Chinese, Korean, Dutch, Portuguese, Russian) have
// no pack in this game. The USA game also maps any other console language to English itself
// (0x025F9448 after reading cafe.language: only 1, 2 and 5 are kept).
#pragma once
#include <string>
#include <vector>

namespace game_lang {

// Wii U console language codes (cafe.language): 0 ja, 1 en, 2 fr, 3 de, 4 it, 5 es, 6 zh, 7 ko,
// 8 nl, 9 pt, 10 ru, 11 zh-TW
constexpr int kLanguages = 12;
const char* name(int language);  // "English", ...; "?" out of range

// The languages whose pack is in <game_dir>/content/Common/Pack, in code order; empty when no pack is
// found (no game files: then nothing is restricted). Looked up once (one directory listing), any thread.
const std::vector<int>& available();
bool is_available(int language);  // true when available() is empty
// "USA", "Europe", "Japan" (joined with " / " if packs of several regions are present), or ""
const std::string& region();
// The language the game gets for `language`: itself when available, else English when available,
// else the first available one
int usable(int language);

// the console language this start runs with, once the game has read it (-1 before)
int started();
void set_started(int language);

}  // namespace game_lang
