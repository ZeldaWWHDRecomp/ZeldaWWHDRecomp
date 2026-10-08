// The characters the game can draw in the player's name, read at run time from the user's own game
// files (nothing of the game is stored in the port): the message font CKingMsg.bffnt in the language
// pack Common/Pack/permanent_2d_<Region><Language>.pack. That font draws the name on the file select and
// in the dialogs (a name with ñ, ã, œ shows there although the menu font CKingMain lacks them), so it
// decides which keys the text prompt (text_entry.h) offers and which typed characters it takes.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

namespace game_font {

using Glyphs = std::unordered_set<uint32_t>;  // code points (the font maps UTF-16 units)

// The font's characters for the swkbd language (0 Japanese, 1 English, 2 French, ...): null when the
// font can't be read (the prompt then offers its full set; the reason is logged once). Any thread;
// read once per language and kept.
std::shared_ptr<const Glyphs> name_glyphs(int language);

// The characters of the message font (CKingMsg.bffnt) in a 2D language pack file
// (permanent_2d_*.pack): null with the reason in `why` when it can't be read. Also used by the
// right-to-left text support (rtl_text_hooks.cpp) on the pack the game actually opened.
std::shared_ptr<const Glyphs> pack_font(const std::string& pack_path, std::string* why);

// The code points a BFFNT (Wii U / 3DS font, either byte order) maps, from its CMAP blocks; false if
// the data is no font. Exposed for tests.
bool parse_bffnt(const uint8_t* data, size_t size, Glyphs& out);

}  // namespace game_font
