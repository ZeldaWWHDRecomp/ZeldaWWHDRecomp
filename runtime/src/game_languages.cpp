// The game's languages from its language packs (game_languages.h).
#include "game_languages.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <string>

#include "runtime.h"

namespace game_lang {
namespace {

namespace fs = std::filesystem;

const char* const kNames[kLanguages] = {"Japanese", "English", "French", "German", "Italian", "Spanish",
                                        "Chinese", "Korean", "Dutch", "Portuguese", "Russian", "Chinese (Taiwan)"};
// the pack names the game knows (lower case): region prefix + language, per console language
struct Pack { const char* region; const char* label; const char* language; int code; };
const Pack kPacks[] = {
    {"jp", "Japan", "japanese", 0}, {"us", "USA", "english", 1},     {"us", "USA", "french", 2},
    {"us", "USA", "spanish", 5},    {"eu", "Europe", "english", 1}, {"eu", "Europe", "french", 2},
    {"eu", "Europe", "german", 3},  {"eu", "Europe", "italian", 4}, {"eu", "Europe", "spanish", 5},
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    return s;
}

struct Found {
    std::vector<int> languages;
    std::string region;
};

// content/Common/Pack matched without case (the disc's spelling, any host file system)
const Found& found() {
    static const Found f = [] {
        Found out;
        std::error_code ec;
        fs::path at = fs::path(config::game_dir) / "content";
        for (const char* part : {"common", "pack"}) {
            fs::path next;
            for (auto& e : fs::directory_iterator(at, ec))
                if (lower(e.path().filename().string()) == part) next = e.path();
            if (next.empty()) {
                LOG("[config] no %s: the game's languages are unknown, all are offered", (at / part).string().c_str());
                return out;
            }
            at = next;
        }
        bool have[kLanguages] = {};
        std::vector<std::string> regions;
        for (auto& e : fs::directory_iterator(at, ec)) {
            const std::string s = lower(e.path().filename().string());
            for (const Pack& p : kPacks) {
                if (s != std::string("permanent_2d_") + p.region + p.language + ".pack") continue;
                have[p.code] = true;
                if (std::find(regions.begin(), regions.end(), p.label) == regions.end()) regions.push_back(p.label);
            }
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

std::atomic<int> g_started{-1};

}  // namespace

const char* name(int language) { return language >= 0 && language < kLanguages ? kNames[language] : "?"; }
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

int started() { return g_started.load(std::memory_order_relaxed); }
void set_started(int language) { g_started.store(language, std::memory_order_relaxed); }

}  // namespace game_lang
