// The game's languages from its language packs and the language sources (game_languages.h).
#include "game_languages.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

#include "runtime.h"

namespace game_lang {
namespace {

namespace fs = std::filesystem;

const char* const kNames[kLanguages] = {"Japanese", "English", "French", "German", "Italian", "Spanish",
                                        "Chinese", "Korean", "Dutch", "Portuguese", "Russian", "Chinese (Taiwan)"};
// the pack names the game knows: region prefix + language, per console language (0x1048DD4C)
struct KnownPack { const char* prefix; int region; const char* language; int code; };
const KnownPack kPacks[] = {
    {"Jp", kJapan, "Japanese", 0}, {"Us", kUsa, "English", 1},     {"Us", kUsa, "French", 2},
    {"Us", kUsa, "Spanish", 5},    {"Eu", kEurope, "English", 1}, {"Eu", kEurope, "French", 2},
    {"Eu", kEurope, "German", 3},  {"Eu", kEurope, "Italian", 4}, {"Eu", kEurope, "Spanish", 5},
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    return s;
}

std::string pack_file(const KnownPack& p) { return std::string("permanent_2d_") + p.prefix + p.language + ".pack"; }

// a known pack for a file name (any case), or null
const KnownPack* known(const std::string& filename) {
    const std::string s = lower(filename);
    for (const KnownPack& p : kPacks)
        if (s == lower(pack_file(p))) return &p;
    return nullptr;
}

// <root>/content/Common/Pack matched without case (the disc's spelling, any host file system); empty if absent
fs::path pack_dir(const fs::path& root) {
    std::error_code ec;
    fs::path at = root;
    for (const char* part : {"content", "common", "pack"}) {
        fs::path next;
        for (auto& e : fs::directory_iterator(at, ec))
            if (lower(e.path().filename().string()) == part && e.is_directory(ec)) next = e.path();
        if (next.empty()) return {};
        at = next;
    }
    return at;
}

struct Found {
    std::vector<int> languages;
    std::string region;
    std::vector<std::string> files;  // lower-case names of the installed game's packs
};

const Found& found() {
    static const Found f = [] {
        Found out;
        std::error_code ec;
        const fs::path at = pack_dir(fs::path(config::game_dir));
        if (at.empty()) {
            LOG("[config] no content/Common/Pack in %s: the game's languages are unknown, all are offered",
                config::game_dir.c_str());
            return out;
        }
        bool have[kLanguages] = {};
        std::vector<std::string> regions;
        for (auto& e : fs::directory_iterator(at, ec)) {
            const KnownPack* p = known(e.path().filename().string());
            if (!p) continue;
            have[p->code] = true;
            out.files.push_back(lower(pack_file(*p)));
            if (std::find(regions.begin(), regions.end(), region_name(p->region)) == regions.end())
                regions.push_back(region_name(p->region));
        }
        for (int i = 0; i < kLanguages; i++)
            if (have[i]) out.languages.push_back(i);
        std::sort(regions.begin(), regions.end());
        for (auto& r : regions) out.region += (out.region.empty() ? "" : " / ") + r;
        std::string list;
        for (int i : out.languages) list += std::string(list.empty() ? "" : ", ") + kNames[i];
        if (out.languages.empty()) LOG("[config] no language pack in %s: all languages are offered", at.string().c_str());
        else LOG("[config] game languages (%s): %s", out.region.c_str(), list.c_str());
        return out;
    }();
    return f;
}

bool sarc_file(const fs::path& p) {
    FILE* f = fopen(p.string().c_str(), "rb");
    if (!f) return false;
    char magic[4] = {};
    const bool ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "SARC", 4);
    fclose(f);
    return ok;
}

std::mutex g_mu;
Start g_start;
std::atomic<bool> g_started{false};
std::string g_redirect_from;  // lower-case "pack/<file>" of the active source pack
std::string g_redirect_to;

}  // namespace

const char* name(int language) { return language >= 0 && language < kLanguages ? kNames[language] : "?"; }

const char* region_code(int region) {
    return region == kJapan ? "jp" : region == kUsa ? "us" : region == kEurope ? "eu" : "";
}
const char* region_name(int region) {
    return region == kJapan ? "Japan" : region == kUsa ? "USA" : region == kEurope ? "Europe" : "";
}
int region_from_code(const std::string& code) {
    const std::string s = lower(code);
    if (s == "jp" || s == "jpn" || s == "japan") return kJapan;
    if (s == "us" || s == "usa") return kUsa;
    if (s == "eu" || s == "eur" || s == "europe") return kEurope;
    return kNoRegion;
}

const std::vector<int>& available() { return found().languages; }
const std::string& region() { return found().region; }

bool is_available(int language) {
    const auto& a = available();
    return a.empty() || std::find(a.begin(), a.end(), language) != a.end();
}

