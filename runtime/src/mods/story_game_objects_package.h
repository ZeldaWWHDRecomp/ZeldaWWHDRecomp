// Game objects and packages: a package that places the game's own objects by reference ("actor": "game:<id>")
// carries no game data of its own. The mod manager refuses such a package when it holds a game format file
// (by name: .bfres, .szs, .sarc, .pack, .dzb, .dzr, .dzs, .ftex, .bflim; or by content: Yaz0, SARC, FRES or FTEX
// data under any name): game objects load from the player's own game files. Unit-tested in
// runtime/tools/mod_package_test.cpp.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace story_game_objects {

// A file of a game format by its name or its first four bytes.
inline bool game_data(const std::string& name, const char magic[4]) {
    std::string ext = std::filesystem::path(name).extension().string();
    for (char& c : ext) c = char(std::tolower((unsigned char)c));
    for (const char* e : {".bfres", ".szs", ".sarc", ".pack", ".dzb", ".dzr", ".dzs", ".ftex", ".bflim", ".yaz0"})
        if (ext == e) return true;
    for (const char* m : {"Yaz0", "SARC", "FRES", "FTEX"})
        if (std::memcmp(magic, m, 4) == 0) return true;
    return false;
}
// The first file under `dir` (package-relative, '/' separators) that is game data; "" when there is none.
// Symbolic links are neither followed nor allowed to hide one (a link is reported by its own name).
inline std::string game_data_file(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::none, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        const std::string rel = p.lexically_relative(dir).generic_string();
        if (it->is_symlink(ec)) {
            if (game_data(rel, "\0\0\0\0")) return rel;
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        char magic[4] = {};
        std::ifstream f(p, std::ios::binary);
        f.read(magic, 4);
        if (game_data(rel, magic)) return rel;
    }
    return {};
}

}  // namespace story_game_objects
