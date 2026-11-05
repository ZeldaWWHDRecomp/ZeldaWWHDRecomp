// SPDX-License-Identifier: MPL-2.0
// Settings > Randomizer: the randomized games made in setup (setup --randomize, tools/installer/randomizer_seeds.py).
// What this game is (seed hash words, permalink), which game the next start plays (a game already built: the same
// selection setup makes, code-mods-active.json), and the spoiler log behind a question: the playthrough, every
// location, entrances, hints, charts and a map of the sea chart's squares with the places of the locations
// (randomizer/places.tsv, read from the player's own stage files by setup).
#include "randomizer_tab.h"

#include "../mods/code_mods.h"
#include "../mods/code_mods_io.h"
#include "../mods/mod_json.h"
#include "../randomizer_identity.h"
#include "../randomizer_spoiler.h"
#include "extensions.h"
#include "hostui.h"
#include "imgui.h"
#include "widgets.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace randomizer_tab {
namespace {
namespace fs = std::filesystem;
namespace json = mods::json;

fs::path install_dir() {
    if (const char* d = std::getenv("WWHD_INSTALL_DIR")) return d;
    // (Android: the seed's files are <setup storage>/randomizer/seeds/<id>/out, given with --randomizer-files)
    if (!randomizer::overlay_root().empty())
        return fs::path(randomizer::overlay_root()).parent_path().parent_path().parent_path().parent_path();
    return {};
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

struct Seed {
    std::string id, hash, permalink, seed, created, spoiler_log;
    bool imported = false;  // made with the official randomizer (setup's Import seed...): no permalink or spoiler log
    std::map<std::string, std::string> builds;  // "plain" / "hooks" -> code-builds fingerprint
};

struct State {
    bool loaded = false;
    std::vector<Seed> seeds;
    std::map<std::string, std::string> vanilla_builds;
    std::string message;
    // the spoiler view
    bool spoilers_shown = false, ask = false;
    rando_spoiler::Log log;
    std::map<std::string, rando_spoiler::Place> places;
    std::map<int, std::string> square_names;
    int square = 0;
} S;

void load() {
    S = State{};
    S.loaded = true;
    const fs::path reg = install_dir() / "randomizer" / "seeds.json";
    std::error_code ec;
    if (!fs::is_regular_file(reg, ec)) return;
    const auto read = mods::code::poll_build_json(reg);
    if (!read) return;
    const json::Value& v = *read;
    for (const auto& s : v.get("seeds").array) {
        Seed x;
        x.id = s.get("id").string(), x.hash = s.get("hash").string(), x.permalink = s.get("permalink").string();
        x.seed = s.get("seed").string(), x.created = s.get("created").string(), x.spoiler_log = s.get("spoiler_log").string();
        x.imported = s.get("imported").boolean;
        for (const auto& [k, b] : s.get("builds").object) x.builds[k] = b.string();
        if (x.id.size() == 16) S.seeds.push_back(x);
    }
    for (const auto& [k, b] : v.get("vanilla_builds").object) S.vanilla_builds[k] = b.string();
}

const Seed* this_seed() {
    if (!randomizer::compiled_seed()) return nullptr;
    const std::string id = std::string(g_guest_build_rpx_sha256).substr(0, 16);
    for (const auto& s : S.seeds)
        if (s.id == id) return &s;
    return nullptr;
}

// the build of a game (code_mods' code-builds/<fingerprint>) with this game's code-mod choice; "" none
std::string build_of(const std::map<std::string, std::string>& builds) {
    auto it = builds.find(mods::code::enabled() ? "hooks" : "plain");
    if (it == builds.end() || it->second.empty()) return {};
    std::error_code ec;
    const fs::path cache = install_dir() / "code-builds" / it->second.substr(0, 16);
    return fs::is_regular_file(cache / "ready.json", ec) ? it->second : std::string();
}

// the next start plays that build: the selection setup writes (code_mods.py rebuild), atomically
bool select_build(const std::string& fingerprint, std::string& why) {
    const fs::path cache = install_dir() / "code-builds" / fingerprint.substr(0, 16);
    const auto ready = mods::code::poll_build_json(cache / "ready.json");
    if (!ready || ready->get("fingerprint").string() != fingerprint)
        return why = "that build is incomplete; prepare it again in setup", false;
    const bool hooks = ready->get("hooks").boolean;
    fs::path exe;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(cache / "bin", ec))
        if (e.path().filename().string().rfind("wwhd", 0) == 0) exe = e.path();
    if (exe.empty()) return why = "that build has no game executable", false;
    const char* user = std::getenv("WWHD_USER_DIR");
    std::string text = "{\"state\": \"ready\", \"hooks\": " + std::string(hooks ? "true" : "false") + ", \"fingerprint\": " +
                       json::quote(fingerprint) + ", \"exe\": " + json::quote(fs::absolute(exe, ec).string()) +
                       ", \"user_dir\": " + json::quote(user ? user : "") + "}\n";
    const fs::path active = install_dir() / "code-mods-active.json", tmp = install_dir() / "code-mods-active.json.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << text;
        if (!f) return why = "cannot write " + tmp.string(), false;
    }
    fs::rename(tmp, active, ec);
    if (ec) return why = "cannot write " + active.string() + ": " + ec.message(), false;
    return true;
}