int usable(int language) {
    if (is_available(language)) return language;
    return is_available(1) ? 1 : available().front();
}

std::string sources_dir() {
    if (const char* e = getenv("WWHD_LANG_DIR"); e && *e) return e;
    fs::path game = fs::path(config::game_dir);
    if (game.filename().empty()) game = game.parent_path();  // "data/game/"
    const fs::path beside = game.parent_path() / "game-lang";
    // a game folder used in place (portable setup from an extracted folder) is not in the data folder;
    // the launchers start the game in the data folder
    std::error_code ec;
    if (!fs::is_directory(beside, ec) && fs::is_directory("game-lang", ec)) return "game-lang";
    return beside.string();
}

const std::vector<Pack>& source_packs() {
    static const std::vector<Pack> packs = [] {
        std::vector<Pack> out;
        std::error_code ec;
        const fs::path root = sources_dir();
        if (!fs::is_directory(root, ec)) return out;
        const auto& own = found().files;
        std::vector<fs::path> folders;
        for (auto& e : fs::directory_iterator(root, ec))
            if (e.is_directory(ec)) folders.push_back(e.path());
        std::sort(folders.begin(), folders.end());
        for (const fs::path& folder : folders) {
            const fs::path at = pack_dir(folder);
            if (at.empty()) continue;
            for (auto& e : fs::directory_iterator(at, ec)) {
                const KnownPack* p = known(e.path().filename().string());
                if (!p || !e.is_regular_file(ec)) continue;
                const std::string file = pack_file(*p);
                if (std::find(own.begin(), own.end(), lower(file)) != own.end()) continue;  // the game has it
                bool dup = false;
                for (const Pack& q : out) dup |= lower(q.file) == lower(file);
                if (dup) continue;
                if (!sarc_file(e.path())) {
                    LOG("[config] language source %s is not a pack (no SARC header): ignored", e.path().string().c_str());
                    continue;
                }
                out.push_back({p->code, p->region, file, e.path().string(), true});
            }
        }
        std::sort(out.begin(), out.end(), [](const Pack& a, const Pack& b) {
            return a.region != b.region ? a.region > b.region : a.language < b.language;  // Europe, then Japan
        });
        for (const Pack& p : out)
            LOG("[config] language source: %s (%s) from %s", name(p.language), region_name(p.region), p.host.c_str());
        return out;
    }();
    return packs;
}

const Pack* source_pack(int language, int region) {
    for (const Pack& p : source_packs())
        if (p.language == language && p.region == region) return &p;
    return nullptr;
}

std::vector<int> source_languages(int region) {
    std::vector<int> out;
    for (const Pack& p : source_packs())
        if (p.region == region) out.push_back(p.language);
    std::sort(out.begin(), out.end());
    return out;
}

Start choose(int language, int region) {
    Start s;
    if (region != kNoRegion && region != kUsa) {
        if (const Pack* p = source_pack(language, region)) {
            s.language = language;
            s.region = region;
            s.pack = p;
            return s;
        }
    }
    s.language = usable(language < 0 || language >= kLanguages ? 1 : language);
    return s;
}

Start current() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_start;
}

void begin(const Start& start) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_start = start;
    g_redirect_from.clear();
    g_redirect_to.clear();
    if (start.pack) {
        g_redirect_from = "pack/" + lower(start.pack->file);
        g_redirect_to = start.pack->host;
    }
    g_started.store(true, std::memory_order_release);
}

int started() { return g_started.load(std::memory_order_acquire) ? current().language : -1; }
void set_started(int language) {
    Start s;
    s.language = language;
    begin(s);
}

std::string redirect(const std::string& guest) {
    if (!g_started.load(std::memory_order_acquire)) return {};
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_redirect_from.empty() || guest.size() < g_redirect_from.size()) return {};
    std::string g = lower(guest);
    std::replace(g.begin(), g.end(), '\\', '/');
    if (g.compare(g.size() - g_redirect_from.size(), g_redirect_from.size(), g_redirect_from) != 0) return {};
    // the pack folder itself: "pack/<file>" alone or after a '/'
    if (g.size() > g_redirect_from.size() && g[g.size() - g_redirect_from.size() - 1] != '/') return {};
    return g_redirect_to;
}

int options_language(int language) {
    switch (language) {
        case 3: return 1;  // German
        case 2: return 2;  // French
        case 5: return 3;  // Spanish
        case 4: return 4;  // Italian
        default: return 0;  // English, Japanese
    }
}

const char* german_genitive_suffix(const std::string& name) {
    const char last = name.empty() ? '\0' : name.back();
    return strchr("sxzSXZ", last) && last ? "'" : "s";
}

}  // namespace game_lang
