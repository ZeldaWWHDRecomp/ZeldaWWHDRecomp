// The file system tests (mod_content_fs_test, language_source_test) without the story layouts, the
// extended item icon pack and the player models: hle/fs.cpp asks them about the game's reads; here they
// serve nothing, so every read goes to the game's own file (as in a session without the story system).
#include <string>

namespace story_lyt_screen {
std::string archive_for(const std::string&) { return ""; }
}  // namespace story_lyt_screen
namespace item_icons {
std::string pack_for(const std::string&, const std::string&) { return ""; }
}  // namespace item_icons
namespace player_models {
void note_open(const std::string&, const std::string&) {}
}  // namespace player_models
