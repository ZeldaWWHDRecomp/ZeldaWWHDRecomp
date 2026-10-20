// World editor: letters on their own, treasure charts and items through the command layer
// (runtime/src/editor/commands_story.cpp): add / set / remove / list with undo and redo, the
// validator's refusals (missing fields, limits, duplicates), removing something still used, item icons
// copied into the package, chart places from Link or the fly camera, the problem list, search and the
// quest graph.
// Usage: editor_story_test
#include "editor/commands.h"
#include "editor/quests.h"
#include "mods/story_items_core.h"
#include "mods/story_mail_book.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

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
    editor::Place link_place, camera_place;
    FakeHost() {
        link_place.valid = true;
        link_place.stage = "sea";
        link_place.room = 11;
        link_place.position = {1759.04f, 1100, -202572.26f};
        camera_place = link_place;
        camera_place.position = {120, 3000, -201000};
    }
    bool apply_actors(const std::string&, const std::vector<story_notebook::ManifestActor>&, const std::vector<std::string>&, std::string&,
                      std::string&) override {
        return true;
    }
    bool reload_room(std::string&) override { return true; }
    std::vector<std::pair<std::string, std::vector<int>>> stage_rooms() override { return {{"sea", {11}}}; }
    bool warp(const editor::Location&, std::string&) override { return true; }
    editor::Place link() override { return link_place; }
    editor::Place camera() override { return camera_place; }
    bool busy(std::string&) override { return false; }
    bool fly(bool, bool, std::string&) override { return true; }
    Value fly_state() override { return Value(); }
    bool set_test_mode(bool, std::string&) override { return true; }
    bool test_mode() override { return false; }
    bool reset_records(const std::string&, const std::vector<editor::Record>&, const std::vector<editor::Record>&, unsigned&, std::string&) override {
        return true;
    }
    bool test_state(bool, int, const std::string&, const std::vector<editor::Record>&, std::string&) override { return true; }
};

Value json(const char* text) { return mods::json::parse(text); }
bool ok(const Value& r) { return r.get("ok").boolean; }
std::string error(const Value& r) { return r.get("error").string(); }
bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

const char* kManifest = R"({
  "format_version": 1, "id": "story-test", "name": "Story test", "version": "1.0.0", "kind": "content",
  "quests_format": 1,
  "quests": [{"id": "lighthouse", "title": "The lighthouse",
    "letter": {"id": "invite", "sender": "The keeper", "text": "Come to Windfall."},
    "steps": [{"n": 1, "text": "Find the keeper's treasure."}, {"n": 2, "text": "Bring the lamp back."}]}],
  "models": [{"id": "lamp-model", "source": "models/lamp.glb", "kind": "static"}],
  "actors": [
    {"id": "keeper", "actor": "sign", "stage": "sea", "room": 11, "position": {"x": 10, "y": 1040, "z": -201500},
     "flag": "keeper-read", "lines": ["Take this."]}
  ]
})";

// a PNG's head: the signature and an IHDR of w x h (all the editor's check reads)
void write_png(const fs::path& to, uint32_t w, uint32_t h) {
    unsigned char b[33] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
    for (int i = 0; i < 4; i++) b[16 + i] = uint8_t(w >> (24 - 8 * i)), b[20 + i] = uint8_t(h >> (24 - 8 * i));
    b[24] = 8, b[25] = 6;
    std::ofstream f(to, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b), sizeof b);
}

