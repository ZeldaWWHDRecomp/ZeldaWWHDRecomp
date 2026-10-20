// World editor: the quest editor's commands (runtime/src/editor/quests.h, commands_quest.cpp): the graph
// built from a quest, adding / editing / moving / removing steps with undo, transitions, start-at-step
// (only the mod's own records, never a game bit), the flags, the checks, search, copy and paste.
// Usage: editor_quest_test
#include "editor/commands.h"
#include "editor/quests.h"
#include "mods/story_items_core.h"
#include "mods/story_mail_book.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>

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

// A game with a story area of its own: records read and written there; start-at-step recorded.
struct FakeHost : editor::Host {
    uint8_t area[4 + 4 * 64] = {};
    bool testing = false, playing_now = false;
    editor::Place link_place, camera_place;
    std::vector<editor::Record> last_erase;
    std::vector<editor::RecordValue> last_values;
    std::vector<std::pair<std::string, int>> last_items;
    editor::Location last_to;
    std::map<std::string, int> items;  // the mod's items Link has
    int starts = 0;
    FakeHost() {
        link_place.valid = true;
        link_place.stage = "sea";
        link_place.room = 11;
        link_place.position = {120.5f, 1100, -201480.25f};
        camera_place = link_place;
        camera_place.position = {0, 2000, -200000};
    }
    story_mail::Book book() { return story_mail::Book(area, 64); }
    bool apply_actors(const std::string&, const std::vector<story_notebook::ManifestActor>&, const std::vector<std::string>&, std::string&,
                      std::string&) override {
        return true;
    }
    bool reload_room(std::string&) override { return true; }
    std::vector<std::pair<std::string, std::vector<int>>> stage_rooms() override { return {{"sea", {11}}, {"M_NewD2", {0}}}; }
    bool warp(const editor::Location&, std::string&) override { return true; }
    editor::Place link() override { return link_place; }
    bool busy(std::string&) override { return false; }
    bool fly(bool, bool, std::string&) override { return true; }
    Value fly_state() override { return Value(); }
    bool set_test_mode(bool on, std::string&) override { return testing = on, true; }
    bool test_mode() override { return testing; }
    bool reset_records(const std::string&, const std::vector<editor::Record>&, const std::vector<editor::Record>&, unsigned&, std::string&) override {
        return true;
    }
    bool test_state(bool, int, const std::string&, const std::vector<editor::Record>&, std::string&) override { return true; }
    editor::Place camera() override { return camera_place; }
    bool read_records(const std::vector<editor::Record>& records, std::vector<int>& values, std::string&) override {
        auto b = book();
        values.clear();
        for (const auto& r : records) values.push_back(b.has(r.kind, r.key) ? int(b.get(r.kind, r.key)) : -1);
        return true;
    }
    bool write_records(const std::string&, const std::vector<editor::RecordValue>& values, std::string& why) override {
        if (!testing) return why = "test mode only", false;
        auto b = book();
        for (const auto& v : values)
            if (v.value < 0) b.erase(v.record.kind, v.record.key);
            else b.set(v.record.kind, v.record.key, uint8_t(v.value));
        return true;
    }
    bool start_at_step(const std::string&, const std::vector<editor::Record>& erase, const std::vector<editor::RecordValue>& values,
                       const std::vector<std::pair<std::string, int>>& give, const editor::Location& to, bool play, std::string&) override {
        testing = true;  // as Play does
        ++starts;
        last_erase = erase, last_values = values, last_items = give, last_to = to;
        auto b = book();
        for (const auto& r : erase) b.erase(r.kind, r.key);
        for (const auto& v : values) b.set(v.record.kind, v.record.key, uint8_t(v.value));
        for (const auto& [id, n] : give) items[id] = n;
        playing_now = play;
        return true;
    }
};

Value json(const char* text) { return mods::json::parse(text); }
bool ok(const Value& r) { return r.get("ok").boolean; }
Value params(const char* text) { return json(text); }

// Windfall: a lighthouse quest of four steps. 1 -> 2 (the sign), 1 -> 3 (the guide: a branch),
// 2 -> 3 (the post, with the old key), the key's use sets 3, 3 -> End (a resident's talk).
const char* kManifest = R"({
  "format_version": 1, "id": "quest-test", "name": "Quest test", "version": "1.0.0", "kind": "content",
  "quests_format": 1,
  "items": [{"id": "old-key", "name": "Old Key", "description": "An old key.", "icon": "assets/key.png",
             "use": {"action": "quest_step", "quest": "lighthouse", "step": 3}},
            {"id": "lens", "name": "Lens", "description": "A lens.", "icon": "assets/lens.png"}],
  "quests": [{"id": "lighthouse", "title": "Lighthouse quest",
    "letter": {"id": "invite", "sender": "The keeper", "text": "Come to Windfall.", "encloses": ["item:lens", "letter:note"]},
    "steps": [
      {"n": 1, "text": "Read the sign.", "where": "Windfall Island", "place": {"x": 0, "z": -200000}},
      {"n": 2, "text": "Check the post.", "place": {"x": 100, "z": -200100}},
      {"n": 3, "text": "Talk to the keeper.", "npc": {"who": "windfall-um1", "lines": ["Thank you!"], "then": {"give": "rupee_blue"}}}
    ]}],
  "actors": [
    {"id": "sign-a", "actor": "sign", "stage": "sea", "room": 11, "position": {"x": 10, "y": 1040, "z": -201500},
     "flag": "sign-a-read", "lines": ["Hello."], "when": {"quest": "lighthouse", "step": 1},
     "then": [{"action": "quest_step", "id": "lighthouse", "step": 2}, {"action": "give", "id": "item:old-key", "count": 1},
              {"action": "flag", "id": "sign-done"}]},
    {"id": "guide", "actor": "resident", "parameters": {"resident": 9}, "stage": "sea", "room": 11,
     "position": {"x": -500, "y": 1200, "z": -201500}, "lines": "Hi.", "when": {"quest": "lighthouse", "step": 1},
     "then": [{"action": "quest_step", "id": "lighthouse", "step": 3}]},
    {"id": "post", "actor": "sign", "stage": "sea", "room": 11, "position": {"x": 300, "y": 1040, "z": -201400}, "flag": "post-read",
     "lines": "A post.", "when": {"quest": "lighthouse", "step": 2, "item": "item:old-key"},
     "then": [{"action": "quest_step", "id": "lighthouse", "step": 3}, {"action": "take", "id": "item:old-key", "count": 1}]},
    {"id": "pot-a", "actor": "pot", "stage": "sea", "room": 11, "position": {"x": 0, "y": 1040, "z": -201300}, "broken_flag": "pot-a-broken", "drops": []}
  ]
})";

