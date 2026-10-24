// World editor: quests, quest talks, letters, charts and items while the game runs (story_reload.h,
// Session::apply_story). The pure rules (which saved step numbers name another step after an edit, which
// item changes wait for a restart), and that every story change, its undo and its redo hand the game the
// whole new manifest of that mod only, while scene and actor changes don't.
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "editor/commands.h"
#include "mods/story_reload.h"

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

Value quest_of(const char* steps) {
    return json((std::string(R"({"quests": [{"id": "q", "title": "Q", "steps": )") + steps + "}]}").c_str());
}
bool has_move(const std::vector<story_reload::StepMove>& m, unsigned saved, unsigned now) {
    for (const auto& x : m)
        if (x.quest == "q" && x.saved == saved && x.now == now) return true;
    return false;
}

void step_moves() {
    const Value three = quest_of(R"([{"n": 1, "text": "A"}, {"n": 2, "text": "B"}, {"n": 3, "text": "C"}])");
    // unchanged, or a step edited in place: nothing moves
    CHECK(story_reload::step_moves(three, three).empty());
    CHECK(story_reload::step_moves(three, quest_of(R"([{"n": 1, "text": "A"}, {"n": 2, "text": "B, edited"}, {"n": 3, "text": "C"}])")).empty());
    // a step inserted after 1: B and C are 3 and 4 now
    auto m = story_reload::step_moves(three, quest_of(R"([{"n": 1, "text": "A"}, {"n": 2, "text": "new"}, {"n": 3, "text": "B"}, {"n": 4, "text": "C"}])"));
    CHECK(m.size() == 2 && has_move(m, 2, 3) && has_move(m, 3, 4));
    // step 2 removed: C is 2 now; a Quest Log at 2 is at C, one at 3 points to nothing
    m = story_reload::step_moves(three, quest_of(R"([{"n": 1, "text": "A"}, {"n": 2, "text": "C"}])"));
    CHECK(m.size() == 2 && has_move(m, 2, 0) && has_move(m, 3, 2));
    // the last step removed
    m = story_reload::step_moves(three, quest_of(R"([{"n": 1, "text": "A"}, {"n": 2, "text": "B"}])"));
    CHECK(m.size() == 1 && has_move(m, 3, 0));
    // steps swapped
    m = story_reload::step_moves(three, quest_of(R"([{"n": 1, "text": "B"}, {"n": 2, "text": "A"}, {"n": 3, "text": "C"}])"));
    CHECK(m.size() == 2 && has_move(m, 1, 2) && has_move(m, 2, 1));
    // two steps with the same declaration: the number decides
    const Value twins = quest_of(R"([{"n": 1, "text": "A"}, {"n": 2, "text": "A"}])");
    CHECK(story_reload::step_moves(twins, twins).empty());
    // a quest removed or new: nothing listed (its records stay)
    CHECK(story_reload::step_moves(three, json(R"({"quests": []})")).empty());
    CHECK(story_reload::step_moves(json(R"({})"), three).empty());
}