void load_spoilers(const Seed& s) {
    S.log = {};
    S.places.clear();
    const fs::path dir = install_dir() / "randomizer" / "seeds" / s.id;
    if (!s.spoiler_log.empty()) rando_spoiler::parse(read_file(dir / s.spoiler_log), S.log);
    S.places = rando_spoiler::parse_places(read_file(install_dir() / "randomizer" / "places.tsv"));
    S.square_names = rando_spoiler::square_names(S.places);
}

// ---- the map: the sea chart's 7 x 7 squares, a dot per location (at its spot in the square when it has one)
void draw_map() {
    const float side = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 30.0f);
    const float cell = side / 7.0f;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::map<int, int> progress, total;
    for (const auto& e : S.log.all) {
        auto it = S.places.find(e.location);
        if (it == S.places.end() || it->second.square <= 0) continue;
        total[it->second.square]++;
        if (e.sphere >= 0) progress[it->second.square]++;
    }
    for (int sq = 1; sq <= 49; sq++) {
        const int col = (sq - 1) % 7, row = (sq - 1) / 7;
        const ImVec2 a(o.x + col * cell, o.y + row * cell), b(a.x + cell, a.y + cell);
        const bool sel = S.square == sq;
        dl->AddRectFilled(a, b, sel ? IM_COL32(60, 110, 160, 255) : IM_COL32(20, 60, 100, 255));
        dl->AddRect(a, b, IM_COL32(120, 170, 210, 255));
        if (total[sq]) {
            char t[16];
            snprintf(t, sizeof t, "%d", total[sq]);
            dl->AddText(ImVec2(a.x + 4, a.y + 2), IM_COL32(230, 230, 230, 255), t);
        }
        if (progress[sq]) dl->AddCircleFilled(ImVec2(b.x - 8, a.y + 8), 5, IM_COL32(240, 200, 60, 255));
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(sq);
        if (ImGui::InvisibleButton("sq", ImVec2(cell, cell))) S.square = sq;
        if (ImGui::IsItemHovered()) {
            auto n = S.square_names.find(sq);
            ImGui::SetTooltip("%s", n != S.square_names.end() ? n->second.c_str() : "(open sea)");
        }
        ImGui::PopID();
    }
    // the locations with a spot, at their place inside the square (the sea's coordinates: 100000 per square)
    for (const auto& e : S.log.all) {
        auto it = S.places.find(e.location);
        if (it == S.places.end() || !it->second.stage.empty() || !it->second.spot()) continue;
        const float fx = (it->second.x + 350000.0f) / 700000.0f, fz = (it->second.z + 350000.0f) / 700000.0f;
        if (fx < 0 || fx > 1 || fz < 0 || fz > 1) continue;
        dl->AddCircleFilled(ImVec2(o.x + fx * side, o.y + fz * side), 2.0f,
                            e.sphere >= 0 ? IM_COL32(240, 200, 60, 255) : IM_COL32(200, 200, 200, 160));
    }
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + side + 4));
    ImGui::Dummy(ImVec2(side, 1));
    overlay::widgets::note("Numbers: the locations in a square. Yellow: something on the way to the end.");
    if (S.square > 0) {
        auto n = S.square_names.find(S.square);
        ImGui::SeparatorText(n != S.square_names.end() ? n->second.c_str() : "Open sea");
        for (const auto& e : S.log.all) {
            auto it = S.places.find(e.location);
            if (it == S.places.end() || it->second.square != S.square) continue;
            if (e.sphere >= 0) ImGui::Text("%s: %s  (step %d)", e.location.c_str(), e.item.c_str(), e.sphere + 1);
            else ImGui::Text("%s: %s", e.location.c_str(), e.item.c_str());
        }
    }
}