const Value* node(const Value& g, const std::string& id) {
    for (const auto& n : g.get("nodes").array)
        if (n.get("id").string() == id) return &n;
    return nullptr;
}
int edges(const Value& g, const std::string& from, const std::string& to, const std::string& via = "") {
    int n = 0;
    for (const auto& e : g.get("edges").array)
        n += e.get("from").string() == from && e.get("to").string() == to && (via.empty() || e.get("via").get("id").string() == via);
    return n;
}
const Value* step_of(const Value& m, const std::string& quest, int n) {
    for (const auto& q : m.get("quests").array)
        if (q.get("id").string() == quest)
            for (const auto& s : q.get("steps").array)
                if (s.get("n").number == n) return &s;
    return nullptr;
}
const Value* actor(const Value& m, const std::string& id) {
    for (const auto& a : m.get("actors").array)
        if (a.get("id").string() == id) return &a;
    return nullptr;
}

void graph() {
    const Value m = json(kManifest);
    Value g;
    CHECK(editor::quest_graph("quest-test", m, "lighthouse", g));
    Value none;
    CHECK(!editor::quest_graph("quest-test", m, "nope", none));
    CHECK(node(g, "start") && node(g, "end") && node(g, "step:1") && node(g, "step:2") && node(g, "step:3"));
    CHECK(edges(g, "start", "step:1") == 1);
    CHECK(edges(g, "start", "step:1", "invite") == 1);
    CHECK(edges(g, "step:1", "step:2", "sign-a") == 1);
    CHECK(edges(g, "step:1", "step:3", "guide") == 1);  // the branch
    CHECK(edges(g, "step:2", "step:3", "post") == 1);
    CHECK(edges(g, "start", "step:3", "old-key") == 1);
    CHECK(edges(g, "step:3", "end", "windfall-um1") == 1);
    // the branch is marked; the post's condition and rewards are on its edge
    for (const auto& e : g.get("edges").array) {
        if (e.get("from").string() == "step:1") CHECK(e.get("branch").boolean);
        if (e.get("via").get("id").string() == "post") {
            CHECK(e.get("conditions").array.size() == 1 && e.get("conditions").array[0].get("kind").string() == "item");
            CHECK(e.get("rewards").array.size() == 1 && e.get("rewards").array[0].get("kind").string() == "take");
            CHECK(e.get("editable").boolean);
        }
    }
    CHECK(node(g, "step:3")->get("final").boolean);  // its way on is the talk to End
    CHECK(!node(g, "step:2")->get("final").boolean);
    bool talk = false;
    for (const auto& b : node(g, "step:3")->get("badges").array) talk = talk || b.get("kind").string() == "talk";
    CHECK(talk);
    // the letter's edge goes from start, so start is marked, with what the letter encloses
    CHECK(node(g, "start")->get("badges").array.size() == 2 && node(g, "start")->get("badges").array[1].get("text").string() == "encloses item:lens");
}

