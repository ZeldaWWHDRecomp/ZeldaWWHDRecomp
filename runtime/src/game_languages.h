// The console languages the installed game has text for, from its language packs, and the optional
// language sources (docs/language-packs.md): text, fonts and 2D layouts of a European or Japanese
// disc the player also owns, played with the USA game code.
//
// The game (cking.rpx) names nine 2D packs, Common/Pack/permanent_2d_<Region><Language>.pack:
// JpJapanese, UsEnglish, UsFrench, UsSpanish, EuEnglish, EuFrench, EuGerman, EuItalian, EuSpanish (a
// table at 0x1048DD4C, filled by 0x02613710). It picks one by the region and the language of its
// system setting object (0x101F4BAC: +0x10 region, Wii U region bits 1 Japan / 2 USA / 4 Europe; +0x14
// console language), 0x02612BE0. A disc carries those of its region; the other Wii U console
// languages (Chinese, Korean, Dutch, Portuguese, Russian) have no pack in this game. The USA game's
// reader of the setting (0x025F9448) always stores region 2 and keeps only languages 1, 2 and 5
// (others become English). With a language source active the runtime stores the source's region and
// language there after it (language_region.cpp), and the source's pack is read in place of the
// installed game's file of that name (redirect()).
//
// Tested with a real European game (German, Italian, French, Spanish, English); UNTESTED with a
// Japanese game (docs/language-packs.md).
#pragma once
#include <string>
#include <vector>

namespace game_lang {

// Wii U console language codes (cafe.language): 0 ja, 1 en, 2 fr, 3 de, 4 it, 5 es, 6 zh, 7 ko,
// 8 nl, 9 pt, 10 ru, 11 zh-TW
constexpr int kLanguages = 12;
const char* name(int language);  // "English", ...; "?" out of range

// Wii U region bits, as the game keeps them in its system setting (+0x10)
enum Region : int { kNoRegion = 0, kJapan = 1, kUsa = 2, kEurope = 4 };
const char* region_code(int region);  // "jp", "us", "eu"; "" for others
const char* region_name(int region);  // "Japan", "USA", "Europe"; "" for others
int region_from_code(const std::string& code);  // "eu", "EU", "Europe" -> kEurope ...; kNoRegion if unknown

// A language pack the game can load.
struct Pack {
    int language = -1;  // console language code
    int region = kNoRegion;
    std::string file;   // the name the game asks for: permanent_2d_EuGerman.pack
    std::string host;   // the file on this computer
    bool source = false;  // from a language source, not from the installed game
};

// The languages whose pack is in <game_dir>/content/Common/Pack, in code order; empty when no pack is
// found (no game files: then nothing is restricted). Looked up once (one directory listing), any thread.
const std::vector<int>& available();
bool is_available(int language);  // true when available() is empty
// "USA", "Europe", "Japan" (joined with " / " if packs of several regions are present), or ""
const std::string& region();
// The language the game gets for `language`: itself when available, else English when available,
// else the first available one
int usable(int language);

// Where the language sources are: WWHD_LANG_DIR, else the folder game-lang next to the game folder
// (the setup's data folder: data/game and data/game-lang/<EU|JP>/content/Common/Pack/...), else
// game-lang in the current folder (the launchers start the game in the data folder; a game folder
// used in place is elsewhere).
std::string sources_dir();
// The packs of the language sources (<sources_dir>/<any folder>/content/Common/Pack, names matched
// without case, files starting with "SARC"), in region and language order; packs the installed game
// has itself are left out. Looked up once, any thread.
const std::vector<Pack>& source_packs();
const Pack* source_pack(int language, int region);
// The languages of the language sources of one region, in code order
std::vector<int> source_languages(int region);

// What a start runs with: the language the game gets and, when it comes from a language source,
// that source's region and pack (pack == nullptr: the installed game's own text).
struct Start {
    int language = -1;
    int region = kNoRegion;  // the region the game is told; kNoRegion: its own (USA)
    const Pack* pack = nullptr;
};
// The start for a wanted language and region (kNoRegion: the installed game). A region whose source
// has no pack for that language falls back to the installed game (usable()).
Start choose(int language, int region);
// The start of this run, set once when the game reads the console language (before that: language -1)
Start current();
void begin(const Start& start);

// the console language this start runs with, once the game has read it (-1 before)
int started();
void set_started(int language);  // begin() with the installed game's own text

// The host file for a guest path when it is the pack of the active language source (the game asks for
// it by its own name, e.g. /vol/content/Common/Pack/permanent_2d_EuGerman.pack): "" otherwise.
std::string redirect(const std::string& guest);

// The language byte of the save's options (save + 0x12F0 + 4, the GameCube PAL order: 0 English,
// 1 German, 2 French, 3 Spanish, 4 Italian) for a console language; 0 for the others
int options_language(int language);

// The German genitive suffix the European game appends to the player's name in three messages (its
// 0x025F85AC; language_region.cpp): "'" after a name ending in s, x or z (either case, last byte),
// else "s"
const char* german_genitive_suffix(const std::string& name);

}  // namespace game_lang