void letters_charts_items(const fs::path& tmp) {
    std::error_code ec;
    fs::remove_all(tmp, ec);
    const fs::path pkg = tmp / "pkg", outside = tmp / "outside";
    fs::create_directories(pkg / "models", ec);
    fs::create_directories(outside, ec);
    { std::ofstream(pkg / "models" / "lamp.glb") << "glb"; }
    { std::ofstream(pkg / "manifest.json") << kManifest; }
    write_png(outside / "lamp.png", 128, 128);
    write_png(outside / "lamp2.png", 64, 64);
    write_png(outside / "huge.png", 4096, 16);
    { std::ofstream(outside / "not.png") << "not a png at all, really"; }

    FakeHost host;
    editor::Session s(&host);
    s.add_package("story-test", "Story test", pkg.string(), json(kManifest));
    const Value start = *s.manifest("story-test");
    auto m = [&]() { return *s.manifest("story-test"); };
    auto run = [&](const char* command, const char* params) { return s.run(command, json(params)); };

    // ---- items
    Value r = run("add_item", R"({"name": "Storm lamp", "description": "Shows the way."})");
    CHECK(!ok(r) && has(error(r), "icon"));
    r = run("add_item", (R"({"name": "Storm lamp", "description": "Shows the way.", "icon_file": ")" + (outside / "not.png").generic_string() + "\"}").c_str());
    CHECK(!ok(r) && has(error(r), "not a PNG"));
    r = run("add_item", (R"({"name": "Storm lamp", "description": "Shows the way.", "icon_file": ")" + (outside / "huge.png").generic_string() + "\"}").c_str());
    CHECK(!ok(r) && has(error(r), "2048"));
    r = run("add_item", R"({"name": "Storm lamp", "description": "Shows the way.", "icon": "icons/none.png"})");
    CHECK(!ok(r) && has(error(r), "not in the package"));
    r = run("add_item", R"({"name": "Storm lamp", "description": "Shows the way.", "icon": "../x.png"})");
    CHECK(!ok(r) && has(error(r), "PNG in the package"));
    CHECK(m() == start && s.undo_depth() == 0);
    r = run("add_item", (R"({"name": "Storm lamp", "description": "Shows the way.", "stack": 3, "use": {"action": "show"}, "icon_file": ")" +
                         (outside / "lamp.png").generic_string() + "\"}").c_str());
    CHECK(ok(r));
    CHECK(r.get("result").get("id").string() == "storm-lamp" && r.get("result").get("icon").string() == "icons/storm-lamp.png");
    CHECK(fs::is_regular_file(pkg / "icons" / "storm-lamp.png"));
    CHECK(m().get("items").array.size() == 1 && m().get("items").array[0].get("stack").number == 3);
    // validation: the parser's own limits and references
    r = run("add_item", R"({"id": "storm-lamp", "name": "Lamp", "description": "x", "icon": "icons/storm-lamp.png"})");
    CHECK(!ok(r) && has(error(r), "already"));
    r = run("add_item", R"({"id": "long", "name": "A name that is far longer than forty bytes allows", "description": "x", "icon": "icons/storm-lamp.png"})");
    CHECK(!ok(r) && has(error(r), "40 bytes"));
    r = run("add_item", R"({"id": "Bad Id", "name": "Lamp", "description": "x", "icon": "icons/storm-lamp.png"})");
    CHECK(!ok(r) && has(error(r), "id"));
    r = run("add_item", R"({"id": "key", "name": "Key", "description": "x", "icon": "icons/storm-lamp.png", "use": {"action": "quest_step", "quest": "nope", "step": 1}})");
    CHECK(!ok(r) && has(error(r), "nope"));
    r = run("add_item", R"({"id": "key", "name": "Key", "description": "x", "icon": "icons/storm-lamp.png", "use": {"action": "show"}, "held_model": "missing"})");
    CHECK(!ok(r) && has(error(r), "held_model"));
    r = run("add_item", R"({"id": "key", "name": "Key", "description": "x", "icon": "icons/storm-lamp.png", "use": {"action": "quest_step", "quest": "lighthouse", "step": 2},
                            "held_model": "lamp-model", "held_frames": 60})");
    CHECK(ok(r));
    r = run("set_item", R"({"id": "key", "fields": {"stack": 0}})");
    CHECK(!ok(r) && has(error(r), "stack"));
    r = run("set_item", R"({"id": "key", "fields": {"colour": "red"}})");
    CHECK(!ok(r) && has(error(r), "not a field"));
    r = run("set_item", R"({"id": "key", "fields": {"name": null}})");
    CHECK(!ok(r) && has(error(r), "keeps its name"));
    r = run("set_item", R"({"id": "key", "fields": {"icon": null}})");
    CHECK(!ok(r) && has(error(r), "keeps its icon"));
    r = run("set_item", R"({"id": "nobody", "fields": {"stack": 2}})");
    CHECK(!ok(r) && has(error(r), "no item"));
    r = run("set_item", R"({"id": "key", "fields": {"held_model": null, "held_frames": null, "stack": 2}})");
    CHECK(ok(r) && m().get("items").array[1].get("held_model").type == Value::Null && m().get("items").array[1].get("stack").number == 2);
    // a new icon: a different picture gets a name of its own (an undo still finds the old one)
    r = run("set_item", (R"({"id": "storm-lamp", "icon_file": ")" + (outside / "lamp2.png").generic_string() + "\"}").c_str());
    CHECK(ok(r) && r.get("result").get("icon").string() == "icons/storm-lamp-2.png" && fs::is_regular_file(pkg / "icons" / "storm-lamp.png"));
    CHECK(ok(s.run("undo")) && m().get("items").array[0].get("icon").string() == "icons/storm-lamp.png");
    CHECK(ok(s.run("redo")) && m().get("items").array[0].get("icon").string() == "icons/storm-lamp-2.png");
    r = s.run("list_items");
    CHECK(ok(r) && r.get("result").array.size() == 2 && r.get("result").array[0].get("icon_ok").boolean);

    // ---- charts
    r = run("add_chart", R"({"name": "Old map"})");
    CHECK(!ok(r) && has(error(r), "place"));
    r = run("add_chart", R"({"name": "Old map", "place": {"x": 400000, "z": 0}})");
    CHECK(!ok(r) && has(error(r), "off the sea"));
    r = run("add_chart", R"({"name": "Old map", "at": "link", "item": "item:none"})");
    CHECK(!ok(r) && has(error(r), "none"));
    r = run("add_chart", R"({"name": "Old map", "at": "link", "item": "game:triforce"})");
    CHECK(!ok(r) && has(error(r), "game:"));
    r = run("add_chart", R"({"name": "Old map", "description": "An X marks the lighthouse.", "at": "link", "item": "item:storm-lamp"})");
    CHECK(ok(r) && r.get("result").get("id").string() == "old-map");
    const Value chart = m().get("charts").array[0];
    CHECK(chart.get("place").get("x").number == 1759 && chart.get("place").get("z").number == -202572.3);
    r = run("add_chart", R"({"id": "old-map", "name": "Old map", "place": {"x": 0, "z": 0}})");
    CHECK(!ok(r) && has(error(r), "already"));
    // eight at most
    for (int i = 2; i <= 8; i++)
        CHECK(ok(s.run("add_chart", json(("{\"id\": \"c" + std::to_string(i) + "\", \"name\": \"C\", \"place\": {\"x\": 0, \"z\": 0}}").c_str()))));
    r = run("add_chart", R"({"id": "c9", "name": "C", "place": {"x": 0, "z": 0}})");
    CHECK(!ok(r) && has(error(r), "up to 8"));
    for (int i = 0; i < 7; i++) CHECK(ok(s.run("undo")));
    CHECK(m().get("charts").array.size() == 1);
    r = run("set_chart", R"({"id": "old-map", "at": "camera", "fields": {"description": null, "item": "game:heart_piece"}})");
    CHECK(ok(r));
    CHECK(m().get("charts").array[0].get("place").get("x").number == 120 && m().get("charts").array[0].get("description").type == Value::Null &&
          m().get("charts").array[0].get("item").string() == "game:heart_piece");
    r = run("set_chart", R"({"id": "old-map", "fields": {"place": null}})");
    CHECK(!ok(r) && has(error(r), "keeps its place"));
    r = run("set_chart", R"({"id": "old-map"})");
    CHECK(!ok(r) && has(error(r), "nothing to set"));
    host.camera_place.stage = "M_NewD2";
    r = run("set_chart", R"({"id": "old-map", "at": "camera"})");
    CHECK(!ok(r) && has(error(r), "not at sea"));
    CHECK(ok(s.run("undo")));  // the item back to the lamp, the description back
    CHECK(m().get("charts").array[0].get("item").string() == "item:storm-lamp" && m().get("charts").array[0].get("description").type == Value::String);
    r = s.run("list_charts");
    CHECK(ok(r) && r.get("result").array[0].get("island").string() == "Windfall Island");

    // ---- letters
    r = run("add_letter", R"({"text": "Hello."})");
    CHECK(!ok(r) && has(error(r), "sender"));
    r = run("add_letter", R"({"sender": "A sailor", "text": "Hello.", "encloses": ["chart:nowhere"]})");
    CHECK(!ok(r) && has(error(r), "nowhere"));
    r = run("add_letter", R"({"sender": "A sailor", "text": "Hello.", "encloses": ["notebook"]})");
    CHECK(!ok(r) && has(error(r), "enclose"));
    r = run("add_letter", R"({"id": "invite", "sender": "A sailor", "text": "Hello."})");
    CHECK(!ok(r) && has(error(r), "lighthouse"));
    r = run("add_letter", R"({"sender": "", "text": "Hello."})");
    CHECK(!ok(r) && has(error(r), "sender or text"));
    r = run("add_letter", R"({"sender": "A sailor", "text": "Dear Link,\nI found this old map in a bottle.", "encloses": ["chart:old-map", "item:key"]})");
    CHECK(ok(r) && r.get("result").get("id").string() == "letter");
    r = run("set_letter", R"({"id": "letter", "fields": {"text": "Dear Link,\nA map for you.", "encloses": null}})");
    CHECK(ok(r) && m().get("letters").array[0].get("encloses").type == Value::Null);
    r = run("set_letter", R"({"id": "letter", "fields": {"title": "x"}})");
    CHECK(!ok(r) && has(error(r), "not a field"));
    CHECK(ok(s.run("undo")) && m().get("letters").array[0].get("encloses").array.size() == 2);
    r = s.run("list_letters");
    CHECK(ok(r) && r.get("result").array.size() == 1 && r.get("result").array[0].get("sender").string() == "A sailor");

    // ---- the uses: an actor gives the item, a step finds the chart, a scene salvages it
    CHECK(ok(run("set_then", R"({"id": "keeper", "actions": [{"action": "give", "id": "item:storm-lamp", "count": 1}]})")));
    CHECK(ok(run("set_actor", R"({"id": "keeper", "property": "when", "value": {"quest": "lighthouse", "step": 2, "item": "item:key"}})")));
    CHECK(ok(run("edit_quest_step", R"({"quest": "lighthouse", "step": 1, "fields": {"find": "chart:old-map"}})")));
    CHECK(ok(run("add_scene", R"({"id": "treasure"})")));
    CHECK(ok(run("set_scene_trigger", R"({"scene": "treasure", "type": "salvage", "chart": "old-map"})")));
    r = s.run("list_charts");
    const std::string chart_uses = mods::json::dump(r.get("result").array[0].get("used_by"));
    CHECK(has(chart_uses, "letter letter > encloses") && has(chart_uses, "quest lighthouse > steps > step 1 > find") &&
          has(chart_uses, "scene treasure > trigger (salvage)"));
    // removing what is still used: confirm is needed, then the uses are listed and nothing changes
    const Value before = m();
    r = run("remove_chart", R"({"id": "old-map"})");
    CHECK(!ok(r) && has(error(r), "confirm"));
    r = run("remove_chart", R"({"id": "old-map", "confirm": true})");
    CHECK(!ok(r) && has(error(r), "still used") && has(error(r), "scene treasure > trigger (salvage)") && has(error(r), "letter letter"));
    r = run("remove_item", R"({"id": "storm-lamp", "confirm": true})");
    CHECK(!ok(r) && has(error(r), "actor keeper > then") && has(error(r), "chart old-map > item"));
    r = run("remove_item", R"({"id": "key", "confirm": true})");
    CHECK(!ok(r) && has(error(r), "actor keeper > when > item") && has(error(r), "letter letter > encloses"));
    CHECK(m() == before);
    // the references gone, the removes work (and undo brings them back)
    CHECK(ok(run("remove_scene", R"({"id": "treasure", "confirm": true})")));
    CHECK(ok(run("edit_quest_step", R"({"quest": "lighthouse", "step": 1, "fields": {"find": null}})")));
    CHECK(ok(run("set_letter", R"({"id": "letter", "fields": {"encloses": ["item:key"]}})")));
    CHECK(ok(run("remove_chart", R"({"id": "old-map", "confirm": true})")) && m().get("charts").type == Value::Null);
    CHECK(ok(s.run("undo")) && m().get("charts").array.size() == 1);
    CHECK(ok(s.run("redo")) && m().get("charts").type == Value::Null);
    r = run("remove_letter", R"({"id": "letter"})");
    CHECK(!ok(r) && has(error(r), "confirm"));
    CHECK(ok(run("remove_letter", R"({"id": "letter", "confirm": true})")) && m().get("letters").type == Value::Null);
    r = run("remove_letter", R"({"id": "letter", "confirm": true})");
    CHECK(!ok(r) && has(error(r), "no letter"));
    // the history names each step
    std::string history = mods::json::dump(s.run("history").get("result"));
    CHECK(has(history, "Add item storm-lamp") && has(history, "Remove chart old-map") && has(history, "Remove letter letter"));
    // and the command log has them, as any command
    bool logged = false;
    for (const auto& e : s.log()) logged = logged || (e.command == "remove_letter" && e.result.get("ok").boolean);
    CHECK(logged);
    // everything undone: the package as it was
    while (s.undo_depth()) CHECK(ok(s.run("undo")));
    CHECK(m() == start);
    fs::remove_all(tmp, ec);
}