void start_at() {
    const Value m = json(kManifest);
    editor::StepStart s;
    std::string why;
    CHECK(editor::step_start("quest-test", m, "lighthouse", 2, "", s, why));
    CHECK(s.reachable && s.path.size() == 3 && s.path[1] == "step:1" && s.path[2] == "step:2");
    CHECK(s.letter == "invite" && s.carried);
    // the sign: its own flag and its flag action; the key it gave; the letter's lens
    std::map<std::string, bool> flags(s.flags.begin(), s.flags.end());
    CHECK(flags.size() == 2 && flags["sign-a-read"] && flags["sign-done"]);
    std::map<std::string, int> items(s.items.begin(), s.items.end());
    CHECK(items["lens"] == 1 && items["old-key"] == 1);
    CHECK(s.location == "actor:quest-test:post");  // in front of what moves it on
    CHECK(editor::step_start("quest-test", m, "lighthouse", 2, "place", s, why) && s.location == "step:quest-test:lighthouse:2");
    CHECK(editor::step_start("quest-test", m, "lighthouse", 2, "none", s, why) && s.location.empty());
    CHECK(!editor::step_start("quest-test", m, "lighthouse", 3, "place", s, why));  // step 3 has no place
    CHECK(!editor::step_start("quest-test", m, "lighthouse", 9, "", s, why));
    CHECK(!editor::step_start("quest-test", m, "lighthouse", 2, "moon", s, why));
    // step 3 the short way (the guide), the key's taken on the long way: never both
    CHECK(editor::step_start("quest-test", m, "lighthouse", 3, "", s, why));
    CHECK(s.path.size() == 3 && s.path[1] == "step:1");
    // only the mod's own records: every record is of this mod's keys, none is a game event bit
    const auto records = editor::step_records("quest-test", "lighthouse", s);
    std::set<uint8_t> kinds;
    for (const auto& r : records) kinds.insert(r.record.kind);
    for (uint8_t k : kinds)
        CHECK(k == story_mail::kRecordQuest || k == story_mail::kRecordQuestOrder || k == story_mail::kRecordTracked || k == story_mail::kRecordLetter ||
              k == story_mail::kRecordCarried || k == story_mail::kRecordActorFlag);
    CHECK(records[0].record.key == editor::quest_key("quest-test", "lighthouse") && records[0].value == 3);
    for (const auto& r : records)
        if (r.record.kind == story_mail::kRecordActorFlag)
            CHECK(r.record.key == story_mail::letter_key("quest-test", "actor-flag:sign-a-read") ||
                  r.record.key == story_mail::letter_key("quest-test", "actor-flag:sign-done"));

    // through the command layer: test mode, records, items, the warp, Play
    FakeHost host;
    editor::Session s2(&host);
    s2.add_package("quest-test", "Quest test", "", m);
    s2.add_package("other", "Other", "", json(R"({"id": "other", "quests_format": 1, "quests": [{"id": "lighthouse", "title": "Other", "steps": [{"n": 1}]}]})"));
    // another mod's progress and a game record stay
    host.book().set(story_mail::kRecordQuest, editor::quest_key("other", "lighthouse"), 1);
    Value r = s2.run("start_at_step", params(R"({"quest": "lighthouse", "step": 2})"));
    CHECK(ok(r));
    CHECK(host.starts == 1 && host.testing && host.playing_now);
    CHECK(host.last_to.id == "actor:quest-test:post");
    CHECK(host.book().get(story_mail::kRecordQuest, editor::quest_key("quest-test", "lighthouse")) == 2);
    CHECK(host.book().get(story_mail::kRecordQuest, editor::quest_key("other", "lighthouse")) == 1);
    CHECK(host.book().get(story_mail::kRecordActorFlag, story_mail::letter_key("quest-test", "actor-flag:sign-a-read")) == 1);
    CHECK(host.items["old-key"] == 1 && host.items["lens"] == 1);
    for (const auto& e : host.last_erase) CHECK(!(e.kind == story_mail::kRecordQuest && e.key == editor::quest_key("other", "lighthouse")));
    // the status bar's text, live
    r = s2.run("quest_state");
    CHECK(ok(r) && r.get("result").get("live").boolean);
    CHECK(r.get("result").get("current").get("text").string() == "Lighthouse quest: step 2 of 3");
    CHECK(editor::quest_progress_text(json(R"({"title": "T", "steps": [{"n": 1}, {"n": 5}, {"n": 9}]})"), 5) == "T: step 2 of 3");
    CHECK(editor::quest_progress_text(json(R"({"title": "T", "steps": [{"n": 1}]})"), 255) == "T: done");
    CHECK(editor::quest_progress_text(json(R"({"title": "T", "steps": [{"n": 1}]})"), 0) == "T: not started");
    // the graph says where the game is
    r = s2.run("quest_graph", params(R"({"quest": "lighthouse"})"));
    CHECK(ok(r) && r.get("result").get("saved_step").number == 2);
    // no game: refused
    editor::Session none;
    none.add_package("quest-test", "Quest test", "", m);
    CHECK(!ok(none.run("start_at_step", params(R"({"quest": "lighthouse", "step": 2})"))));
}

void flags() {
    const Value m = json(kManifest);
    const auto list = editor::mod_flags("quest-test", m);
    std::map<std::string, editor::ModFlag> by;
    for (const auto& f : list) by[f.id] = f;
    CHECK(by.count("sign-a-read") && by["sign-a-read"].kind == "actor" && by["sign-a-read"].settable);
    CHECK(by.count("sign-done") && by["sign-done"].kind == "action");
    CHECK(by.count("pot-a-broken") && by["pot-a-broken"].kind == "prop" && !by["pot-a-broken"].settable);
    CHECK(by["sign-a-read"].record.kind == story_mail::kRecordActorFlag && by["sign-a-read"].record.key == story_mail::letter_key("quest-test", "actor-flag:sign-a-read"));

    FakeHost host;
    editor::Session s(&host);
    s.add_package("quest-test", "Quest test", "", m);
    Value r = s.run("list_flags");
    CHECK(ok(r) && r.get("result").get("flags").array.size() == list.size());
    // test mode only, mod flags only
    CHECK(!ok(s.run("set_flag", params(R"({"id": "sign-a-read", "value": true})"))));
    host.testing = true;
    CHECK(ok(s.run("set_flag", params(R"({"id": "sign-a-read", "value": true})"))));
    CHECK(host.book().get(story_mail::kRecordActorFlag, story_mail::letter_key("quest-test", "actor-flag:sign-a-read")) == 1);
    CHECK(!ok(s.run("set_flag", params(R"({"id": "pot-a-broken", "value": true})"))));  // shown, not set
    CHECK(!ok(s.run("set_flag", params(R"({"id": "0x2D01", "value": true})"))));        // never a game flag
    CHECK(!ok(s.run("set_flag", params(R"({"id": "someone-elses", "value": true})"))));
    r = s.run("list_flags");
    for (const auto& f : r.get("result").get("flags").array)
        if (f.get("id").string() == "sign-a-read") CHECK(f.get("value").boolean);
}

