// SPDX-License-Identifier: MPL-2.0
// wwhd-rando: TWWHD Randomizer 1.2.0 (tools/rando/twwhd_rando, MIT) as a command for the port's setup.
// Adapted from GreenNaugahyde/ZeldaWWHDRecompAndroid (MPL-2.0) runtime/src/rando/rando_jni.cpp at db65d60:
// the same calls, as one command per process (the randomizer keeps global state, and a run needs about 1 GB).
//
//   wwhd-rando --data DIR --work DIR COMMAND [ARGS]
//
// DIR --data: the randomizer's data (asm/, asm_eur/, asm_jpn/, logic/, customizer/; built by tools/rando/CMakeLists.txt).
// DIR --work: config.yaml + preferences.yaml (the randomizer's own files), logs/, progress, last.json.
// Every command prints one JSON object on stdout ({"ok":true,...} or {"ok":false,"error":"..."}) and exits 0 or 1;
// the randomizer's log lines go to stderr.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <command/Log.hpp>
#include <libs/yaml.hpp>
#include <options.hpp>
#include <randomizer.hpp>
#include <seedgen/config.hpp>
#include <seedgen/seed.hpp>
#include <utility/path.hpp>
#include <utility/wwhd_region.hpp>
#include <version.hpp>

namespace {

std::string json_str(const std::string& s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof b, "\\u%04x", c);
                    r += b;
                } else r += (char)c;
        }
    }
    return r + "\"";
}

struct Json {
    std::vector<std::pair<std::string, std::string>> fields;  // key, encoded value
    Json& s(const std::string& k, const std::string& v) { fields.emplace_back(k, json_str(v)); return *this; }
    Json& raw(const std::string& k, const std::string& v) { fields.emplace_back(k, v); return *this; }
    Json& i(const std::string& k, long long v) { fields.emplace_back(k, std::to_string(v)); return *this; }
    Json& b(const std::string& k, bool v) { fields.emplace_back(k, v ? "true" : "false"); return *this; }
    std::string str() const {
        std::string r = "{";
        for (size_t n = 0; n < fields.size(); n++) r += (n ? "," : "") + json_str(fields[n].first) + ":" + fields[n].second;
        return r + "}";
    }
};

int ok(Json j = {}) {
    Json r;
    r.b("ok", true);
    for (auto& f : j.fields) r.fields.push_back(f);
    printf("%s\n", r.str().c_str());
    return 0;
}

int fail(const std::string& why) {
    printf("%s\n", Json().b("ok", false).s("error", why).str().c_str());
    return 1;
}

fspath g_progress;

void write_progress(int percent, const std::string& label) {
    if (g_progress.empty()) return;
    fspath tmp = g_progress;
    tmp += ".tmp";
    if (FILE* f = fopen(tmp.string().c_str(), "w")) {
        fprintf(f, "%d\t%s\n", percent, label.c_str());
        fclose(f);
        std::error_code ec;
        std::filesystem::rename(tmp, g_progress, ec);
    }
}

int g_percent = 0;

fspath config_path() { return wwhd_rando::save_path / "config.yaml"; }
fspath prefs_path() { return wwhd_rando::save_path / "preferences.yaml"; }

bool load(Config& c, std::string& err) {
    ConfigError e = Config::writeDefault(config_path(), prefs_path());
    if (e != ConfigError::NONE) return err = "cannot write the default settings: " + ConfigErrorGetName(e), false;
    e = c.loadFromFile(config_path(), prefs_path(), true);
    if (e != ConfigError::NONE && e != ConfigError::DIFFERENT_RANDO_VERSION)
        return err = "cannot read the settings: " + ConfigErrorGetName(e), false;
    return true;
}

bool save(const Config& c, std::string& err) {
    ConfigError e = c.writeToFile(config_path(), prefs_path());
    if (e != ConfigError::NONE) return err = "cannot write the settings: " + ConfigErrorGetName(e), false;
    return true;
}