// The problem list names the item, chart or letter a refused package is about, and a missing icon.
void problems() {
    editor::Session s;
    Value bad = json(kManifest);
    bad["items"] = json(R"([{"id": "lamp", "name": "Lamp", "description": "x", "icon": "icons/lamp.png"}])");
    bad["letters"] = json(R"([{"id": "map-letter", "sender": "A sailor", "text": "Hi.", "encloses": ["chart:old-map"]}])");
    s.add_package("story-test", "Story test", "", bad);
    Value r = s.run("check_package");
    std::string all = mods::json::dump(r.get("result").get("problems"));
    CHECK(!r.get("result").get("valid").boolean && has(all, "\"kind\":\"letter\"") && has(all, "\"id\":\"map-letter\"") && has(all, "old-map"));
    // a package folder without the icon: the item's icon problem
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "wwhd-editor-story-problems";
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "models", ec);
    { std::ofstream(dir / "models" / "lamp.glb") << "glb"; }
    Value fine = json(kManifest);
    fine["items"] = bad["items"];
    editor::Session t;
    t.add_package("story-test", "Story test", dir.string(), fine);
    r = t.run("check_package");
    all = mods::json::dump(r.get("result").get("problems"));
    CHECK(has(all, "icons/lamp.png is not in the package") && has(all, "\"field\":\"icon\""));
    fs::remove_all(dir, ec);
}