void items_and_sections() {
    const Value started = json(R"({"items": [{"id": "lamp", "name": "Lamp", "icon": "icons/lamp.png"},
                                            {"id": "key", "name": "Key", "icon": "icons/key.png", "held_model": "key", "hold_up": true}]})");
    CHECK(story_reload::restart_items(started, started).empty());
    // texts change at once; an icon, a held model, hold_up, new and removed items wait for a restart
    Value next = json(R"({"items": [{"id": "lamp", "name": "Old lamp", "description": "It glows.", "icon": "icons/lamp.png", "stack": 3},
                                   {"id": "key", "name": "Key", "icon": "icons/key2.png", "held_model": "key", "hold_up": true},
                                   {"id": "map", "name": "Map", "icon": "icons/map.png"}]})");
    auto later = story_reload::restart_items(started, next);
    CHECK(later.size() == 2 && later[0] == "key (its icon)" && later[1] == "map (a new item)");
    CHECK(story_reload::entry_id(later[1]) == "map" && story_reload::entry_why(later[1]) == "a new item");
    CHECK(story_reload::live_items(started, next) == std::vector<std::string>{"lamp"});
    next = json(R"({"items": [{"id": "key", "name": "Key", "icon": "icons/key.png", "held_model": "key"}]})");
    later = story_reload::restart_items(started, next);
    CHECK(later.size() == 2 && later[0] == "key (its hold_up)" && later[1] == "lamp (removed)");
    CHECK(story_reload::live_items(started, next).empty());
    // live icon places (the editor's spare frames): a new item takes a spare place while there is one, an
    // icon change of an item with a place is live; removed items, held models and hold_up still wait
    {
        const Value with_new = json(R"({"items": [{"id": "lamp", "name": "Lamp", "icon": "icons/lamp2.png"},
                                                 {"id": "key", "name": "Key", "icon": "icons/key.png", "held_model": "key", "hold_up": true},
                                                 {"id": "map", "name": "Map", "icon": "icons/map.png"},
                                                 {"id": "rope", "name": "Rope", "icon": "icons/rope.png"}]})");
        story_reload::LiveIcons live;
        live.placed = [](const std::string& id) { return id == "lamp" || id == "key"; };
        live.spare = 1;
        auto l = story_reload::restart_items(started, with_new, live);
        CHECK(l.size() == 1 && l[0] == "rope (a new item: the spare icon places are all in use)");
        CHECK((story_reload::live_items(started, with_new, live) == std::vector<std::string>{"lamp", "key", "map"}));
        // "map" bound since: both new ones fit with one spare left
        live.placed = [](const std::string& id) { return id != "rope"; };
        CHECK(story_reload::restart_items(started, with_new, live).empty());
        const Value removed = json(R"({"items": [{"id": "key", "name": "Key", "icon": "icons/key.png", "held_model": "key2", "hold_up": true}]})");
        l = story_reload::restart_items(started, removed, live);
        CHECK(l.size() == 2 && l[0] == "key (its held model)" && l[1] == "lamp (removed)");
    }
    // the sections
    CHECK(!story_reload::story_changed(json(R"({"actors": [1], "scenes": [1]})"), json(R"({"actors": [2], "scenes": [2]})")));
    for (const char* k : {"quests", "letters", "charts", "items"}) {
        Value a, b;
        b[k] = json("[1]");
        CHECK(story_reload::story_changed(a, b));
    }
    // letters: each quest's letter and the letters on their own
    const Value m = json(R"({"quests": [{"id": "q", "letter": {"id": "start", "sender": "S", "text": "T"}}, {"id": "r"}],
                             "letters": [{"id": "thanks"}]})");
    CHECK((story_reload::letter_ids(m) == std::vector<std::string>{"start", "thanks"}));
    CHECK((story_reload::gone({"start", "thanks", "old"}, story_reload::letter_ids(m)) == std::vector<std::string>{"old"}));
}

// The game side as the session sees it: each apply_story call kept
struct StoryHost : editor::Host {
    struct Call {
        std::string mod, name;
        Value started, manifest;
    };
    std::vector<Call> calls;
    bool live = true;
    bool apply_actors(const std::string&, const std::vector<story_notebook::ManifestActor>&, const std::vector<std::string>&, std::string& note,
                      std::string&) override {
        note = "";
        return true;
    }
    bool apply_story(const std::string& mod, const std::string& name, const std::string&, const Value& started, const Value& manifest,
                     std::string& note, std::string& why) override {
        if (!live) return why = "the story system was not running at start-up", false;
        calls.push_back({mod, name, started, manifest});
        note = "Changes in the game at the next calm moment.";
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
    bool villagers(Value& out, std::string&) override {
        out = json(R"([{"id": "windfall-um1", "name": "Windmill man", "description": "", "where": "Windfall Island", "stage": "sea", "room": 11,
                        "position": {"x": 1000, "y": 900, "z": -198000}, "talks": true}])");
        return true;
    }
};

const char* kTalks = R"({"format_version": 1, "id": "talks", "name": "Talks", "version": "0.1.0", "game_id": "wwhd", "kind": "content",
  "dependencies": [{"id": "story-notebook", "minimum_version": "0.0.0"}], "actors": [],
  "quests_format": 1, "quests": [{"id": "old", "title": "An old quest", "steps": [{"n": 1, "text": "First."}, {"n": 2, "text": "Second."}]}]})";