// Settings that are plain value indices (the others: lists, files, internal state)
bool plain_setting(Option o) {
    switch (o) {
        case Option::INVALID:
        case Option::GameVersion:
        case Option::StartingGear:
        case Option::ExcludedLocations:
        case Option::Plandomizer:
        case Option::PlandomizerFile:
        case Option::GohmaRequired:
        case Option::KalleDemosRequired:
        case Option::GohdanRequired:
        case Option::HelmarocKingRequired:
        case Option::JalhallaRequired:
        case Option::MolgeraRequired:
        case Option::COUNT:
            return false;
        default:
            return true;
    }
}

// How a setting's value index is chosen, as the PC randomizer's window offers it (gui/desktop/mainwindow.cpp ranges)
struct Kind {
    const char* kind;  // bool | count | choice
    int lo = 0, hi = 1;
    std::vector<const char*> choices;
};

Kind kind_of(Option o) {
    auto count = [](int lo, int hi) { return Kind{"count", lo, hi, {}}; };
    auto choice = [](std::vector<const char*> c) { return Kind{"choice", 0, int(c.size()) - 1, std::move(c)}; };
    switch (o) {
        case Option::ProgressDungeons: return choice({"Off", "Standard", "Race mode (required dungeons only)"});
        case Option::DungeonSmallKeys:
        case Option::DungeonBigKeys:
        case Option::DungeonMapsAndCompasses: return choice({"Vanilla", "Own dungeon", "Any dungeon", "Overworld", "Keysanity"});
        case Option::RandomizeCaveEntrances: return choice({"Off", "Caves", "Caves and fairy fountains"});
        case Option::PigColor: return choice({"Black", "Pink", "Spotted", "Random"});
        case Option::TargetType: return choice({"Hold", "Switch"});
        case Option::Camera: return choice({"Standard", "Reverse left/right"});
        case Option::FirstPersonCamera: return choice({"Standard", "Reverse up/down"});
        case Option::Gyroscope: return choice({"Off", "On"});
        case Option::UIDisplay: return choice({"On", "Off"});
        case Option::NumRequiredDungeons: return count(0, MAXIMUM_NUM_DUNGEONS);
        case Option::DamageMultiplier: return count(2, int(MAXIMUM_DAMAGE_MULTIPLIER));
        case Option::PathHints: return count(0, MAXIMUM_PATH_HINT_COUNT);
        case Option::BarrenHints: return count(0, MAXIMUM_BARREN_HINT_COUNT);
        case Option::ItemHints: return count(0, MAXIMUM_ITEM_HINT_COUNT);
        case Option::LocationHints: return count(0, MAXIMUM_LOCATION_HINT_COUNT);
        case Option::StartingHC: return count(1, MAXIMUM_STARTING_HC);
        case Option::StartingHP: return count(0, MAXIMUM_STARTING_HP);
        case Option::StartingJoyPendants: return count(0, MAXIMUM_STARTING_JOY_PENDANTS);
        case Option::StartingSkullNecklaces: return count(0, MAXIMUM_STARTING_SKULL_NECKLACES);
        case Option::StartingBokoBabaSeeds: return count(0, MAXIMUM_STARTING_BOKO_BABA_SEEDS);
        case Option::StartingGoldenFeathers: return count(0, MAXIMUM_STARTING_GOLDEN_FEATHERS);
        case Option::StartingKnightsCrests: return count(0, MAXIMUM_STARTING_KNIGHTS_CRESTS);
        case Option::StartingRedChuJellys: return count(0, MAXIMUM_STARTING_RED_CHU_JELLYS);
        case Option::StartingGreenChuJellys: return count(0, MAXIMUM_STARTING_GREEN_CHU_JELLYS);
        case Option::StartingBlueChuJellys: return count(0, MAXIMUM_STARTING_BLUE_CHU_JELLYS);
        default: return Kind{"bool", 0, 1, {}};
    }
}

