#pragma once
#include <filesystem>
#include <map>
#include <string>
namespace mods::content {
using Files=std::map<std::string,std::string>;
// Validate a replacement tree. Keys follow Wii U case-insensitive path semantics.
bool known_pack(const std::string& filename);
// "english" for permanent_2d_UsEnglish.pack (any region, any case); "" if not one of the game's 2D language packs
std::string pack_language(const std::string& filename);
// The installed game folder: loose imported files that name one of its content files are placed at that path.
void set_game_root(const std::filesystem::path& game);
std::filesystem::path game_root_path();  // "" before set_game_root
void import_legacy(const std::filesystem::path& stage,const std::string& source_name);
// Empty trees are allowed only after the package validated meaningful world declarations.
Files index(const std::filesystem::path& directory, bool allow_empty_world = false);
// Called once before guest execution. Never swap resources during a session.
void activate(Files files);
std::string replacement(const std::string& guest, const std::string& mode="rb");
}