void step_commands() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("quest-test", "Quest test", "", json(kManifest));
    auto m = [&] { return *s.manifest("quest-test"); };
    // add at the end, then after step 1: the later steps move up, and so do the actors that name them
    Value r = s.run("add_quest_step", params(R"({"quest": "lighthouse", "text": "Celebrate."})"));
    CHECK(ok(r) && r.get("result").get("step").number == 4 && step_of(m(), "lighthouse", 4));
    r = s.run("add_quest_step", params(R"({"quest": "lighthouse", "after": 1, "text": "Look around."})"));
    CHECK(ok(r) && r.get("result").get("step").number == 2);
    CHECK(step_of(m(), "lighthouse", 2)->get("text").string() == "Look around.");
    CHECK(step_of(m(), "lighthouse", 3)->get("text").string() == "Check the post.");
    CHECK(actor(m(), "post")->get("when").get("step").number == 3);
    CHECK(actor(m(), "sign-a")->get("then").array[0].get("step").number == 3);
    CHECK(actor(m(), "guide")->get("then").array[0].get("step").number == 4);
    const Value items = m();  // a copy that lives through the loop (a temporary's member would not)
    for (const auto& it : items.get("items").array)
        if (it.get("id").string() == "old-key") CHECK(it.get("use").get("step").number == 4);
    CHECK(ok(s.run("undo")));
    CHECK(actor(m(), "post")->get("when").get("step").number == 2 && step_of(m(), "lighthouse", 2)->get("text").string() == "Check the post.");
    CHECK(ok(s.run("redo")));
    CHECK(actor(m(), "post")->get("when").get("step").number == 3);
    CHECK(ok(s.run("undo")) && ok(s.run("undo")));
    CHECK(!step_of(m(), "lighthouse", 4));
    // edit several fields; n is refused
    r = s.run("edit_quest_step", params(R"({"quest": "lighthouse", "step": 2, "fields": {"text": "Check the old post.", "where": "Windfall Island"}})"));
    CHECK(ok(r) && step_of(m(), "lighthouse", 2)->get("where").string() == "Windfall Island");
    CHECK(!ok(s.run("edit_quest_step", params(R"({"quest": "lighthouse", "step": 2, "fields": {"n": 7}})"))));
    CHECK(!ok(s.run("edit_quest_step", params(R"({"quest": "lighthouse", "step": 2, "fields": {"text": 5}})"))));  // the validator
    CHECK(ok(s.run("undo")) && step_of(m(), "lighthouse", 2)->get("text").string() == "Check the post.");
    // duplicate
    r = s.run("duplicate_quest_step", params(R"({"quest": "lighthouse", "step": 1})"));
    CHECK(ok(r) && step_of(m(), "lighthouse", 4) && step_of(m(), "lighthouse", 4)->get("text").string() == "Read the sign.");
    CHECK(ok(s.run("undo")));
    // move: step 3 first; the numbers stay 1..3 in the new order, the references follow
    r = s.run("move_quest_step", params(R"({"quest": "lighthouse", "step": 3, "to": 1})"));
    CHECK(ok(r));
    CHECK(step_of(m(), "lighthouse", 1)->get("text").string() == "Talk to the keeper.");
    CHECK(step_of(m(), "lighthouse", 2)->get("text").string() == "Read the sign.");
    CHECK(actor(m(), "sign-a")->get("when").get("step").number == 2);
    CHECK(actor(m(), "post")->get("when").get("step").number == 3);
    CHECK(actor(m(), "post")->get("then").array[0].get("step").number == 1);  // follows its step
    CHECK(ok(s.run("undo")) && step_of(m(), "lighthouse", 1)->get("text").string() == "Read the sign.");
    CHECK(!ok(s.run("move_quest_step", params(R"({"quest": "lighthouse", "step": 3, "to": 9})"))));
    // remove step 2: the post's condition and the sign's action go with it
    CHECK(!ok(s.run("remove_quest_step", params(R"({"quest": "lighthouse", "step": 2})"))));  // confirm
    r = s.run("remove_quest_step", params(R"({"quest": "lighthouse", "step": 2, "confirm": true})"));
    CHECK(ok(r) && !step_of(m(), "lighthouse", 2));
    CHECK(actor(m(), "post")->get("when").get("quest").type == Value::Null);
    const Value after = m();
    for (const auto& t : actor(after, "sign-a")->get("then").array) CHECK(!(t.get("action").string() == "quest_step" && t.get("step").number == 2));
    CHECK(ok(s.run("undo")) && step_of(m(), "lighthouse", 2));
    // a scene that names a step keeps it (scenes change first)
    {
        Value with = json(kManifest);
        with["scenes"] = json(R"([{"id": "glow", "trigger": {"type": "quest_step", "quest": "lighthouse", "step": 2},
                                  "steps": [{"type": "wait", "frames": 30}, {"type": "end"}]}])");
        editor::Session t(&host);
        t.add_package("quest-test", "Quest test", "", with);
        CHECK(!ok(t.run("remove_quest_step", params(R"({"quest": "lighthouse", "step": 2, "confirm": true})"))));
        CHECK(!ok(t.run("move_quest_step", params(R"({"quest": "lighthouse", "step": 2, "to": 3})"))));
        CHECK(ok(t.run("move_quest_step", params(R"({"quest": "lighthouse", "step": 3, "to": 3})"))));  // nothing moves
    }
    // the place: from Link (at sea), from a warp destination, none
    r = s.run("set_step_place", params(R"({"quest": "lighthouse", "step": 3, "from": "link"})"));
    CHECK(ok(r) && step_of(m(), "lighthouse", 3)->get("place").get("x").number == 120.5);
    CHECK(step_of(m(), "lighthouse", 3)->get("where").string() == "Windfall Island");
    r = s.run("set_step_place", params(R"({"quest": "lighthouse", "step": 3, "from": "location", "location": "island:44"})"));
    CHECK(ok(r) && step_of(m(), "lighthouse", 3)->get("where").string() == "Outset Island");
    CHECK(!ok(s.run("set_step_place", params(R"({"quest": "lighthouse", "step": 3, "from": "location", "location": "room:M_NewD2:0"})"))));
    host.link_place.stage = "M_NewD2";
    CHECK(!ok(s.run("set_step_place", params(R"({"quest": "lighthouse", "step": 3, "from": "link"})"))));
    CHECK(ok(s.run("set_step_place", params(R"({"quest": "lighthouse", "step": 3, "from": "none"})"))));
    CHECK(step_of(m(), "lighthouse", 3)->get("place").type == Value::Null);
    // quests: add, set, remove (the actor conditions and actions that name it go too)
    r = s.run("add_quest", params(R"({"title": "The Tide Pool", "text": "Find the pool."})"));
    CHECK(ok(r) && r.get("result").get("quest").string() == "the-tide-pool");
    CHECK(ok(s.run("set_quest", params(R"({"quest": "the-tide-pool", "property": "journal", "value": "hidden"})"))));
    CHECK(!ok(s.run("set_quest", params(R"({"quest": "the-tide-pool", "property": "journal", "value": "sometimes"})"))));
    CHECK(!ok(s.run("set_quest", params(R"({"quest": "the-tide-pool", "property": "steps", "value": []})"))));
    CHECK(!ok(s.run("remove_quest", params(R"({"quest": "lighthouse", "confirm": true})"))));  // the key's use names it
    CHECK(ok(s.run("remove_quest", params(R"({"quest": "the-tide-pool", "confirm": true})"))));
    CHECK(m().get("quests").array.size() == 1);
}

