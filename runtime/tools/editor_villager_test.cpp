// World editor: the game's own villagers (runtime/src/editor/commands.h). The NPC catalogue's mapping from
// a loaded resident (stage, room, resident number) to its catalogue id; list_villagers and pick_villager
// over the host's list; add_villager_talk as a new quest, a new step and a step's talk, each one undo step,
// with its refusals; and that only the open mod's manifest changes (the villager is the game's).
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "editor/commands.h"
#include "mods/story_manifest.h"
#include "mods/story_npc_catalogue.h"

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

// two villagers on Windfall: the man on the windmill (um1) and one more, as the game host lists them
const char* kVillagers = R"([
  {"id": "windfall-um1", "name": "Windmill man", "description": "the man up on the windmill by the light", "where": "Windfall Island",
   "stage": "sea", "room": 11, "position": {"x": 1000, "y": 900, "z": -198000}, "talks": true},
  {"id": "windfall-uo1", "name": "Old man", "description": "someone", "where": "Windfall Island",
   "stage": "sea", "room": 11, "position": {"x": 3000, "y": 100, "z": -198000}, "talks": true}
])";

struct FakeHost : editor::Host {
    bool has_villagers = true;
    bool apply_actors(const std::string&, const std::vector<story_notebook::ManifestActor>&, const std::vector<std::string>&, std::string& note,
                      std::string&) override {
        note = "";
        return true;
    }
    bool reload_room(std::string&) override { return true; }
    std::vector<std::pair<std::string, std::vector<int>>> stage_rooms() override { return {{"sea", {11}}}; }
    bool warp(const editor::Location&, std::string&) override { return true; }
    editor::Place link() override { return {}; }
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
    bool villagers(Value& out, std::string& why) override {
        if (!has_villagers) return why = "the story notebook is not loaded", false;
        out = json(kVillagers);
        return true;
    }
};

const char* kMod = R"({"format_version": 1, "id": "talks", "name": "Talks", "version": "0.1.0", "game_id": "wwhd", "kind": "content",
  "dependencies": [{"id": "story-notebook", "minimum_version": "0.0.0"}], "actors": [],
  "quests_format": 1, "quests": [{"id": "old", "title": "An old quest", "steps": [{"n": 1, "text": "First."}, {"n": 2, "text": "Second."}]}]})";

void catalogue_mapping() {
    const story_notebook::NpcCatalogue& c = story_notebook::npc_catalogue();
    const story_notebook::Resident um1 = c.find("windfall-um1");
    CHECK(um1 != story_notebook::kResidentNone);
    const story_notebook::NpcEntry* e = c.entry(um1);
    CHECK(e && e->talks());
    // the resident number of its people actor (Um1) where the recipe has it stands: on the sea, room 11
    const uint8_t number = uint8_t(story_notebook::people_number_of("Um1"));
    CHECK(c.people_at("sea", 11, number) == um1 && e->people_at("sea", 11, number));
    CHECK(c.people_at("sea", 12, number) == story_notebook::kResidentNone);  // another room
    CHECK(c.people_at("Opub", 11, number) == story_notebook::kResidentNone);  // another stage
    CHECK(c.people_at("sea", 11, uint8_t(story_notebook::people_number_of("Uo3"))) != um1);
    CHECK(c.people_at("sea", 11, 0xFE) == story_notebook::kResidentNone);  // no such resident
    // every people actor of the catalogue maps back to its own entry where it stands
    for (size_t i = 0; i < c.entries.size(); i++)
        for (const auto& a : c.entries[i].actors)
            if (a.actor_class == "people" && a.room >= 0) CHECK(c.people_at(a.stage, a.room, a.people_number) == story_notebook::Resident(i + 1));
}

void picking() {
    const Value list = json(kVillagers);
    // straight at the windmill man's middle from the south-west, and past him
    CHECK(editor::pick_villager(list, {1000, 980, -199000}, {0, 0, 1}, 20000) == 0);
    CHECK(editor::pick_villager(list, {1200, 980, -199000}, {0, 0, 1}, 20000) == -1);  // 200 units to the side
    CHECK(editor::pick_villager(list, {1000, 980, -199000}, {0, 0, 1}, 500) == -1);    // out of range
    CHECK(editor::pick_villager(list, {1000, 980, -199000}, {0, 0, -1}, 20000) == -1);  // behind the ray
    // from above, down onto the other one
    CHECK(editor::pick_villager(list, {3000, 2000, -198000}, {0, -1, 0}, 20000) == 1);
    // two in a row: the nearer one
    CHECK(editor::pick_villager(list, {0, 160, -198000}, {1, 0, 0}, 20000) == 1);
    const float s = std::sqrt(0.5f);
    CHECK(editor::pick_villager(list, {0, 980, -199000}, {s, 0, s}, 20000) == 0);
}

