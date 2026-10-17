// World editor command layer (runtime/src/editor/commands.h): apply / undo / redo, refused changes,
// JSON round trips, the warp list, the test-mode record reset and saving into the package.
// Usage: editor_test <examples/guest-mods>
#include "editor/commands.h"
#include "editor/stage_files.h"
#include "mods/story_mail_book.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

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

struct FakeHost : editor::Host {
    int applied = 0, warps = 0, reloads = 0;
    std::vector<story_notebook::ManifestActor> last_actors;
    editor::Location last_warp;
    bool is_busy = false, flying = false, testing = false;
    uint8_t area[4 + 4 * 32] = {};
    std::string last_state;
    std::vector<std::string> last_changed;
    bool apply_actors(const std::string&, const std::vector<story_notebook::ManifestActor>& actors, const std::vector<std::string>& changed,
                      std::string& note, std::string&) override {
        ++applied;
        last_actors = actors;
        last_changed = changed;
        note = "";
        return true;
    }
    bool reload_room(std::string&) override { return ++reloads, true; }
    std::vector<std::pair<std::string, std::vector<int>>> stage_rooms() override { return {{"M_NewD2", {0, 1, 2}}, {"sea", {11}}}; }
    bool warp(const editor::Location& to, std::string&) override {
        ++warps;
        last_warp = to;
        return true;
    }
    editor::Place link() override {
        editor::Place p;
        p.valid = true;
        p.stage = "sea";
        p.room = 44;
        p.position = {1, 2, 3};
        p.angle = 100;
        return p;
    }
    bool busy(std::string& why) override { return is_busy && (why = "a cutscene is running", true); }
    bool fly(bool on, bool, std::string&) override { return flying = on, true; }
    Value fly_state() override { Value v; v["on"] = flying; return v; }
    bool set_test_mode(bool on, std::string&) override { return testing = on, true; }
    bool test_mode() override { return testing; }
    bool reset_records(const std::string&, const std::vector<editor::Record>& own, const std::vector<editor::Record>& shared, unsigned& erased,
                       std::string&) override {
        story_mail::Book book(area, 32);
        unsigned skipped = 0;
        erased = editor::reset_records(book, own, shared, skipped);
        return true;
    }
    bool test_state(bool save, int slot, const std::string& mod, const std::vector<editor::Record>&, std::string&) override {
        last_state = (save ? "save " : "load ") + std::to_string(slot) + " " + mod;
        return true;
    }
    // building: the ground is at y 5 everywhere near the sea square, the camera flies at (100, 500, 200)
    float ground_height = 5;
    bool has_ground = true, camera_on = false, is_playing = false, last_restore = false, last_from_camera = false;
    std::string preview, prepared;
    int focused = 0;
    bool ground(const editor::WorldPoint&, float& h, std::string& why) override {
        if (!has_ground) return why = "no ground below", false;
        h = ground_height;
        return true;
    }
    bool ray(const editor::WorldPoint& from, const editor::WorldPoint& d, float range, editor::WorldPoint& hit, std::string& why) override {
        if (d.y >= 0) return why = "the ray meets no ground", false;
        const float t = (from.y - ground_height) / -d.y;
        if (t > range) return why = "too far", false;
        hit = {from.x + d.x * t, ground_height, from.z + d.z * t};
        return true;
    }
    editor::Place camera() override {
        editor::Place p;
        p.valid = camera_on;
        p.stage = "sea";
        p.room = 44;
        p.position = {100, 500, 200};
        p.angle = 16384;
        return p;
    }
    bool focus(const editor::WorldPoint&, std::string&) override { return ++focused, true; }
    bool prepare_model(const std::string& mod, const std::string&, const Value& model, std::string& note, std::string&) override {
        prepared = mod + ":" + model.get("id").string();
        note = "prepared";
        return true;
    }
    bool preview_text(const std::string& text, bool close, std::string&) override { return preview = close ? "" : text, true; }
    bool play(bool from_camera, bool restore, std::string&) override {
        is_playing = true, last_from_camera = from_camera, last_restore = restore;
        return true;
    }
    bool stop(std::string& why) override {
        if (!is_playing) return why = "not playing", false;
        return is_playing = false, true;
    }
    bool playing() override { return is_playing; }
};

Value json(const char* text) { return mods::json::parse(text); }
Value point(double x, double y, double z) {
    Value v;
    v["x"] = x, v["y"] = y, v["z"] = z;
    return v;
}
bool ok(const Value& r) { return r.get("ok").boolean; }

const char* kManifest = R"({
  "format_version": 1, "id": "editor-test", "name": "Editor test", "version": "1.0.0", "kind": "content",
  "actors": [
    {"id": "sign-a", "actor": "sign", "stage": "sea", "room": 11, "position": {"x": 10, "y": 1040, "z": -201500},
     "flag": "sign-a-read", "lines": ["Hello."]},
    {"id": "guide", "actor": "resident", "parameters": {"resident": 9}, "stage": "sea", "room": 11,
     "position": {"x": -500, "y": 1200, "z": -201500}, "lines": "Hi.", "then": [{"action": "flag", "id": "met-guide"}]}
  ],
  "scenes": [{"id": "intro", "steps": [{"type": "wait", "frames": 30}, {"type": "end"}]}]
})";