void transitions() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("quest-test", "Quest test", "", json(kManifest));
    auto m = [&] { return *s.manifest("quest-test"); };
    auto graph = [&] { return s.run("quest_graph", params(R"({"quest": "lighthouse"})")).get("result"); };
    // the guide no longer moves 1 -> 3: disconnect; then 1 -> 2 again from the guide with a condition
    CHECK(ok(s.run("disconnect_quest_steps", params(R"({"quest": "lighthouse", "from": 1, "to": 3, "via": "guide"})"))));
    CHECK(edges(graph(), "step:1", "step:3", "guide") == 0);
    CHECK(!ok(s.run("disconnect_quest_steps", params(R"({"quest": "lighthouse", "from": 1, "to": 3, "via": "guide"})"))));
    CHECK(ok(s.run("connect_quest_steps", params(R"({"quest": "lighthouse", "from": 1, "to": 2, "via": "guide", "time": "night"})"))));
    CHECK(edges(graph(), "step:1", "step:2", "guide") == 1);
    CHECK(actor(m(), "guide")->get("when").get("time").string() == "night");
    // the edge's condition changes on the same edge (no second action)
    CHECK(ok(s.run("connect_quest_steps", params(R"({"quest": "lighthouse", "from": 1, "to": 2, "via": "guide", "time": "-", "item": "lens"})"))));
    CHECK(actor(m(), "guide")->get("when").get("time").type == Value::Null);
    CHECK(actor(m(), "guide")->get("when").get("item").string() == "item:lens");
    CHECK(edges(graph(), "step:1", "step:2", "guide") == 1);
    // refused: backwards, to End, an unknown actor, a second step from one actor
    CHECK(!ok(s.run("connect_quest_steps", params(R"({"quest": "lighthouse", "from": 2, "to": 1, "via": "guide"})"))));
    CHECK(!ok(s.run("connect_quest_steps", params(R"({"quest": "lighthouse", "from": 1, "to": 9, "via": "guide"})"))));
    CHECK(!ok(s.run("connect_quest_steps", params(R"({"quest": "lighthouse", "from": 1, "to": 3, "via": "nobody"})"))));
    CHECK(!ok(s.run("connect_quest_steps", params(R"({"quest": "lighthouse", "from": 1, "to": 3, "via": "guide"})"))));
    // any time (from 0): the actor's step condition goes
    CHECK(ok(s.run("connect_quest_steps", params(R"({"quest": "lighthouse", "from": 0, "to": 2, "via": "guide"})"))));
    CHECK(actor(m(), "guide")->get("when").get("quest").type == Value::Null && edges(graph(), "start", "step:2", "guide") == 1);
    // undo walks every change back
    while (ok(s.run("undo"))) {
    }
    CHECK(*s.manifest("quest-test") == json(kManifest));
}