// The official randomizer builds add a secret key to race-mode (no spoiler log) seeds; no other build can make the same game.
const char* kRaceMode =
    "Race-mode settings (no spoiler log) are not supported: the official TWWHD Randomizer adds a secret key to those "
    "seeds, so this build would make a different game from the same permalink. Use the official randomizer and import "
    "its output folder in setup instead.";

// Presets: the randomizer's defaults, then a few changes (as GreenNaugahyde's app offers them)
bool apply_preset(Config& c, const std::string& name) {
    auto set = [&](const char* n, size_t v) { c.settings.setSetting(nameToSetting(n), v); };
    std::string seed = c.seed;
    c.resetDefaultSettings();
    c.seed = seed;
    if (name == "standard") return true;
    if (name == "beginner") {
        set("Progress Puzzle Caves", 0);
        set("Progress Exp Purchases", 0);
        set("Korl Hints", 1);
        set("Clearer Hints", 1);
        set("Path Hints", 3);
        set("Barren Hints", 2);
        set("Item Hints", 3);
        return true;
    }
    if (name == "everything") {
        static const char* const progress[] = {"Progress Great Fairies", "Progress Puzzle Caves", "Progress Combat Caves",
            "Progress Short Sidequests", "Progress Long Sidequests", "Progress Spoils Trading", "Progress Minigames",
            "Progress Free Gifts", "Progress Mail", "Progress Platforms Rafts", "Progress Submarines", "Progress Eye Reefs",
            "Progress Octos Gunboats", "Progress Triforce Charts", "Progress Treasure Charts", "Progress Exp Purchases",
            "Progress Misc", "Progress Tingle Chests", "Progress Savage Labyrinth", "Progress Island Puzzles",
            "Progress Dungeon Secrets"};
        for (const char* p : progress) set(p, 1);
        set("Korl Hints", 1);
        set("Path Hints", 4);
        set("Barren Hints", 3);
        return true;
    }
    return false;
}

std::string read_title_id(const fspath& game) {
    std::ifstream f(game / "meta" / "meta.xml");
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t a = s.find("<title_id");
    if (a == std::string::npos) return "";
    a = s.find('>', a);
    size_t b = s.find('<', a);
    if (a == std::string::npos || b == std::string::npos) return "";
    std::string id = s.substr(a + 1, b - a - 1);
    for (char& ch : id) ch = (char)toupper((unsigned char)ch);
    return id;
}

std::string spoiler_log() {
    std::error_code ec;
    fspath best;
    std::filesystem::file_time_type t{};
    for (auto& e : std::filesystem::directory_iterator(Utility::get_logs_path(), ec)) {
        std::string n = e.path().filename().string();
        if (n.size() > 15 && n.ends_with("Spoiler Log.txt") && !n.ends_with("Non-Spoiler Log.txt")) {
            auto w = e.last_write_time(ec);
            if (best.empty() || w > t) best = e.path(), t = w;
        }
    }
    return best.empty() ? "" : best.string();
}

int usage() {
    fprintf(stderr,
            "usage: wwhd-rando --data DIR --work DIR COMMAND\n"
            "  version | locations | settings | get NAME | set NAME INDEX | seed | set-seed TEXT | new-seed | defaults\n"
            "  preset beginner|standard|everything | permalink | load-permalink TEXT\n"
            "  generate --game GAME_DIR --out OUT_DIR [--allow-eur]\n");
    return 2;
}

}  // namespace

namespace wwhd_rando {
void progress_value(int percent) { g_percent = percent; }
void progress_label(const std::string& label) {
    fprintf(stderr, "[rando] %s\n", label.c_str());
    write_progress(g_percent, label);
}
}  // namespace wwhd_rando

