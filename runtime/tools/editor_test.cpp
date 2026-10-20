#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
// World editor command layer (runtime/src/editor/commands.h): apply / undo / redo, refused changes,
// JSON round trips, the warp list, the test-mode record reset and saving into the package.
// Usage: editor_test <examples/guest-mods>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <iterator>
#include <utility>
#include <vector>
#include "editor/commands.h"
#include "editor/ray_ground.h"
#include "editor/stage_files.h"
#include "overlay/editor_live_kinds.h"
#include "mods/story_mail_book.h"
#include "mods/story_enemy_variants.h"


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
    int live_kinds = -1;  // -1: the type's own default; 0 / 1: the game has (not) installed the live kinds
    bool live_kind(const std::string&, bool fallback) override { return live_kinds < 0 ? fallback : live_kinds == 1; }
    std::vector<story_notebook::ManifestActor> last_actors;
    editor::Location last_warp;
    bool is_busy = false, flying = false, testing = false;
    uint8_t area[4 + 4 * 32] = {};
    std::string last_state;
    Value load_result;
    Value load_state_result() override {return load_result;}
    bool last_full=false;
    bool test_state_format(bool save,int slot,const std::string&mod,const std::vector<editor::Record>&own,bool full,std::string&why) override {
        last_full=full;return test_state(save,slot,mod,own,why);
    }
    std::vector<std::string> last_changed;
    bool apply_actors(const std::string&, const std::vector<story_notebook::ManifestActor>& actors, const std::vector<std::string>& changed,
                      std::string& note, std::string&) override {
        ++applied;
        last_actors = actors;
        last_changed = changed;
        note = "";
        return true;
    }
    std::vector<story_room_path::Path> last_paths;
    int path_applies = 0;
    bool apply_paths(const std::string&, const std::vector<story_room_path::Path>& paths, std::string&) override {
        ++path_applies;
        last_paths = paths;
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
    bool prepared_collision=false, queried_wireframe=false, query_fails=false, query_clear=true;
    int world_queries=0;
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
        prepared_collision = model.get("collision").boolean;
        note = "prepared";
        return true;
    }
    bool world_object_query(const std::string&, const story_notebook::ManifestActor& actor, bool wireframe, Value& result, std::string& why) override {
        ++world_queries; queried_wireframe=wireframe;
        if(query_fails)return why="collision is not prepared",false;
        result["clear"]=query_clear;
        result["id"]=actor.id;result["position"]["x"]=actor.position.x;
        result["conflicts"].type=Value::Array;result["budget"]["bytes"]=1234;
        if(!query_clear)result["conflicts"].array.push_back(mods::json::parse("{\"kind\":\"event\",\"message\":\"protected event\"}"));
        if(wireframe){result["vertices"]=mods::json::parse("[[0,0,0],[1,0,0]]");result["edges"]=mods::json::parse("[[0,1]]");}
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
    CHECK(ok(s.run("save_state",json(R"({"slot":2,"kind":"full"})")))&&host.last_full);
    CHECK(!ok(s.run("save_state",json(R"({"slot":2,"kind":"unknown"})"))));
    host.load_result=json(R"({"request":"1","slot":2,"pending":true})");
    r=s.run("load_state",json(R"({"slot":2,"confirm":true})"));
    CHECK(ok(r)&&r.get("result").get("state_load").get("pending").boolean);
    // An accepted request can later be refused; read_state exposes its result
    // to command/MCP callers without claiming the load succeeded.
    host.load_result=json(R"({"request":"1","slot":2,"pending":false,"ok":false,"reason":"native event transaction"})");
    r=s.run("read_state");
    CHECK(ok(r)&&!r.get("result").get("state_load").get("ok").boolean);
    CHECK(r.get("result").get("state_load").get("reason").string()=="native event transaction");
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
    {auto interior=m;interior["quests"].array[0]["steps"].array[0]["place"]=json(R"({"stage":"Pnezumi","room":0,"point":2,"sea":{"x":-200000,"z":300000}})");
        const auto places=editor::build_locations({},{{"editor-test",interior}});bool found=false;
        for(const auto&p:places)if(p.id=="step:editor-test:q:1"){found=true;CHECK(p.stage=="Pnezumi"&&p.room==0&&p.point==2&&p.group=="Quest");}CHECK(found);}

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
    m["actors"].array[1].object.erase("lines");
    m["actors"].array[1].object.erase("then");
    m["actors"].array[1]["talks"] = json(R"([{"lines":"Hi.","then":[{"action":"flag","id":"met-guide"},{"action":"flag","id":"branch-seen"}]}])");
    const auto own = editor::mod_records("editor-test", m);
    using namespace story_mail;
    auto has = [&](uint8_t kind, uint16_t key) { return std::find(own.begin(), own.end(), editor::Record{kind, key}) != own.end(); };
    CHECK(has(kRecordActorFlag, letter_key("editor-test", "actor-flag:sign-a-read")));
    CHECK(has(kRecordActorFlag, letter_key("editor-test", "actor-flag:met-guide")));
    CHECK(has(kRecordActorFlag, letter_key("editor-test", "actor-flag:branch-seen")));
    CHECK(has(kRecordQuestActionReward, letter_key("editor-test", "q")));
    CHECK(has(kRecordQuest, letter_key("editor-test", "q")) && has(kRecordQuestFlags, letter_key("editor-test", "q")));

    // a story area with the mod's records, another mod's, and a key both mods' names map to
    uint8_t area[4 + 4 * 64] = {};
    Book book(area, 64);
    CHECK(book.set(kRecordActorFlag, letter_key("editor-test", "actor-flag:sign-a-read"), 1));
    CHECK(book.set(kRecordQuest, letter_key("editor-test", "q"), 2));
    CHECK(book.set(kRecordQuestActionReward, letter_key("editor-test", "q"), 6));
    CHECK(book.set(kRecordActorFlag, letter_key("editor-test", "actor-flag:branch-seen"), 1));
    CHECK(book.set(kRecordActorFlag, letter_key("other", "actor-flag:x"), 1));
    CHECK(book.set(kRecordQuest, letter_key("other", "q"), 5));
    CHECK(book.set(kRecordItem, 1, 1));  // the notebook
    const editor::Record collide{kRecordActorFlag, letter_key("editor-test", "actor-flag:met-guide")};
    CHECK(book.set(collide.kind, collide.key, 1));
    uint8_t copy[sizeof area];
    std::memcpy(copy, area, sizeof area);
    unsigned skipped = 0;
    const unsigned erased = editor::reset_records(book, own, {collide}, skipped);
    CHECK(erased == 4 && skipped == 1);
    CHECK(!book.has(kRecordQuestActionReward, letter_key("editor-test", "q")));
    CHECK(!book.has(kRecordActorFlag, letter_key("editor-test", "actor-flag:branch-seen")));
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

void world_object_commands() {
    FakeHost host;editor::Session session(&host);
    auto manifest=json(kManifest);
    manifest["models"]=json(R"([{"id":"island","source":"assets/island.glb"},{"id":"skin","source":"assets/skin.glb","kind":"skinned"}])");
    session.add_package("editor-test","Editor test","",manifest);
    Value place=json(R"({"id":"island-a","model":"island","collision":{"mesh":"island"},"stage":"sea","room":12,"position":{"x":75000,"y":0,"z":-201500}})");
    auto result=session.run("place_world_object",place);
    CHECK(ok(result)&&session.undo_depth()==1);
    const auto* actor=actor_of(session,"island-a");
    CHECK(actor&&actor->get("behaviour").string()=="static"&&actor->get("collision").get("mesh").string()=="island");
    CHECK(ok(session.run("undo"))&&!actor_of(session,"island-a"));
    CHECK(ok(session.run("redo"))&&actor_of(session,"island-a"));
    auto before=*session.manifest("editor-test");const auto depth=session.undo_depth();
    CHECK(!ok(session.run("place_world_object",place))&&*session.manifest("editor-test")==before&&session.undo_depth()==depth);
    place["id"]="bad";place["behaviour"]="drifting";
    CHECK(!ok(session.run("place_world_object",place))&&!actor_of(session,"bad")&&session.undo_depth()==depth);
    CHECK(!ok(session.run("set_world_object",json(R"({"id":"island-a","collision":{"mesh":"missing"}})")))&&*session.manifest("editor-test")==before);
    CHECK(ok(session.run("set_world_object",json(R"({"id":"island-a","collision":{"shape":"box","size":{"x":100,"y":40,"z":200},"surface":"wood"}})"))));
    CHECK(actor_of(session,"island-a")->get("collision").get("shape").string()=="box");
    CHECK(ok(session.run("undo"))&&actor_of(session,"island-a")->get("collision").get("mesh").string()=="island");
    CHECK(!ok(session.run("set_actor",json(R"({"id":"island-a","property":"behaviour","value":null})"))));
    CHECK(ok(session.run("set_actor",json(R"({"id":"island-a","property":"behaviour","value":"static"})"))));
    before=*session.manifest("editor-test");const auto query_depth=session.undo_depth();
    result=session.run("preview_collision",json(R"({"id":"island-a"})"));
    CHECK(ok(result)&&host.queried_wireframe&&result.get("result").get("vertices").array.size()==2&&result.get("result").get("position").get("x").number==75000);
    result=session.run("placement_conflicts",json(R"({"id":"island-a"})"));
    CHECK(ok(result)&&!host.queried_wireframe&&result.get("result").get("budget").get("bytes").number==1234);
    CHECK(*session.manifest("editor-test")==before&&session.undo_depth()==query_depth);
    CHECK(!ok(session.run("preview_collision",json(R"({"id":"sign-a"})"))));
    host.query_fails=true;CHECK(!ok(session.run("preview_collision",json(R"({"id":"island-a"})"))));
    CHECK(ok(session.run("prepare_collision",json(R"({"model":"island"})")))&&host.prepared_collision&&host.prepared=="editor-test:island");
    CHECK(*session.manifest("editor-test")==before&&session.undo_depth()==query_depth);
    CHECK(!ok(session.run("prepare_collision",json(R"({"model":"skin"})"))));
    CHECK(!ok(session.run("prepare_collision",json(R"({"model":"missing"})"))));
    // A place that can't be checked is refused before committing a manifest or undo step.
    auto move=json(R"({"id":"island-a","position":{"x":76000,"y":0,"z":-201500}})");
    CHECK(!ok(session.run("move_actor",move))&&*session.manifest("editor-test")==before&&session.undo_depth()==query_depth);
    host.query_fails=false;host.query_clear=false;
    // A place where it can't stand is kept (one undo step) and reported: not in the game until it fits
    result=session.run("move_actor",move);
    CHECK(ok(result)&&result.get("result").get("not_active").array.size()==1&&result.get("result").get("not_active").array[0].string().find("island-a can't stand here: in the way: protected event")==0);
    CHECK(session.undo_depth()==query_depth+1&&result.get("result").get("note").string().find("isn't in the game")!=std::string::npos);
    auto validate=session.run("validate");CHECK(ok(validate)&&!validate.get("result").get("valid").boolean);
    {auto problems=session.run("check_package");bool listed=false;for(const auto&pr:problems.get("result").get("problems").array)listed|=pr.get("id").string()=="island-a"&&pr.get("field").string()=="position";CHECK(listed&&!problems.get("result").get("valid").boolean);}
    CHECK(!ok(session.run("export_package",json(R"({"path":"/nonexistent/x.zip"})"))));
    CHECK(!ok(session.run("save_package")));  // this test package has no folder; a real one saves (the placement is listed)
    CHECK(ok(session.run("undo"))&&*session.manifest("editor-test")==before);
    // An existing unprepared object does not prevent unrelated dialogue work.
    host.query_fails=true;
    CHECK(ok(session.run("set_dialogue",json(R"({"id":"sign-a","lines":["Changed independently."]})"))));
    host.query_fails=false;host.query_clear=true;
    CHECK(ok(session.run("move_actor",move)));

}

void floating_world_object_commands() {
    FakeHost host;editor::Session session(&host);auto manifest=json(kManifest);
    manifest["models"]=json(R"([{"id":"raft","source":"assets/raft.glb"}])");session.add_package("editor-test","Editor test","",manifest);
    auto place=json(R"({"id":"raft-a","model":"raft","collision":{"shape":"box","size":{"x":100,"y":20,"z":200},"surface":"wood"},"behaviour":"floating","buoyancy":{"draft":-20,"damping":0.3,"max_tilt":10},"stage":"sea","room":12,"position":{"x":75000,"y":0,"z":-201500}})");
    CHECK(ok(session.run("place_world_object",place)));
    CHECK(actor_of(session,"raft-a")->get("buoyancy").get("draft").number==-20);
    auto drift=json(R"({"id":"raft-a","behaviour":"drifting","drift":{"wind_speed":4,"current":{"x":3,"z":-2},"max_speed":8},"anchor":{"point":{"x":75000,"y":0,"z":-201500},"radius":500}})");
    CHECK(ok(session.run("set_world_object",drift)));
    auto actor=*actor_of(session,"raft-a");CHECK(actor.get("behaviour").string()=="drifting"&&actor.get("collision").get("shape").string()=="box"&&actor.get("anchor").get("radius").number==500);
    CHECK(ok(session.run("set_world_object",json(R"({"id":"raft-a","buoyancy":{"draft":-30,"damping":0.5,"max_tilt":15}})"))));
    CHECK(actor_of(session,"raft-a")->get("behaviour").string()=="drifting"&&actor_of(session,"raft-a")->get("drift").get("wind_speed").number==4&&actor_of(session,"raft-a")->get("buoyancy").get("draft").number==-30);
    CHECK(ok(session.run("set_actor",json(R"({"id":"raft-a","property":"anchor","value":null})"))));CHECK(actor_of(session,"raft-a")->get("anchor").type==Value::Null);
    CHECK(ok(session.run("undo"))&&actor_of(session,"raft-a")->get("anchor").get("radius").number==500);
    CHECK(ok(session.run("set_actor",json(R"({"id":"raft-a","property":"drift","value":{"wind_speed":2,"max_speed":5}})"))));
    CHECK(ok(session.run("set_actor",json(R"({"id":"raft-a","property":"buoyancy","value":{"damping":1}})"))));
    CHECK(ok(session.run("set_world_object",json(R"({"id":"raft-a","anchor":null})"))));CHECK(actor_of(session,"raft-a")->get("anchor").type==Value::Null);
    CHECK(ok(session.run("undo"))&&actor_of(session,"raft-a")->get("anchor").get("radius").number==500);
    CHECK(ok(session.run("set_world_object",json(R"({"id":"raft-a","behaviour":"floating"})"))));
    CHECK(actor_of(session,"raft-a")->get("drift").type==Value::Null&&actor_of(session,"raft-a")->get("anchor").type==Value::Null&&actor_of(session,"raft-a")->get("buoyancy").get("damping").number==1);
    CHECK(ok(session.run("undo"))&&actor_of(session,"raft-a")->get("behaviour").string()=="drifting");
    CHECK(ok(session.run("redo"))&&actor_of(session,"raft-a")->get("behaviour").string()=="floating");
    CHECK(ok(session.run("set_world_object",json(R"({"id":"raft-a","behaviour":"static"})"))));CHECK(actor_of(session,"raft-a")->get("buoyancy").type==Value::Null);
    auto before=*session.manifest("editor-test");auto depth=session.undo_depth();
    CHECK(!ok(session.run("set_world_object",json(R"({"id":"raft-a","behaviour":"drifting","drift":{"wind_speed":0,"max_speed":5}})")))&&*session.manifest("editor-test")==before&&session.undo_depth()==depth);
    CHECK(!ok(session.run("set_world_object",json(R"({"id":"raft-a","behaviour":"floating","drift":{"wind_speed":1,"max_speed":5}})")))&&*session.manifest("editor-test")==before);
    CHECK(!ok(session.run("set_actor",json(R"({"id":"raft-a","property":"buoyancy","value":{"draft":2}})")))&&*session.manifest("editor-test")==before);
    host.query_clear=false;{auto r=session.run("set_world_object",json(R"({"id":"raft-a","behaviour":"floating"})"));CHECK(ok(r)&&r.get("result").get("not_active").array.size()==1&&session.undo_depth()==depth+1);}
    CHECK(ok(session.run("undo"))&&*session.manifest("editor-test")==before);
}

void grapple_world_object_commands() {
    FakeHost host;editor::Session session(&host);auto manifest=json(kManifest);
    manifest["models"]=json(R"([{"id":"tower","source":"assets/tower.glb"}])");session.add_package("editor-test","Editor test","",manifest);
    auto place=json(R"({"id":"tower-a","model":"tower","collision":{"shape":"box","size":{"x":100,"y":1000,"z":100}},"stage":"sea","room":12,"position":{"x":100,"y":500,"z":200},"grapple_points":[{"id":"beam","position":{"x":0,"y":800,"z":-330},"yaw":90}]})");
    CHECK(ok(session.run("place_world_object",place)));CHECK(actor_of(session,"tower-a")->get("grapple_points").array.size()==1);
    CHECK(ok(session.run("set_world_object",json(R"({"id":"tower-a","collision":{"shape":"box","size":{"x":200,"y":1000,"z":100}}})"))));CHECK(actor_of(session,"tower-a")->get("grapple_points").array.size()==1);
    CHECK(ok(session.run("set_world_object",json(R"({"id":"tower-a","grapple_points":[{"id":"new-beam","position":{"x":1,"y":900,"z":-300}}]})"))));CHECK(actor_of(session,"tower-a")->get("grapple_points").array[0].get("id").string()=="new-beam");
    CHECK(ok(session.run("undo"))&&actor_of(session,"tower-a")->get("grapple_points").array[0].get("id").string()=="beam");CHECK(ok(session.run("redo")));
    CHECK(ok(session.run("set_world_object",json(R"({"id":"tower-a","grapple_points":null})"))));CHECK(actor_of(session,"tower-a")->get("grapple_points").type==Value::Null);CHECK(ok(session.run("undo")));
    CHECK(ok(session.run("set_actor",json(R"({"id":"tower-a","property":"grapple_points","value":[{"id":"third","position":{"x":0,"y":10,"z":0},"yaw":180}]})"))));
    CHECK(actor_of(session,"tower-a")->get("grapple_points").array[0].get("id").string()=="third");
    CHECK(ok(session.run("set_world_object",json(R"({"id":"tower-a","behaviour":"floating","buoyancy":{"draft":0}})"))));CHECK(actor_of(session,"tower-a")->get("grapple_points").array.size()==1);
    CHECK(ok(session.run("set_world_object",json(R"({"id":"tower-a","behaviour":"static"})"))));CHECK(actor_of(session,"tower-a")->get("grapple_points").array.size()==1);
    const auto before=*session.manifest("editor-test");const auto depth=session.undo_depth();
    CHECK(!ok(session.run("set_world_object",json(R"({"id":"tower-a","collision":{"shape":"sphere","radius":10},"grapple_points":[{"id":"bad","position":{"x":20001,"y":0,"z":0}}]})")))&&*session.manifest("editor-test")==before&&session.undo_depth()==depth);
    CHECK(!ok(session.run("set_actor",json(R"({"id":"tower-a","property":"grapple_points","value":[{"id":"same","position":{"x":0,"y":0,"z":0}},{"id":"same","position":{"x":1,"y":0,"z":0}}]})")))&&*session.manifest("editor-test")==before);
    CHECK(!ok(session.run("set_actor",json(R"({"id":"sign-a","property":"grapple_points","value":[]})")))&&*session.manifest("editor-test")==before);
    host.query_clear=false;CHECK(ok(session.run("set_world_object",json(R"({"id":"tower-a","grapple_points":[]})")))&&session.undo_depth()==depth+1);CHECK(ok(session.run("undo"))&&*session.manifest("editor-test")==before);host.query_clear=true;
    CHECK(ok(session.run("set_actor",json(R"({"id":"tower-a","property":"grapple_points","value":null})"))));CHECK(actor_of(session,"tower-a")->get("grapple_points").type==Value::Null);
    auto cleared=place;cleared["id"]="tower-b";cleared["grapple_points"]=Value{};CHECK(ok(session.run("place_world_object",cleared))&&actor_of(session,"tower-b")->get("grapple_points").type==Value::Null);
}

void island_world_object_commands() {
    FakeHost host;editor::Session session(&host);auto manifest=json(kManifest);manifest["models"]=json(R"([{"id":"island","source":"assets/island.glb"},{"id":"low","source":"assets/low.glb"},{"id":"coast","source":"assets/coast.glb"}])");session.add_package("editor-test","Editor test","",manifest);
    auto place=json(R"({"id":"island-a","model":"island","collision":{"shape":"box","size":{"x":100,"y":100,"z":100}},"stage":"sea","room":12,"position":{"x":100,"y":0,"z":200},"distant":{"model":"low"},"shoreline":{"model":"coast"},"chart_marker":{"label":"Horizon Isle","when_flag":"island.arrived"},"arrival":{"name":"Horizon Isle","radius":2200,"flag":"island.arrived"}})");
    CHECK(ok(session.run("place_world_object",place)));CHECK(actor_of(session,"island-a")->get("distant").get("model").string()=="low"&&actor_of(session,"island-a")->get("chart_marker").get("label").string()=="Horizon Isle");
    CHECK(ok(session.run("set_world_object",json(R"({"id":"island-a","collision":{"shape":"sphere","radius":50}})"))));CHECK(actor_of(session,"island-a")->get("distant").get("model").string()=="low"&&actor_of(session,"island-a")->get("arrival").get("radius").number==2200);
    CHECK(ok(session.run("set_world_object",json(R"({"id":"island-a","distant":null})"))));CHECK(actor_of(session,"island-a")->get("distant").type==Value::Null);CHECK(ok(session.run("undo"))&&actor_of(session,"island-a")->get("distant").get("model").string()=="low");CHECK(ok(session.run("redo")));CHECK(ok(session.run("undo")));
    CHECK(ok(session.run("set_actor",json(R"({"id":"island-a","property":"shoreline","value":null})"))));CHECK(actor_of(session,"island-a")->get("shoreline").type==Value::Null);CHECK(ok(session.run("undo")));
    CHECK(ok(session.run("set_actor",json(R"({"id":"island-a","property":"arrival","value":{"name":"New Horizon","radius":3000,"flag":"island.arrived"}})"))));CHECK(actor_of(session,"island-a")->get("arrival").get("name").string()=="New Horizon");
    const auto before=*session.manifest("editor-test");const auto depth=session.undo_depth();
    CHECK(!ok(session.run("set_world_object",json(R"({"id":"island-a","arrival":null})")))&&*session.manifest("editor-test")==before&&session.undo_depth()==depth); // Marker still reads this producer.
    CHECK(!ok(session.run("set_world_object",json(R"({"id":"island-a","collision":{"shape":"box","size":{"x":200,"y":100,"z":100}},"distant":{"model":"missing"}})")))&&*session.manifest("editor-test")==before&&session.undo_depth()==depth);
    CHECK(!ok(session.run("set_actor",json(R"({"id":"sign-a","property":"chart_marker","value":{"label":"Foreign"}})")))&&*session.manifest("editor-test")==before);
    host.query_clear=false;CHECK(ok(session.run("set_world_object",json(R"({"id":"island-a","arrival":{"name":"Other","radius":3000,"flag":"island.arrived"}})")))&&session.undo_depth()==depth+1);CHECK(ok(session.run("undo"))&&*session.manifest("editor-test")==before);host.query_clear=true;
    CHECK(ok(session.run("set_world_object",json(R"({"id":"island-a","distant":null,"shoreline":null,"chart_marker":null,"arrival":null,"behaviour":"floating"})"))));const auto*a=actor_of(session,"island-a");CHECK(a->get("behaviour").string()=="floating"&&a->get("distant").type==Value::Null&&a->get("chart_marker").type==Value::Null);
    CHECK(ok(session.run("undo"))&&actor_of(session,"island-a")->get("distant").get("model").string()=="low"&&actor_of(session,"island-a")->get("arrival").get("name").string()=="New Horizon");
    auto empty=place;empty["id"]="island-b";for(const char*key:{"distant","shoreline","chart_marker","arrival"})empty[key]=Value{};CHECK(ok(session.run("place_world_object",empty))&&actor_of(session,"island-b")->get("arrival").type==Value::Null);
}

// The Inspector's restart note: enemies (with variants), the Miniblin and residents count as live when the
// game installed its live kinds; the answer is read from a reply kept alive (a destroyed temporary read all
// types as unknown and showed the note on every live enemy).
void live_kinds_note() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("editor-test", "Editor test", "", json(kManifest));
    auto note = [&](const char* actor_json) {
        const Value reply = s.run("list_actor_types");
        const auto live = editor_live_kinds::read(reply);
        const Value a = json(actor_json);
        const std::string actor = a.get("actor").string();
        const bool enemy = actor == "chuchu" || actor == "bokoblin" || actor == "moblin" || actor == "keese" || actor == "miniblin" || actor == "darknut" || actor == "peahat" || actor == "bokobaba" || actor == "bubble" || actor == "octorok";
        const bool prop = actor == "pot" || actor == "barrel" || actor == "crate";
        CHECK(!live.empty());
        return editor_live_kinds::restart_only(a, live, enemy, prop);
    };
    const char* live_ones[] = {R"({"actor": "chuchu", "parameters": {"variant": "blue"}})", R"({"actor": "bokoblin", "parameters": {"variant": "shield"}})",
                               R"({"actor": "miniblin"})", R"({"actor": "darknut", "parameters": {"variant": "mighty"}})", R"({"actor": "keese"})", R"({"actor": "keese", "parameters": {"variant": "fire"}})", R"({"actor": "bokobaba"})", R"({"actor": "bubble", "parameters": {"variant": "blue"}})", R"({"actor": "octorok"})", R"({"actor": "peahat", "parameters": {"variant": "seahat"}})", R"({"actor": "pot"})", R"({"actor": "switch"})",
                               R"({"actor": "resident", "parameters": {"resident": "sa2"}})", R"({"actor": "sign"})"};
    host.live_kinds = 1;
    for (const char* a : live_ones) CHECK(!note(a));
    host.live_kinds = 0;  // the game has not installed them (no --editor live kinds): the note shows
    for (const char* a : {R"({"actor": "chuchu"})", R"({"actor": "miniblin"})", R"({"actor": "darknut"})", R"({"actor": "bokoblin", "parameters": {"variant": "machete"}})", R"({"actor": "bokobaba"})", R"({"actor": "bubble"})", R"({"actor": "octorok"})"}) CHECK(note(a));
    CHECK(note(R"({"kind": "mod", "model": "m", "combat": {}})"));
    // no answer yet: the start-up-only kinds are assumed
    const editor_live_kinds::Live none;
    CHECK(editor_live_kinds::restart_only(json(R"({"actor": "miniblin"})"), none, true, false));
    CHECK(!editor_live_kinds::restart_only(json(R"({"actor": "sign"})"), none, false, false));
}