void checks() {
    Value m = json(kManifest);
    auto problems = editor::quest_problems("quest-test", m);
    // only the "nothing finishes it" kinds are absent: the talk finishes it; step 3 has no place but a talk
    for (const auto& p : problems) CHECK(p.severity == "warning" || p.severity == "info" || p.severity == "error");
    CHECK(problems.empty());
    // unreachable step, missing place, a dead end, an item nothing gives, a flag action on a prop's flag
    m["quests"].array[0]["steps"].array.push_back(json(R"({"n": 7, "text": "Lost."})"));
    m["actors"].array[1]["then"] = json(R"([{"action": "quest_step", "id": "lighthouse", "step": 3}, {"action": "flag", "id": "pot-a-broken"}])");
    m["actors"].array[2]["when"]["item"] = "item:lens";
    m["quests"].array[0]["letter"]["encloses"] = json(R"(["letter:note"])");
    problems = editor::quest_problems("quest-test", m);
    auto has = [&](const std::string& field, const std::string& words) {
        for (const auto& p : problems)
            if (p.field == field && p.message.find(words) != std::string::npos) return true;
        return false;
    };
    CHECK(has("step:7", "nothing leads to it"));
    CHECK(has("step:7", "no place"));
    CHECK(has("step:2", "lens"));
    CHECK(has("step:1", "pot-a-broken"));
    // a dead end in the middle
    Value d = json(kManifest);
    d["actors"].array[2].object.erase("then");
    d["items"].array[0].object.erase("use");
    problems = editor::quest_problems("quest-test", d);
    CHECK(has("step:2", "nothing moves the quest on"));
    // through check_package: warnings, with the step they are about; the package stays valid
    FakeHost host;
    editor::Session s(&host);
    s.add_package("quest-test", "Quest test", "", m);
    Value r = s.run("check_package");
    CHECK(ok(r) && r.get("result").get("valid").boolean);
    bool found = false;
    for (const auto& p : r.get("result").get("problems").array)
        found = found || (p.get("field").string() == "step:7" && p.get("severity").string() == "warning" && p.get("kind").string() == "quest");
    CHECK(found);
    // and on the graph's nodes
    r = s.run("quest_graph", params(R"({"quest": "lighthouse"})"));
    CHECK(node(r.get("result"), "step:7")->get("problems").array.size() == 2);
    CHECK(!node(r.get("result"), "step:7")->get("reachable").boolean);
}

void search_copy_paste(const fs::path& tmp) {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("quest-test", "Quest test", "", json(kManifest));
    auto m = [&] { return *s.manifest("quest-test"); };
    Value r = s.run("search", params(R"({"text": "POST"})"));
    CHECK(ok(r));
    std::set<std::string> kinds;
    for (const auto& h : r.get("result").array) kinds.insert(h.get("kind").string() + ":" + h.get("id").string());
    CHECK(kinds.count("actor:post") && kinds.count("quest_step:lighthouse") && kinds.count("flag:post-read"));
    r = s.run("search", params(R"({"text": "lens"})"));
    kinds.clear();
    for (const auto& h : r.get("result").array) kinds.insert(h.get("kind").string() + ":" + h.get("id").string());
    CHECK(ok(r) && r.get("result").array.size() == 2 && kinds.count("item:lens") && kinds.count("quest:lighthouse"));  // the letter encloses it
    CHECK(ok(s.run("search", params(R"({"text": ""})"))) && s.run("search", params(R"({"text": ""})")).get("result").array.empty());
    // copy two actors, paste at an offset: new ids, own flags, one undo step
    CHECK(!ok(s.run("paste")));
    CHECK(ok(s.run("copy", params(R"({"ids": ["sign-a", "pot-a"]})"))));
    r = s.run("paste", params(R"({"offset": {"x": 0, "y": 0, "z": 50}})"));
    CHECK(ok(r) && r.get("result").get("ids").array.size() == 2);
    CHECK(actor(m(), "sign-a-2") && actor(m(), "pot-a-2"));
    CHECK(actor(m(), "sign-a-2")->get("flag").string() != "sign-a-read");
    CHECK(actor(m(), "pot-a-2")->get("broken_flag").string() != "pot-a-broken");
    CHECK(actor(m(), "sign-a-2")->get("position").get("z").number == -201450);
    CHECK(ok(s.run("undo")) && !actor(m(), "sign-a-2"));
    // into the room the camera is in (another sea square)
    host.camera_place.room = 44;
    host.camera_place.position = {-200000, 100, 250000};
    r = s.run("paste", params(R"({"at": "camera"})"));
    CHECK(ok(r));
    CHECK(actor(m(), "sign-a-2")->get("room").number == 44 && actor(m(), "pot-a-2")->get("room").number == 44);
    CHECK(actor(m(), "sign-a-2")->get("position").get("x").number == -200000);
    CHECK(ok(s.run("undo")));
    // where the camera looks (a picked ground point): the first one there, the others around it
    r = s.run("paste", params(R"({"at": "position", "position": {"x": 50, "y": 1000, "z": -200050}})"));
    CHECK(ok(r) && actor(m(), "sign-a-2")->get("position").get("x").number == 50 && actor(m(), "sign-a-2")->get("room").number == 11);
    CHECK(actor(m(), "pot-a-2")->get("position").get("x").number == 40 && actor(m(), "pot-a-2")->get("position").get("z").number == -199850);
    CHECK(!ok(s.run("paste", params(R"({"at": "position"})"))));
    CHECK(ok(s.run("undo")));
    // several actors: move and remove as one step
    CHECK(ok(s.run("move_actors", params(R"({"ids": ["sign-a", "post"], "offset": {"x": 10, "y": 0, "z": 0}})"))));
    CHECK(actor(m(), "sign-a")->get("position").get("x").number == 20 && actor(m(), "post")->get("position").get("x").number == 310);
    CHECK(!ok(s.run("remove_actors", params(R"({"ids": ["pot-a"]})"))));
    CHECK(ok(s.run("remove_actors", params(R"({"ids": ["pot-a"], "confirm": true})"))) && !actor(m(), "pot-a"));
    CHECK(ok(s.run("undo")) && ok(s.run("undo")) && *s.manifest("quest-test") == json(kManifest));
    // quest steps: copied into the same quest as new last steps
    CHECK(ok(s.run("copy", params(R"({"quest": "lighthouse", "steps": [1, 2]})"))));
    r = s.run("paste");
    CHECK(ok(r) && step_of(m(), "lighthouse", 4) && step_of(m(), "lighthouse", 5));
    CHECK(step_of(m(), "lighthouse", 5)->get("text").string() == "Check the post.");
    CHECK(ok(s.run("undo")));

    // the graph's node places: the editor's own file in the package, undoable, saved with the package
    const fs::path dir = tmp / "quest-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    { std::ofstream(dir / "manifest.json") << kManifest; }
    editor::Session p(&host);
    p.add_package("quest-test", "Quest test", dir.string(), json(kManifest));
    CHECK(!p.dirty("quest-test"));
    r = p.run("set_quest_layout", params(R"({"quest": "lighthouse", "positions": {"step:1": {"x": 40, "y": 80.4}, "start": {"x": 0, "y": 0}}})"));
    CHECK(ok(r) && p.dirty("quest-test"));
    CHECK(!ok(p.run("set_quest_layout", params(R"({"quest": "lighthouse", "positions": {"nowhere": {"x": 1, "y": 1}}})"))));
    r = p.run("quest_graph", params(R"({"quest": "lighthouse"})"));
    CHECK(r.get("result").get("layout").get("step:1").get("y").number == 80);
    CHECK(ok(p.run("undo")) && !p.dirty("quest-test"));
    CHECK(ok(p.run("redo")) && p.dirty("quest-test"));
    CHECK(ok(p.run("save_package", params(R"({"confirm": true})"))));
    CHECK(fs::exists(dir / "editor" / "quest-layout.json") && !p.dirty("quest-test"));
    // the manifest never holds it, and a new session reads it back
    CHECK(json(kManifest) == *p.manifest("quest-test"));
    editor::Session again(&host);
    again.add_package("quest-test", "Quest test", dir.string(), json(kManifest));
    r = again.run("quest_graph", params(R"({"quest": "lighthouse"})"));
    CHECK(r.get("result").get("layout").get("step:1").get("x").number == 40);
    // the package with its sidecar still validates; an export carries it along
    CHECK(ok(again.run("validate")) && again.run("validate").get("result").get("valid").boolean);
    fs::remove_all(dir);
}
}  // namespace

