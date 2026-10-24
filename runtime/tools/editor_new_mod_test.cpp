// World editor: new_mod (runtime/src/editor/commands.h) through the command layer. A new, empty mod gets
// a package folder of its own in the mods folder (the fake host does what the game's mod manager does:
// writes it with write_package_folder, refuses an existing id), is registered with the session and opened;
// ids are checked, duplicates refused, it saves and exports like any package, and nothing outside its
// folder changes.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "editor/commands.h"

namespace fs = std::filesystem;
using editor::Value;

namespace {
int g_failures = 0;
#define CHECK(x)                                                                    \
    do {                                                                            \
        if (!(x)) {                                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

Value json(const char* text) { return mods::json::parse(text); }
bool ok(const Value& r) { return r.get("ok").boolean; }
std::string error_of(const Value& r) { return r.get("error").string(); }

std::string file_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
// every file under `root` with its bytes
std::map<std::string, std::string> tree(const fs::path& root) {
    std::map<std::string, std::string> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file()) out[it->path().lexically_relative(root).generic_string()] = file_text(it->path());
        else if (it->is_directory()) out[it->path().lexically_relative(root).generic_string() + "/"] = "";
    return out;
}

struct FakeHost : editor::Host {
    std::string mods_dir;
    bool refuse = false;
    int created = 0;
    Value last_manifest;
    std::vector<std::string> applied_mods;
    bool apply_actors(const std::string& mod, const std::vector<story_notebook::ManifestActor>&, const std::vector<std::string>&, std::string& note,
                      std::string&) override {
        applied_mods.push_back(mod);
        note = "";
        return true;
    }
    bool reload_room(std::string&) override { return true; }
    std::vector<std::pair<std::string, std::vector<int>>> stage_rooms() override { return {{"sea", {11}}}; }
    bool warp(const editor::Location&, std::string&) override { return true; }
    editor::Place link() override {
        editor::Place p;
        p.valid = true;
        p.stage = "sea";
        p.room = 11;
        p.position = {0, 1040, -201500};
        return p;
    }
    bool busy(std::string&) override { return false; }
    bool fly(bool, bool, std::string&) override { return true; }
    Value fly_state() override { return Value(); }
    bool set_test_mode(bool, std::string&) override { return true; }
    bool test_mode() override { return false; }
    bool reset_records(const std::string&, const std::vector<editor::Record>&, const std::vector<editor::Record>&, unsigned& erased,
                       std::string&) override {
        erased = 0;
        return true;
    }
    bool test_state(bool, int, const std::string&, const std::vector<editor::Record>&, std::string&) override { return true; }
    bool ground(const editor::WorldPoint&, float& h, std::string&) override { return h = 1040, true; }
    // as the game's mod manager: a new folder in the mods folder, or a refusal
    bool create_package(const Value& manifest, std::string& dir, std::string& why) override {
        if (refuse) return why = "the mod manager refused the new mod: test", false;
        last_manifest = manifest;
        if (!editor::write_package_folder(mods_dir, manifest, dir, why)) return false;
        ++created;
        return true;
    }
};

const char* kOther = R"({
  "format_version": 1, "id": "other", "name": "Someone else's mod", "version": "1.0.0", "game_id": "wwhd", "kind": "content",
  "dependencies": [{"id": "story-notebook", "minimum_version": "0.0.0"}],
  "actors": [{"id": "sign-a", "actor": "sign", "stage": "sea", "room": 11, "position": {"x": 10, "y": 1040, "z": -201500},
              "flag": "sign-a-read", "lines": ["Hello."]}]
})";