void building(const fs::path& examples) {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("editor-test", "Editor test", "", json(kManifest));
    Value r = s.run("list_actor_types");
    CHECK(ok(r) && r.get("result").array.size() == 22 + std::size(story_enemy_variants::kMoreEnemies));

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
    // who a resident is: the declared variants (list_actor_types), each one placeable; unsafe ones refused with the reason
    {
        const Value types = s.run("list_actor_types").get("result");
        const Value* resident = nullptr;
        for (const auto& t : types.array)
            if (t.get("type").string() == "resident") resident = &t;
        CHECK(resident && resident->get("option").string() == "resident" && resident->get("variants").array.size() == std::size(story_resident_types::kTypes));
        bool sailor = false;
        for (const auto& v : resident->get("variants").array) {
            CHECK(!v.get("label").string().empty() && !v.get("help").string().empty());
            sailor = sailor || v.get("value").string() == "sa2";
        }
        CHECK(sailor);
        r = s.run("add_actor", json(R"({"type": "resident", "id": "lookout", "at": "position", "position": {"x": 0, "y": 900, "z": -201500}, "stage": "sea", "options": {"resident": "sa2"}})"));
        CHECK(ok(r) && actor_of(s, "lookout")->get("parameters").get("resident").string() == "sa2");
        r = s.run("add_actor", json(R"({"type": "resident", "id": "stargazer", "options": {"resident": "um3"}})"));
        CHECK(!ok(r) && r.get("error").string().find("telescope") != std::string::npos && !actor_of(s, "stargazer"));
        CHECK(!ok(s.run("add_actor", json(R"({"type": "resident", "id": "nobody", "options": {"resident": "zz9"}})"))));
        CHECK(!ok(s.run("add_actor", json(R"({"type": "resident", "id": "studio", "at": "position", "position": {"x": 0, "y": 0, "z": 0}, "stage": "Ocmera", "room": 0, "options": {"resident": "ub4"}})"))));
        CHECK(ok(s.run("remove_actor", json(R"({"id": "lookout", "confirm": true})"))));
    }
    // no ground below on the sea: the water's surface (y 0); elsewhere placed where asked, with a note
    host.has_ground = false;
    r = s.run("add_actor", json(R"({"type": "crate"})"));
    CHECK(ok(r) && r.get("result").get("snapped").boolean && actor_of(s, "crate")->get("position").get("y").number == 0 &&
          actor_of(s, "crate")->get("broken_flag").string() == "crate-broken");
    r = s.run("add_actor", json(R"({"type": "crate", "id": "crate-in", "at": "position", "position": {"x": 10, "y": 300, "z": 20}, "stage": "M_NewD2", "room": 1})"));
    CHECK(ok(r) && r.get("result").get("snap_note").type == Value::String && actor_of(s, "crate-in")->get("position").get("y").number == 300);
    CHECK(ok(s.run("remove_actor", json(R"({"id": "crate-in", "confirm": true})"))));
    host.has_ground = true;
    // a door needs a switch; enemies; a model type needs a model
    CHECK(!ok(s.run("add_actor", json(R"({"type": "door"})"))));
    CHECK(ok(s.run("add_actor", json(R"({"type": "switch"})"))));
    r = s.run("add_actor", json(R"({"type": "door"})"));
    CHECK(ok(r) && actor_of(s, "door")->get("switch").string() == "switch");
    CHECK(ok(s.run("add_actor", json(R"({"type": "bokoblin"})"))) && actor_of(s, "bokoblin")->get("defeated_flag").string() == "bokoblin-defeated");
    // an enemy's variant: declared choices only; the default needs no parameter
    CHECK(ok(s.run("add_actor", json(R"({"type": "chuchu", "id": "red-chu", "options": {"variant": "red"}})"))) &&
          actor_of(s, "red-chu")->get("parameters").get("variant").string() == "red");
    CHECK(ok(s.run("add_actor", json(R"({"type": "chuchu", "id": "green-chu", "options": {"variant": "green"}})"))) &&
          actor_of(s, "green-chu")->get("parameters").type == Value::Null);
    CHECK(!ok(s.run("add_actor", json(R"({"type": "chuchu", "id": "odd-chu", "options": {"variant": "purple"}})"))) && !actor_of(s, "odd-chu"));
    CHECK(!ok(s.run("add_actor", json(R"({"type": "keese", "id": "odd-keese", "options": {"variant": "red"}})"))) && !actor_of(s, "odd-keese"));
    CHECK(ok(s.run("add_actor", json(R"({"type": "darknut", "id": "knight", "options": {"variant": "mighty"}})"))) &&
          actor_of(s, "knight")->get("parameters").get("variant").string() == "mighty" && actor_of(s, "knight")->get("defeated_flag").string() == "knight-defeated");
    CHECK(!ok(s.run("add_actor", json(R"({"type": "darknut", "id": "statue", "options": {"variant": "frozen"}})"))) && !actor_of(s, "statue"));
    CHECK(ok(s.run("set_actor", json(R"({"id": "knight", "property": "defeat_switch", "value": "switch"})"))) && actor_of(s, "knight")->get("defeat_switch").string() == "switch");
    CHECK(ok(s.run("remove_actor", json(R"({"id": "knight", "confirm": true})"))));
    // batch 3: Boko Baba, Bubbles (red, blue), Octorok, Fire Keese and the Seahat; no Ice Keese, no Bubble skull
    CHECK(ok(s.run("add_actor", json(R"({"type": "bokobaba", "id": "baba"})"))) && actor_of(s, "baba")->get("defeated_flag").string() == "baba-defeated" && actor_of(s, "baba")->get("parameters").type == Value::Null);
    CHECK(ok(s.run("add_actor", json(R"({"type": "bubble", "id": "blue-bubble", "options": {"variant": "blue"}})"))) && actor_of(s, "blue-bubble")->get("parameters").get("variant").string() == "blue");
    CHECK(ok(s.run("add_actor", json(R"({"type": "bubble", "id": "red-bubble"})"))) && actor_of(s, "red-bubble")->get("parameters").type == Value::Null);
    CHECK(ok(s.run("add_actor", json(R"({"type": "octorok", "id": "octo"})"))) && actor_of(s, "octo"));
    CHECK(ok(s.run("add_actor", json(R"({"type": "keese", "id": "fire-keese", "options": {"variant": "fire"}})"))) && actor_of(s, "fire-keese")->get("parameters").get("variant").string() == "fire");
    CHECK(ok(s.run("add_actor", json(R"({"type": "peahat", "id": "seahat", "options": {"variant": "seahat"}})"))) && actor_of(s, "seahat")->get("parameters").get("variant").string() == "seahat");
    CHECK(!ok(s.run("add_actor", json(R"({"type": "keese", "id": "ice-keese", "options": {"variant": "ice"}})"))) && !actor_of(s, "ice-keese"));
    CHECK(!ok(s.run("add_actor", json(R"({"type": "bubble", "id": "skull", "options": {"variant": "skull"}})"))) && !actor_of(s, "skull"));
    CHECK(!ok(s.run("add_actor", json(R"({"type": "octorok", "id": "big-octo", "options": {"variant": "big"}})"))) && !actor_of(s, "big-octo"));
    CHECK(ok(s.run("set_actor", json(R"({"id": "baba", "property": "defeat_switch", "value": "switch"})"))) && actor_of(s, "baba")->get("defeat_switch").string() == "switch");
    CHECK(ok(s.run("validate")));
    // the table-driven enemies: listed, placed with their own defeat flag, removed again
    for (const char* more : story_enemy_variants::kMoreEnemies) {
        const std::string type = more, id = "more-" + type;
        bool listed = false;
        const Value types = s.run("list_actor_types");
        for (const auto& t : types.get("result").array) listed |= t.get("type").string() == type && t.get("group").string() == "Enemies";
        CHECK(listed);
        CHECK(ok(s.run("add_actor", json(("{\"type\": \"" + type + "\", \"id\": \"" + id + "\"}").c_str()))) && actor_of(s, id) &&
              actor_of(s, id)->get("defeated_flag").string() == id + "-defeated" && actor_of(s, id)->get("drops").type == Value::Array);
        CHECK(ok(s.run("remove_actor", json(("{\"id\": \"" + id + "\", \"confirm\": true}").c_str()))) && !actor_of(s, id));
    }
    {
        bool listed = false;
        const Value types = s.run("list_actor_types");
        for (const auto& t : types.get("result").array)
            if (t.get("type").string() == "chuchu") listed = t.get("option").string() == "variant" && t.get("variants").array.size() == 5;
        CHECK(listed);
    }
    // defeat_switch over set_actor: one of the mod's switches; a door's Inspector names the enemies
    CHECK(ok(s.run("set_actor", json(R"({"id": "red-chu", "property": "defeat_switch", "value": "switch"})"))) && actor_of(s, "red-chu")->get("defeat_switch").string() == "switch");
    CHECK(!ok(s.run("set_actor", json(R"({"id": "red-chu", "property": "defeat_switch", "value": "door"})"))) && actor_of(s, "red-chu")->get("defeat_switch").string() == "switch");
    CHECK(ok(s.run("set_actor", json(R"({"id": "red-chu", "property": "defeat_switch", "value": null})"))) && actor_of(s, "red-chu")->get("defeat_switch").type == Value::Null);
    CHECK(ok(s.run("remove_actor", json(R"({"id": "red-chu", "confirm": true})"))) && ok(s.run("remove_actor", json(R"({"id": "green-chu", "confirm": true})"))));
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
    CHECK(ok(s.run("focus", json(R"({"position": {"x": 10, "y": 20, "z": 30}, "distance": 900})"))) && host.focused == 2);  // a place
    CHECK(!ok(s.run("focus", json(R"({"id": "nobody"})"))));
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

// The handle drag (Session::drag_preview): the game shows every step of it, but the package, the undo
// stack and the log change only once, when the handle is let go.
void handle_drag() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("editor-test", "Editor test", "", json(kManifest));
    const Value before = *s.manifest("editor-test");
    const size_t logged = s.log().size();
    std::vector<std::string> later;
    std::string why;
    for (int i = 1; i <= 20; i++) {
        Value poses = json(R"([{"id": "sign-a"}])");
        poses.array[0]["position"] = point(10 + i, 1040, -201500);
        CHECK(s.drag_preview("editor-test", poses, later, why) && later.empty());
        CHECK(host.last_changed == std::vector<std::string>{"sign-a"} && host.last_actors[0].position.x == 10 + i);
    }
    CHECK(host.applied == 20);
    CHECK(*s.manifest("editor-test") == before && s.undo_depth() == 0 && s.log().size() == logged && !s.dirty("editor-test"));
    // let go: one command, one undo step, one log entry
    Value p;
    p["id"] = "sign-a";
    p["position"] = point(30, 1040, -201500);
    Value r = s.run("move_actor", p);
    CHECK(ok(r) && s.undo_depth() == 1 && s.log().size() == logged + 1 && host.last_actors[0].position.x == 30);
    // undo: back at the start in one step
    r = s.run("undo");
    CHECK(ok(r) && s.undo_depth() == 0 && host.last_actors[0].position.x == 10 && *s.manifest("editor-test") == before);
    // a group drag: both actors follow every step; letting go is one move_actors (one undo step)
    const int applied = host.applied;
    for (int i = 1; i <= 5; i++) {
        Value poses = json(R"([{"id": "sign-a"}, {"id": "guide"}])");
        poses.array[0]["position"] = point(10 + i, 1040, -201500);
        poses.array[1]["position"] = point(-500 + i, 1200, -201500);
        CHECK(s.drag_preview("editor-test", poses, later, why));
        CHECK(host.last_changed.size() == 2 && host.last_actors[1].position.x == -500 + i);
    }
    CHECK(host.applied == applied + 5 && s.undo_depth() == 0);
    r = s.run("move_actors", json(R"({"ids": ["sign-a", "guide"], "offset": {"x": 5, "y": 0, "z": 0}})"));
    CHECK(ok(r) && s.undo_depth() == 1 && host.last_actors[0].position.x == 15 && host.last_actors[1].position.x == -495);
    // a turn drag
    Value turn = json(R"([{"id": "guide", "rotation": {"x": 0, "y": 90, "z": 0}}])");
    CHECK(s.drag_preview("editor-test", turn, later, why) && host.last_changed == std::vector<std::string>{"guide"});
    // cancelled (Esc): a pose without a place shows the package's place again
    CHECK(s.drag_preview("editor-test", json(R"([{"id": "sign-a"}, {"id": "guide"}])"), later, why));
    CHECK(host.last_actors[0].position.x == 15 && host.last_actors[1].position.x == -495 && s.undo_depth() == 1);
    // refused: an unknown actor or mod, nothing dragged
    CHECK(!s.drag_preview("editor-test", json(R"([{"id": "nobody"}])"), later, why) && !why.empty());
    CHECK(!s.drag_preview("no-such-mod", turn, later, why));
    CHECK(!s.drag_preview("editor-test", json("[]"), later, why));
    CHECK(s.log().size() == logged + 3);  // move_actor, undo, move_actors: the drag steps never show in the log
}

