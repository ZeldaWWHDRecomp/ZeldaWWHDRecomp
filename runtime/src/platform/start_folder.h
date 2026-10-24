#pragma once
// The game's start folder (issue #266). Setup's launcher starts the game in its data folder (the one
// with install.json, game/, save/, guest-sdk.json): the game finds its files there through relative
// paths (game, save, captures/, guest-sdk.json, ...). Started any other way (bin/wwhd.exe from another
// folder, a double click, a terminal elsewhere) it now finds that folder from its own location and
// works there, exactly as when the launcher starts it; nothing is stored anywhere else.
//   - release / portable layout: <data>/bin/wwhd -> <data>
//   - a code-mod build (<data>/code-builds/<id>/bin/wwhd) runs with WWHD_INSTALL_DIR=<data>
//   - the macOS app: Contents/MacOS/wwhd next to setup's "launch" script, which names the data folder
// --data-dir <folder> chooses the folder explicitly. Source builds (build/wwhd, no install.json next to
// it) keep the working folder they were started in.
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace start_folder {
namespace fs = std::filesystem;

// a path from UTF-8 text (arguments, settings, the launcher script; wide paths on Windows)
inline fs::path utf8_path(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

// a data folder setup made: it writes install.json there
inline bool is_data_folder(const fs::path& dir) {
    std::error_code ec;
    return !dir.empty() && fs::is_regular_file(dir / "install.json", ec);
}

// The macOS app's launcher (setup.py mac_app): `cd "<data folder>" || exit 1`; the folder, or empty.
inline fs::path app_launcher_folder(const fs::path& exe_dir) {
    std::ifstream f(exe_dir / "launch");
    std::string line;
    while (f && std::getline(f, line)) {
        if (line.rfind("cd \"", 0) != 0) continue;
        std::string out;
        for (size_t i = 4; i < line.size(); i++) {
            if (line[i] == '\\' && i + 1 < line.size()) out += line[++i];
            else if (line[i] == '"') return utf8_path(out);
            else out += line[i];
        }
    }
    return {};
}

// The folder to work in: `chosen` (--data-dir) when given, else the data folder found from the
// executable's folder (see the top); empty when there is none (a source build).
inline fs::path find(const fs::path& exe_dir, const std::string& chosen, const std::string& install_env) {
    if (!chosen.empty()) return utf8_path(chosen);
    if (!install_env.empty() && is_data_folder(utf8_path(install_env))) return utf8_path(install_env);
    if (exe_dir.empty()) return {};
    if (exe_dir.filename() == "bin" && is_data_folder(exe_dir.parent_path())) return exe_dir.parent_path();
    const fs::path app = app_launcher_folder(exe_dir);
    if (is_data_folder(app)) return app;
    return {};
}

// Whether the game has to change folder: not when it already runs in `data` (the launcher's start).
inline bool needs_change(const fs::path& current, const fs::path& data) {
    if (data.empty()) return false;
    std::error_code ec;
    if (fs::equivalent(current, data, ec)) return false;
    return true;
}

// A WWHD_ setting that names a file or folder (WWHD_SHADER_CACHE=build/cache.bin, WWHD_STATE_DIR=...):
// a relative one keeps meaning the folder the game was started in, so it is made absolute before the
// game changes folder. Plain values (0, 1, 2048, metal) are left alone.
inline bool path_setting(const std::string& name, const std::string& value) {
    if (name.rfind("WWHD_", 0) != 0 || value.empty()) return false;
    static const char* const suffixes[] = {"_DIR", "_DUMP", "_PATH", "_FILE", "_CACHE", "_CONFIG", "_SETTINGS", "_CONTROLS"};
    bool named = false;
    for (const char* s : suffixes) {
        const std::string suffix = s;
        if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) named = true;
    }
    if (!named) return false;
    bool number = true;
    for (unsigned char c : value) number = number && (std::isdigit(c) || c == '.' || c == '-' || c == ':' || c == ',');
    if (number) return false;
    return utf8_path(value).is_relative();
}

// `value` (relative to `from`) as an absolute path
inline std::string absolute_from(const fs::path& from, const std::string& value) {
    const fs::path p = utf8_path(value);
    if (!p.is_relative()) return value;
    const auto bytes = (from / p).lexically_normal().u8string();
    return std::string(bytes.begin(), bytes.end());
}
}  // namespace start_folder