void command_layer() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("editor-test", "Editor test", "", json(kManifest));
    CHECK(s.open_mod() == "editor-test");

    // list and get
    Value r = s.run("list_actors");
    CHECK(ok(r) && r.get("result").array.size() == 2);
    r = s.run("get_actor", json(R"({"id": "guide"})"));
    CHECK(ok(r) && r.get("result").get("actor").string() == "resident");

    // apply, undo, redo
    Value p;
    p["id"] = "sign-a";
    p["position"] = point(20, 1040, -201400);
    r = s.run("move_actor", p);
    CHECK(ok(r) && r.get("result").get("changed").boolean && r.get("result").get("live").boolean);
    CHECK(host.applied == 1 && host.last_actors[0].position.x == 20);
    CHECK(host.last_changed == std::vector<std::string>{"sign-a"});
    CHECK(s.dirty("editor-test") && s.undo_depth() == 1);
    r = s.run("undo");
    CHECK(ok(r) && host.applied == 2 && host.last_actors[0].position.x == 10);
    CHECK(!s.dirty("editor-test") && s.redo_depth() == 1);
    r = s.run("redo");
    CHECK(ok(r) && host.last_actors[0].position.x == 20 && s.undo_depth() == 1 && s.redo_depth() == 0);
    CHECK(!ok(s.run("redo")));

    // validation failures leave the manifest unchanged and add no undo step
    const Value before = *s.manifest("editor-test");
    const int applied = host.applied;
    p["position"] = point(20, 1040, 9e9);  // off the world
    r = s.run("move_actor", p);
    CHECK(!ok(r) && !r.get("error").string().empty());
    CHECK(*s.manifest("editor-test") == before && s.undo_depth() == 1 && host.applied == applied);
    r = s.run("set_dialogue", json(R"({"id": "sign-a", "lines": ["one", "two", "three", "four"]})"));
    CHECK(!ok(r) && *s.manifest("editor-test") == before);
    r = s.run("set_actor", json(R"({"id": "sign-a", "property": "flag", "value": null})"));  // signs need a flag
    CHECK(!ok(r) && *s.manifest("editor-test") == before);
    r = s.run("set_actor", json(R"({"id": "sign-a", "property": "id", "value": "renamed"})"));
    CHECK(!ok(r) && *s.manifest("editor-test") == before);
    r = s.run("scale_actor", json(R"({"id": "sign-a", "scale": {"x": 0, "y": 1, "z": 1}})"));
    CHECK(!ok(r) && *s.manifest("editor-test") == before);
    r = s.run("move_actor", json(R"({"id": "nobody", "position": {"x": 0, "y": 0, "z": 0}})"));
    CHECK(!ok(r) && *s.manifest("editor-test") == before);
    // parameter checks: unknown, missing, wrong type
    CHECK(!ok(s.run("move_actor", json(R"({"id": "sign-a"})"))));
    CHECK(!ok(s.run("move_actor", json(R"({"id": "sign-a", "position": [1, 2, 3]})"))));
    CHECK(!ok(s.run("move_actor", json(R"({"id": "sign-a", "position": {"x": 0, "y": 0, "z": 0}, "speed": 3})"))));
    CHECK(!ok(s.run("no_such_command")));
    CHECK(*s.manifest("editor-test") == before);

    // a valid property edit, then a scene edit (not live)
    r = s.run("set_dialogue", json(R"({"id": "guide", "lines": ["Welcome", "to Windfall."]})"));
    CHECK(ok(r) && host.last_actors[1].lines == "Welcome\nto Windfall.");
    r = s.run("set_scene", json(R"({"id": "intro", "property": "steps", "value": [{"type": "wait", "frames": 60}, {"type": "end"}]})"));
    CHECK(ok(r) && !r.get("result").get("live").boolean);
    r = s.run("set_scene", json(R"({"id": "intro", "property": "steps", "value": [{"type": "wait", "frames": 60}]})"));  // no end step
    CHECK(!ok(r));
    CHECK(s.undo_depth() == 3);

    // destructive commands need confirm
    CHECK(!ok(s.run("revert_package")));
    r = s.run("revert_package", json(R"({"confirm": true})"));
    CHECK(ok(r) && !s.dirty("editor-test") && host.last_actors[0].position.x == 10);
    r = s.run("undo");  // the revert is an undo step itself
    CHECK(ok(r) && s.dirty("editor-test"));

    // JSON round trip: every parameter and result goes through text and back unchanged
    for (const char* request : {R"({"command":"move_actor","params":{"id":"guide","position":{"x":-499.5,"y":1200,"z":-201500.25}}})",
                                R"({"command":"list_actors","params":{}})", R"({"command":"help","params":{"command":"warp"}})",
                                R"({"command":"history"})", R"({"command":"read_state","params":{}})"}) {
        const std::string out = s.run_json(request);
        const Value parsed = mods::json::parse(out);
        CHECK(parsed.get("ok").boolean);
        CHECK(mods::json::dump(parsed) == out);
        const Value logged = s.log().back().result;
        CHECK(mods::json::parse(mods::json::dump(logged)) == logged);
        CHECK(mods::json::parse(mods::json::dump(s.log().back().params)) == s.log().back().params);
    }
    CHECK(host.last_actors[1].position.x == -499.5f);
    CHECK(!mods::json::parse(s.run_json("not json")).get("ok").boolean);
    CHECK(!mods::json::parse(s.run_json(R"({"params":{}})")).get("ok").boolean);

    // the help lists every command, with typed parameters
    r = s.run("help");
    CHECK(ok(r) && r.get("result").array.size() == editor::command_list().size());
    for (const auto& c : editor::command_list())
        for (const auto& q : c.params) CHECK(!q.type.empty());

    // the command log keeps the last N calls
    for (int i = 0; i < 80; i++) s.run("list_mods");
    CHECK(s.log().size() == editor::Session::kLogSize);
    r = s.run("command_log", json(R"({"count": 3})"));
    CHECK(ok(r) && r.get("result").array.size() == 3 && r.get("result").array[0].get("command").string() == "list_mods");

    // warps: refused while busy, "back" returns to where Link was
    host.is_busy = true;
    CHECK(!ok(s.run("warp_to", json(R"({"location": "island:44"})"))) && host.warps == 0);
    CHECK(!ok(s.run("fly_camera", json(R"({"on": true})"))));
    host.is_busy = false;
    r = s.run("warp_to", json(R"({"location": "island:44"})"));
    CHECK(ok(r) && host.warps == 1 && host.last_warp.stage == "sea" && host.last_warp.room == 44);
    CHECK(s.last_place().valid && s.last_place().room == 44);
    r = s.run("warp_back");
    CHECK(ok(r) && host.last_warp.point == editor::kAtPosition && host.last_warp.position.z == 3 && host.last_warp.angle == 100);
    r = s.run("warp", json(R"({"stage": "M_NewD2", "room": 2})"));
    CHECK(ok(r) && host.last_warp.point == editor::kFirstSpawn);
    CHECK(!ok(s.run("warp", json(R"({"stage": "M_NewD2", "room": 99})"))));
    CHECK(!ok(s.run("warp_to", json(R"({"location": "island:99"})"))));

    // fly camera, test mode
    CHECK(ok(s.run("fly_camera", json(R"({"on": true})"))) && host.flying);
    CHECK(ok(s.run("fly_camera", json(R"({"on": false, "place_link": true})"))) && !host.flying);
    CHECK(!ok(s.run("save_state", json(R"({"slot": 1})"))));  // not in test mode
    CHECK(!ok(s.run("reset_quest", json(R"({"confirm": true})"))));
    CHECK(ok(s.run("test_mode", json(R"({"on": true})"))));
    CHECK(ok(s.run("save_state", json(R"({"slot": 2})"))) && host.last_state == "save 2 editor-test");
    CHECK(!ok(s.run("load_state", json(R"({"slot": 4})"))));
}