// the pick (ray_ground.h) against a made-up world with the game ground check's contract: the highest
// ground at x/z at or below the asked height. A pier slab at 89.4 over the sea floor at -5100 (the
// Windfall pier), and a grass hill rising to 301 with no ground under its surface but the sea floor.
void ray_pick() {
    using story_notebook::WorldPoint;
    auto world = [](double x, double y, double z, float& h) {
        (void)z;
        float best = -1e30f;
        bool any = false;
        auto offer = [&](float g) { if (g <= y && g > best) best = g, any = true; };
        offer(-5100);                                                 // the sea floor
        if (x > 800 && x < 1100) offer(89.4f);                        // the pier, a thin slab
        if (x < -1500) offer(float(std::min(301.0, 301 + (x + 2000) * 0.1)));  // the hill
        h = best;
        return any;
    };
    WorldPoint hit;
    // straight down onto the pier: its top, not the sea floor below
    CHECK(editor::ray_ground({927.8f, 600, -197972.2f}, {0, -1, 0}, 20000, world, hit));
    CHECK(std::fabs(hit.y - 89.4f) < 0.2f && std::fabs(hit.x - 927.8f) < 0.01f);
    // straight down onto the grass
    CHECK(editor::ray_ground({-2244.1f, 805, -199433.7f}, {0, -1, 0}, 20000, world, hit));
    CHECK(std::fabs(hit.y - 276.59f) < 0.5f);
    // slanted down from the camera: the hill where the ray first reaches it
    const float n = std::sqrt(0.6f * 0.6f * 2 + 0.25f);
    CHECK(editor::ray_ground({-2244.1f, 805, -199433.7f}, {0.6f / n, -0.6f / n, -0.5f / n}, 20000, world, hit));
    CHECK(hit.x < -1500 && hit.y > 250 && hit.y < 302);
    // slanted down across the pier from the side: the pier's top
    CHECK(editor::ray_ground({700, 300, 0}, {0.7071f, -0.7071f, 0}, 20000, world, hit));
    CHECK(std::fabs(hit.y - 89.4f) < 0.2f && hit.x > 800 && hit.x < 1100);
    // nothing below within range, or a ray up into the sky: no hit
    CHECK(!editor::ray_ground({0, 600, 0}, {0, -1, 0}, 1000, world, hit));
    CHECK(!editor::ray_ground({0, 600, 0}, {0, 1, 0}, 20000, world, hit));
    // the sea's surface: a ray over open sea gives the point where it meets y 0; ground nearer than the water wins
    WorldPoint sea;
    const float m = std::sqrt(0.6f * 0.6f + 0.8f * 0.8f);
    CHECK(editor::sea_pick({1000, 800, 5000}, {0.6f / m, -0.8f / m, 0}, 20000, false, sea));
    CHECK(sea.y == 0 && std::fabs(sea.x - 1600) < 0.5f && sea.z == 5000);
    sea = {1200, 650, 5000};  // an island top met first
    CHECK(!editor::sea_pick({1000, 800, 5000}, {0.6f / m, -0.8f / m, 0}, 20000, true, sea) && sea.y == 650);
    sea = {9000, -5000, 5000};  // the deep sea floor far beyond the water's surface
    CHECK(editor::sea_pick({1000, 800, 5000}, {0.6f / m, -0.8f / m, 0}, 20000, true, sea) && sea.y == 0);
    CHECK(!editor::sea_pick({1000, 800, 5000}, {0, 1, 0}, 20000, false, sea));       // up into the sky
    CHECK(!editor::sea_pick({1000, -50, 5000}, {0, -1, 0}, 20000, false, sea));      // from below the water
    CHECK(!editor::sea_pick({1000, 800, 5000}, {0.6f / m, -0.8f / m, 0}, 900, false, sea));  // beyond the range
}