void commands() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("talks", "Talks", "", json(kMod));
    Value r = s.run("list_villagers");
    CHECK(ok(r) && r.get("result").array.size() == 2);
    r = s.run("pick_villager", json(R"({"origin": {"x": 1000, "y": 980, "z": -199000}, "direction": {"x": 0, "y": 0, "z": 2}})"));
    CHECK(ok(r) && r.get("result").get("id").string() == "windfall-um1");
    CHECK(!ok(s.run("pick_villager", json(R"({"origin": {"x": 1000, "y": 980, "z": -199000}, "direction": {"x": 0, "y": 0, "z": 0}})"))));
    CHECK(!ok(s.run("pick_villager", json(R"({"origin": {"x": 0, "y": 0, "z": 0}, "direction": {"x": 0, "y": 1, "z": 0}})"))));
    host.has_villagers = false;
    CHECK(!ok(s.run("list_villagers")));
    host.has_villagers = true;
    editor::Session no_game;
    CHECK(!ok(no_game.run("list_villagers")));

    const Value before = *s.manifest("talks");
    // refusals: nothing changes
    CHECK(!ok(s.run("add_villager_talk", json(R"({"who": "windfall-nobody", "lines": "Hi.", "title": "T"})"))));
    CHECK(!ok(s.run("add_villager_talk", json(R"({"who": "windfall-um1", "lines": "Hi."})"))));  // no quest, no title
    CHECK(!ok(s.run("add_villager_talk", json(R"({"who": "windfall-um1", "lines": [], "title": "T"})"))));
    CHECK(!ok(s.run("add_villager_talk", json(R"({"who": "windfall-um1", "lines": ["a", "b", "c", "d"], "title": "T"})"))));
    CHECK(!ok(s.run("add_villager_talk", json(R"({"who": "windfall-um1", "lines": "Hi.", "quest": "missing"})"))));
    CHECK(!ok(s.run("add_villager_talk", json(R"({"who": "windfall-um1", "lines": "Hi.", "quest": "old", "step": 9})"))));
    CHECK(!ok(s.run("add_villager_talk", json(R"({"who": "windfall-um1", "lines": "Hi.", "quest": "old", "title": "x"})"))));
    Value long_line;
    long_line["who"] = "windfall-um1";
    long_line["title"] = "T";
    long_line["lines"] = std::string(300, 'a');  // more than one message box: the validator refuses it
    CHECK(!ok(s.run("add_villager_talk", long_line)));
    CHECK(*s.manifest("talks") == before && s.undo_depth() == 0);

    // a new quest whose first step is the talk: one undo step
    r = s.run("add_villager_talk", json(R"({"who": "windfall-um1", "lines": "Welcome up here!", "title": "The windmill"})"));
    CHECK(ok(r) && r.get("result").get("quest").string() == "the-windmill" && r.get("result").get("step").number == 1);
    CHECK(s.undo_depth() == 1);
    Value q = s.run("get_quest", json(R"({"quest": "the-windmill"})")).get("result");
    CHECK(q.get("title").string() == "The windmill" && q.get("steps").array.size() == 1);
    const Value& npc = q.get("steps").array[0].get("npc");
    CHECK(npc.get("who").string() == "windfall-um1" && npc.get("lines").array.size() == 1 && npc.get("lines").array[0].string() == "Welcome up here!");
    CHECK(q.get("steps").array[0].get("text").string() == "Talk to Windmill man." ||
          q.get("steps").array[0].get("text").string().rfind("Talk to ", 0) == 0);
    // the manifest still passes the mod manager's quest rules (the catalogue knows him; he can talk)
    std::vector<story_notebook::ManifestQuest> quests;
    std::string why;
    CHECK(story_notebook::parse_quests(*s.manifest("talks"), quests, why));
    CHECK(ok(s.run("validate")) && s.run("validate").get("result").get("valid").boolean);
    // the same title again: a quest id of its own
    r = s.run("add_villager_talk", json(R"({"who": "windfall-um1", "lines": ["One.", "Two."], "title": "The windmill"})"));
    CHECK(ok(r) && r.get("result").get("quest").string() == "the-windmill-2");
    CHECK(ok(s.run("undo")) && s.manifest("talks")->get("quests").array.size() == 2);
    // a new last step of a quest
    r = s.run("add_villager_talk", json(R"({"who": "windfall-uo1", "lines": "Step three.", "quest": "old", "text": "Ask the old man."})"));
    CHECK(ok(r) && r.get("result").get("step").number == 3);
    q = s.run("get_quest", json(R"({"quest": "old"})")).get("result");
    CHECK(q.get("steps").array.size() == 3 && q.get("steps").array[2].get("text").string() == "Ask the old man." &&
          q.get("steps").array[2].get("npc").get("who").string() == "windfall-uo1");
    // a step's talk (replaces one it had)
    r = s.run("add_villager_talk", json(R"({"who": "windfall-um1", "lines": "On step one.", "quest": "old", "step": 1})"));
    CHECK(ok(r) && r.get("result").get("step").number == 1);
    q = s.run("get_quest", json(R"({"quest": "old"})")).get("result");
    CHECK(q.get("steps").array[0].get("npc").get("who").string() == "windfall-um1" && q.get("steps").array[0].get("text").string() == "First.");
    CHECK(s.undo_depth() == 3);
    // undo all of it: as before
    while (s.undo_depth()) CHECK(ok(s.run("undo")));
    CHECK(*s.manifest("talks") == before);
}
}  // namespace

int main() {
    catalogue_mapping();
    picking();
    commands();
    if (g_failures) {
        std::fprintf(stderr, "editor_villager_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("editor_villager_test: ok");
    return 0;
}