void ids_and_names() {
    Value m;
    std::string why;
    CHECK(editor::package_id_valid("my-mod") && editor::package_id_valid("a.b_c-9") && editor::package_id_valid(std::string(64, 'a')));
    for (const char* bad : {"", "My-Mod", ".hidden", "a/b", "a b", "a\\b", "..", "mod!", "\xC3\xA9t\xC3\xA9"}) CHECK(!editor::package_id_valid(bad));
    CHECK(!editor::package_id_valid(std::string(65, 'a')));
    CHECK(!editor::new_package_manifest(json(R"({"id": "story-notebook", "name": "x"})"), m, why) && why.find("Story Notebook") != std::string::npos);
    CHECK(!editor::new_package_manifest(json(R"({"id": "ok", "name": "   "})"), m, why) && why.find("name") != std::string::npos);
    Value long_name;
    long_name["id"] = "ok";
    long_name["name"] = std::string(129, 'n');
    CHECK(!editor::new_package_manifest(long_name, m, why));
    long_name["name"] = "fine";
    long_name["author"] = std::string(257, 'a');
    CHECK(!editor::new_package_manifest(long_name, m, why));
    // the manifest: the current format, a content package with an empty actor list and the notebook dependency
    CHECK(editor::new_package_manifest(json(R"({"id": "my-mod", "name": "My mod", "author": "Me"})"), m, why));
    CHECK(m.get("format_version").number == 1 && m.get("id").string() == "my-mod" && m.get("name").string() == "My mod");
    CHECK(m.get("author").string() == "Me" && m.get("description").type == Value::Null && m.get("version").string() == "0.1.0");
    CHECK(m.get("kind").string() == "content" && m.get("game_id").string() == "wwhd" && m.get("content_dir").type == Value::Null);
    CHECK(m.get("actors").type == Value::Array && m.get("actors").array.empty());
    CHECK(m.get("dependencies").array.size() == 1 && m.get("dependencies").array[0].get("id").string() == "story-notebook");
    CHECK(m.get("experimental").type == Value::Null);
    CHECK(editor::validate_package("my-mod", m, {}, why));
}

void folders(const fs::path& root) {
    std::string dir, why;
    Value m;
    CHECK(editor::new_package_manifest(json(R"({"id": "folder-test", "name": "Folder test"})"), m, why));
    CHECK(!editor::write_package_folder((root / "missing").string(), m, dir, why) && !fs::exists(root / "missing"));
    CHECK(!editor::write_package_folder("", m, dir, why));
    fs::create_directories(root / "f");
    CHECK(editor::write_package_folder((root / "f").string(), m, dir, why));
    CHECK(fs::path(dir) == root / "f" / "folder-test" && fs::is_regular_file(root / "f" / "folder-test" / "manifest.json"));
    CHECK(mods::json::parse(file_text(root / "f" / "folder-test" / "manifest.json")) == m);
    CHECK(tree(root / "f" / "folder-test").size() == 1);  // the manifest only, no temporary file left
    const auto before = tree(root / "f");
    CHECK(!editor::write_package_folder((root / "f").string(), m, dir, why) && why.find("exists") != std::string::npos);
    CHECK(tree(root / "f") == before);
    Value no_id = m;
    no_id["id"] = "../escape";
    CHECK(!editor::write_package_folder((root / "f").string(), no_id, dir, why) && !fs::exists(root / "escape"));
}