// Placing on an island ground and on the open sea: an island (here a plateau whose top is at 650, around x 0,
// z -201500 in sea room 11) is ground like any other, every kind stands on its top; over open sea (no ground the
// game's check finds) a pick gives the water's surface and things placed there stay at sea level.
struct IslandHost : FakeHost {
    std::map<std::string, std::string> states;
    static bool on_island(double x, double z) { return std::fabs(x) < 3000 && std::fabs(z + 201500) < 3000; }
    editor::Place link() override {
        editor::Place p;
        p.valid = true, p.stage = "sea", p.room = 11, p.position = {0, 650, -201500};
        return p;
    }
    bool ground(const editor::WorldPoint& at, float& h, std::string& why) override {
        if (on_island(at.x, at.z) && at.y >= 650) return h = 650, true;
        return why = "no ground below", false;  // the open sea: nothing the game's ground check finds
    }
    bool ray(const editor::WorldPoint& from, const editor::WorldPoint& d, float range, editor::WorldPoint& hit, std::string& why) override {
        if (d.y >= 0) return why = "the ray meets no ground", false;
        const float t = (from.y - 650) / -d.y;
        const editor::WorldPoint at{from.x + d.x * t, 650, from.z + d.z * t};
        if (t > range || !on_island(at.x, at.z)) return why = "the ray meets no ground", false;
        return hit = at, true;
    }
    bool prepare_piece(const std::string& mod, const std::string& id, std::string& note, std::string&) override {
        states[mod + "/" + id] = "preparing";
        return note = "preparing", true;
    }
    std::string piece_state(const std::string& mod, const std::string& id) override {
        auto found = states.find(mod + "/" + id);
        return found == states.end() ? "" : found->second;
    }
};
void island_and_sea_placement() {
    IslandHost host;
    editor::Session s(&host);
    s.add_package("editor-test", "Editor test", "", json(kManifest));
    // the pick: over the island its top, over open sea the water's surface (with its sea room)
    Value r = s.run("pick", json(R"({"origin": {"x": 0, "y": 3000, "z": -201500}, "direction": {"x": 0, "y": -1, "z": 0}})"));
    CHECK(ok(r) && r.get("result").get("position").get("y").number == 650 && r.get("result").get("surface").type == Value::Null);
    r = s.run("pick", json(R"({"origin": {"x": 20000, "y": 3000, "z": -201500}, "direction": {"x": 0.6, "y": -0.8, "z": 0}})"));
    CHECK(ok(r) && r.get("result").get("position").get("y").number == 0 && r.get("result").get("surface").string() == "sea");
    CHECK(std::fabs(r.get("result").get("position").get("x").number - 22250) < 1 && r.get("result").get("room").number == 11);
    // an island ground clicked onto the open sea: at the water's level, and it stays there when moved
    r = s.run("add_actor", json(R"({"type": "piece:star-island", "id": "isle", "at": "position", "position": {"x": 22250, "y": 0, "z": -201500}, "stage": "sea"})"));
    CHECK(ok(r) && actor_of(s, "isle") && actor_of(s, "isle")->get("position").get("y").number == 0);
    CHECK(ok(s.run("move_actor", json(R"({"id": "isle", "position": {"x": 23000, "y": 640, "z": -201000}})"))));
    CHECK(actor_of(s, "isle")->get("position").get("y").number == 0 && actor_of(s, "isle")->get("position").get("x").number == 23000);
    CHECK(ok(s.run("move_actors", json(R"({"ids": ["isle"], "offset": {"x": 10, "y": 300, "z": 0}})"))) && actor_of(s, "isle")->get("position").get("y").number == 0);
    // every kind placed over the island stands on its top: a mod actor (a sign), a resident, an enemy, a prop, a building
    struct Kind { const char* json; const char* id; };
    for (const Kind& k : {Kind{R"({"type": "sign", "id": "k-sign"})", "k-sign"}, Kind{R"({"type": "resident", "id": "k-resident", "options": {"resident": "um1"}})", "k-resident"},
                          Kind{R"({"type": "chuchu", "id": "k-enemy"})", "k-enemy"}, Kind{R"({"type": "pot", "id": "k-prop"})", "k-prop"},
                          Kind{R"({"type": "piece:outset-house-small", "id": "k-house"})", "k-house"}}) {
        std::string req = k.json;
        req.insert(req.size() - 1, R"(, "at": "position", "position": {"x": 400, "y": 1400, "z": -201300}, "stage": "sea")");
        r = s.run("add_actor", json(req.c_str()));
        CHECK(ok(r));
        const Value* a = actor_of(s, k.id);
        CHECK(a && a->get("position").get("y").number == 650 && a->get("room").number == 11);
        if (!ok(r) || !a) std::fprintf(stderr, "kind %s: %s\n", k.id, r.get("error").string().c_str());
    }
    // dropped on open water: the sea's surface (whether it may stand there is the usual validation's matter)
    r = s.run("add_actor", json(R"({"type": "chuchu", "id": "k-sea", "at": "position", "position": {"x": 15000, "y": 300, "z": -201500}, "stage": "sea"})"));
    CHECK(ok(r) && actor_of(s, "k-sea") && actor_of(s, "k-sea")->get("position").get("y").number == 0);
}