void draw_spoilers() {
    if (S.log.all.empty()) {
        overlay::widgets::note("This game's spoiler log is missing.");
        return;
    }
    if (ImGui::BeginTabBar("spoiler")) {
        if (!S.places.empty() && ImGui::BeginTabItem("Map")) {
            draw_map();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Way to the end")) {
            if (!S.log.starting_island.empty()) ImGui::Text("Starting island: %s", S.log.starting_island.c_str());
            for (size_t i = 0; i < S.log.spheres.size(); i++) {
                ImGui::SeparatorText(("Step " + std::to_string(i + 1)).c_str());
                for (const auto& e : S.log.spheres[i]) ImGui::BulletText("%s: %s", e.location.c_str(), e.item.c_str());
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("All locations")) {
            std::string area;
            for (const auto& e : S.log.all) {
                const std::string a = rando_spoiler::area(e.location);
                if (a != area) ImGui::SeparatorText((area = a).c_str());
                ImGui::BulletText("%s: %s", e.location.c_str(), e.item.c_str());
            }
            ImGui::EndTabItem();
        }
        if (!S.log.entrances.empty() && ImGui::BeginTabItem("Entrances")) {
            for (const auto& [a, b] : S.log.entrances) ImGui::BulletText("%s: %s", a.c_str(), b.c_str());
            ImGui::EndTabItem();
        }
        if (!S.log.hints.empty() && ImGui::BeginTabItem("Hints")) {
            for (const auto& [who, what] : S.log.hints) {
                ImGui::TextWrapped("%s: %s", who.c_str(), what.c_str());
            }
            ImGui::EndTabItem();
        }
        if (!S.log.charts.empty() && ImGui::BeginTabItem("Charts")) {
            for (const auto& [c, i] : S.log.charts) ImGui::BulletText("%s: %s", c.c_str(), i.c_str());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void draw() {
    if (!S.loaded) {
        load();
        // test aid: WWHD_TEST_RANDOMIZER_SPOILER=<square> opens this game's spoiler log on its map with that square
        // chosen (0: none), without the question
        if (const char* t = std::getenv("WWHD_TEST_RANDOMIZER_SPOILER"); t && this_seed()) {
            S.spoilers_shown = true;
            load_spoilers(*this_seed());
            S.square = std::atoi(t);
        }
    }
    const Seed* me = this_seed();
    if (me && me->imported) {
        ImGui::SeparatorText("This game");
        ImGui::Text("Imported seed %s", me->id.c_str());
        overlay::widgets::note("Made with the official TWWHD Randomizer, imported %s. Its spoiler log is in the official "
                               "randomizer's logs folder.", me->created.c_str());
    } else if (me) {
        ImGui::SeparatorText("This game");
        ImGui::Text("Randomized game %s", me->hash.c_str());
        overlay::widgets::note("Seed %s, made %s. TWWHD Randomizer 1.2.0 by SuperDude88 and contributors (MIT).", me->seed.c_str(),
                               me->created.c_str());
        ImGui::TextWrapped("Permalink: %s", me->permalink.c_str());
        if (ImGui::Button("Copy the permalink")) {
            const std::string link = me->permalink;
            hostui::post([link] { hostui::set_clipboard(link); });
        }
    } else if (randomizer::compiled_seed()) {
        ImGui::SeparatorText("This game");
        ImGui::TextWrapped("A randomized game from an imported output folder.");
    } else {
        ImGui::SeparatorText("This game");
        ImGui::TextUnformatted("The game as it is (not randomized).");
    }

    ImGui::SeparatorText("The game the next start plays");
#ifdef __ANDROID__
    // (the phone's setup screen keeps the builds and chooses: Randomized game...)
    constexpr bool switchable = false;
    overlay::widgets::note("Randomized games are made, imported and chosen in the setup screen (Randomized game...). Each "
                           "has its own saves and save states.");
#else
    constexpr bool switchable = true;
    overlay::widgets::note("Randomized games are made, imported (Import seed...) and built in setup (Randomized games). Each "
                           "has its own saves and save states.");
#endif
    if (switchable) {
    auto row = [&](const std::string& label, const std::map<std::string, std::string>& builds, bool current) {
        ImGui::PushID(label.c_str());
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine();
        if (current) {
            overlay::widgets::note("(playing now)");
        } else if (const std::string fp = build_of(builds); !fp.empty()) {
            if (ImGui::SmallButton("Play from the next start")) {
                std::string why;
                S.message = select_build(fp, why) ? label + " starts next time. Restart the game to play it." : why;
            }
        } else {
            overlay::widgets::note("(not built: prepare it in setup)");
        }
        ImGui::PopID();
    };
    row("The game as it is", S.vanilla_builds, !randomizer::compiled_seed());
    for (const auto& s : S.seeds)
        row(s.imported ? "Imported seed " + s.id : "Randomized game " + s.hash + " (" + s.id + ")", s.builds, me && me->id == s.id);
    if (!S.message.empty()) overlay::widgets::note("%s", S.message.c_str());
    }

    if (!me || me->imported) return;
    ImGui::SeparatorText("Spoiler log");
    if (!S.spoilers_shown) {
        if (ImGui::Button("Show the spoiler log...")) S.ask = true;
        if (S.ask) {
            ImGui::TextWrapped("The spoiler log shows where every item of this game is. Show it?");
            if (ImGui::Button("Show")) S.spoilers_shown = true, S.ask = false, load_spoilers(*me);
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) S.ask = false;
        }
        return;
    }
    if (ImGui::Button("Hide the spoiler log")) S.spoilers_shown = false;
    draw_spoilers();
}

}  // namespace

void register_tab() {
    overlay::ext::Tab tab{"randomizer", "Randomizer", [] { draw(); }, nullptr};
    // in a randomized game, or once setup has made one
    tab.shown = [] {
        static const bool any = [] {
            std::error_code ec;
            return randomizer::compiled_seed() || fs::is_regular_file(install_dir() / "randomizer" / "seeds.json", ec);
        }();
        return any;
    };
    overlay::ext::add_tab(std::move(tab), "about");
}

}  // namespace randomizer_tab