void command_layer(const fs::path& root) {
    const fs::path mods = root / "Mods";
    fs::create_directories(mods / "other");
    { std::ofstream(mods / "other" / "manifest.json", std::ios::binary) << kOther; }
    { std::ofstream(mods / "other" / "readme.txt", std::ios::binary) << "keep me"; }
    fs::create_directories(mods / "on-disk");  // a folder the session doesn't know (a disabled package, say)
    { std::ofstream(mods / "on-disk" / "manifest.json", std::ios::binary) << "{}"; }
    { std::ofstream(root / "profiles.json", std::ios::binary) << R"({"active": "Default"})"; }
    const auto before = tree(root);

    FakeHost host;
    host.mods_dir = mods.string();
    editor::Session s(&host);
    s.add_package("other", "Someone else's mod", (mods / "other").string(), json(kOther));
    CHECK(s.open_mod() == "other");
    const Value other_manifest = *s.manifest("other");

    // needs confirm; refused ids and names change nothing
    Value r = s.run("new_mod", json(R"({"id": "my-mod", "name": "My mod"})"));
    CHECK(!ok(r) && error_of(r).find("confirm") != std::string::npos);
    for (const char* bad : {R"({"id": "My Mod", "name": "x", "confirm": true})", R"({"id": ".x", "name": "x", "confirm": true})",
                            R"({"id": "../x", "name": "x", "confirm": true})", R"({"id": "", "name": "x", "confirm": true})",
                            R"({"id": "x", "name": "", "confirm": true})", R"({"id": "x", "confirm": true})",
                            R"({"id": "x", "name": "x", "confirm": true, "kind": "guest"})", R"({"id": 5, "name": "x", "confirm": true})"})
        CHECK(!ok(s.run("new_mod", json(bad))));
    // duplicates: a mod the session has, and a folder in the mods folder
    r = s.run("new_mod", json(R"({"id": "other", "name": "x", "confirm": true})"));
    CHECK(!ok(r) && error_of(r).find("exists") != std::string::npos);
    r = s.run("new_mod", json(R"({"id": "on-disk", "name": "x", "confirm": true})"));
    CHECK(!ok(r) && error_of(r).find("exists") != std::string::npos);
    // the host's refusal: nothing registered
    host.refuse = true;
    r = s.run("new_mod", json(R"({"id": "my-mod", "name": "My mod", "confirm": true})"));
    CHECK(!ok(r) && error_of(r).find("refused") != std::string::npos);
    host.refuse = false;
    CHECK(tree(root) == before && host.created == 0 && s.mods().size() == 1 && s.open_mod() == "other" && s.undo_depth() == 0);
    // without the game
    editor::Session no_game;
    CHECK(!ok(no_game.run("new_mod", json(R"({"id": "my-mod", "name": "My mod", "confirm": true})"))) && no_game.mods().empty());

    // create: registered, opened, its folder written; not an undo step
    r = s.run("new_mod", json(R"({"id": "my-mod", "name": "My mod", "author": "Me", "description": "A test.", "confirm": true})"));
    CHECK(ok(r));
    CHECK(r.get("result").get("mod").string() == "my-mod" && r.get("result").get("open").boolean);
    CHECK(host.created == 1 && s.open_mod() == "my-mod" && s.package_dir("my-mod") == (mods / "my-mod").string());
    CHECK(s.undo_depth() == 0 && !s.dirty("my-mod"));
    CHECK(mods::json::parse(file_text(mods / "my-mod" / "manifest.json")) == *s.manifest("my-mod"));
    CHECK(s.manifest("my-mod")->get("description").string() == "A test.");
    Value list = s.run("list_mods").get("result");
    CHECK(list.array.size() == 2 && list.array[1].get("id").string() == "my-mod" && list.array[1].get("open").boolean &&
          list.array[1].get("actors").number == 0);
    CHECK(ok(s.run("validate")) && s.run("validate").get("result").get("valid").boolean);
    CHECK(ok(s.run("check_package")) && s.run("check_package").get("result").get("problems").array.empty());
    // the same id again
    r = s.run("new_mod", json(R"({"id": "my-mod", "name": "Again", "confirm": true})"));
    CHECK(!ok(r) && host.created == 1);
    // nothing outside the new folder changed
    auto after = tree(root);
    for (const auto& [path, bytes] : before) CHECK(after.count(path) && after.at(path) == bytes);
    for (const auto& [path, bytes] : after) CHECK(before.count(path) || path.rfind("Mods/my-mod/", 0) == 0);
    CHECK(*s.manifest("other") == other_manifest);

    // building in it: an actor (a live update for the new mod), save, export
    r = s.run("add_actor", json(R"({"type": "resident", "id": "greeter", "at": "position", "position": {"x": 0, "y": 1040, "z": -201000},
                                    "stage": "sea", "room": 11, "snap": false, "options": {"resident": "uo2", "lines": ["Welcome!"]}})"));
    CHECK(ok(r));
    CHECK(!host.applied_mods.empty() && host.applied_mods.back() == "my-mod");
    CHECK(s.dirty("my-mod") && s.undo_depth() == 1 && s.manifest("my-mod")->get("actors").array.size() == 1);
    CHECK(!ok(s.run("save_package")));
    r = s.run("save_package", json(R"({"confirm": true})"));
    CHECK(ok(r) && !s.dirty("my-mod"));
    const Value saved = mods::json::parse(file_text(mods / "my-mod" / "manifest.json"));
    CHECK(saved == *s.manifest("my-mod") && saved.get("actors").array[0].get("id").string() == "greeter");
    CHECK(saved.get("actors").array[0].get("actor").string() == "resident");
    r = s.run("export_package", json((R"({"confirm": true, "path": ")" + (root / "export" / "my-mod").generic_string() + R"("})").c_str()));
    CHECK(ok(r));
    CHECK(mods::json::parse(file_text(root / "export" / "my-mod" / "manifest.json")) == saved);
    after = tree(root);
    for (const auto& [path, bytes] : before) CHECK(after.count(path) && after.at(path) == bytes);
    for (const auto& [path, bytes] : after)
        CHECK(before.count(path) || path.rfind("Mods/my-mod/", 0) == 0 || path.rfind("export/", 0) == 0 || path == "export/");
    // undo takes back the actor, never the mod
    CHECK(ok(s.run("undo")) && s.manifest("my-mod")->get("actors").array.empty() && s.mods().size() == 2);
    CHECK(!ok(s.run("undo")));
    // the other mod opens again as before
    CHECK(ok(s.run("open_mod", json(R"({"mod": "other"})"))) && s.open_mod() == "other");
    CHECK(*s.manifest("other") == other_manifest);
}