void locations() {
    CHECK(editor::sea_room(-800, -201500) == 11);  // Windfall
    CHECK(editor::sea_room(-200000, 300000) == 44);  // Outset
    CHECK(editor::sea_room(-349999, -349999) == 1 && editor::sea_room(349999, 349999) == 49);
    CHECK(editor::sea_room(400000, 0) == 0);
    for (int room = 1; room <= 49; room++) {
        const auto c = editor::sea_room_centre(room);
        CHECK(editor::sea_room(c.x, c.z) == room);
        CHECK(editor::island_name(room) != nullptr);
    }
    CHECK(std::string(editor::island_name(11)) == "Windfall Island" && std::string(editor::island_name(44)) == "Outset Island");

    Value m = json(kManifest);
    m["quests_format"] = 1;
    m["quests"] = json(R"([{"id": "q", "title": "Q", "steps": [{"n": 1, "text": "Go", "place": {"x": -200000, "z": 300000}},
                                                             {"n": 2, "text": "Back"}]}])");
    const auto list = editor::build_locations({{"M_NewD2", {0, 3}}, {"sea", {11}}, {"LinkRM", {0}}}, {{"editor-test", m}});
    int islands = 0, rooms = 0, quest = 0;
    for (const auto& l : list) {
        islands += l.group == "Islands";
        rooms += l.group == "Rooms";
        quest += l.group == "Quest";
    }
    CHECK(islands == 49 && rooms == 3 && quest == 3);  // two actors, one step with a place
    auto find = [&](const std::string& id) -> const editor::Location* {
        for (const auto& l : list)
            if (l.id == id) return &l;
        return nullptr;
    };
    CHECK(find("room:M_NewD2:3") && find("room:M_NewD2:3")->point == editor::kFirstSpawn);
    const auto* sign = find("actor:editor-test:sign-a");
    CHECK(sign && sign->stage == "sea" && sign->room == 11 && sign->point == editor::kAtPosition);
    CHECK(sign && std::fabs(sign->position.z - (-201500 + 200)) < 0.01);  // in front of it (rotation 0 faces +z)
    const auto* step = find("step:editor-test:q:1");
    CHECK(step && step->room == 44 && step->stage == "sea");
    CHECK(find("island:11") && find("island:11")->label.find("Windfall") != std::string::npos);
}