// Search finds the charts, items and letters and where they are used; the quest graph shows them.
void search_and_graph() {
    editor::Session s;
    Value m = json(kManifest);
    m["items"] = json(R"([{"id": "lamp", "name": "Storm lamp", "description": "Shows the way.", "icon": "icons/lamp.png"}])");
    m["charts"] = json(R"([{"id": "old-map", "name": "Old map", "place": {"x": 0, "z": -200000}, "item": "item:lamp"}])");
    m["letters"] = json(R"([{"id": "bottle", "sender": "A sailor", "text": "A map in a bottle.", "encloses": ["chart:old-map"]}])");
    m["quests"].array[0]["letter"]["encloses"] = json(R"(["item:lamp"])");
    m["quests"].array[0]["reward"] = json(R"({"chart": "chart:old-map"})");
    m["quests"].array[0]["steps"].array[0]["find"] = "chart:old-map";
    m["actors"].array[0]["then"] = json(R"([{"action": "give", "id": "item:lamp"}, {"action": "quest_step", "id": "lighthouse", "step": 2}])");
    m["actors"].array[0]["when"] = json(R"({"quest": "lighthouse", "step": 1})");
    m["scenes"] = json(R"([{"id": "dive", "trigger": {"type": "salvage", "chart": "old-map"},
                            "steps": [{"type": "end", "then": [{"action": "quest_done", "id": "lighthouse"}]}]}])");
    s.add_package("story-test", "Story test", "", m);
    Value r = s.run("validate");
    CHECK(r.get("result").get("valid").boolean);
    if (!r.get("result").get("valid").boolean) std::fprintf(stderr, "  %s\n", r.get("result").get("problem").string().c_str());
    std::set<std::string> kinds;
    Value found = s.run("search", json(R"({"text": "old-map"})"));
    for (const auto& h : found.get("result").array) kinds.insert(h.get("kind").string() + ":" + h.get("id").string());
    CHECK(kinds.count("chart:old-map") && kinds.count("letter:bottle") && kinds.count("scene:dive") && kinds.count("quest:lighthouse") &&
          kinds.count("quest_step:lighthouse"));
    kinds.clear();
    found = s.run("search", json(R"({"text": "item:lamp"})"));
    for (const auto& h : found.get("result").array) kinds.insert(h.get("kind").string() + ":" + h.get("id").string());
    CHECK(kinds.count("actor:keeper") && kinds.count("chart:old-map"));
    kinds.clear();
    found = s.run("search", json(R"({"text": "bottle"})"));
    for (const auto& h : found.get("result").array) kinds.insert(h.get("kind").string() + ":" + h.get("id").string());
    CHECK(kinds.count("letter:bottle"));
    // the graph: the letter's enclosures, the find step, the actor that gives the item, the salvage scene, the reward chart
    r = s.run("quest_graph", json(R"({"quest": "lighthouse"})"));
    const std::string g = mods::json::dump(r.get("result"));
    CHECK(has(g, "encloses item:lamp") && has(g, "find chart:old-map") && has(g, "gives 1 lamp") && has(g, "salvage chart old-map: scene dive ends") &&
          has(g, "gives chart:old-map"));
    bool salvage_edge = false;
    for (const auto& e : r.get("result").get("edges").array)
        salvage_edge = salvage_edge || (e.get("via").get("id").string() == "dive" && e.get("from").string() == "step:1" && e.get("to").string() == "end");
    CHECK(salvage_edge);
    // the references helper says each use for people
    const auto refs = editor::story_references(m, "item", "lamp");
    CHECK(refs.size() == 3);
    CHECK(editor::story_references(m, "letter", "bottle").empty());
}