int main(int argc, char** argv) {
    std::string data, work;
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) data = argv[++i];
        else if (a == "--work" && i + 1 < argc) work = argv[++i];
        else args.push_back(a);
    }
    if (args.empty()) return usage();
    const std::string cmd = args[0];
    if (cmd == "version") return ok(Json().s("version", RANDOMIZER_VERSION));
    // the randomizer's locations with the game files and offsets they change (logic/location_data.yaml), for the
    // spoiler map's places (places.py): no settings needed
    if (cmd == "locations" && !data.empty()) {
        YAML::Node locs;
        if (!LoadYAML(locs, fspath(data) / "logic" / "location_data.yaml", true) || !locs.IsSequence())
            return fail("cannot read " + (fspath(data) / "logic" / "location_data.yaml").string());
        std::string r = "[";
        for (size_t n = 0; n < locs.size(); n++) {
            const YAML::Node& l = locs[n];
            std::string paths = "{";
            auto add = [&](const std::string& path, const YAML::Node& offs) {
                std::string o = "[";
                if (offs && offs.IsSequence())
                    for (size_t k = 0; k < offs.size(); k++) o += (k ? "," : "") + std::to_string(offs[k].as<uint32_t>());
                paths += (paths.size() > 1 ? "," : "") + json_str(path) + ":" + o + "]";
            };
            if (const YAML::Node ps = l["Paths"]; ps && ps.IsMap())
                for (const auto& p : ps) add(p.first.as<std::string>(), p.second);
            else if (const YAML::Node p = l["Path"]; p && p.IsScalar())
                add(p.as<std::string>(), l["Offsets"] ? l["Offsets"] : YAML::Node());
            r += (n ? "," : "") + Json().s("name", l["Names"]["English"].as<std::string>()).raw("paths", paths + "}").str();
        }
        return ok(Json().raw("locations", r + "]"));
    }
    if (data.empty() || work.empty()) return usage();
    wwhd_rando::data_path = fspath(data) / "";
    wwhd_rando::save_path = fspath(work) / "";
    std::error_code ec;
    std::filesystem::create_directories(work, ec);
    if (ec) return fail("cannot make " + work + ": " + ec.message());
    srand((unsigned)time(nullptr));  // (seed names and random colours, as the randomizer's own main does; not the fill)

    Config c;
    std::string err;
    if (!load(c, err)) return fail(err);
    auto store = [&](Json j = {}) { return save(c, err) ? ok(j) : fail(err); };
    auto setting = [&](const std::string& name, Option& o) {
        o = nameToSetting(name);
        if (!plain_setting(o)) return fail("no plain setting named " + name), false;
        return true;
    };

    if (cmd == "settings") {
        std::string r = "{";
        bool first = true;
        for (int n = 1; n < (int)Option::COUNT; n++) {
            Option o = (Option)n;
            if (!plain_setting(o)) continue;
            const Kind k = kind_of(o);
            std::string choices = "[";
            for (size_t i = 0; i < k.choices.size(); i++) choices += (i ? "," : "") + json_str(k.choices[i]);
            Json d;
            d.i("value", c.settings.getSetting(o)).s("kind", k.kind).i("min", k.lo).i("max", k.hi).raw("choices", choices + "]");
            r += (first ? "" : ",") + json_str(settingToName(o)) + ":" + d.str();
            first = false;
        }
        return ok(Json().raw("settings", r + "}").s("seed", c.seed).s("permalink", c.getPermalink()));
    }
    if (cmd == "get" && args.size() == 2) {
        Option o;
        if (!setting(args[1], o)) return 1;
        return ok(Json().s("name", args[1]).i("value", c.settings.getSetting(o)));
    }
    if (cmd == "set" && args.size() == 3) {
        Option o;
        if (!setting(args[1], o)) return 1;
        char* end = nullptr;
        long v = strtol(args[2].c_str(), &end, 10);
        const Kind k = kind_of(o);
        if (!end || *end || v < k.lo || v > k.hi)
            return fail("not a value of " + args[1] + ": " + args[2] + " (" + std::to_string(k.lo) + ".." + std::to_string(k.hi) + ")");
        c.settings.setSetting(o, (size_t)v);
        return store(Json().s("name", args[1]).i("value", c.settings.getSetting(o)));
    }
    if (cmd == "seed") return ok(Json().s("seed", c.seed));
    if (cmd == "set-seed" && args.size() == 2) {
        c.seed = args[1];
        return store(Json().s("seed", c.seed));
    }
    if (cmd == "new-seed") {
        c.seed = generate_seed();
        return store(Json().s("seed", c.seed));
    }
    if (cmd == "defaults") {
        c.resetDefaultSettings();
        return store(Json().s("seed", c.seed));
    }
    if (cmd == "preset" && args.size() == 2) {
        if (!apply_preset(c, args[1])) return fail("no preset " + args[1]);
        return store(Json().s("preset", args[1]).s("permalink", c.getPermalink()));
    }
    if (cmd == "permalink") return ok(Json().s("permalink", c.getPermalink()));
    if (cmd == "load-permalink" && args.size() == 2) {
        Config t = c;
        PermalinkError e = t.loadPermalink(args[1]);
        if (e != PermalinkError::NONE) return fail("permalink: " + PermalinkErrorGetName(e));
        if (t.settings.getSetting(Option::NoSpoilerLog)) return fail(kRaceMode);
        if (t.settings.getSetting(Option::Plandomizer)) return fail("plandomizer permalinks are not supported");
        c = t;
        return store(Json().s("seed", c.seed).s("permalink", c.getPermalink()));
    }
    if (cmd == "generate") {
        fspath game, out;
        bool allowEur = false;
        for (size_t n = 1; n < args.size(); n++) {
            if (args[n] == "--game" && n + 1 < args.size()) game = args[++n];
            else if (args[n] == "--out" && n + 1 < args.size()) out = args[++n];
            else if (args[n] == "--allow-eur") allowEur = true;
            else return usage();
        }
        if (game.empty() || out.empty()) return usage();
        if (c.settings.getSetting(Option::NoSpoilerLog)) return fail(kRaceMode);
        if (c.settings.getSetting(Option::Plandomizer)) return fail("plandomizer settings are not supported");
        const std::string id = read_title_id(game);
        if (id == "0005000010143400") return fail("The Japanese release cannot be randomized yet: the port has no Japanese build.");
        (void)allowEur;  // (the European release is supported since its compatibility profile; the flag stays accepted)
        if (id != "0005000010143500" && id != "0005000010143600")
            return fail("Not a release of The Wind Waker HD the randomizer knows (title id " + id + ").");
        if (std::filesystem::exists(out, ec) && !std::filesystem::is_empty(out, ec))
            return fail("the output folder is not empty: " + out.string());
        g_progress = wwhd_rando::save_path / "progress";
        c.gameBaseDir = game;
        c.outputDir = out;
        // only the changed files go to the output (RandoSession.cpp, WWHD); meta.xml is read from there
        for (const char* d : {"code", "content", "meta"}) std::filesystem::create_directories(out / d, ec);
        std::filesystem::copy_file(game / "meta/meta.xml", out / "meta/meta.xml", std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return fail("cannot prepare " + out.string() + ": " + ec.message());
        if (!wwhd_rando::set_region(game, err)) return fail(err);
        std::filesystem::create_directories(Utility::get_logs_path(), ec);
        if (ec) return fail("cannot make the log folder: " + ec.message());
        wwhd_rando::progress_label("Randomizing...");
        if (wwhdRandomize(c) != 0) {
            std::string e = ErrorLog::getInstance().getLastErrors();
            return fail(e.empty() ? "the randomizer failed" : e);
        }
        if (wwhd_rando::port_failed())
            return fail(std::string("the game's ") + wwhd_rando::region_name() + " addresses:\n" + wwhd_rando::port_errors());
        Json j;
        j.s("version", RANDOMIZER_VERSION).s("release", wwhd_rando::region_name()).s("hash", LogInfo::getSeedHash())
            .s("permalink", c.getPermalink()).s("seed", c.seed).s("spoiler_log", spoiler_log()).s("out", out.string());
        std::ofstream(wwhd_rando::save_path / "last.json") << j.str() << "\n";
        g_percent = 100;
        write_progress(100, "Done");
        return ok(j);
    }
    return usage();
}