void records() {
    Value m = json(kManifest);
    m["quests_format"] = 1;
    m["quests"] = json(R"([{"id": "q", "title": "Q", "steps": [{"n": 1, "text": "Go"}]}])");
    const auto own = editor::mod_records("editor-test", m);
    using namespace story_mail;
    auto has = [&](uint8_t kind, uint16_t key) { return std::find(own.begin(), own.end(), editor::Record{kind, key}) != own.end(); };
    CHECK(has(kRecordActorFlag, letter_key("editor-test", "actor-flag:sign-a-read")));
    CHECK(has(kRecordActorFlag, letter_key("editor-test", "actor-flag:met-guide")));
    CHECK(has(kRecordQuest, letter_key("editor-test", "q")) && has(kRecordQuestFlags, letter_key("editor-test", "q")));

    // a story area with the mod's records, another mod's, and a key both mods' names map to
    uint8_t area[4 + 4 * 64] = {};
    Book book(area, 64);
    CHECK(book.set(kRecordActorFlag, letter_key("editor-test", "actor-flag:sign-a-read"), 1));
    CHECK(book.set(kRecordQuest, letter_key("editor-test", "q"), 2));
    CHECK(book.set(kRecordActorFlag, letter_key("other", "actor-flag:x"), 1));
    CHECK(book.set(kRecordQuest, letter_key("other", "q"), 5));
    CHECK(book.set(kRecordItem, 1, 1));  // the notebook
    const editor::Record collide{kRecordActorFlag, letter_key("editor-test", "actor-flag:met-guide")};
    CHECK(book.set(collide.kind, collide.key, 1));
    uint8_t copy[sizeof area];
    std::memcpy(copy, area, sizeof area);
    unsigned skipped = 0;
    const unsigned erased = editor::reset_records(book, own, {collide}, skipped);
    CHECK(erased == 2 && skipped == 1);
    CHECK(!book.has(kRecordActorFlag, letter_key("editor-test", "actor-flag:sign-a-read")) && !book.has(kRecordQuest, letter_key("editor-test", "q")));
    CHECK(book.get(kRecordActorFlag, letter_key("other", "actor-flag:x")) == 1 && book.get(kRecordQuest, letter_key("other", "q")) == 5);
    CHECK(book.get(kRecordItem, 1) == 1 && book.get(collide.kind, collide.key) == 1);
    // nothing else of the area changed
    for (size_t i = 0; i < sizeof area; i += 4) {
        const bool erased_here = std::memcmp(area + i, copy + i, 4) != 0;
        if (erased_here) CHECK(area[i] == 0 && area[i + 1] == 0 && area[i + 2] == 0 && area[i + 3] == 0);
    }

    // through the session: test mode only, and only after confirm
    FakeHost host;
    editor::Session s(&host);
    s.add_package("editor-test", "Editor test", "", m);
    Value other = json(R"({"id": "other", "actors": [{"id": "x", "actor": "sign", "stage": "sea", "room": 1,
                          "position": {"x": 0, "y": 0, "z": 0}, "flag": "x", "lines": "x"}]})");
    s.add_package("other", "Other", "", other);
    Book live(host.area, 32);
    live.set(kRecordQuest, letter_key("editor-test", "q"), 3);
    live.set(kRecordActorFlag, letter_key("other", "actor-flag:x"), 1);
    CHECK(!ok(s.run("reset_quest", json(R"({"confirm": true})"))));
    s.run("test_mode", json(R"({"on": true})"));
    CHECK(!ok(s.run("reset_quest")));
    CHECK(ok(s.run("reset_quest", json(R"({"confirm": true})"))));
    CHECK(!live.has(kRecordQuest, letter_key("editor-test", "q")) && live.get(kRecordActorFlag, letter_key("other", "actor-flag:x")) == 1);
}