// The game objects palette (commands_game_objects.cpp): list_game_objects with the player's game files, add_actor
// "game:<id>" (the manifest's requires), variants, the transform limits, undo; game_archive on a game folder.
struct GameObjectHost : FakeHost {
    std::string game_archive(const std::string& archive) override { return archive == "Oyashi" || archive == "Lwood" ? "Pack/first_szs_permanent.pack" : ""; }
    bool live_kind(const std::string& type, bool fallback) override { return type.rfind("game:", 0) == 0 ? true : fallback; }
};
void game_objects() {
    GameObjectHost host;
    editor::Session s(&host);
    s.add_package("editor-test", "Editor test", "", json(kManifest));
    Value r = s.run("list_game_objects");
    const auto& catalogue = story_game_objects::catalogue();
    const auto& pieces = story_game_pieces::catalogue();  // island buildings are listed too (game_pieces below)
    CHECK(ok(r) && r.get("result").get("catalogue").number == std::max(catalogue.version, pieces.version) &&
          r.get("result").get("objects").array.size() == catalogue.entries.size() + pieces.pieces.size());
    for (const auto& o : r.get("result").get("objects").array) {
        if (o.get("id").string() == "palm") CHECK(o.get("available").boolean && o.get("type").string() == "game:palm" && o.get("live").boolean && o.get("scale").get("max").number == 1);
        if (o.get("id").string() == "table") CHECK(!o.get("available").boolean && o.get("why").string() == "not available in your game files" && o.get("variants").array.size() == 3);
    }
    r = s.run("list_game_objects", json(R"({"query": "STOOL"})"));
    CHECK(ok(r) && r.get("result").get("objects").array.size() == 1 && r.get("result").get("objects").array[0].get("id").string() == "table");
    r = s.run("list_game_objects", json(R"({"category": "trees and plants"})"));
    {
        std::set<std::string> categories;
        size_t trees = 0;
        for (const auto& e : catalogue.entries) categories.insert(e.category), trees += e.category == "Trees and plants";
        for (const auto& p : pieces.pieces) categories.insert(p.category);
        CHECK(ok(r) && r.get("result").get("objects").array.size() == trees && r.get("result").get("categories").array.size() == categories.size());
    }
    // a shared batch (grass) says that it changes when the room loads again; its variants name their units
    r = s.run("list_game_objects", json(R"({"query": "grass"})"));
    for (const auto& o : r.get("result").get("objects").array)
        if (o.get("id").string() == "grass") CHECK(o.get("batch").string() == "grass" && o.get("note").string().find("room loads again") != std::string::npos && o.get("variants").array[2].get("units").number == 21);
    // without the game: availability unknown
    editor::Session offline;
    offline.add_package("editor-test", "Editor test", "", json(kManifest));
    r = offline.run("list_game_objects");
    CHECK(ok(r) && r.get("result").get("objects").array[0].get("available").type == Value::Null);

    // add: at Link, snapped, the manifest names the catalogue version it needs; one undo step
    CHECK(!s.manifest("editor-test")->get("requires").object.count("game_objects"));
    r = s.run("add_actor", json(R"({"type": "game:palm"})"));
    CHECK(ok(r) && r.get("result").get("id").string() == "palm");
    const Value* a = actor_of(s, "palm");
    CHECK(a && a->get("actor").string() == "game:palm" && a->get("position").get("y").number == 5 && a->get("rotation").get("y").number == 0.5);
    CHECK(a && a->get("variant").type == Value::Null && a->get("flag").type == Value::Null && a->get("params").type == Value::Null);
    CHECK(s.manifest("editor-test")->get("requires").get("game_objects").number == 1);
    CHECK(host.last_changed.size() == 1 && host.last_changed[0] == "palm");
    r = s.run("add_actor", json(R"({"type": "game:table", "options": {"variant": "stool"}})"));
    CHECK(ok(r) && r.get("result").get("id").string() == "table" && actor_of(s, "table")->get("variant").string() == "stool");
    CHECK(!ok(s.run("add_actor", json(R"({"type": "game:table", "options": {"variant": "throne"}})"))));
    CHECK(!ok(s.run("add_actor", json(R"({"type": "game:castle"})"))));
    CHECK(!ok(s.run("add_actor", json(R"({"type": "game:palm", "id": "Bad Id"})"))));
    // the Inspector's picker: variant, turn, scale within the limits; everything else refused
    CHECK(ok(s.run("set_actor", json(R"({"id": "table", "property": "variant", "value": "stool-open"})"))) && actor_of(s, "table")->get("variant").string() == "stool-open");
    CHECK(!ok(s.run("set_actor", json(R"({"id": "table", "property": "variant", "value": "nope"})"))) && actor_of(s, "table")->get("variant").string() == "stool-open");
    CHECK(ok(s.run("set_actor", json(R"({"id": "table", "property": "variant", "value": null})"))) && actor_of(s, "table")->get("variant").type == Value::Null);
    CHECK(!ok(s.run("set_actor", json(R"({"id": "table", "property": "params", "value": 1})"))));
    CHECK(!ok(s.run("set_actor", json(R"({"id": "table", "property": "flag", "value": "f"})"))));
    CHECK(ok(s.run("rotate_actor", json(R"({"id": "palm", "rotation": {"x": 0, "y": 45, "z": 0}})"))));
    CHECK(!ok(s.run("rotate_actor", json(R"({"id": "palm", "rotation": {"x": 10, "y": 45, "z": 0}})"))));
    CHECK(!ok(s.run("scale_actor", json(R"({"id": "palm", "scale": {"x": 1.5, "y": 1.5, "z": 1.5}})"))));
    CHECK(ok(s.run("add_actor", json(R"({"type": "game:tree-large"})"))));
    CHECK(ok(s.run("scale_actor", json(R"({"id": "tree-large", "scale": {"x": 1.5, "y": 1.5, "z": 1.5}})"))));
    CHECK(!ok(s.run("scale_actor", json(R"({"id": "tree-large", "scale": {"x": 2, "y": 2, "z": 2}})"))));
    // duplicate and remove with undo
    CHECK(ok(s.run("duplicate_actor", json(R"({"id": "palm"})"))) && actor_of(s, "palm-2") && actor_of(s, "palm-2")->get("actor").string() == "game:palm");
    CHECK(ok(s.run("remove_actor", json(R"({"id": "palm-2", "confirm": true})"))) && !actor_of(s, "palm-2"));
    CHECK(ok(s.run("undo")) && actor_of(s, "palm-2"));
    CHECK(ok(s.run("validate")));
    // undo back to before the first game object: the requirement goes with it
    while (actor_of(s, "palm")) CHECK(ok(s.run("undo")));
    CHECK(s.manifest("editor-test")->get("requires").get("game_objects").type == Value::Null);

    // game_archive: a pack member (read from the pack's header) or content/Common/Object/<name>.szs
    const fs::path dir = fs::temp_directory_path() / "wwhd-editor-game-objects";
    fs::remove_all(dir);
    fs::create_directories(dir / "content" / "Common" / "Pack");
    fs::create_directories(dir / "content" / "Common" / "Object");
    std::ofstream(dir / "content" / "Common" / "Object" / "Rock.szs") << "Yaz0";
    {
        std::vector<uint8_t> d(0x14 + 0xC + 16 + 8 + 16, 0);
        auto be32 = [&](size_t at, uint32_t v) { for (int i = 0; i < 4; i++) d[at + i] = uint8_t(v >> (24 - 8 * i)); };
        auto be16 = [&](size_t at, uint16_t v) { d[at] = uint8_t(v >> 8), d[at + 1] = uint8_t(v); };
        std::memcpy(&d[0], "SARC", 4); be16(4, 0x14); be16(6, 0xFEFF); be32(8, uint32_t(d.size())); be32(0xC, uint32_t(d.size()));
        std::memcpy(&d[0x14], "SFAT", 4); be16(0x18, 0xC); be16(0x1A, 1);
        be32(0x20 + 4, 0x01000000); be32(0x20 + 8, 0); be32(0x20 + 12, 0);
        std::memcpy(&d[0x30], "SFNT", 4); be16(0x34, 8);
        std::memcpy(&d[0x38], "Palm.szs", 8);
        std::ofstream(dir / "content" / "Common" / "Pack" / "test.pack", std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
    }
    CHECK(editor::game_archive(dir.string(), "Palm") == "Pack/test.pack");
    CHECK(editor::game_archive(dir.string(), "Rock") == "Object");
    CHECK(editor::game_archive(dir.string(), "Castle").empty());
    CHECK(editor::game_archive(dir.string(), "../Rock").empty());
    fs::remove_all(dir);
}

void path_commands() {
    // the mod's room paths: an area or a line around Link or a position, points added, moved and removed,
    // closed or open, renamed and removed; each one undo step and given to the game at once
    FakeHost host;
    editor::Session s(&host);
    s.add_package("editor-test", "Editor test", "", json(kManifest));
    auto path_of = [&](const std::string& id) -> Value {
        Value r = s.run("list_paths", json("{}"));
        for (const auto& v : r.get("result").get("paths").array)
            if (v.get("id").string() == id) return v;
        return Value();
    };
    // an area around Link (sea room 44, at 1, 2, 3 on the ground at 5): a square of 800
    Value r = s.run("add_path", json("{}"));
    CHECK(ok(r) && r.get("result").get("id").string() == "area");
    Value a = path_of("area");
    CHECK(a.get("closed").boolean && a.get("stage").string() == "sea" && a.get("room").number == 44 && a.get("points").array.size() == 4);
    CHECK(a.get("points").array[0].get("x").number == -399 && a.get("points").array[0].get("y").number == 5 && a.get("points").array[2].get("z").number == 403);
    CHECK(host.path_applies == 1 && host.last_paths.size() == 1 && host.last_paths[0].id == "area" && host.last_paths[0].closed);
    CHECK(a.get("used_by").array.empty());
    // a second one gets its own id; a line from given points; a duplicate id is refused
    CHECK(ok(s.run("add_path", json("{}"))) && path_of("area-2").type == Value::Object);
    r = s.run("add_path", json(R"({"id": "walk", "closed": false, "points": [{"x": 0, "y": 0, "z": 0}, {"x": 300, "y": 0, "z": 0}]})"));
    CHECK(ok(r) && !path_of("walk").get("closed").boolean && path_of("walk").get("points").array.size() == 2);
    CHECK(!ok(s.run("add_path", json(R"({"id": "walk"})"))));
    CHECK(ok(s.run("add_path", json(R"({"id": "drop", "closed": false, "points": [{"x": 0, "y": 0, "z": 0}, {"x": 300, "y": 0, "z": 0, "release": true}]})"))) &&
          path_of("drop").get("points").array[1].get("release").boolean);
    // a position in another room (said with stage and room)
    CHECK(ok(s.run("add_path", json(R"({"id": "dungeon", "at": "position", "position": {"x": 0, "y": 0, "z": 0}, "stage": "M_NewD2", "room": 2, "size": 400})"))));
    CHECK(path_of("dungeon").get("stage").string() == "M_NewD2" && path_of("dungeon").get("room").number == 2 && path_of("dungeon").get("points").array[1].get("x").number == 200);
    // points: added after point 1, moved, removed; an area keeps three points
    r = s.run("add_path_point", json(R"({"id": "area", "at": "position", "position": {"x": 500, "y": 9, "z": -500}, "after": 1})"));
    CHECK(ok(r) && r.get("result").get("point").number == 2 && path_of("area").get("points").array.size() == 5 && path_of("area").get("points").array[1].get("x").number == 500);
    CHECK(!ok(s.run("add_path_point", json(R"({"id": "area", "after": 9})"))));
    CHECK(ok(s.run("move_path_point", json(R"({"id": "area", "point": 2, "position": {"x": 450, "y": 9, "z": -600}})"))) && path_of("area").get("points").array[1].get("z").number == -600);
    CHECK(!ok(s.run("move_path_point", json(R"({"id": "area", "point": 6, "position": {"x": 0, "y": 0, "z": 0}})"))));
    // a moved point that makes the area's edges cross is refused by the validator; nothing changes
    r = s.run("move_path_point", json(R"({"id": "area", "point": 2, "position": {"x": 0, "y": 5, "z": 2000}})"));
    CHECK(!ok(r) && r.get("error").string().find("cross") != std::string::npos && path_of("area").get("points").array[1].get("z").number == -600);
    CHECK(ok(s.run("remove_path_point", json(R"({"id": "area", "point": 2})"))) && path_of("area").get("points").array.size() == 4);
    CHECK(ok(s.run("remove_path_point", json(R"({"id": "area", "point": 1})"))));
    r = s.run("remove_path_point", json(R"({"id": "area", "point": 1})"));
    CHECK(!ok(r) && r.get("error").string().find("at least 3") != std::string::npos);
    // open and closed; a line of two points can't be an area
    CHECK(ok(s.run("set_path", json(R"({"id": "area", "closed": false})"))) && !path_of("area").get("closed").boolean);
    CHECK(!ok(s.run("set_path", json(R"({"id": "walk", "closed": true})"))));
    // the game's own area test
    r = s.run("path_inside", json(R"({"id": "area-2", "at": "position", "position": {"x": 100, "y": 0, "z": 100}, "stage": "sea", "room": 44})"));
    CHECK(ok(r) && r.get("result").get("inside").boolean);
    r = s.run("path_inside", json(R"({"id": "area-2", "at": "position", "position": {"x": 900, "y": 0, "z": 100}, "stage": "sea", "room": 44})"));
    CHECK(ok(r) && !r.get("result").get("inside").boolean);
    CHECK(!ok(s.run("path_inside", json(R"({"id": "walk"})"))));
    // renamed, removed, undone
    CHECK(ok(s.run("set_path", json(R"({"id": "area-2", "new_id": "yard"})"))) && path_of("yard").type == Value::Object && path_of("area-2").type == Value::Null);
    CHECK(ok(s.run("remove_path", json(R"({"id": "yard"})"))) && path_of("yard").type == Value::Null);
    const int before = host.path_applies;
    CHECK(ok(s.run("undo", json("{}"))) && path_of("yard").type == Value::Object && host.path_applies == before + 1);
    CHECK(!ok(s.run("remove_path", json(R"({"id": "nothing"})"))));
    // a wall cannon: placed with a new firing area in front of it (Link faces angle 100: ahead is +z),
    // or on an area the mod has (options.path); its path follows a rename; removing a path it uses is
    // refused with its name; a line or a missing path is refused
    r = s.run("add_actor", json(R"({"type": "wall_cannon", "id": "cannon"})"));
    CHECK(ok(r) && actor_of(s, "cannon")->get("path").string() == "cannon-area");
    Value area = path_of("cannon-area");
    CHECK(area.get("closed").boolean && area.get("room").number == 44 && area.get("points").array.size() == 4 && area.get("used_by").array.size() == 1 &&
          area.get("used_by").array[0].string() == "cannon");
    CHECK(area.get("points").array[0].get("z").number > 3 && area.get("points").array[2].get("z").number > 1000);  // in front of it
    r = s.run("remove_path", json(R"({"id": "cannon-area"})"));
    CHECK(!ok(r) && r.get("error").string().find("used by cannon") != std::string::npos && path_of("cannon-area").type == Value::Object);
    CHECK(ok(s.run("set_path", json(R"({"id": "cannon-area", "new_id": "fort"})"))) && actor_of(s, "cannon")->get("path").string() == "fort");
    CHECK(!ok(s.run("set_path", json(R"({"id": "fort", "closed": false})"))));  // a cannon's path stays an area
    CHECK(ok(s.run("add_actor", json(R"({"type": "wall_cannon", "id": "cannon2", "options": {"path": "fort"}})"))) && actor_of(s, "cannon2")->get("path").string() == "fort");
    CHECK(path_of("fort").get("used_by").array.size() == 2 && path_of("cannon2-area").type == Value::Null);
    CHECK(!ok(s.run("add_actor", json(R"({"type": "wall_cannon", "id": "cannon3", "options": {"path": "walk"}})"))));  // a line
    CHECK(!ok(s.run("add_actor", json(R"({"type": "wall_cannon", "id": "cannon4", "options": {"path": "dungeon"}})"))));  // another room
    CHECK(!ok(s.run("add_actor", json(R"({"type": "chuchu", "id": "cc", "options": {"path": "fort"}})"))));
    CHECK(!ok(s.run("set_actor", json(R"({"id": "cannon", "property": "path", "value": null})"))));
    CHECK(host.last_paths.size() >= 1);
    bool declared = false;
    for (const auto& a : host.last_actors)
        if (a.id == "cannon") declared = a.room_path == "fort";
    CHECK(declared);
    CHECK(ok(s.run("remove_actor", json(R"({"id": "cannon", "confirm": true})"))) && ok(s.run("remove_actor", json(R"({"id": "cannon2", "confirm": true})"))));
    CHECK(ok(s.run("remove_path", json(R"({"id": "fort"})"))));
    // a warship on a route: placed with a route around it; the anchored one has none
    r = s.run("add_actor", json(R"({"type": "warship", "id": "ship", "options": {"variant": "route"}})"));
    CHECK(ok(r) && actor_of(s, "ship")->get("path").string() == "ship-route" && path_of("ship-route").get("points").array.size() == 4 &&
          path_of("ship-route").get("points").array[0].get("x").number == -1499);
    CHECK(ok(s.run("add_actor", json(R"({"type": "warship", "id": "ship2"})"))) && actor_of(s, "ship2")->get("path").type == Value::Null);
    r = s.run("add_actor", json(R"({"type": "floormaster", "id": "hand", "options": {"variant": "wandering", "path": "walk"}})"));
    CHECK(ok(r) && actor_of(s, "hand")->get("path").string() == "walk");
    CHECK(editor::room_path_use(*actor_of(s, "ship")).find("round and round") != std::string::npos && editor::room_path_use(*actor_of(s, "ship2")).empty());
    // the warning checks: a Floormaster's route on the floor or off it, a warship's route over land or sea
    host.has_ground = true;
    host.ground_height = 5;
    r = s.run("check_path", json(R"({"id": "walk"})"));
    CHECK(ok(r) && r.get("result").get("for").string() == "floormaster" && r.get("result").get("segments").array.size() == 2 && r.get("result").get("segments").array[0].get("ok").boolean);
    host.ground_height = 300;
    r = s.run("check_path", json(R"({"id": "walk"})"));
    CHECK(ok(r) && !r.get("result").get("segments").array[0].get("ok").boolean && r.get("result").get("segments").array[0].get("problem").string().find("floor") != std::string::npos);
    r = s.run("check_path", json(R"({"id": "ship-route"})"));
    CHECK(ok(r) && r.get("result").get("for").string() == "warship" && r.get("result").get("segments").array[0].get("problem").string() == "land");
    host.has_ground = false;
    r = s.run("check_path", json(R"({"id": "ship-route"})"));
    CHECK(ok(r) && r.get("result").get("segments").array[0].get("ok").boolean);
    host.has_ground = true;
    host.ground_height = 5;
    // a Kargaroc with a rider: placed with a flight route whose third point lets the rider go; the mark stays
    // when the point moves; set_path_point moves it
    r = s.run("add_actor", json(R"({"type": "kargaroc", "id": "bird", "options": {"variant": "moblin_rider"}})"));
    CHECK(ok(r) && actor_of(s, "bird")->get("path").string() == "bird-route" && path_of("bird-route").get("points").array[2].get("release").boolean);
    CHECK(ok(s.run("move_path_point", json(R"({"id": "bird-route", "point": 3, "position": {"x": 900, "y": 5, "z": 900}})"))) && path_of("bird-route").get("points").array[2].get("release").boolean);
    CHECK(ok(s.run("set_path_point", json(R"({"id": "bird-route", "point": 1, "release": true})"))) && path_of("bird-route").get("points").array[0].get("release").boolean);
    CHECK(ok(s.run("set_path_point", json(R"({"id": "bird-route", "point": 3, "release": false})"))) && path_of("bird-route").get("points").array[2].get("release").type == Value::Null);
    r = s.run("set_path_point", json(R"({"id": "bird-route", "point": 1, "release": false})"));
    CHECK(!ok(r) && r.get("error").string().find("release") != std::string::npos);  // its last release point
    // a Wizzrobe summoner: placed with its path and that path's next path (its summon points); the next path
    // can't go while a path names it
    r = s.run("add_actor", json(R"({"type": "wizzrobe", "id": "wiz", "options": {"variant": "summoner_chuchus"}})"));
    CHECK(ok(r) && actor_of(s, "wiz")->get("path").string() == "wiz-route" && path_of("wiz-route").get("next").string() == "wiz-route-spots");
    r = s.run("remove_path", json(R"({"id": "wiz-route-spots"})"));
    CHECK(!ok(r) && r.get("error").string().find("next path") != std::string::npos);
    CHECK(ok(s.run("set_path", json(R"({"id": "wiz-route-spots", "new_id": "spots"})"))) && path_of("wiz-route").get("next").string() == "spots");
    CHECK(!ok(s.run("set_path", json(R"({"id": "wiz-route", "next": null})"))));  // a summoner's path keeps its next path
    CHECK(ok(s.run("remove_actor", json(R"({"id": "wiz", "confirm": true})"))));
    CHECK(editor::room_path_use(json(R"({"actor": "wall_cannon"})")).find("firing area") != std::string::npos && editor::room_path_use(json(R"({"actor": "chuchu"})")).empty());
}

void route_commands() {
    // an actor's route: points at Link, the camera or a position (on the ground), moved, reordered and
    // removed, the settings; one undo step each; the villagers without a walk are refused with the reason
    FakeHost host;
    editor::Session s(&host);
    s.add_package("editor-test", "Editor test", "", json(kManifest));
    Value r = s.run("add_actor", json(R"({"type": "resident", "id": "walker", "at": "position", "position": {"x": 0, "y": 5, "z": 0}, "stage": "sea", "room": 44, "options": {"resident": "uo2"}})"));
    CHECK(ok(r));
    const auto path = [&]() { return actor_of(s, "walker")->get("path"); };
    r = s.run("add_route_point", json(R"({"id": "walker", "at": "position", "position": {"x": 100, "y": 50, "z": 0}})"));
    CHECK(ok(r) && r.get("result").get("point").number == 1);
    CHECK(path().get("points").array.size() == 1 && path().get("points").array[0].get("y").number == 5 && path().get("mode").string() == "once");
    CHECK(path().get("speed").type == Value::Null);  // a resident: its own pace
    CHECK(host.last_changed.size() == 1 && host.last_changed[0] == "walker" && !host.last_actors.empty());
    bool walks = false;
    for (const auto& a : host.last_actors)
        if (a.id == "walker") walks = a.path.points.size() == 1 && !a.path.speed_given;
    CHECK(walks);
    // at Link (1, 2, 3 -> ground 5), at the camera (off: refused), after point 0 (first)
    CHECK(ok(s.run("add_route_point", json(R"({"id": "walker"})"))) && path().get("points").array.size() == 2 && path().get("points").array[1].get("x").number == 1);
    CHECK(!ok(s.run("add_route_point", json(R"({"id": "walker", "at": "camera"})"))));
    host.camera_on = true;
    r = s.run("add_route_point", json(R"({"id": "walker", "at": "camera", "after": 0})"));
    CHECK(ok(r) && r.get("result").get("point").number == 1 && path().get("points").array[0].get("x").number == 100 && path().get("points").array.size() == 3);
    // move, reorder, remove
    CHECK(ok(s.run("move_route_point", json(R"({"id": "walker", "point": 2, "position": {"x": 200, "y": 5, "z": 50}})"))) && path().get("points").array[1].get("x").number == 200);
    CHECK(!ok(s.run("move_route_point", json(R"({"id": "walker", "point": 4, "position": {"x": 200, "y": 5, "z": 50}})"))));
    CHECK(ok(s.run("reorder_route_point", json(R"({"id": "walker", "point": 3, "to": 1})"))) && path().get("points").array[0].get("x").number == 1 && path().get("points").array[2].get("x").number == 200);
    CHECK(ok(s.run("remove_route_point", json(R"({"id": "walker", "point": 1})"))) && path().get("points").array.size() == 2);
    // settings: speed within half to twice its own pace (uo2: 30 units a second), mode, pauses; null: its own pace
    CHECK(ok(s.run("set_route", json(R"({"id": "walker", "speed": 45, "mode": "back_and_forth", "pauses": true})"))));
    CHECK(path().get("speed").number == 45 && path().get("mode").string() == "back_and_forth" && path().get("pauses").boolean);
    CHECK(!ok(s.run("set_route", json(R"({"id": "walker", "speed": 90})"))) && path().get("speed").number == 45);  // refused: twice is 60
    CHECK(!ok(s.run("set_route", json(R"({"id": "walker", "mode": "circle"})"))));
    CHECK(!ok(s.run("set_route", json(R"({"id": "walker", "walk": "run"})"))));  // a resident walks with its own walk
    CHECK(ok(s.run("set_route", json(R"({"id": "walker", "speed": null, "pauses": false})"))) && path().get("speed").type == Value::Null && path().get("pauses").type == Value::Null);
    // undo: one step each
    CHECK(ok(s.run("undo")) && path().get("speed").number == 45);
    CHECK(ok(s.run("redo")) && path().get("speed").type == Value::Null);
    // the ground under each segment: start -> 1 -> 2 (once): two segments on the ground at y 5
    r = s.run("check_route", json(R"({"id": "walker"})"));
    CHECK(ok(r) && r.get("result").get("segments").array.size() == 2 && r.get("result").get("segments").array[0].get("ok").boolean);
    CHECK(r.get("result").get("segments").array[0].get("from").number == 0 && r.get("result").get("segments").array[1].get("to").number == 2);
    host.has_ground = false;
    r = s.run("check_route", json(R"({"id": "walker"})"));
    CHECK(ok(r) && !r.get("result").get("segments").array[0].get("ok").boolean && r.get("result").get("segments").array[0].get("problem").string() == "no ground");
    host.has_ground = true;
    CHECK(ok(s.run("set_route", json(R"({"id": "walker", "mode": "loop"})"))));
    r = s.run("check_route", json(R"({"id": "walker"})"));
    CHECK(ok(r) && r.get("result").get("segments").array.size() == 3 && r.get("result").get("segments").array[2].get("to").number == 0);
    CHECK(!ok(s.run("route_status", json(R"({"id": "walker"})"))));  // this host has no game: no live route
    // the last point removed: no route; remove_route
    CHECK(ok(s.run("remove_route_point", json(R"({"id": "walker", "point": 1})"))) && path().get("points").array.size() == 1);
    r = s.run("remove_route_point", json(R"({"id": "walker", "point": 1})"));
    CHECK(ok(r) && r.get("result").get("route_removed").boolean && path().type == Value::Null);
    CHECK(!ok(s.run("set_route", json(R"({"id": "walker", "speed": 30})"))));  // no route yet
    CHECK(ok(s.run("undo")) && path().type == Value::Object);
    CHECK(ok(s.run("remove_route", json(R"({"id": "walker"})"))) && path().type == Value::Null);
    // a villager without a walk of its own walks with a borrowed one; the seated man and a sign: refused with the reason
    CHECK(ok(s.run("add_actor", json(R"({"type": "resident", "id": "chatter", "at": "position", "position": {"x": 300, "y": 5, "z": 0}, "stage": "sea", "room": 44, "options": {"resident": "ub1"}})"))));
    r = s.run("add_route_point", json(R"({"id": "chatter", "at": "position", "position": {"x": 400, "y": 5, "z": 0}})"));
    CHECK(ok(r) && actor_of(s, "chatter")->get("path").get("points").array.size() == 1);
    CHECK(ok(s.run("set_route", json(R"({"id": "chatter", "speed": 72, "mode": "back_and_forth", "pauses": true})"))));
    CHECK(!ok(s.run("set_route", json(R"({"id": "chatter", "speed": 80})"))));  // twice her pace of 36 at most
    CHECK(ok(s.run("add_actor", json(R"({"type": "resident", "id": "seated", "options": {"resident": "ym1"}})"))));
    CHECK(!ok(s.run("add_route_point", json(R"({"id": "seated"})"))));
    CHECK(ok(s.run("add_actor", json(R"({"type": "sign", "id": "board"})"))));
    r = s.run("add_route_point", json(R"({"id": "board"})"));
    CHECK(!ok(r) && r.get("error").string().find("only residents, Bokoblins") != std::string::npos);
    // a Bokoblin patrols: its route starts at its position, back and forth by default, loop allowed,
    // never its own speed or pauses
    CHECK(ok(s.run("add_actor", json(R"({"type": "bokoblin", "id": "patrol", "options": {"variant": "shield"}})"))));
    CHECK(ok(s.run("add_route_point", json(R"({"id": "patrol", "at": "position", "position": {"x": 400, "y": 50, "z": 0}})"))));
    CHECK(actor_of(s, "patrol")->get("path").get("mode").string() == "back_and_forth" && actor_of(s, "patrol")->get("path").get("points").array.size() == 1);
    CHECK(ok(s.run("set_route", json(R"({"id": "patrol", "mode": "loop"})"))) && actor_of(s, "patrol")->get("path").get("mode").string() == "loop");
    CHECK(!ok(s.run("set_route", json(R"({"id": "patrol", "mode": "once"})"))) && actor_of(s, "patrol")->get("path").get("mode").string() == "loop");
    CHECK(!ok(s.run("set_route", json(R"({"id": "patrol", "speed": 100})"))) && !ok(s.run("set_route", json(R"({"id": "patrol", "pauses": true})"))));
    CHECK(editor::route_refusal(*actor_of(s, "patrol")).empty());
    CHECK(ok(s.run("add_actor", json(R"({"type": "moblin", "id": "brute"})"))) && ok(s.run("add_route_point", json(R"({"id": "brute", "at": "position", "position": {"x": 500, "y": 50, "z": 0}})"))));
    CHECK(actor_of(s, "brute")->get("path").get("mode").string() == "back_and_forth" && !ok(s.run("set_route", json(R"({"id": "brute", "speed": 50})"))));
    CHECK(ok(s.run("add_actor", json(R"({"type": "keese", "id": "bat"})"))) && !ok(s.run("add_route_point", json(R"({"id": "bat"})"))));
    // the pure helpers the Inspector uses
    CHECK(editor::resident_pace(*actor_of(s, "walker")) == 30 && editor::resident_pace(*actor_of(s, "chatter")) == 36 && editor::route_refusal(*actor_of(s, "walker")).empty() &&
          editor::route_refusal(*actor_of(s, "chatter")).empty());
}

// Island buildings (commands_game_pieces.cpp): listed under Buildings with this mod's state, add_actor "piece:<id>"
// (requires, yaw only, scale 1, no other field), preparation started once ("preparing…"), validation and undo.
struct PieceHost : FakeHost {
    std::map<std::string, std::string> states;
    int prepared = 0;
    bool prepare_piece(const std::string& mod, const std::string& id, std::string& note, std::string& why) override {
        (void)why;
        ++prepared;
        states[mod + "/" + id] = "preparing";
        note = "preparing";
        return true;
    }
    std::string piece_state(const std::string& mod, const std::string& id) override {
        auto found = states.find(mod + "/" + id);
        return found == states.end() ? "" : found->second;
    }
};
void game_pieces() {
    PieceHost host;
    editor::Session s(&host);
    s.add_package("editor-test", "Editor test", "", json(kManifest));
    Value r = s.run("list_game_objects", json(R"({"category": "buildings"})"));
    const auto& pieces = story_game_pieces::catalogue();
    size_t buildings = 0, parts = 0, islands = 0;
    for (const auto& p : pieces.pieces) buildings += p.category == "Buildings", parts += p.category == "Island parts", islands += p.category == "Islands";
    CHECK(ok(r) && r.get("result").get("objects").array.size() == buildings && buildings > 0);
    // island parts (rocks, a torii, walkways, a windmill cap, shipwrecks) are their own category
    Value kit = s.run("list_game_objects", json(R"({"category": "island parts"})"));
    CHECK(ok(kit) && kit.get("result").get("objects").array.size() == parts && parts >= 10);
    bool listed = false;
    for (const auto& c : kit.get("result").get("categories").array) listed |= c.string() == "Island parts";
    CHECK(listed);
    for (const auto& o : r.get("result").get("objects").array) {
        CHECK(o.get("type").string().rfind("piece:", 0) == 0 && o.get("building").boolean && o.get("state").string().empty() && !o.get("preparing").boolean);
        CHECK(o.get("scale").get("min").number == 1 && o.get("scale").get("max").number == 1 && o.get("variants").array.empty() && o.get("memory_bytes").number > 0);
    }
    r = s.run("list_game_objects", json(R"({"query": "lookout tower"})"));
    CHECK(ok(r) && r.get("result").get("objects").array.size() >= 1);
    // add: the manifest names the version it needs; preparation starts once
    r = s.run("add_actor", json(R"({"type": "piece:outset-house-small"})"));
    CHECK(ok(r) && r.get("result").get("id").string() == "outset-house-small" && r.get("result").get("state").string() == "preparing" && host.prepared == 1);
    const Value* a = actor_of(s, "outset-house-small");
    CHECK(a && a->get("actor").string() == "piece:outset-house-small" && a->get("scale").type == Value::Null && a->get("variant").type == Value::Null);
    CHECK(s.manifest("editor-test")->get("requires").get("game_objects").number == pieces.find("outset-house-small")->since);
    r = s.run("add_actor", json(R"({"type": "piece:outset-house-small", "id": "house-2"})"));
    CHECK(ok(r) && host.prepared == 1 && r.get("result").get("note").string().find("preparing") != std::string::npos);
    r = s.run("list_game_objects", json(R"({"query": "small outset house"})"));
    CHECK(ok(r) && r.get("result").get("objects").array.size() == 1 && r.get("result").get("objects").array[0].get("preparing").boolean);
    CHECK(!ok(s.run("add_actor", json(R"({"type": "piece:outset-castle"})"))));
    CHECK(!ok(s.run("add_actor", json(R"({"type": "piece:outset-house-small", "id": "Bad Id"})"))));
    // a turn about the vertical axis only, no scale, no other field
    CHECK(ok(s.run("rotate_actor", json(R"({"id": "house-2", "rotation": {"x": 0, "y": 90, "z": 0}})"))));
    CHECK(!ok(s.run("rotate_actor", json(R"({"id": "house-2", "rotation": {"x": 10, "y": 90, "z": 0}})"))));
    CHECK(!ok(s.run("scale_actor", json(R"({"id": "house-2", "scale": {"x": 1.5, "y": 1.5, "z": 1.5}})"))));
    CHECK(!ok(s.run("set_actor", json(R"({"id": "house-2", "property": "lines", "value": "hello"})"))));
    CHECK(ok(s.run("validate")));
    while (actor_of(s, "outset-house-small")) CHECK(ok(s.run("undo")));
    CHECK(s.manifest("editor-test")->get("requires").get("game_objects").type == Value::Null);
    // island grounds: their own category, each with a distant view; placed at the sea's level under Link (y 0)
    Value isles = s.run("list_game_objects", json(R"({"category": "islands"})"));
    CHECK(ok(isles) && isles.get("result").get("objects").array.size() == islands && islands == 6);
    bool isles_listed = false;
    for (const auto& c : isles.get("result").get("categories").array) isles_listed |= c.string() == "Islands";
    CHECK(isles_listed);
    for (const auto& o : isles.get("result").get("objects").array)
        CHECK(o.get("island").boolean && o.get("building").boolean && o.get("collision").boolean && pieces.find(o.get("id").string())->distant_triangles > 0);
    r = s.run("add_actor", json(R"({"type": "piece:crescent-moon-island"})"));
    CHECK(ok(r) && r.get("result").get("state").string() == "preparing");
    a = actor_of(s, "crescent-moon-island");
    CHECK(a && a->get("position").get("y").number == 0 && a->get("position").get("x").number == 1 && a->get("stage").string() == "sea");
    CHECK(s.manifest("editor-test")->get("requires").get("game_objects").number == pieces.find("crescent-moon-island")->since);
    CHECK(ok(s.run("validate")));
    while (actor_of(s, "crescent-moon-island")) CHECK(ok(s.run("undo")));
    // a failed preparation shows the reason and the building as unavailable
    host.states["editor-test/outset-well-shelter"] = "failed: the room archive is not in your game files";
    r = s.run("list_game_objects", json(R"({"query": "well shelter"})"));
    CHECK(ok(r) && !r.get("result").get("objects").array[0].get("available").boolean &&
          r.get("result").get("objects").array[0].get("why").string().find("not in your game files") != std::string::npos);
}

// ---------------------------------------------------------------- the water tool (commands_water.cpp)
// The ground: 400 west of x 1500, a cliff down to 100 east of it (above the sea: ground under it is the sea); the room's own ground reaches up to 1000.
struct WaterHost : FakeHost {
    std::vector<Value> prepared_models;
    float room_top = 1000;
    int grounds_asked = 0;
    bool ground(const editor::WorldPoint& above, float& h, std::string& why) override {
        if (!has_ground) return why = "no ground below", false;
        h = above.x < 1500 ? 400.0f : 100.0f;
        ++grounds_asked;
        return true;
    }
    bool room_height(const std::string& stage, int room, float& low, float& high, std::string& why) override {
        if (stage != "sea" || room != 11) return why = "unknown room", false;
        low = -900, high = room_top;
        return true;
    }
    bool prepare_model(const std::string&, const std::string&, const Value& model, std::string& note, std::string&) override {
        prepared_models.push_back(model);
        note = "prepared";
        return true;
    }
};
size_t water_files(const fs::path& dir) {
    size_t n = 0;
    std::error_code ec;
    if (fs::is_directory(dir / "models" / "water", ec))
        for (const auto& e : fs::directory_iterator(dir / "models" / "water")) n += e.path().extension() == ".glb";
    return n;
}
void water_tool() {
    WaterHost host;
    editor::Session s(&host);
    const fs::path dir = temp_dir("water");
    fs::create_directories(dir);
    s.add_package("editor-test", "Editor test", dir.string(), json(kManifest));
    const char* pond = R"({"points": [{"x": 600, "y": 100, "z": -201800}, {"x": 1400, "z": -201800}, {"x": 1400, "z": -201100}, {"x": 600, "z": -201100}], "level": 400, "depth": 300, "rim": 20, "foam": true})";
    // check: nothing changes; its place, room, counts; the ground was asked at each point of the (rounded) outline and
    // five places beyond it
    Value r = s.run("check_water", [&] { Value v = json(pond); v["kind"] = "pond"; return v; }());
    CHECK(ok(r) && r.get("result").get("ok").boolean && r.get("result").get("problems").array.empty() && s.undo_depth() == 0 && water_files(dir) == 0);
    CHECK(r.get("result").get("room").number == 11 && r.get("result").get("position").get("y").number == 400 && r.get("result").get("position").get("x").number == 1000);
    CHECK(r.get("result").get("surface_triangles").number > 10 && r.get("result").get("material").string() == "pond");
    CHECK(int(r.get("result").get("ground_samples").number) % 6 == 0 && r.get("result").get("ground_samples").number >= 16 * 6);
    // the known limit: above the room's highest ground the game doesn't find the water (an error, said in red)
    host.room_top = 350;
    r = s.run("check_water", [&] { Value v = json(pond); v["kind"] = "pond"; return v; }());
    CHECK(ok(r) && !r.get("result").get("ok").boolean && r.get("result").get("problems").array[0].get("field").string() == "level" &&
          r.get("result").get("problems").array[0].get("message").string().find("won't find this water") != std::string::npos);
    CHECK(!ok(s.run("add_pond", json(pond))) && s.undo_depth() == 0);
    host.room_top = 1000;
    // what is in the way (the island's protected places) is an error too
    host.query_clear = false;
    r = s.run("check_water", [&] { Value v = json(pond); v["kind"] = "pond"; return v; }());
    CHECK(ok(r) && !r.get("result").get("ok").boolean && r.get("result").get("problems").array[0].get("message").string() == "in the way: protected event");
    host.query_clear = true;
    // a crossing outline, a pond off the sea
    CHECK(!ok(s.run("add_pond", json(R"({"points": [[0,-201000],[800,-200200],[800,-201000],[0,-200200]], "level": 300})"))));
    CHECK(!ok(s.run("add_pond", json(R"({"points": [[0,-201000],[800,-201000],[800,-200200]], "level": 300, "stage": "M_NewD2"})"))));
    // add: two generated models in the package, a static world object with col_water and the water surface; one undo step
    r = s.run("add_pond", json(pond));
    CHECK(ok(r) && r.get("result").get("id").string() == "pond" && s.undo_depth() == 1 && water_files(dir) == 2);
    const Value* a = actor_of(s, "pond");
    CHECK(a && a->get("behaviour").string() == "static" && a->get("shoreline").get("material").string() == "pond" && a->get("collision").get("mesh").string() == a->get("model").string());
    CHECK(a && a->get("position").get("y").number == 400 && a->get("room").number == 11 && a->get("stage").string() == "sea");
    const std::string main = a ? a->get("model").string() : "", surface = a ? a->get("shoreline").get("model").string() : "";
    CHECK(main.rfind("pond-m", 0) == 0 && surface.rfind("pond-s", 0) == 0 && s.manifest("editor-test")->get("models").array.size() == 2);
    CHECK(fs::is_regular_file(dir / "models" / "water" / (main + ".glb")));
    // both prepared for the game: the main model with collision, the surface as a water shoreline
    CHECK(host.prepared_models.size() == 2 && host.prepared_models[0].get("collision").boolean && host.prepared_models[1].get("shoreline").string() == "pond");
    // get_water: the parameters back in world units
    r = s.run("get_water", json(R"({"id": "pond"})"));
    CHECK(ok(r) && r.get("result").get("kind").string() == "pond" && r.get("result").get("level").number == 400 && r.get("result").get("depth").number == 300 &&
          r.get("result").get("points").array.size() == 4 && r.get("result").get("points").array[1].get("x").number == 1400 && r.get("result").get("foam").boolean);
    // edit: built again where it stands; new models, the old ones leave the manifest (their files stay for undo); unmoved
    // points keep their ground (not asked again)
    const int asked = host.grounds_asked;
    r = s.run("edit_water", json(R"({"id": "pond", "depth": 200, "floor": "#335577"})"));
    CHECK(ok(r) && s.undo_depth() == 2 && host.grounds_asked == asked && water_files(dir) == 3);  // the surface is the same file
    a = actor_of(s, "pond");
    CHECK(a && a->get("model").string() != main && a->get("shoreline").get("model").string() == surface && s.manifest("editor-test")->get("models").array.size() == 2);
    r = s.run("get_water", json(R"({"id": "pond"})"));
    CHECK(ok(r) && r.get("result").get("depth").number == 200 && r.get("result").get("floor").string() == "#335577");
    // a moved actor: its water moves with it (the level is its height)
    CHECK(ok(s.run("move_actor", json(R"({"id": "pond", "position": {"x": 1100, "y": 380, "z": -201450}})"))));
    r = s.run("get_water", json(R"({"id": "pond"})"));
    CHECK(ok(r) && r.get("result").get("level").number == 380 && r.get("result").get("points").array[0].get("x").number == 700);
    // undo: back to the first build, its models again; redo
    CHECK(ok(s.run("undo")) && ok(s.run("undo")));
    a = actor_of(s, "pond");
    CHECK(a && a->get("model").string() == main);
    r = s.run("get_water", json(R"({"id": "pond"})"));
    CHECK(ok(r) && r.get("result").get("depth").number == 300);
    CHECK(ok(s.run("redo")) && actor_of(s, "pond")->get("model").string() != main);
    // copy: the copy shares the models until it is edited
    r = s.run("duplicate_actor", json(R"({"id": "pond", "new_id": "pond-b", "offset": {"x": 0, "y": 0, "z": 1500}})"));
    CHECK(ok(r) && actor_of(s, "pond-b")->get("model").string() == actor_of(s, "pond")->get("model").string());
    CHECK(ok(s.run("edit_water", json(R"({"id": "pond-b", "rim": 0})"))) && actor_of(s, "pond-b")->get("model").string() != actor_of(s, "pond")->get("model").string());
    CHECK(s.manifest("editor-test")->get("models").array.size() == 4);
    // eight models at most: a third body of water would make ten
    r = s.run("check_water", json(R"({"kind": "pond", "points": [[600,-200000],[1400,-200000],[1000,-199500]], "level": 300})"));
    CHECK(ok(r) && r.get("result").get("ok").boolean);
    CHECK(ok(s.run("add_pond", json(R"({"points": [[600,-200000],[1400,-200000],[1000,-199500]], "level": 300})"))));
    r = s.run("check_water", json(R"({"kind": "pond", "points": [[600,-199000],[1400,-199000],[1000,-198500]], "level": 300})"));
    CHECK(ok(r) && r.get("result").get("ok").boolean);  // 6 + 2 = 8
    CHECK(ok(s.run("add_pond", json(R"({"id": "pond-d", "points": [[600,-199000],[1400,-199000],[1000,-198500]], "level": 300})"))));
    r = s.run("check_water", json(R"({"kind": "pond", "points": [[600,-198000],[1400,-198000],[1000,-197500]], "level": 300})"));
    CHECK(ok(r) && !r.get("result").get("ok").boolean && r.get("result").get("problems").array[0].get("field").string() == "models");
    // removing a body of water takes its models along (once nothing uses them)
    CHECK(ok(s.run("remove_actor", json(R"({"id": "pond-d", "confirm": true})"))) && s.manifest("editor-test")->get("models").array.size() == 6);
    CHECK(ok(s.run("remove_actor", json(R"({"id": "pond-b", "confirm": true})"))) && s.manifest("editor-test")->get("models").array.size() == 4);
    CHECK(ok(s.run("remove_actor", json(R"({"id": "pond-2", "confirm": true})"))) && s.manifest("editor-test")->get("models").array.size() == 2);

    // a river: west of x 1500 the ground is at 400, east of it at 100: a step at the cliff, where a waterfall goes
    const char* river = R"({"points": [{"x": 300, "z": -200500}, {"x": 1200, "z": -200500}, {"x": 2400, "z": -200500}], "width": 300, "depth": 120, "shallow": [true, false]})";
    r = s.run("check_water", [&] { Value v = json(river); v["kind"] = "river"; return v; }());
    CHECK(ok(r) && r.get("result").get("ok").boolean && r.get("result").get("steps").array.size() == 1 && r.get("result").get("material").string() == "river");
    CHECK(ok(r) && r.get("result").get("levels").array.front().number == 430);  // the shallow section: 30 above its bed
    r = s.run("add_river", json(river));
    CHECK(ok(r) && actor_of(s, "river") && actor_of(s, "river")->get("shoreline").get("material").string() == "river");
    r = s.run("get_water", json(R"({"id": "river"})"));
    CHECK(ok(r) && r.get("result").get("steps").array.size() == 1 && r.get("result").get("shallow").array[0].boolean && !r.get("result").get("shallow").array[1].boolean);
    const Value step = r.get("result").get("steps").array[0];
    CHECK(step.get("top").number == 430 && step.get("bottom").number == 220);
    // the waterfall at its step: the top edge across the river, falling downstream to the ground below
    r = s.run("check_water", json(R"({"kind": "waterfall", "river": "river", "step": 1})"));
    CHECK(ok(r) && r.get("result").get("ok").boolean && r.get("result").get("position").get("y").number == 430 && r.get("result").get("material").string() == "waterfall");
    CHECK(ok(r) && r.get("result").get("minimum").get("y").number <= 100);
    CHECK(!ok(s.run("check_water", json(R"({"kind": "waterfall", "river": "river", "step": 2})"))));
    CHECK(ok(s.run("add_waterfall", json(R"({"river": "river", "step": 1})"))) && actor_of(s, "waterfall"));
    r = s.run("get_water", json(R"({"id": "waterfall"})"));
    CHECK(ok(r) && r.get("result").get("side").number == -1 && r.get("result").get("bottom").number == 100 && r.get("result").get("top").number == 430);
    // a waterfall from two points on a cliff's top edge: it falls to the lower side by itself
    r = s.run("check_water", json(R"({"kind": "waterfall", "points": [{"x": 1490, "y": 400, "z": -199800}, {"x": 1490, "y": 400, "z": -199300}]})"));
    CHECK(ok(r) && r.get("result").get("ok").boolean && r.get("result").get("position").get("y").number == 400);
    CHECK(ok(r) && r.get("result").get("minimum").get("x").number > 1440);  // its splash pad lies east (the lower side)
    // a body of water that isn't one: edit_water refuses it
    CHECK(!ok(s.run("edit_water", json(R"({"id": "sign-a", "depth": 100})"))));
    CHECK(!ok(s.run("get_water", json(R"({"id": "sign-a"})"))));
    fs::remove_all(dir);
}

// A pond on the top terrace of the mod's own island (its world object, standing in the open sea): three square
// terraces round (1000, -201000) with tops at 600, 400 and 200 (1000, 2000 and 3000 wide), the sea floor at -3000
// around them. The banks reach out beyond the top terrace (down to the next one, and beyond the island to the sea
// floor); the water's level is the ground at its shore, and the check sees the pond's own collision.
struct TerraceHost : FakeHost {
    std::vector<story_world_collision::mesh::Mesh> checked;
    story_notebook::WorldPoint checked_at;
    bool ground(const editor::WorldPoint& above, float& h, std::string&) override {
        const float d = std::max(std::fabs(above.x - 1000), std::fabs(above.z + 201000));
        h = d <= 500 ? 600.0f : d <= 1000 ? 400.0f : d <= 1500 ? 200.0f : -3000.0f;
        if (h > above.y) h = -3000;  // asked from below a terrace's top: the floor (not reached here)
        return true;
    }
    bool room_height(const std::string&, int, float& low, float& high, std::string&) override { return low = -3000, high = 1200, true; }
    bool prepare_model(const std::string&, const std::string&, const Value&, std::string& note, std::string&) override { return note = "prepared", true; }
    bool world_object_query_with(const std::string&, const story_notebook::ManifestActor& actor, const story_world_collision::mesh::Mesh& collision, Value& result,
                                 std::string&) override {
        checked.push_back(collision);
        checked_at = actor.position;
        result["clear"] = true;
        result["conflicts"].type = Value::Array;
        return true;
    }
};
void pond_on_terrace() {
    TerraceHost host;
    editor::Session s(&host);
    const fs::path dir = temp_dir("water-terrace");
    fs::create_directories(dir);
    s.add_package("editor-test", "Editor test", dir.string(), json(kManifest));
    // picked on the top terrace (y 600), no level of its own
    const char* pond = R"({"points": [{"x": 750, "y": 600, "z": -201250}, {"x": 1250, "y": 600, "z": -201250}, {"x": 1250, "y": 600, "z": -200750}, {"x": 750, "y": 600, "z": -200750}]})";
    Value r = s.run("check_water", [&] { Value v = json(pond); v["kind"] = "pond"; return v; }());
    const Value& c = r.get("result");
    // the level: its depth (200) above the top terrace (not above the lower terraces or the sea floor the banks reach
    // out to): deep enough to swim in, standing on the terrace
    CHECK(ok(r) && c.get("ok").boolean && c.get("position").get("y").number == 800);
    // its body: the floor on the terrace, the banks a mound ending at the terrace's edge (not down to the sea floor)
    CHECK(c.get("minimum").get("y").number >= 590 && c.get("maximum").get("y").number <= 840);
    // the placement check got the pond's own collision where it will stand: its open water at the level, its floor
    // and banks in their surfaces
    CHECK(host.checked.size() == 1 && host.checked_at.x == 1000 && host.checked_at.y == 800 && host.checked_at.z == -201000);
    if (!host.checked.empty()) {
        const auto& m = host.checked[0];
        float lowest = 1e9f;
        for (const auto& v : m.vertices) lowest = std::min(lowest, v.y);
        CHECK(!m.vertices.empty() && lowest == m.minimum.y);
        size_t water = 0, floor = 0;
        for (const auto& t : m.triangles) {
            if (t.semantic == story_world_collision::mesh::SurfaceWater) {
                ++water;
                for (auto i : t.indices) CHECK(m.vertices[i].y == 0);
            }
            floor += t.semantic == 3;
        }
        CHECK(water > 4 && floor > 4 && m.minimum.y <= -200 && m.minimum.y >= -215);  // the floor, the banks just into the terrace
    }
    // made: the same level; a raised level (the water deep enough to swim in, its banks a mound on the terrace) too
    r = s.run("add_pond", json(pond));
    CHECK(ok(r) && actor_of(s, "pond") && actor_of(s, "pond")->get("position").get("y").number == 800);
    // a deeper pond stands higher; a level of its own (at the terrace's height) is kept
    r = s.run("check_water", [&] { Value v = json(pond); v["kind"] = "pond"; v["depth"] = 300; return v; }());
    CHECK(ok(r) && r.get("result").get("ok").boolean && r.get("result").get("position").get("y").number == 900);
    r = s.run("check_water", [&] { Value v = json(pond); v["kind"] = "pond"; v["level"] = 600; return v; }());
    CHECK(ok(r) && r.get("result").get("ok").boolean && r.get("result").get("position").get("y").number == 600);
    // a shore partly over the sea (the island's edge, a harbour): the sea's surface is its lowest ground there, not the
    // sea floor, so the pond stands its depth above the water instead of hanging down to the floor
    r = s.run("check_water", json(R"({"kind": "pond", "points": [{"x": 2300, "z": -201150}, {"x": 2700, "z": -201150}, {"x": 2700, "z": -200850}, {"x": 2300, "z": -200850}]})"));
    CHECK(ok(r) && r.get("result").get("position").get("y").number == 200 && r.get("result").get("minimum").get("y").number >= -10);
    // what is in the way says which of the island's places it is: its game name and where it is
    CHECK(editor::conflict_words(json(R"({"kind": "event", "message": "a cutscene area of the island", "native": "TagEv", "at": {"x": -671.4, "y": 115, "z": -198464}, "reach": 300})")) ==
          "a cutscene area of the island [TagEv] at x -671, y 115, z -198464");
    CHECK(editor::conflict_words(json(R"({"kind": "world_object", "message": "this mod's world object"})")) == "this mod's world object");
    fs::remove_all(dir);
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
    world_object_commands();
    pond_on_terrace();
    floating_world_object_commands();
    grapple_world_object_commands();
    island_world_object_commands();
    command_layer();
    handle_drag();
    locations();
    records();
    package_files(argv[1]);
    live_kinds_note();
    building(argv[1]);
    game_objects();
    route_commands();
    path_commands();
    game_pieces();
    island_and_sea_placement();
    stage_tables();
    ray_pick();
    water_tool();
    if (g_failures) {
        std::fprintf(stderr, "editor_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("editor_test: ok");
    return 0;
}
