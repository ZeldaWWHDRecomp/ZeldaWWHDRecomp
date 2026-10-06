// Tests the game language detection (game_languages.cpp) on a synthetic USA game folder: the packs
// found without case, the region, and the language the game gets for one that is not on the disc.
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "game_languages.h"
namespace config { std::string game_dir, save_dir; }
void log_msg(const char*, ...) {}
int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const fs::path root = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "wwhd_game_languages_test";
    fs::remove_all(root);
    const fs::path pack = root / "content" / "Common" / "PACK";  // any case, as on a case-sensitive host
    fs::create_directories(pack);
    for (const char* f : {"permanent_2d_UsEnglish.pack", "permanent_2d_usfrench.pack", "permanent_2d_UsSpanish.pack",
                          "permanent_3d.pack", "permanent_2d_UsGerman.pack.bak"})
        std::ofstream(pack / f) << "x";
    config::game_dir = root.string();
    assert((game_lang::available() == std::vector<int>{1, 2, 5}));
    assert(game_lang::region() == "USA");
    assert(game_lang::is_available(1) && game_lang::is_available(5) && !game_lang::is_available(3) && !game_lang::is_available(0));
    assert(game_lang::usable(2) == 2 && game_lang::usable(3) == 1 && game_lang::usable(10) == 1);
    assert(std::string(game_lang::name(11)) == "Chinese (Taiwan)" && std::string(game_lang::name(12)) == "?");
    assert(game_lang::started() == -1);
    game_lang::set_started(5);
    assert(game_lang::started() == 5);
    fs::remove_all(root);
    puts("game_languages_test: USA packs, region, fallback to English passed");
}