void package_files(const fs::path& examples) {
    // a copy of the SDK v3 Windfall example: its models must be files of the package
    const fs::path dir = fs::temp_directory_path() / ("wwhd-editor-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(dir);
    fs::copy(examples / "sdk-v3-windfall", dir, fs::copy_options::recursive);
    std::ifstream in(dir / "manifest.json");
    std::stringstream text;
    text << in.rdbuf();
    in.close();  // Windows cannot replace or delete a file that is still open
    const Value manifest = mods::json::parse(text.str());
    FakeHost host;
    editor::Session s(&host);
    s.add_package("sdk-v3-windfall", "Windfall", dir.string(), manifest);
    Value r = s.run("validate");
    CHECK(ok(r) && r.get("result").get("valid").boolean);
    r = s.run("move_actor", json(R"({"id": "carved-post", "position": {"x": 150, "y": 1040, "z": -201450}})"));
    CHECK(ok(r));
    r = s.run("list_models");
    CHECK(ok(r) && r.get("result").array.size() == 2 && r.get("result").array[0].get("used_by").array.size() == 1);
    // a model whose file is not in the package is refused
    Value bad;
    bad["id"] = "carved-post";
    bad["property"] = "model";
    bad["value"] = "missing";
    CHECK(!ok(s.run("set_actor", bad)));
    fs::remove(dir / "assets" / "tide_sprite.glb");
    CHECK(!ok(s.run("move_actor", json(R"({"id": "tide-sprite", "position": {"x": 300, "y": 1040, "z": -201400}})"))));
    std::ofstream(dir / "assets" / "tide_sprite.glb") << "x";
    // save: only with confirm, writes manifest.json only, and the result reads back as the same manifest
    CHECK(!ok(s.run("save_package")));
    const auto before = fs::last_write_time(dir / "manifest.json");
    (void)before;
    r = s.run("save_package", json(R"({"confirm": true})"));
    CHECK(ok(r) && !s.dirty("sdk-v3-windfall"));
    std::ifstream saved(dir / "manifest.json");
    std::stringstream st;
    st << saved.rdbuf();
    saved.close();
    const Value back = mods::json::parse(st.str());
    CHECK(back == *s.manifest("sdk-v3-windfall"));
    CHECK(back.get("actors").array[2].get("position").get("x").number == 150);
    CHECK(st.str().find("  \"actors\": [") != std::string::npos);  // indented
    CHECK(!fs::exists(dir / "manifest.json.editor-tmp"));
    size_t files = 0;
    for (auto& e : fs::recursive_directory_iterator(dir)) files += e.is_regular_file();
    size_t source = 0;
    for (auto& e : fs::recursive_directory_iterator(examples / "sdk-v3-windfall")) source += e.is_regular_file();
    CHECK(files == source);  // nothing but the manifest was written
    fs::remove_all(dir);

    CHECK(editor::pretty(json(R"({"a": 0.1, "b": -201500, "c": [1, 2], "d": {}})")) == "{\n  \"a\": 0.1,\n  \"b\": -201500,\n  \"c\": [1, 2],\n  \"d\": {}\n}\n");
}

void stage_tables() {
    // the room files of a stage folder: <stage>_Room<n>.szs, and the member names of a pack
    std::vector<std::string> names = {"M_NewD2_Room0.szs", "M_NewD2_Room11.szs", "M_NewD2_Stage.szs", "sea_Room44.szs", "sea_LODALL.szs", "x.txt",
                                      "Abship_Room3.szs", "Abship_Room3.szs"};
    const auto rooms = editor::stage_rooms_from_names(names);
    CHECK(rooms.size() == 3);
    CHECK(rooms[0].first == "Abship" && rooms[0].second == std::vector<int>{3});
    CHECK(rooms[1].first == "M_NewD2" && rooms[1].second == (std::vector<int>{0, 11}));
    CHECK(rooms[2].first == "sea" && rooms[2].second == std::vector<int>{44});
    // the spawn points of a room's data (PLYR chunk of room.dzr)
    std::vector<uint8_t> dzr(4 + 12 * 2 + 0x20 * 2, 0);
    auto put32 = [&](size_t at, uint32_t v) { dzr[at] = uint8_t(v >> 24), dzr[at + 1] = uint8_t(v >> 16), dzr[at + 2] = uint8_t(v >> 8), dzr[at + 3] = uint8_t(v); };
    auto putf = [&](size_t at, float f) { uint32_t u; std::memcpy(&u, &f, 4); put32(at, u); };
    put32(0, 2);
    std::memcpy(&dzr[4], "RTBL", 4);
    put32(8, 0);
    put32(12, 0);
    std::memcpy(&dzr[16], "PLYR", 4);
    put32(20, 2);
    put32(24, 28);
    for (int i = 0; i < 2; i++) {
        const size_t e = 28 + 0x20 * size_t(i);
        std::memcpy(&dzr[e], "Link", 4);
        put32(e + 8, 0xFF000000u | 3);  // room 3
        putf(e + 0xC, 100.0f * (i + 1));
        putf(e + 0x10, 50);
        putf(e + 0x14, -20);
        dzr[e + 0x1A] = 0x40;  // angle y 0x4000
        dzr[e + 0x1D] = uint8_t(i == 0 ? 5 : 0);
    }
    const auto spawns = editor::parse_spawns(dzr);
    CHECK(spawns.size() == 2 && spawns[0].point == 5 && spawns[1].point == 0 && spawns[0].room == 3);
    CHECK(spawns[1].position.x == 200 && spawns[0].angle == 0x4000);
    CHECK(editor::parse_spawns(std::vector<uint8_t>(3)).empty());
    // embedded in other data (the HD room BFRES carries its dzr inside), found by its chunk table
    std::vector<uint8_t> bfres(1000, 0x5A);
    std::memcpy(&bfres[10], "PLYR", 4);  // a stray tag without a table before it
    bfres.insert(bfres.begin() + 333, dzr.begin(), dzr.end());
    const auto found = editor::find_spawns(bfres);
    CHECK(found.size() == 2 && found[0].point == 5 && found[1].position.x == 200);
    std::vector<uint8_t> cut(dzr.begin(), dzr.begin() + 40);
    CHECK(editor::parse_spawns(cut).empty());  // a chunk past the end is ignored
}
}  // namespace

