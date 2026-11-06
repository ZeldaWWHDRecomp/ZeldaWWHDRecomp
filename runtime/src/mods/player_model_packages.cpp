// SPDX-License-Identifier: MPL-2.0
// GameCube player model packages (player_model_packages.h).
#include "player_model_packages.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

#include "../gcmodel/gcmodel.h"
#include "mod_json.h"

namespace mods::player_model_packages {
namespace fs = std::filesystem;
namespace {

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}

bool read(const fs::path& p, std::vector<uint8_t>& out, uintmax_t limit) {
    std::error_code ec;
    if (fs::is_symlink(p, ec) || !fs::is_regular_file(p, ec) || fs::file_size(p, ec) > limit) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

// written beside it first, then moved over: a file the game reads is never half written
void write(const fs::path& p, const void* data, size_t n) {
    fs::create_directories(p.parent_path());
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write((const char*)data, (std::streamsize)n);
        if (!f) throw std::runtime_error("Cannot write " + p.filename().string());
    }
    fs::rename(tmp, p);
}

// metadata.txt "key: value" (as the fork reads it)
std::string metadata(const fs::path& f, const std::string& key) {
    std::vector<uint8_t> d;
    if (!read(f, d, 64 * 1024)) return {};
    std::string text(d.begin(), d.end()), line;
    for (size_t at = 0; at <= text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        line = text.substr(at, end - at);
        at = end + 1;
        const size_t c = line.find(':');
        if (c == std::string::npos) continue;
        auto trim = [](std::string s) {
            const auto a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        };
        if (lower(trim(line.substr(0, c))) == key) {
            std::string v = trim(line.substr(c + 1));
            if (v.size() > 128) v.resize(128);
            // (printable only: it is shown in the mod list)
            v.erase(std::remove_if(v.begin(), v.end(), [](unsigned char ch) { return ch < 32; }), v.end());
            return v;
        }
    }
    return {};
}

}  // namespace

std::vector<fs::path> find_arcs(const fs::path& tree) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(tree, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_symlink(ec)) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && lower(it->path().filename().string()) == "link.arc") out.push_back(it->path());
    }
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
        const auto da = std::distance(a.begin(), a.end()), db = std::distance(b.begin(), b.end());
        return da != db ? da < db : a < b;
    });
    return out;
}

std::string make_package(const fs::path& tree, const fs::path& arc, const fs::path& out, const std::string& fallback) {
    const fs::path dir = arc.parent_path();
    std::string name = dir == tree || dir.filename().empty() ? fallback : dir.filename().string();
    if (name.empty()) name = "Player model";
    if (name.size() > 96) name.resize(96);
    std::string id = lower(name);
    for (char& c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) c = '-';
    if (id.size() > 50) id.resize(50);
    id = "player-model." + id;
    const std::string author = metadata(dir / "metadata.txt", "author");
    json::Value m;
    m["format_version"] = 1;
    m["id"] = id;
    m["name"] = name;
    m["version"] = "1.0.0";
    m["game_id"] = "wwhd";
    m["kind"] = kKind;
    m["minimum_manager_version"] = "1.10.0";
    if (!author.empty()) m["author"] = author;
    m["description"] = "A GameCube player model converted for HD Link. Choose it for a save file on this page; the game "
                       "shows it at once.";
    fs::create_directories(out / "source");
    std::error_code ec;
    fs::copy_file(arc, out / kSource, fs::copy_options::overwrite_existing, ec);
    if (ec) throw std::runtime_error("Cannot copy Link.arc: " + ec.message());
    if (fs::is_regular_file(dir / "metadata.txt", ec) && !fs::is_symlink(dir / "metadata.txt", ec))
        fs::copy_file(dir / "metadata.txt", out / "source/metadata.txt", fs::copy_options::overwrite_existing, ec);
    const std::string text = json::dump(m) + "\n";
    write(out / "manifest.json", text.data(), text.size());
    return id;
}

fs::path game_pack(const fs::path& game) {
    if (game.empty()) return {};
    fs::path p = game / "content" / "Common" / "Pack" / "permanent_3d.pack";
    std::error_code ec;
    return fs::is_regular_file(p, ec) ? p : fs::path();
}

bool convert(const fs::path& package, const fs::path& gamePack, const std::function<void(int)>& progress, std::string& error) {
    std::error_code ec;
    fs::remove(package / kFailed, ec);
    auto fail = [&](const std::string& why) {
        error = why;
        try {
            write(package / kFailed, why.data(), why.size());
        } catch (const std::exception&) {
        }
        return false;
    };
    std::vector<uint8_t> arc, pack, szs;
    if (!read(package / kSource, arc, 64ull << 20)) return fail("Cannot read the model's Link.arc");
    if (gamePack.empty() || !read(gamePack, pack, 256ull << 20)) return fail("The game's permanent_3d.pack is missing");
    const gcmodel::Result r = gcmodel::convert(arc, pack, szs, [&](int pc, const std::string&) {
        if (progress) progress(pc);
    });
    if (!r.error.empty()) return fail(r.error);
    try {
        write(package / kReport, r.report.data(), r.report.size());
        write(package / kModel, szs.data(), szs.size());
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    error.clear();
    return true;
}

}  // namespace mods::player_model_packages