// The story format's gaps closed (letters that wait, charts from dialogue, items held up on receipt,
// resets that clear charts and items): the fields through the commands, with undo, the validator's
// refusals, references that follow, and the records reset_quest and test states cover.
struct RecordingHost : FakeHost {
    bool test = true;
    std::vector<editor::Record> own, shared;
    int days = 0;
    bool test_mode() override { return test; }
    bool reset_records(const std::string&, const std::vector<editor::Record>& o, const std::vector<editor::Record>& sh, unsigned&, std::string&) override {
        own = o, shared = sh;
        return true;
    }
    bool advance_days(int n, std::string&) override {
        days += n;
        return true;
    }
};
bool has_record(const std::vector<editor::Record>& list, uint8_t kind, uint16_t key) {
    return std::find(list.begin(), list.end(), editor::Record{kind, key}) != list.end();
}

void format_gaps(const fs::path& tmp) {
    std::error_code ec;
    fs::remove_all(tmp, ec);
    const fs::path pkg = tmp / "pkg";
    fs::create_directories(pkg / "models", ec);
    fs::create_directories(pkg / "icons", ec);
    { std::ofstream(pkg / "models" / "lamp.glb") << "glb"; }
    write_png(pkg / "icons" / "crystal.png", 64, 64);
    RecordingHost host;
    editor::Session s(&host);
    s.add_package("story-test", "Story test", pkg.string(), json(kManifest));
    auto m = [&]() { return *s.manifest("story-test"); };
    auto run = [&](const char* command, const char* params) { return s.run(command, json(params)); };
    Value r;

    // ---- items held up on receipt (hold_up needs held_model; held_model no longer needs a use with it)
    r = run("add_item", R"({"id": "crystal", "name": "Lamp crystal", "description": "It glows.", "icon": "icons/crystal.png", "hold_up": true})");
    CHECK(!ok(r) && has(error(r), "held_model"));
    r = run("add_item", R"({"id": "crystal", "name": "Lamp crystal", "description": "It glows.", "icon": "icons/crystal.png", "held_model": "lamp-model"})");
    CHECK(!ok(r) && has(error(r), "use action"));
    r = run("add_item", R"({"id": "crystal", "name": "Lamp crystal", "description": "It glows.", "icon": "icons/crystal.png", "held_model": "lamp-model", "hold_up": "yes"})");
    CHECK(!ok(r) && has(error(r), "boolean"));  // the command's own parameter check
    r = run("set_item", R"({"id": "storm", "fields": {"hold_up": "yes"}})");
    CHECK(!ok(r));
    r = run("add_item", R"({"id": "crystal", "name": "Lamp crystal", "description": "It glows.", "icon": "icons/crystal.png", "held_model": "lamp-model", "hold_up": true})");
    CHECK(ok(r) && m().get("items").array[0].get("hold_up").boolean);
    r = run("set_item", R"({"id": "crystal", "fields": {"hold_up": null}})");
    CHECK(!ok(r) && has(error(r), "use action"));  // the held model would be left without a use
    r = run("set_item", R"({"id": "crystal", "fields": {"hold_up": null, "held_model": null}})");
    CHECK(ok(r) && m().get("items").array[0].get("hold_up").type == Value::Null);
    CHECK(ok(s.run("undo")) && m().get("items").array[0].get("hold_up").boolean && m().get("items").array[0].get("held_model").string() == "lamp-model");

    // ---- charts from dialogue: give chart:<id> in an actor's then, a talk's then, a scene's end
    CHECK(ok(run("add_chart", R"({"id": "keeper-map", "name": "Keeper's map", "place": {"x": 1000, "z": -200000}})")));
    r = run("set_then", R"({"id": "keeper", "actions": [{"action": "give", "id": "chart:nowhere"}]})");
    CHECK(!ok(r) && has(error(r), "undeclared mod chart"));
    r = run("set_then", R"({"id": "keeper", "actions": [{"action": "give", "id": "chart:keeper-map", "count": 2}]})");
    CHECK(!ok(r) && has(error(r), "no count"));
    r = run("set_then", R"({"id": "keeper", "actions": [{"action": "take", "id": "chart:keeper-map"}]})");
    CHECK(!ok(r) && has(error(r), "can't be taken"));
    r = run("set_then", R"({"id": "keeper", "actions": [{"action": "give", "id": "chart:keeper-map"}, {"action": "give", "id": "item:crystal"},
                                                        {"action": "quest_step", "id": "lighthouse", "step": 2}]})");
    CHECK(ok(r) && m().get("actors").array[0].get("then").array[0].get("id").string() == "chart:keeper-map");
    r = run("set_actor", R"({"id": "keeper", "property": "talks", "value": [{"lines": ["Here."], "then": [{"action": "give", "id": "chart:keeper-map"}]}]})");
    CHECK(!ok(r));  // talks never together with lines or then
    CHECK(ok(run("add_scene", R"({"id": "thanks"})")));
    CHECK(ok(run("edit_scene_step", R"({"scene": "thanks", "step": 1, "fields": {"then": [{"action": "give", "id": "chart:keeper-map"}]}})")));
    r = s.run("list_charts");
    const std::string uses = mods::json::dump(r.get("result").array[0].get("used_by"));
    CHECK(has(uses, "actor keeper > then") && has(uses, "scene thanks > steps"));
    r = run("remove_chart", R"({"id": "keeper-map", "confirm": true})");
    CHECK(!ok(r) && has(error(r), "still used"));
    // the quest graph says it, and starting at step 2 the chart is Link's
    r = s.run("quest_graph", json(R"({"quest": "lighthouse"})"));
    CHECK(has(mods::json::dump(r.get("result")), "gives chart:keeper-map"));
    editor::StepStart start;
    std::string why;
    CHECK(editor::step_start("story-test", m(), "lighthouse", 2, "none", start, why));
    CHECK(start.charts == std::vector<std::string>{"keeper-map"});
    bool chart_record = false;
    for (const auto& v : editor::step_records("story-test", "lighthouse", start))
        chart_record = chart_record || (v.record.kind == story_mail::kRecordMarker && v.record.key == story_mail::letter_key("story-test", "keeper-map") && v.value == 1);
    CHECK(chart_record);
    for (const auto& [id, n] : start.items) CHECK(id != "keeper-map");  // a chart is no item

    // ---- letters that wait: when (a quest step or done, flags) and after_days
    r = run("add_letter", R"({"id": "thanks", "sender": "The keeper", "text": "The ships come home.", "when": {"quest": "nope", "done": true}})");
    CHECK(!ok(r) && has(error(r), "doesn't declare"));
    r = run("add_letter", R"({"id": "thanks", "sender": "The keeper", "text": "The ships come home.", "after_days": 0})");
    CHECK(!ok(r) && has(error(r), "1..255"));
    r = run("add_letter", R"({"id": "thanks", "sender": "The keeper", "text": "The ships come home.", "when": {"quest": "lighthouse", "done": true},
                              "after_days": 3, "encloses": ["chart:keeper-map"]})");
    CHECK(ok(r) && m().get("letters").array[0].get("after_days").number == 3 && m().get("letters").array[0].get("when").get("done").boolean);
    r = run("set_letter", R"({"id": "thanks", "fields": {"when": {"quest": "lighthouse", "step": 2, "flag": "keeper-read"}}})");
    CHECK(ok(r) && m().get("letters").array[0].get("when").get("step").number == 2);
    r = run("set_letter", R"({"id": "thanks", "fields": {"when": {"flag": "nobody-sets-it"}}})");
    CHECK(!ok(r) && has(error(r), "flag"));
    // the quest's steps move: the letter's step follows; a step or quest it waits for can't go
    CHECK(ok(run("move_quest_step", R"({"quest": "lighthouse", "step": 2, "to": 1})")));
    CHECK(m().get("letters").array[0].get("when").get("step").number == 1);
    CHECK(ok(s.run("undo")) && m().get("letters").array[0].get("when").get("step").number == 2);
    r = run("remove_quest_step", R"({"quest": "lighthouse", "step": 2, "confirm": true})");
    CHECK(!ok(r) && has(error(r), "letter thanks waits"));
    r = run("remove_quest", R"({"quest": "lighthouse", "confirm": true})");
    CHECK(!ok(r) && has(error(r), "letter thanks waits"));
    r = run("set_letter", R"({"id": "thanks", "fields": {"when": null, "after_days": null}})");
    CHECK(ok(r) && m().get("letters").array[0].get("when").type == Value::Null && m().get("letters").array[0].get("after_days").type == Value::Null);
    CHECK(ok(s.run("undo")) && m().get("letters").array[0].get("after_days").number == 3);
    // the calendar for testing: test mode only
    host.test = false;
    CHECK(!ok(run("advance_days", R"({"days": 2})")));
    host.test = true;
    CHECK(!ok(run("advance_days", R"({"days": 0})")));
    CHECK(ok(run("advance_days", R"({"days": 2})")) && host.days == 2);

    // ---- reset_quest: the charts and items Link got from the mod, its letters' waiting days, never another mod's
    Value other = json(kManifest);
    other["id"] = "other";
    other["charts"] = json(R"([{"id": "keeper-map", "name": "Other map", "place": {"x": 0, "z": 0}}])");
    s.add_package("other", "Other", "", other);
    CHECK(ok(run("reset_quest", R"({"mod": "story-test", "confirm": true})")));
    using namespace story_mail;
    const uint16_t chart = letter_key("story-test", "keeper-map"), item = letter_key("story-test", "crystal"), letter = letter_key("story-test", "thanks"),
                   quest = letter_key("story-test", "lighthouse");
    CHECK(has_record(host.own, kRecordMarker, chart));
    for (uint8_t kind : {story_items::kRecordItemCount, story_items::kRecordItemPlace, story_items::kRecordItemNumber, story_items::kRecordItemButtons})
        CHECK(has_record(host.own, kind, item));
    CHECK(has_record(host.own, kRecordLetterDayLow, letter) && has_record(host.own, kRecordLetterDayHigh, letter));
    CHECK(has_record(host.own, story_items::kRecordQuestRewardItem, quest));
    CHECK(has_record(host.shared, kRecordMarker, letter_key("other", "keeper-map")) && !has_record(host.own, kRecordMarker, letter_key("other", "keeper-map")));
    // everything undone: the package as it was
    while (s.undo_depth()) CHECK(ok(s.run("undo")));
    CHECK(m() == json(kManifest));
    fs::remove_all(tmp, ec);
}