// The quest format's later forms through the command layer: talks, flag and multi-quest conditions,
// quest_done, interior places and flag availability; each with undo, and refused bad values.
const char* kFormat = R"({
  "format_version": 1, "id": "format-test", "name": "Format test", "version": "1.0.0", "kind": "content",
  "quests_format": 1,
  "quests": [
    {"id": "notice", "title": "The harbor notice", "steps": [
      {"n": 1, "text": "Speak to the keeper.", "place": {"x": 100, "z": -201500}},
      {"n": 2, "text": "Check the building."}]},
    {"id": "bell", "title": "The bell", "steps": [{"n": 1, "text": "Ring it."}]}
  ],
  "actors": [
    {"id": "notice-sign", "actor": "sign", "flag": "read", "stage": "sea", "room": 11,
     "position": {"x": -800, "y": 1040, "z": -201500}, "lines": ["Speak to the keeper."],
     "then": [{"action": "quest_step", "id": "notice", "step": 1}]},
    {"id": "keeper", "actor": "resident", "parameters": {"resident": "uo2"}, "stage": "sea", "room": 11,
     "position": {"x": 100, "y": 1040, "z": -201500}, "lines": ["Hello."]},
    {"id": "post", "actor": "sign", "flag": "post-read", "stage": "sea", "room": 11,
     "position": {"x": 300, "y": 1040, "z": -201500}, "lines": ["A post."]}
  ]
})";
void quest_format() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("format-test", "Format test", "", json(kFormat));
    auto m = [&] { return *s.manifest("format-test"); };
    const Value start = m();
    // talks: refused next to lines (the validator's rule), kept once lines go; undo brings lines back
    const char* talks = R"({"id": "keeper", "property": "talks", "value": [
        {"when": {"quest": "notice", "step": 1}, "lines": ["The building is marked."], "then": [{"action": "quest_step", "id": "notice", "step": 2}]},
        {"when": {"quest": "notice", "step": 2, "flag": "post-read"}, "lines": ["Thank you."],
         "then": [{"action": "flag", "id": "thanked"}, {"action": "quest_done", "id": "notice"}]},
        {"lines": ["All done."]}]})";
    CHECK(!ok(s.run("set_actor", params(talks))));
    CHECK(ok(s.run("set_actor", params(R"({"id": "keeper", "property": "lines", "value": null})"))));
    Value r = s.run("set_actor", params(talks));
    CHECK(ok(r));
    CHECK(actor(m(), "keeper")->get("talks").array.size() == 3);
    // nine talks, a talk with an unknown action: refused
    {
        Value nine = params(talks);
        while (nine["value"].array.size() < 9) nine["value"].array.insert(nine["value"].array.begin(), nine["value"].array[1]);
        CHECK(!ok(s.run("set_actor", nine)));
        Value bad = params(talks);
        bad["value"].array[0]["then"].array[0]["action"] = "dance";
        CHECK(!ok(s.run("set_actor", bad)));
    }
    // the graph: talk 1 moves 1 -> 2, talk 2 finishes the quest from step 2 (with its flag condition)
    Value g = s.run("quest_graph", params(R"({"quest": "notice"})")).get("result");
    CHECK(edges(g, "step:1", "step:2", "keeper") == 1);
    CHECK(edges(g, "step:2", "end", "keeper") == 1);
    for (const auto& e : g.get("edges").array)
        if (e.get("to").string() == "end" && e.get("via").get("id").string() == "keeper") {
            bool flag = false, done = false;
            for (const auto& c : e.get("conditions").array) flag = flag || (c.get("kind").string() == "flag" && c.get("id").string() == "post-read");
            for (const auto& w : e.get("rewards").array) done = done || w.get("kind").string() == "done";
            CHECK(flag && done && !e.get("editable").boolean);
        }
    CHECK(node(g, "step:2") && node(g, "step:2")->get("final").boolean);
    // a talk's flag action is one of the mod's flags
    bool thanked = false;
    const Value flags = s.run("list_flags").get("result").get("flags");
    for (const auto& f : flags.array) thanked = thanked || f.get("id").string() == "thanked";
    CHECK(thanked);
    // flag conditions: a string, a list, not_flag; a flag nothing sets is refused
    CHECK(ok(s.run("set_actor", params(R"({"id": "post", "property": "when", "value": {"flag": "read"}})"))));
    CHECK(ok(s.run("set_actor", params(R"({"id": "post", "property": "when", "value": {"flag": "thanked", "not_flag": "read"}})"))));
    CHECK(actor(m(), "post")->get("when").get("not_flag").string() == "read");
    // an actor's when takes one flag id each (a list is the quest step's available form)
    CHECK(!ok(s.run("set_actor", params(R"({"id": "post", "property": "when", "value": {"flag": ["read", "thanked"]}})"))));
    CHECK(!ok(s.run("set_actor", params(R"({"id": "post", "property": "when", "value": {"flag": "nobody-sets-this"}})"))));
    CHECK(ok(s.run("undo")) && actor(m(), "post")->get("when").get("flag").string() == "read");
    // several quests at once
    CHECK(ok(s.run("set_actor", params(R"({"id": "post", "property": "when", "value": {"quests": [{"quest": "notice", "step": 2}, {"quest": "bell", "step": 1}]}})"))));
    CHECK(!ok(s.run("set_actor", params(R"({"id": "post", "property": "when", "value": {"quests": [{"quest": "nope", "step": 1}]}})"))));
    // quest_done in an actor's actions; an unknown quest is refused
    CHECK(ok(s.run("set_then", params(R"({"id": "post", "actions": [{"action": "quest_done", "id": "bell"}]})"))));
    CHECK(!ok(s.run("set_then", params(R"({"id": "post", "actions": [{"action": "quest_done", "id": "nope"}]})"))));
    g = s.run("quest_graph", params(R"({"quest": "bell"})")).get("result");
    CHECK(edges(g, "step:1", "end", "post") == 1);
    // interior places: a spawn point, or coordinates; both, or no sea hint, are refused
    CHECK(ok(s.run("edit_quest_step", params(R"({"quest": "notice", "step": 2, "fields": {"place": {"stage": "Pnezumi", "room": 0, "sea": {"x": 0, "z": -200000}, "point": 0}}})"))));
    CHECK(step_of(m(), "notice", 2)->get("place").get("stage").string() == "Pnezumi");
    CHECK(ok(s.run("set_quest_step", params(R"({"quest": "notice", "step": 2, "property": "place", "value": {"stage": "Pnezumi", "room": 0, "sea": {"x": 0, "z": -200000}, "x": 10, "y": 0, "z": 20}})"))));
    CHECK(!ok(s.run("edit_quest_step", params(R"({"quest": "notice", "step": 2, "fields": {"place": {"stage": "Pnezumi", "room": 0, "sea": {"x": 0, "z": -200000}, "point": 0, "x": 1, "y": 2, "z": 3}}})"))));
    CHECK(!ok(s.run("edit_quest_step", params(R"({"quest": "notice", "step": 2, "fields": {"place": {"stage": "Pnezumi", "room": 0, "point": 0}}})"))));
    bool warp = false;
    const Value places = s.run("list_locations", params(R"({"group": "Quest"})")).get("result");
    for (const auto& l : places.array) warp = warp || (l.get("stage").string() == "Pnezumi" && l.get("room").number == 0);
    CHECK(warp);
    // availability by flags
    CHECK(ok(s.run("edit_quest_step", params(R"({"quest": "notice", "step": 2, "fields": {"available": {"flag": ["read", "post-read"], "not_flag": "thanked", "text": "Read both first."}}})"))));
    CHECK(!ok(s.run("edit_quest_step", params(R"({"quest": "notice", "step": 2, "fields": {"available": {"flag": "nobody-sets-this"}}})"))));
    // a new step 2: the talks' conditions and actions follow (2 -> 3), undo puts them back
    CHECK(ok(s.run("add_quest_step", params(R"({"quest": "notice", "after": 1, "text": "Look around."})"))));
    const Value after = m();
    const Value tk = actor(after, "keeper")->get("talks");
    CHECK(tk.array[1].get("when").get("step").number == 3 && tk.array[0].get("then").array[0].get("step").number == 3);
    CHECK(actor(m(), "post")->get("when").get("quests").array[0].get("step").number == 3);
    CHECK(ok(s.run("undo")) && actor(m(), "keeper")->get("talks").array[1].get("when").get("step").number == 2);
    // removing a quest takes its conditions and actions out of the talks and quests lists too
    CHECK(ok(s.run("remove_quest", params(R"({"quest": "bell", "confirm": true})"))));
    CHECK(actor(m(), "post")->get("when").get("quests").array.size() == 1 && actor(m(), "post")->get("then").type == Value::Null);
    CHECK(ok(s.run("undo")) && actor(m(), "post")->get("then").array.size() == 1);
    // everything undone: the package as it was
    while (s.undo_depth()) CHECK(ok(s.run("undo")));
    CHECK(m() == start);
}

int main() {
    graph();
    start_at();
    flags();
    step_commands();
    transitions();
    checks();
    search_copy_paste(fs::temp_directory_path() / "wwhd-editor-quest-test");
    quest_format();
    if (g_failures) {
        std::fprintf(stderr, "editor_quest_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("editor_quest_test: ok");
    return 0;
}