// a small ground model: a GLB with only its JSON chunk (what the editor reads of it: bounds, UVs, materials)
void write_glb(const fs::path& file) {
    std::string j = R"({"asset": {"version": "2.0"}, "materials": [{"name": "floor"}, {"name": "wall"}],
        "accessors": [{"type": "VEC3", "componentType": 5126, "count": 3, "min": [-500, 0, -500], "max": [500, 400, 500]},
                      {"type": "VEC2", "componentType": 5126, "count": 3}],
        "meshes": [{"name": "room", "primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1}, "material": 0},
                                                   {"attributes": {"POSITION": 0, "TEXCOORD_0": 1}, "material": 1}]}],
        "nodes": [{"name": "room", "mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0})";
    while (j.size() % 4) j += ' ';
    std::string b = "glTF";
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) b += char(v >> (8 * i) & 0xFF); };
    u32(2);
    u32(uint32_t(20 + j.size()));
    u32(uint32_t(j.size()));
    b += "JSON";
    b += j;
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << b;
}

// a host whose Link can stand in a built mod stage
struct StagesHost : FakeHost {
    editor::Place where;
    std::vector<ModStage> built;
    editor::Place link() override { return where; }
    std::vector<ModStage> mod_stages() override { return built; }
};

// the stage commands: a room added before its door is kept (and listed by Check and refused by export until a
// door leads to it); place_in_stage where Link stands and at a position of the stage
void stage_commands(const fs::path& root) {
    const fs::path mods = root / "Mods";
    fs::create_directories(mods);
    StagesHost host;
    host.mods_dir = mods.string();
    host.where.valid = true;
    host.where.stage = "sea", host.where.room = 34, host.where.position = {0, 0, 0};
    editor::Session s(&host);
    Value r = s.run("new_mod", json(R"({"id": "dun", "name": "Dungeon", "stages": true, "confirm": true})"));
    CHECK(ok(r) && s.open_mod() == "dun");
    CHECK(s.manifest("dun")->get("dependencies").array.empty());
    write_glb(mods / "dun" / "assets" / "room.glb");
    CHECK(ok(s.run("add_stage", json(R"({"id": "keep", "ground": "assets/room.glb"})"))));
    r = s.run("add_room", json(R"({"stage": "keep", "ground": "assets/room.glb"})"));
    CHECK(ok(r));
    CHECK(s.manifest("dun")->get("stages").array[0].get("rooms").array.size() == 2);
    // "kit" is a room number or {stage, room} (a parameter of two types); other values are refused
    CHECK(ok(s.run("add_room", json(R"({"stage": "keep", "ground": "assets/room.glb", "kit": {"stage": "Hyroom", "room": 0}})"))));
    CHECK(s.manifest("dun")->get("stages").array[0].get("rooms").array[2].get("kit").get("stage").string() == "Hyroom");
    CHECK(ok(s.run("add_room", json(R"({"stage": "keep", "ground": "assets/room.glb", "kit": 0})"))));
    CHECK(!ok(s.run("add_room", json(R"({"stage": "keep", "ground": "assets/room.glb", "kit": "Hyroom"})"))));
    CHECK(ok(s.run("undo")) && ok(s.run("undo")));
    CHECK(s.manifest("dun")->get("stages").array[0].get("rooms").array.size() == 2);
    // Check lists the room nothing leads to; export refuses it; saving keeps the work
    auto reach_problem = [&] {
        const Value checked = s.run("check_package");
        for (const auto& pr : checked.get("result").get("problems").array)
            if (pr.get("message").string().find("room 1 can't be reached") != std::string::npos) return true;
        return false;
    };
    CHECK(reach_problem());
    r = s.run("export_package", json((R"({"confirm": true, "path": ")" + (root / "export").generic_string() + R"("})").c_str()));
    CHECK(!ok(r) && error_of(r).find("can't be reached") != std::string::npos && !fs::exists(root / "export"));
    CHECK(ok(s.run("save_package", json(R"({"confirm": true})"))));
    // outside the stage: refused, with the way in
    r = s.run("place_in_stage", json(R"({"stage": "keep", "what": "chest", "item": "small_key"})"));
    CHECK(!ok(r) && error_of(r).find("go into stage keep") != std::string::npos);
    // in it (room 0, facing +z): a chest 1.5 m ahead, an enemy 3 m ahead, then the key door to room 1
    host.built = {{"dun", "keep", "Zkeep00", {0, 1}}};
    host.where.stage = "Zkeep00", host.where.room = 0, host.where.position = {100, 0, 200}, host.where.angle = 0;
    r = s.run("place_in_stage", json(R"({"stage": "keep", "what": "door", "to_room": 1, "lock": "small_key"})"));
    CHECK(!ok(r) && error_of(r).find("small key") != std::string::npos);  // no key in the stage yet
    r = s.run("place_in_stage", json(R"({"stage": "keep", "what": "chest", "item": "small_key"})"));
    CHECK(ok(r));
    const Value& chest = r.get("result").get("added");
    CHECK(chest.get("position").get("x").number == 100 && chest.get("position").get("z").number == 350 && chest.get("angle").number == 180);
    r = s.run("place_in_stage", json(R"({"stage": "keep", "what": "enemy", "actor": "bokoblin"})"));
    CHECK(ok(r) && r.get("result").get("added").get("position").get("z").number == 500);
    r = s.run("place_in_stage", json(R"({"stage": "keep", "what": "door", "to_room": 1, "lock": "small_key"})"));
    CHECK(ok(r));
    CHECK(!reach_problem());
    const Value check = s.run("check_stage", json(R"({"stage": "keep"})")).get("result");
    CHECK(check.get("errors").number == 0);
    // the game's lava floor at Link's feet, turned his way, with its size (default 10 m square), and the heat haze;
    // its size changes with set_stage_field (the picture's corner handle and the Stages panel use it); one undo step each
    host.where.angle = 0x4000;  // facing +x
    r = s.run("place_in_stage", json(R"({"stage": "keep", "what": "lava", "size": {"x": 2000, "z": 1200}})"));
    CHECK(ok(r));
    {
        const Value& lava = r.get("result").get("added");
        CHECK(lava.get("actor").string() == "lava" && lava.get("id").string() == "lava" && lava.get("size").get("x").number == 2000 &&
              lava.get("size").get("z").number == 1200 && lava.get("angle").number == 90 && lava.get("position").get("x").number == 100 &&
              lava.get("position").get("z").number == 200);
    }
    r = s.run("place_in_stage", json(R"({"stage": "keep", "what": "lava"})"));
    CHECK(ok(r) && r.get("result").get("added").get("id").string() != "lava" && r.get("result").get("added").get("size").get("x").number == 1000);
    CHECK(ok(s.run("undo")));
    r = s.run("place_in_stage", json(R"({"stage": "keep", "what": "lava", "size": {"x": 50, "z": 1200}})"));
    CHECK(!ok(r) && error_of(r).find("lava's size") != std::string::npos);
    r = s.run("place_in_stage", json(R"({"stage": "keep", "what": "heat_shimmer"})"));
    CHECK(ok(r) && r.get("result").get("added").get("actor").string() == "heat_shimmer" && r.get("result").get("added").get("size").type == Value::Null);
    {
        const auto& acts = s.manifest("dun")->get("stages").array[0].get("rooms").array[0].get("actors").array;
        CHECK(acts.size() == 4 && acts[2].get("actor").string() == "lava" && acts[3].get("actor").string() == "heat_shimmer");
    }
    r = s.run("set_stage_field", json(R"({"stage": "keep", "field": "rooms/0/actors/2/size", "value": {"x": 3000, "z": 800}})"));
    CHECK(ok(r) && s.manifest("dun")->get("stages").array[0].get("rooms").array[0].get("actors").array[2].get("size").get("x").number == 3000);
    // a size the stage can't have is refused (the manifest's own check)
    r = s.run("set_stage_field", json(R"({"stage": "keep", "field": "rooms/0/actors/2/size", "value": {"x": 30000, "z": 800}})"));
    CHECK(!ok(r) && s.manifest("dun")->get("stages").array[0].get("rooms").array[0].get("actors").array[2].get("size").get("x").number == 3000);
    CHECK(ok(s.run("undo")) && s.manifest("dun")->get("stages").array[0].get("rooms").array[0].get("actors").array[2].get("size").get("x").number == 2000);
    CHECK(ok(s.run("redo")) && s.manifest("dun")->get("stages").array[0].get("rooms").array[0].get("actors").array[2].get("size").get("x").number == 3000);
    host.where.angle = 0;
    // at = position, in a room of the stage while Link is elsewhere
    host.where.stage = "sea", host.where.room = 34;
    r = s.run("place_in_stage", json(R"({"stage": "keep", "what": "pot", "at": "position", "room": 1, "position": {"x": 10, "y": 0, "z": 10}})"));
    CHECK(ok(r));
    const Value& keep = s.manifest("dun")->get("stages").array[0];
    CHECK(keep.get("rooms").array[1].get("actors").array.size() == 1 && keep.get("rooms").array[0].get("actors").array.size() == 4);
    CHECK(keep.get("doors").array.size() == 1);
    // one undo step each
    CHECK(ok(s.run("undo")) && s.manifest("dun")->get("stages").array[0].get("rooms").array[1].get("actors").array.empty());
    // an updraft entry where Link stands in a game room (a column of wind), and a warp that waits for a chest
    r = s.run("place_in_stage", json(R"({"stage": "keep", "what": "entry", "style": "updraft", "height": 1200})"));
    CHECK(ok(r) && r.get("result").get("added").get("style").string() == "updraft" && r.get("result").get("added").get("size").get("y").number == 800);
    r = s.run("place_in_stage", json(R"({"stage": "keep", "what": "warp", "at": "position", "room": 0, "position": {"x": 0, "y": 0, "z": 0}, "after": "nothing"})"));
    CHECK(!ok(r) && error_of(r).find("names a chest") != std::string::npos);
}
}  // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / ("wwhd-editor-new-mod-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    ids_and_names();
    folders(root / "folders");
    command_layer(root / "manager");
    stage_commands(root / "stages");
    std::error_code ec;
    fs::remove_all(root, ec);  // this test's own temporary folder
    if (g_failures) {
        std::fprintf(stderr, "editor_new_mod_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("editor_new_mod_test: ok");
    return 0;
}