// What a placed chest holds (set_actor parameters.item): the game items a chart chest can hold, or one of the
// mod's items; undo; an item a chest holds can't be removed.
void chest_contents() {
    FakeHost host;
    editor::Session s(&host);
    Value start = json(kManifest);
    start["items"] = json(R"([{"id": "crystal", "name": "Lamp crystal", "description": "It glows.", "icon": "icons/crystal.png"}])");
    start["actors"].array.push_back(json(R"({"id": "box", "actor": "chest", "stage": "sea", "room": 11, "position": {"x": 0, "y": 1040, "z": -201000}, "flag": "box-open"})"));
    s.add_package("story-test", "Story test", "", start);
    auto m = [&]() { return *s.manifest("story-test"); };
    auto box = [&]() { return m().get("actors").array[1]; };
    auto run = [&](const char* params) { return s.run("set_actor", json(params)); };
    Value r = run(R"({"id": "box", "property": "parameters", "value": {"item": "rupee_red"}})");
    CHECK(ok(r) && box().get("parameters").get("item").string() == "rupee_red");
    for (const char* item : {"rupee_silver", "heart_piece", "heart_container", "item:crystal"}) {
        r = s.run("set_actor", json((std::string(R"({"id": "box", "property": "parameters", "value": {"item": ")") + item + "\"}}").c_str()));
        CHECK(ok(r) && box().get("parameters").get("item").string() == item);
    }
    for (const char* bad : {"item:nothing", "triforce", "game:heart_piece"}) {
        r = s.run("set_actor", json((std::string(R"({"id": "box", "property": "parameters", "value": {"item": ")") + bad + "\"}}").c_str()));
        CHECK(!ok(r) && box().get("parameters").get("item").string() == "item:crystal");
    }
    CHECK(has(error(r), "chest item must be"));
    r = s.run("remove_item", json(R"({"id": "crystal", "confirm": true})"));
    CHECK(!ok(r) && has(error(r), "actor box > parameters > item"));
    // the sign can't hold anything
    r = run(R"({"id": "keeper", "property": "parameters", "value": {"item": "rupee_red"}})");
    CHECK(!ok(r));
    while (s.undo_depth()) CHECK(ok(s.run("undo")));
    CHECK(m() == start && box().get("parameters").type == Value::Null);
    CHECK(ok(s.run("redo")) && box().get("parameters").get("item").string() == "rupee_red");
}

}  // namespace

int main() {
    letters_charts_items(fs::temp_directory_path() / "wwhd-editor-story-test");
    problems();
    search_and_graph();
    format_gaps(fs::temp_directory_path() / "wwhd-editor-story-gaps-test");
    chest_contents();
    if (g_failures) {
        std::fprintf(stderr, "editor_story_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("editor_story_test: ok");
    return 0;
}