// ---------------------------------------------------------------- building new content
const Value* actor_of(const editor::Session& s, const std::string& id) {
    for (const auto& a : s.manifest(s.open_mod())->get("actors").array)
        if (a.get("id").string() == id) return &a;
    return nullptr;
}
fs::path temp_dir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / (std::string("wwhd-editor-") + name + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(dir);
    return dir;
}

void building(const fs::path& examples) {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("editor-test", "Editor test", "", json(kManifest));
    Value r = s.run("list_actor_types");
    CHECK(ok(r) && r.get("result").array.size() == 15);

    // add: at Link (sea room 44, heading 100 of 65536), snapped onto the ground (y 5); one undo step
    r = s.run("add_actor", json(R"({"type": "sign"})"));
    CHECK(ok(r) && r.get("result").get("id").string() == "sign");
    const Value* a = actor_of(s, "sign");
    CHECK(a && a->get("stage").string() == "sea" && a->get("room").number == 44 && a->get("position").get("y").number == 5);
    CHECK(a && a->get("flag").string() == "sign-read" && a->get("rotation").get("y").number == 0.5);
    CHECK(host.last_changed.size() == 1 && host.last_changed[0] == "sign" && host.last_actors.size() == 3);
    r = s.run("add_actor", json(R"({"type": "sign", "snap": false})"));
    CHECK(ok(r) && r.get("result").get("id").string() == "sign-2" && actor_of(s, "sign-2")->get("position").get("y").number == 2);
    CHECK(ok(s.run("undo")) && !actor_of(s, "sign-2"));
    CHECK(ok(s.run("redo")) && actor_of(s, "sign-2"));
    // at the camera (off: refused) and at a position on the sea (its square is the room)
    CHECK(!ok(s.run("add_actor", json(R"({"type": "chest", "at": "camera"})"))));
    host.camera_on = true;
    r = s.run("add_actor", json(R"({"type": "chest", "at": "camera", "id": "box"})"));
    CHECK(ok(r) && actor_of(s, "box")->get("position").get("x").number == 100 && actor_of(s, "box")->get("rotation").get("y").number == 90);
    r = s.run("add_actor", json(R"({"type": "resident", "at": "position", "position": {"x": 0, "y": 900, "z": -201500}, "stage": "sea", "options": {"resident": "um1"}})"));
    CHECK(ok(r) && actor_of(s, "resident")->get("room").number == 11 && actor_of(s, "resident")->get("parameters").get("resident").string() == "um1");
    // no ground below: placed where asked, with a note
    host.has_ground = false;
    r = s.run("add_actor", json(R"({"type": "crate"})"));
    CHECK(ok(r) && r.get("result").get("snap_note").type == Value::String && actor_of(s, "crate")->get("broken_flag").string() == "crate-broken");
    host.has_ground = true;
    // a door needs a switch; enemies; a model type needs a model
    CHECK(!ok(s.run("add_actor", json(R"({"type": "door"})"))));
    CHECK(ok(s.run("add_actor", json(R"({"type": "switch"})"))));
    r = s.run("add_actor", json(R"({"type": "door"})"));
    CHECK(ok(r) && actor_of(s, "door")->get("switch").string() == "switch");
    CHECK(ok(s.run("add_actor", json(R"({"type": "bokoblin"})"))) && actor_of(s, "bokoblin")->get("defeated_flag").string() == "bokoblin-defeated");
    CHECK(!ok(s.run("add_actor", json(R"({"type": "model"})"))));
    CHECK(!ok(s.run("add_actor", json(R"({"type": "dragon"})"))));
    CHECK(!ok(s.run("add_actor", json(R"({"type": "sign", "id": "Bad Id"})"))));
    CHECK(!ok(s.run("add_actor", json(R"({"type": "sign", "id": "guide"})"))));  // taken

    // remove: needs confirm; the switch a door uses can't go; undo brings it back
    CHECK(!ok(s.run("remove_actor", json(R"({"id": "sign-2"})"))));
    CHECK(ok(s.run("remove_actor", json(R"({"id": "sign-2", "confirm": true})"))) && !actor_of(s, "sign-2"));
    CHECK(!ok(s.run("remove_actor", json(R"({"id": "switch", "confirm": true})"))) && actor_of(s, "switch"));
    CHECK(ok(s.run("undo")) && actor_of(s, "sign-2"));

    // duplicate: a new id with flags of its own, next to it
    r = s.run("duplicate_actor", json(R"({"id": "sign-a"})"));
    CHECK(ok(r) && r.get("result").get("id").string() == "sign-a-2");
    a = actor_of(s, "sign-a-2");
    CHECK(a && a->get("flag").string() == "sign-a-2-read" && a->get("position").get("x").number == 110);
    CHECK(ok(s.run("duplicate_actor", json(R"({"id": "switch", "new_id": "switch-b", "offset": {"x": 0, "y": 0, "z": 50}})"))));
    CHECK(actor_of(s, "switch-b")->get("switch_flag").string() == "switch-b-pressed");

    // place: at the camera, at Link, on the ground below
    CHECK(ok(s.run("place_actor", json(R"({"id": "sign-a", "at": "camera"})"))) && actor_of(s, "sign-a")->get("position").get("z").number == 200);
    host.ground_height = 40;
    r = s.run("place_actor", json(R"({"id": "sign-a", "at": "ground"})"));
    CHECK(ok(r) && actor_of(s, "sign-a")->get("position").get("y").number == 40 && actor_of(s, "sign-a")->get("position").get("z").number == 200);
    CHECK(!ok(s.run("place_actor", json(R"({"id": "sign-a", "at": "moon"})"))));
    // pick: a ray from above at 45 degrees meets the ground
    r = s.run("pick", json(R"({"origin": {"x": 0, "y": 140, "z": -201500}, "direction": {"x": 1, "y": -1, "z": 0}})"));
    CHECK(ok(r) && std::fabs(r.get("result").get("position").get("x").number - 100) < 0.2 && r.get("result").get("room").number == 11);
    CHECK(!ok(s.run("pick", json(R"({"origin": {"x": 0, "y": 140, "z": 0}, "direction": {"x": 0, "y": 1, "z": 0}})"))));

    // then actions and drops: checked like the package
    CHECK(ok(s.run("set_then", json(R"({"id": "sign-a", "actions": [{"action": "flag", "id": "sign-a-done"}]})"))));
    CHECK(actor_of(s, "sign-a")->get("then").array.size() == 1);
    CHECK(!ok(s.run("set_then", json(R"({"id": "sign-a", "actions": [{"action": "give", "id": "item:none"}]})"))));
    CHECK(actor_of(s, "sign-a")->get("then").array.size() == 1);
    CHECK(ok(s.run("set_then", json(R"({"id": "sign-a", "actions": []})"))) && actor_of(s, "sign-a")->get("then").type == Value::Null);
    CHECK(!ok(s.run("set_drops", json(R"({"id": "crate", "drops": [{"item": "item:none", "quantity": 1}]})"))));
    CHECK(ok(s.run("set_drops", json(R"({"id": "crate", "drops": []})"))));

    // dialogue preview: the actor's lines or given ones, in the game's message box
    CHECK(ok(s.run("preview_dialogue", json(R"({"id": "sign-a"})"))) && host.preview == "Hello.");
    CHECK(ok(s.run("preview_dialogue", json(R"({"lines": ["One", "Two"]})"))) && host.preview == "One\nTwo");
    CHECK(ok(s.run("preview_dialogue", json(R"({"close": true})"))) && host.preview.empty());

    // focus, play and stop
    CHECK(ok(s.run("focus", json(R"({"id": "guide"})"))) && host.focused == 1);
    CHECK(ok(s.run("play", json(R"({"from": "link", "restore": false})"))) && host.is_playing && !host.last_from_camera && !host.last_restore);
    CHECK(ok(s.run("stop")) && !host.is_playing && !ok(s.run("stop")));
    CHECK(ok(s.run("play")) && host.last_from_camera && host.last_restore);
    CHECK(!ok(s.run("play", json(R"({"from": "moon"})"))));

    // a refused package: the problem names the actor and the field
    Value broken = json(kManifest);
    broken["actors"].array[0].object.erase("flag");
    editor::Session b(&host);
    b.add_package("editor-test", "Editor test", "", broken);
    r = b.run("check_package");
    CHECK(ok(r) && r.get("result").get("problems").array.size() == 1);
    CHECK(r.get("result").get("problems").array[0].get("id").string() == "sign-a" && r.get("result").get("problems").array[0].get("field").string() == "flag");
    // the package as a whole is still valid; every step is in the log
    r = s.run("check_package");
    CHECK(ok(r) && r.get("result").get("valid").boolean);
    CHECK(s.log().back().command == "check_package");

    // models: check, import into the package (one undo step), use, export
    const fs::path dir = temp_dir("build");
    fs::copy(examples / "sdk-v3-windfall", dir, fs::copy_options::recursive);
    std::ifstream in(dir / "manifest.json");
    std::stringstream text;
    text << in.rdbuf();
    in.close();  // Windows cannot replace or delete a file that is still open
    editor::Session w(&host);
    w.add_package("sdk-v3-windfall", "Windfall", dir.string(), mods::json::parse(text.str()));
    const std::string creature = (examples / "sdk-v3-creature" / "assets" / "creature.glb").string();
    Value p;
    p["file"] = creature;
    r = w.run("check_model", p);
    CHECK(ok(r) && r.get("result").get("kind").string() == "skinned" && r.get("result").get("bones").number >= 1 && !r.get("result").get("clips").array.empty());
    p["file"] = (examples / "sdk-v3-windfall" / "assets" / "carved_post.glb").string();
    r = w.run("check_model", p);
    CHECK(ok(r) && r.get("result").get("kind").string() == "static" && r.get("result").get("triangles").number > 0);
    std::ofstream(dir / "broken.glb") << "not a model";
    p["file"] = (dir / "broken.glb").string();
    r = w.run("check_model", p);
    CHECK(!ok(r) && r.get("error").string().rfind("Size", 0) == 0);
    fs::remove(dir / "broken.glb");
    p["file"] = creature;
    r = w.run("import_model", p);
    CHECK(ok(r) && fs::exists(dir / "models" / "creature.glb") && host.prepared == "sdk-v3-windfall:creature");
    CHECK(!ok(w.run("import_model", p)));  // the id is taken
    r = w.run("add_actor", json(R"({"type": "model", "model": "creature", "at": "position", "position": {"x": 0, "y": 1040, "z": -201400}, "stage": "sea"})"));
    CHECK(ok(r) && r.get("result").get("id").string() == "creature");
    CHECK(ok(w.run("undo")) && ok(w.run("undo")));
    CHECK(w.manifest("sdk-v3-windfall")->get("models").array.size() == 2);

    // problems: each with what it is about
    Value bad = *w.manifest("sdk-v3-windfall");
    bad["models"].array[0]["source"] = "assets/missing.glb";
    editor::Session v(&host);
    v.add_package("sdk-v3-windfall", "Windfall", dir.string(), bad);
    r = v.run("check_package");
    CHECK(ok(r) && !r.get("result").get("valid").boolean);
    bool model_problem = false;
    for (const auto& pr : r.get("result").get("problems").array) model_problem |= pr.get("kind").string() == "model" && pr.get("id").string() == "carved-post";
    CHECK(model_problem);
    Value bad_export;  // built as a value: a Windows path has backslashes that are not JSON escapes
    bad_export["confirm"] = true;
    bad_export["path"] = dir.string() + "-out";
    CHECK(!ok(v.run("export_package", bad_export)));

    // export: a new folder with the manifest and the package's files; never into the package, never game files
    const fs::path out = temp_dir("export");
    Value e;
    e["path"] = out.string();
    CHECK(!ok(w.run("export_package", e)));  // confirm
    e["confirm"] = true;
    fs::create_directories(dir / "assets" / "__pycache__");
    std::ofstream(dir / "assets" / "__pycache__" / "tool.cpython-312.pyc") << "x";
    std::ofstream(dir / ".hidden") << "x";
    r = w.run("export_package", e);
    CHECK(!fs::exists(out / "assets" / "__pycache__") && !fs::exists(out / ".hidden"));
    if (!ok(r)) std::fprintf(stderr, "export: %s\n", r.get("error").string().c_str());
    CHECK(ok(r) && fs::exists(out / "manifest.json") && fs::exists(out / "assets" / "carved_post.glb") && fs::exists(out / "models" / "creature.glb"));
    CHECK(!ok(w.run("export_package", e)));  // not empty now
    e["path"] = (dir / "inside").string();
    CHECK(!ok(w.run("export_package", e)));
    std::ofstream(dir / "assets" / "stolen.szs") << "x";
    e["path"] = (out.string() + "-2");
    r = w.run("export_package", e);
    CHECK(!ok(r) && r.get("error").string().find("stolen.szs") != std::string::npos && !fs::exists(out.string() + "-2"));
    fs::remove_all(dir);
    fs::remove_all(out);
}

int main(int argc, char** argv) {
    // editor_test --game <game dir> [stage room]: what the editor reads from a game folder (manual check)
    if (argc >= 3 && std::string(argv[1]) == "--game") {
        const auto rooms = editor::game_stage_rooms(argv[2]);
        size_t n = 0;
        for (const auto& [stage, list] : rooms) n += list.size();
        std::printf("%zu stages, %zu rooms\n", rooms.size(), n);
        if (argc == 5)
            for (const auto& s : editor::room_spawns(argv[2], argv[3], std::atoi(argv[4])))
                std::printf("point %d room %d at %.1f %.1f %.1f angle %d\n", s.point, s.room, s.position.x, s.position.y, s.position.z, s.angle);
        return 0;
    }
    if (argc != 2) {
        std::fprintf(stderr, "usage: editor_test <examples/guest-mods>\n");
        return 2;
    }
    command_layer();
    locations();
    records();
    package_files(argv[1]);
    building(argv[1]);
    stage_tables();
    if (g_failures) {
        std::fprintf(stderr, "editor_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("editor_test: ok");
    return 0;
}