const char* kOther = R"({"format_version": 1, "id": "other", "name": "Other", "version": "0.1.0", "game_id": "wwhd", "kind": "content",
  "dependencies": [{"id": "story-notebook", "minimum_version": "0.0.0"}], "actors": [],
  "quests_format": 1, "quests": [{"id": "tale", "title": "Another tale", "steps": [{"n": 1, "text": "Go."}]}]})";

void session() {
    StoryHost host;
    editor::Session s(&host);
    s.add_package("talks", "Talks", "", json(kTalks));
    s.add_package("other", "Other", "", json(kOther));
    CHECK(ok(s.run("open_mod", json(R"({"mod": "talks"})"))));
    const Value started = *s.manifest("talks");
    // a quest talk for a villager: the game gets the mod's whole new manifest and the one it started with
    Value r = s.run("add_villager_talk", json(R"({"who": "windfall-um1", "lines": "Welcome up here!", "title": "The windmill"})"));
    CHECK(ok(r) && r.get("result").get("story_live").boolean);
    CHECK(r.get("result").get("note").string().find("calm moment") != std::string::npos);
    CHECK(host.calls.size() == 1 && host.calls[0].mod == "talks" && host.calls[0].name == "Talks");
    CHECK(host.calls[0].manifest == *s.manifest("talks") && host.calls[0].started == started);
    const Value with_talk = *s.manifest("talks");
    // the talk's lines edited: once more
    CHECK(ok(s.run("add_villager_talk", json(R"({"who": "windfall-um1", "lines": "Hello again!", "quest": "the-windmill", "step": 1})"))));
    CHECK(host.calls.size() == 2 && host.calls[1].manifest == *s.manifest("talks"));
    // undo and redo: the same update, with the manifest of that step
    CHECK(ok(s.run("undo")) && host.calls.size() == 3 && host.calls[2].manifest == with_talk);
    CHECK(ok(s.run("undo")) && host.calls.size() == 4 && host.calls[3].manifest == started && *s.manifest("talks") == started);
    CHECK(ok(s.run("redo")) && host.calls.size() == 5 && host.calls[4].manifest == with_talk);
    // letters and charts are story changes too; the other mod is never handed over
    CHECK(ok(s.run("add_letter", json(R"({"id": "thanks", "sender": "The miller", "text": "Thank you."})"))));
    CHECK(ok(s.run("add_chart", json(R"({"id": "mill-map", "name": "Mill map", "place": {"x": 1200, "z": -198000}})"))));
    CHECK(host.calls.size() == 7);
    for (const auto& c : host.calls) CHECK(c.mod == "talks");
    // an actor change names no story section: no story update
    const size_t before = host.calls.size();
    r = s.run("add_actor", json(R"({"type": "sign", "id": "post", "at": "position", "position": {"x": 0, "y": 1040, "z": -201000}, "stage": "sea", "room": 11, "snap": false})"));
    CHECK(ok(r) && s.manifest("talks")->get("actors").array.size() == 1);
    CHECK(host.calls.size() == before);
    // the game can't take it: the note says it waits for a restart, nothing is lost
    host.live = false;
    r = s.run("add_letter", json(R"({"id": "later", "sender": "The miller", "text": "Later."})"));
    CHECK(ok(r) && !r.get("result").get("story_live").boolean && !r.get("result").get("live").boolean);
    CHECK(r.get("result").get("note").string().find("starts again") != std::string::npos);
    CHECK(r.get("result").get("note").string().find("not running at start-up") != std::string::npos);
    CHECK(s.manifest("talks")->get("letters").array.size() == 2);
    // without a game (no host): the same
    editor::Session alone;
    alone.add_package("talks", "Talks", "", json(kTalks));
    r = alone.run("add_letter", json(R"({"id": "thanks", "sender": "The miller", "text": "Thank you."})"));
    CHECK(ok(r) && !r.get("result").get("story_live").boolean);
}
}  // namespace

int main() {
    step_moves();
    items_and_sections();
    session();
    if (g_failures) {
        std::fprintf(stderr, "editor_live_story_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("editor_live_story_test: ok");
    return 0;
}
