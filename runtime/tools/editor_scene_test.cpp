// World editor: the scene editor's commands (runtime/src/editor/scenes.h, commands_scene.cpp): scenes and
// their steps with undo, camera keys from the fly camera, the timeline and retiming, scrubbing, choices,
// walking paths, triggers, the native version, previews in the game, copy and paste, and a handle drag
// that shows in the game every step but is one undo step. With the docs path: every command is in the
// commands table of docs/mod-editor.md.
// Usage: editor_scene_test [docs/mod-editor.md]
#include "editor/commands.h"
#include "editor/scenes.h"
#include "editor/demo_tracks.h"
#include "editor/demo_names.h"
#include "editor/stage_files.h"
#include "mods/story_world_manifest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

// A game with a fly camera and a scene player: what the editor asks for is recorded.
struct FakeHost : editor::Host {
    editor::Place link_place;
    bool fly_on = true, testing = false, playing_now = false;
    editor::WorldPoint eye{1000, 1500, -201000}, target{300, 1100, -201500};
    std::vector<std::pair<editor::WorldPoint, editor::WorldPoint>> looks;
    int previews = 0, skips = 0, stops = 0, poses = 0, recreated = 0;
    story_notebook::ManifestScene last_scene;
    bool last_declared = false, last_restore = false;
    std::string last_mod;
    std::vector<std::string> in_place;  // the actors pose_actors moves in place (the others are created again)
    // the demo pickers: a game whose object name table has these names and whose archive "Dk" has files
    std::vector<uint32_t> played;
    bool stage_actor_names(std::vector<std::string>& out, std::string&) override { return out = {"Dk", "Ls1", "Tsubo", "Zz9"}, true; }
    bool archive_files(const std::string& archive, std::vector<std::string>& out, std::string& why) override {
        if (archive != "Dk") return why = "the game has no object archive called " + archive, false;
        return out = {"dk.bdl", "fly1.bck", "wait1.bck"}, true;
    }
    bool archive_joints(const std::string& archive, std::vector<std::string>& out, std::string& why) override {
        if (archive != "Dk") return why = "no joints", false;
        return out = {"dk_allroot", "j_dk_ashi_r3"}, true;
    }
    std::vector<std::string> game_archives() override { return {"Dk", "Kamome", "Link"}; }
    std::vector<NamedActor> more_actor_names() override { return {{"Tsubo", "Pot", "Always", "pot", "game objects"}}; }
    bool play_sound(uint32_t id, std::string&) override { played.push_back(id); return true; }
    FakeHost() {
        link_place.valid = true;
        link_place.stage = "sea";
        link_place.room = 11;
        link_place.position = {200, 1040, -201400};
    }
    bool apply_actors(const std::string&, const std::vector<story_notebook::ManifestActor>&, const std::vector<std::string>&, std::string&, std::string&) override {
        return true;
    }
    bool preview_actors(const std::string&, const std::vector<story_notebook::ManifestActor>&, const std::vector<std::string>& changed, std::vector<std::string>&,
                        std::string&) override {
        recreated += int(changed.size());
        return true;
    }
    bool pose_actors(const std::string&, const Value& list, std::vector<std::string>& moved, std::string&) override {
        ++poses;
        for (const auto& pose : list.array)
            if (std::find(in_place.begin(), in_place.end(), pose.get("id").string()) != in_place.end()) moved.push_back(pose.get("id").string());
        return true;
    }
    bool reload_room(std::string&) override { return true; }
    std::vector<std::pair<std::string, std::vector<int>>> stage_rooms() override { return {{"sea", {11}}}; }
    bool warp(const editor::Location&, std::string&) override { return true; }
    editor::Place link() override { return link_place; }
    bool busy(std::string&) override { return false; }
    bool fly(bool, bool, std::string&) override { return true; }
    Value fly_state() override {
        Value v;
        v["on"] = fly_on;
        if (fly_on) {
            v["position"] = editor::point_value(eye);
            v["target"] = editor::point_value(target);
        }
        return v;
    }
    bool set_test_mode(bool on, std::string&) override { return testing = on, true; }
    bool test_mode() override { return testing; }
    bool reset_records(const std::string&, const std::vector<editor::Record>&, const std::vector<editor::Record>&, unsigned&, std::string&) override { return true; }
    bool test_state(bool, int, const std::string&, const std::vector<editor::Record>&, std::string&) override { return true; }
    editor::Place camera() override {
        editor::Place p = link_place;
        p.position = eye;
        return p;
    }
    bool ray(const editor::WorldPoint&, const editor::WorldPoint&, float, editor::WorldPoint& hit, std::string&) override {
        hit = {target.x, 1040, target.z};
        return true;
    }
    bool playing() override { return playing_now; }
    bool look(const editor::WorldPoint& e, const editor::WorldPoint& t, std::string&) override {
        looks.push_back({e, t});
        return true;
    }
    bool preview_scene(const std::string& mod, const story_notebook::ManifestScene& scene, bool declared, bool restore, std::string&) override {
        ++previews;
        testing = true;
        last_mod = mod, last_scene = scene, last_declared = declared, last_restore = restore;
        return true;
    }
    bool scene_control(bool stop, std::string&) override {
        ++(stop ? stops : skips);
        return true;
    }
    Value scene_state() override {
        Value v;
        v["playing"] = previews > stops;
        v["scene"] = last_scene.id;
        return v;
    }
};

Value json(const char* text) { return mods::json::parse(text); }
bool ok(const Value& r) { return r.get("ok").boolean; }
Value params(const char* text) { return json(text); }
std::string err(const Value& r) { return r.get("error").string(); }

// Windfall: two uo2 residents, a skinned mod actor, a sign that starts the harbor scene.
const char* kManifest = R"({
  "format_version": 1, "id": "scene-test", "name": "Scene test", "version": "1.0.0", "kind": "content",
  "models": [{"id": "sprite-model", "kind": "skinned", "source": "assets/sprite.glb"}],
  "actors": [
    {"id": "guide", "actor": "resident", "parameters": {"resident": "uo2"}, "stage": "sea", "room": 11,
     "position": {"x": 100, "y": 1040, "z": -201500}, "lines": ["Come and see."]},
    {"id": "friend", "actor": "resident", "parameters": {"resident": "uo2"}, "stage": "sea", "room": 11,
     "position": {"x": 400, "y": 1040, "z": -201500}, "lines": ["The breeze."]},
    {"id": "sprite", "kind": "mod", "model": "sprite-model", "stage": "sea", "room": 11,
     "position": {"x": 300, "y": 1040, "z": -201500}, "collision": {"radius": 35, "height": 140, "mode": "solid"}},
    {"id": "sign", "actor": "sign", "stage": "sea", "room": 11, "position": {"x": -800, "y": 1040, "z": -201500},
     "flag": "sign-read", "lines": ["Look."], "then": [{"action": "scene", "id": "harbor"}]}
  ],
  "scenes": [
    {"id": "harbor", "steps": [
      {"type": "face", "actor": "guide", "position": {"x": 400, "y": 1040, "z": -201500}},
      {"type": "face", "actor": "friend", "position": {"x": 100, "y": 1040, "z": -201500}},
      {"type": "camera", "eye": {"x": -500, "y": 1350, "z": -201100}, "target": {"x": 250, "y": 1100, "z": -201500}, "frames": 240},
      {"type": "wait", "frames": 120},
      {"type": "end"}
    ]}
  ]
})";

const Value* scene_of(const editor::Session& s, const std::string& id) {
    for (const auto& sc : s.manifest("scene-test")->get("scenes").array)
        if (sc.get("id").string() == id) return &sc;
    return nullptr;
}
const Value& step_at(const editor::Session& s, const std::string& scene, size_t n) { return scene_of(s, scene)->get("steps").array[n - 1]; }
size_t steps(const editor::Session& s, const std::string& scene) { return scene_of(s, scene)->get("steps").array.size(); }
bool valid(const editor::Session& s) {
    std::vector<story_notebook::ManifestScene> parsed;
    std::string why;
    const bool good = story_notebook::parse_scenes(*s.manifest("scene-test"), parsed, why);
    if (!good) std::fprintf(stderr, "  invalid: %s\n", why.c_str());
    return good;
}
bool near(double a, double b) { return std::fabs(a - b) < 0.05; }

void scenes_and_steps() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("scene-test", "Scene test", "", json(kManifest));
    // a new scene: one end step
    Value r = s.run("add_scene", params(R"({"id": "meeting"})"));
    CHECK(ok(r) && r.get("result").get("scene").string() == "meeting");
    CHECK(steps(s, "meeting") == 1 && step_at(s, "meeting", 1).get("type").string() == "end");
    CHECK(!ok(s.run("add_scene", params(R"({"id": "meeting"})"))));
    CHECK(ok(s.run("add_scene")) && scene_of(s, "scene"));
    CHECK(ok(s.run("undo")) && !scene_of(s, "scene"));
    // every step type, each with defaults that pass the checks, before the end
    for (const char* type : {"camera", "say", "move", "face", "wait", "sound", "fade", "camera_path", "camera_follow", "animation", "walk", "particle"}) {
        Value p;
        p["scene"] = "meeting";
        p["type"] = type;
        r = s.run("add_scene_step", p);
        if (!ok(r)) std::fprintf(stderr, "  %s: %s\n", type, err(r).c_str());
        CHECK(ok(r));
    }
    CHECK(steps(s, "meeting") == 13 && step_at(s, "meeting", 13).get("type").string() == "end" && valid(s));
    // the camera took the fly camera's view
    CHECK(near(step_at(s, "meeting", 1).get("eye").get("x").number, 1000) && near(step_at(s, "meeting", 1).get("target").get("z").number, -201500));
    // fields over the defaults; a place in the list
    r = s.run("add_scene_step", params(R"({"scene": "meeting", "type": "wait", "at": 1, "fields": {"frames": 45, "id": "pause"}})"));
    CHECK(ok(r) && r.get("result").get("step").number == 1 && step_at(s, "meeting", 1).get("id").string() == "pause" &&
          step_at(s, "meeting", 1).get("frames").number == 45);
    // an unknown type, a bad field: refused, nothing changes
    const size_t before = s.undo_depth();
    CHECK(!ok(s.run("add_scene_step", params(R"({"scene": "meeting", "type": "dance"})"))));
    CHECK(!ok(s.run("add_scene_step", params(R"({"scene": "meeting", "type": "wait", "fields": {"tempo": 3}})"))));
    CHECK(s.undo_depth() == before);
    // edit several fields at once; by number and by id
    CHECK(ok(s.run("edit_scene_step", params(R"({"scene": "meeting", "step": "pause", "fields": {"frames": 90}})"))));
    CHECK(step_at(s, "meeting", 1).get("frames").number == 90);
    CHECK(ok(s.run("edit_scene_step", params(R"({"scene": "meeting", "step": 3, "fields": {"text": ["Hello there."]}})"))));
    CHECK(step_at(s, "meeting", 3).get("text").array[0].string() == "Hello there.");
    CHECK(ok(s.run("undo")) && step_at(s, "meeting", 3).get("text").array[0].string() == "...");
    CHECK(ok(s.run("redo")) && step_at(s, "meeting", 3).get("text").array[0].string() == "Hello there.");
    // move and duplicate
    CHECK(ok(s.run("move_scene_step", params(R"({"scene": "meeting", "step": "pause", "to": 3})"))));
    CHECK(step_at(s, "meeting", 3).get("id").string() == "pause");
    CHECK(ok(s.run("duplicate_scene_step", params(R"({"scene": "meeting", "step": "pause"})"))));
    CHECK(step_at(s, "meeting", 4).get("id").string() == "pause-2" && step_at(s, "meeting", 4).get("frames").number == 90);
    CHECK(!ok(s.run("duplicate_scene_step", params(R"({"scene": "meeting", "step": 15})"))));  // the end
    // remove; the end can't go (the scene must finish with one)
    const size_t n = steps(s, "meeting");
    CHECK(ok(s.run("remove_scene_step", params(R"({"scene": "meeting", "step": "pause-2"})"))) && steps(s, "meeting") == n - 1);
    Value last;
    last["scene"] = "meeting";
    last["step"] = int(n - 1);
    CHECK(!ok(s.run("remove_scene_step", last)));
    CHECK(valid(s));
    // undo takes back each command, one at a time
    const size_t depth = s.undo_depth();
    CHECK(ok(s.run("undo")) && steps(s, "meeting") == n && s.undo_depth() == depth - 1);
    CHECK(ok(s.run("redo")) && steps(s, "meeting") == n - 1);
}

void choices() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("scene-test", "Scene test", "", json(kManifest));
    // a choice comes with its two branches; the first branch goes on to the step after both
    Value r = s.run("add_scene_step", params(R"({"scene": "harbor", "type": "choice", "at": 4})"));
    CHECK(ok(r) && r.get("result").get("count").number == 3);
    CHECK(step_at(s, "harbor", 4).get("type").string() == "choice");
    const std::string a = step_at(s, "harbor", 4).get("options").array[0].get("next").string();
    const std::string b = step_at(s, "harbor", 4).get("options").array[1].get("next").string();
    CHECK(step_at(s, "harbor", 5).get("id").string() == a && step_at(s, "harbor", 6).get("id").string() == b);
    CHECK(step_at(s, "harbor", 5).get("next").string() == step_at(s, "harbor", 7).get("id").string());
    CHECK(valid(s));
    // labels, where an option leads, the default
    CHECK(ok(s.run("set_scene_choice", params(R"({"scene": "harbor", "step": 4, "option": 1, "label": "Watch"})"))));
    CHECK(step_at(s, "harbor", 4).get("options").array[0].get("text").string() == "Watch");
    CHECK(ok(s.run("set_scene_choice", params(R"({"scene": "harbor", "step": 4, "default": 2})"))) && step_at(s, "harbor", 4).get("default").number == 1);
    CHECK(!ok(s.run("set_scene_choice", params(R"({"scene": "harbor", "step": 4, "option": 1, "label": "This label is far too long for the native choice pane"})"))));
    CHECK(!ok(s.run("set_scene_choice", params(R"({"scene": "harbor", "step": 4, "option": 2, "next": 2})"))));  // backwards
    CHECK(!ok(s.run("set_scene_choice", params(R"({"scene": "harbor", "step": 3, "default": 1})"))));             // not a choice
    // option 2 straight to the end would leave its branch with nothing leading to it: refused, nothing changes
    const size_t depth = s.undo_depth();
    CHECK(!ok(s.run("set_scene_choice", params(R"({"scene": "harbor", "step": 4, "option": 2, "next": 8})"))));
    CHECK(s.undo_depth() == depth && step_at(s, "harbor", 4).get("options").array[1].get("next").string() == b && valid(s));
    // a step another one leads to can't go; renaming it moves the links along
    CHECK(!ok(s.run("remove_scene_step", params(R"({"scene": "harbor", "step": 5})"))));
    Value p;
    p["scene"] = "harbor";
    p["step"] = a;
    p["fields"]["id"] = "watch";
    CHECK(ok(s.run("edit_scene_step", p)));
    CHECK(step_at(s, "harbor", 4).get("options").array[0].get("next").string() == "watch");
    // the timeline knows the branches
    const Value t = s.run("scene_timeline", params(R"({"scene": "harbor"})")).get("result");
    CHECK(t.get("steps").array.size() == steps(s, "harbor"));
    bool branch = false;
    for (const auto& st : t.get("steps").array) branch = branch || st.get("branch").string() == "4:1";
    CHECK(branch);  // the default is option 2: option 1's step is a branch
    // undo all of it
    while (s.undo_depth()) CHECK(ok(s.run("undo")));
    CHECK(steps(s, "harbor") == 5);
}

void camera_keys_timeline_scrub() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("scene-test", "Scene test", "", json(kManifest));
    CHECK(ok(s.run("add_scene", params(R"({"id": "tour"})"))));
    // without a step: a camera step with the fly camera's view
    Value r = s.run("add_camera_key", params(R"({"scene": "tour"})"));
    CHECK(ok(r) && step_at(s, "tour", 1).get("type").string() == "camera" && step_at(s, "tour", 1).get("frames").number == 60);
    // on the camera step: it becomes a path from its view to the new one
    host.eye = {1200, 1500, -201000};
    CHECK(ok(s.run("add_camera_key", params(R"({"scene": "tour", "step": 1, "frames": 120})"))));
    const Value& path = step_at(s, "tour", 1);
    CHECK(path.get("type").string() == "camera_path" && path.get("points").array.size() == 2 && path.get("points").array[0].get("frames").number == 120 &&
          !path.get("points").array[1].object.contains("frames"));
    host.eye = {1400, 1500, -201000};
    CHECK(ok(s.run("add_camera_key", params(R"({"scene": "tour", "step": 1, "frames": 60})"))));
    CHECK(step_at(s, "tour", 1).get("points").array.size() == 3 && valid(s));
    // explicit eye and target; a key between
    CHECK(ok(s.run("add_camera_key", params(R"({"scene": "tour", "step": 1, "at": 2, "frames": 30, "eye": {"x": 1100, "y": 1600, "z": -201000}, "target": {"x": 300, "y": 1100, "z": -201500}})"))));
    CHECK(step_at(s, "tour", 1).get("points").array.size() == 4 && near(step_at(s, "tour", 1).get("points").array[1].get("eye").get("y").number, 1600));
    CHECK(!ok(s.run("add_camera_key", params(R"({"scene": "tour", "step": 1, "eye": {"x": 0, "y": 0, "z": 0}, "target": {"x": 0, "y": 0, "z": 0}})"))));
    // without the fly camera there is no view to take
    host.fly_on = false;
    CHECK(!ok(s.run("add_camera_key", params(R"({"scene": "tour", "step": 1})"))));
    host.fly_on = true;
    // set a key from the camera; remove keys down to a camera step
    host.eye = {900, 1450, -201100};
    CHECK(ok(s.run("set_camera_key", params(R"({"scene": "tour", "step": 1, "key": 4})"))));
    CHECK(near(step_at(s, "tour", 1).get("points").array[3].get("eye").get("x").number, 900));
    CHECK(ok(s.run("remove_camera_key", params(R"({"scene": "tour", "step": 1, "key": 2})"))) && step_at(s, "tour", 1).get("points").array.size() == 3);
    // the timeline: the path's keys at their times
    Value t = s.run("scene_timeline", params(R"({"scene": "tour"})")).get("result");
    CHECK(t.get("duration").number == 180);  // 120 + 60
    CHECK(t.get("steps").array[0].get("keys").array.size() == 3 && t.get("steps").array[0].get("keys").array[1].number == 120);
    // retime one key, then the whole path (scaled: each key keeps its share, the sum exact)
    CHECK(ok(s.run("retime_scene_step", params(R"({"scene": "tour", "step": 1, "key": 2, "frames": 90})"))));
    t = s.run("scene_timeline", params(R"({"scene": "tour"})")).get("result");
    CHECK(t.get("duration").number == 210);
    CHECK(ok(s.run("retime_scene_step", params(R"({"scene": "tour", "step": 1, "frames": 421})"))));
    t = s.run("scene_timeline", params(R"({"scene": "tour"})")).get("result");
    CHECK(t.get("duration").number == 421);
    const auto& pts = step_at(s, "tour", 1).get("points").array;
    CHECK(pts[0].get("frames").number + pts[1].get("frames").number == 421 && pts[0].get("frames").number == 241);
    CHECK(!ok(s.run("retime_scene_step", params(R"({"scene": "tour", "step": 1, "key": 3, "frames": 10})"))));  // the last key
    CHECK(!ok(s.run("retime_scene_step", params(R"({"scene": "tour", "step": 1, "frames": 0})"))));
    // a timed step: frames, "seconds" goes; an untimed one has no duration
    CHECK(ok(s.run("add_scene_step", params(R"({"scene": "tour", "type": "wait", "fields": {"frames": null, "seconds": 2}})"))));
    t = s.run("scene_timeline", params(R"({"scene": "tour"})")).get("result");
    CHECK(t.get("duration").number == 541 && t.get("steps").array[1].get("start").number == 421 && t.get("steps").array[1].get("frames").number == 120);
    CHECK(ok(s.run("retime_scene_step", params(R"({"scene": "tour", "step": 2, "frames": 30})"))));
    CHECK(!step_at(s, "tour", 2).object.contains("seconds") && step_at(s, "tour", 2).get("frames").number == 30);
    CHECK(ok(s.run("add_scene_step", params(R"({"scene": "tour", "type": "sound"})"))));
    CHECK(!ok(s.run("retime_scene_step", params(R"({"scene": "tour", "step": 3, "frames": 30})"))));
    // retiming undoes in one step each
    const double dur = s.run("scene_timeline", params(R"({"scene": "tour"})")).get("result").get("duration").number;
    CHECK(ok(s.run("undo")) && ok(s.run("undo")));  // the sound, the wait's retime
    CHECK(s.run("scene_timeline", params(R"({"scene": "tour"})")).get("result").get("duration").number == dur + 90);
    // scrubbing: the camera at a time; the fly camera shows it
    host.looks.clear();
    r = s.run("scrub_scene", params(R"({"scene": "tour", "time": 0})"));
    CHECK(ok(r) && host.looks.size() == 1 && near(r.get("result").get("eye").get("x").number, step_at(s, "tour", 1).get("points").array[0].get("eye").get("x").number));
    // half way along the first segment: smoothstep(0.5) = 0.5
    Value q;
    q["scene"] = "tour";
    q["time"] = step_at(s, "tour", 1).get("points").array[0].get("frames").number / 2;
    q["move"] = false;
    r = s.run("scrub_scene", q);
    const double x0 = step_at(s, "tour", 1).get("points").array[0].get("eye").get("x").number, x1 = step_at(s, "tour", 1).get("points").array[1].get("eye").get("x").number;
    CHECK(ok(r) && near(r.get("result").get("eye").get("x").number, (x0 + x1) / 2) && host.looks.size() == 1);
    // after the path: its last key holds
    r = s.run("scrub_scene", params(R"({"scene": "tour", "time": 100000, "move": false})"));
    CHECK(ok(r) && near(r.get("result").get("eye").get("x").number, step_at(s, "tour", 1).get("points").array.back().get("eye").get("x").number));
    // a pan: the eye moves along it
    CHECK(ok(s.run("edit_scene_step", params(R"({"scene": "harbor", "step": 3, "fields": {"pan": true, "eye_to": {"x": 500, "y": 1350, "z": -201100}, "target_to": {"x": 250, "y": 1100, "z": -201500}}})"))));
    r = s.run("scrub_scene", params(R"({"scene": "harbor", "time": 120, "move": false})"));
    CHECK(ok(r) && near(r.get("result").get("eye").get("x").number, 0) && r.get("result").get("step").number == 3);
    // before the first camera: nothing to show
    CHECK(!ok(s.run("scrub_scene", params(R"({"scene": "meeting-none", "time": 0})"))));
    CHECK(ok(s.run("add_scene", params(R"({"id": "dark"})"))));
    CHECK(!ok(s.run("scrub_scene", params(R"({"scene": "dark", "time": 0})"))));
}

void walking_paths_and_triggers() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("scene-test", "Scene test", "", json(kManifest));
    CHECK(ok(s.run("add_scene_step", params(R"({"scene": "harbor", "type": "walk"})"))));
    const size_t w = steps(s, "harbor") - 1;  // before the end
    Value p;
    p["scene"] = "harbor";
    p["step"] = int(w);
    CHECK(step_at(s, "harbor", w).get("actor").string() == "sprite");
    // a point where the camera looks (on the ground there), at Link, at a position
    CHECK(ok(s.run("add_walk_point", p)));
    CHECK(step_at(s, "harbor", w).get("points").array.size() == 3 && near(step_at(s, "harbor", w).get("points").array[2].get("y").number, 1040));
    Value q = p;
    q["from"] = "link";
    q["at"] = 1;
    CHECK(ok(s.run("add_walk_point", q)) && near(step_at(s, "harbor", w).get("points").array[0].get("x").number, 200));
    q = p;
    q["point"] = 2;
    q["position"]["x"] = 700, q["position"]["y"] = 1040, q["position"]["z"] = -201300;
    CHECK(ok(s.run("set_walk_point", q)) && near(step_at(s, "harbor", w).get("points").array[1].get("x").number, 700));
    q = p;
    q["point"] = 4;
    CHECK(ok(s.run("remove_walk_point", q)) && step_at(s, "harbor", w).get("points").array.size() == 3);
    q["point"] = 1;
    CHECK(ok(s.run("remove_walk_point", q)));
    CHECK(!ok(s.run("remove_walk_point", q)));  // two stay
    CHECK(valid(s));
    // not a walk
    Value r = p;
    r["step"] = 1;
    CHECK(!ok(s.run("add_walk_point", r)));
    // triggers: talk, an area around Link, none
    CHECK(ok(s.run("set_scene_trigger", params(R"({"scene": "harbor", "type": "talk", "actor": "guide"})"))));
    CHECK(scene_of(s, "harbor")->get("trigger").get("actor").string() == "guide");
    CHECK(!ok(s.run("set_scene_trigger", params(R"({"scene": "harbor", "type": "talk", "actor": "nobody"})"))));
    CHECK(ok(s.run("set_scene_trigger", params(R"({"scene": "harbor", "type": "area", "radius": 300})"))));
    const Value& t = scene_of(s, "harbor")->get("trigger");
    CHECK(t.get("type").string() == "area" && t.get("stage").string() == "sea" && t.get("room").number == 11 && near(t.get("position").get("x").number, 200) &&
          t.get("radius").number == 300);
    CHECK(ok(s.run("set_scene_trigger", params(R"({"scene": "harbor", "type": "room", "at": "camera"})"))));
    CHECK(scene_of(s, "harbor")->get("trigger").get("type").string() == "room");
    CHECK(ok(s.run("set_scene_trigger", params(R"({"scene": "harbor", "type": "none"})"))) && !scene_of(s, "harbor")->object.contains("trigger"));
    CHECK(ok(s.run("undo")) && scene_of(s, "harbor")->get("trigger").get("type").string() == "room");
}

void native_version() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("scene-test", "Scene test", "", json(kManifest));
    Value r = s.run("native_scene", params(R"({"scene": "harbor"})"));
    CHECK(ok(r) && !r.get("result").get("eligible").boolean);  // none yet
    r = s.run("native_scene", params(R"({"scene": "harbor", "from_steps": true})"));
    CHECK(ok(r) && r.get("result").get("eligible").boolean);
    const Value& n = scene_of(s, "harbor")->get("native");
    CHECK(n.get("type").string() == "turn_camera" && n.get("actors").array.size() == 2 && n.get("actors").array[0].get("id").string() == "guide" &&
          near(n.get("actors").array[0].get("turn_to").get("x").number, 400));
    CHECK(n.get("camera").get("hold_ticks").number == 120 && n.get("wait_ticks").number == 180 && n.get("watchdog_ticks").number > n.get("timekeeper_ticks").number);
    CHECK(valid(s));
    // a say step can't be native: the native version stays, but would not play
    CHECK(ok(s.run("add_scene_step", params(R"({"scene": "harbor", "type": "say", "at": 1})"))));
    r = s.run("native_scene", params(R"({"scene": "harbor"})"));
    CHECK(ok(r) && !r.get("result").get("eligible").boolean && !r.get("result").get("why").string().empty());
    CHECK(!ok(s.run("native_scene", params(R"({"scene": "harbor", "from_steps": true})"))));
    // removed with null
    CHECK(ok(s.run("native_scene", params(R"({"scene": "harbor", "value": null})"))) && !scene_of(s, "harbor")->object.contains("native"));
    // a block that breaks the rules: refused by the validator
    CHECK(!ok(s.run("native_scene", params(R"({"scene": "harbor", "value": {"type": "turn_camera"}})"))));
    CHECK(ok(s.run("undo")) && scene_of(s, "harbor")->object.contains("native"));
    // another scene of the package (the sign starts the first one): checked with the package's scenes
    CHECK(ok(s.run("add_scene", params(R"({"id": "dock"})"))));
    for (const char* step : {R"({"scene": "dock", "type": "face", "fields": {"actor": "guide", "position": {"x": 0, "y": 1040, "z": -201500}}})",
                             R"({"scene": "dock", "type": "face", "fields": {"actor": "friend", "position": {"x": 500, "y": 1040, "z": -201500}}})",
                             R"({"scene": "dock", "type": "camera"})", R"({"scene": "dock", "type": "wait"})"})
        CHECK(ok(s.run("add_scene_step", params(step))));
    r = s.run("native_scene", params(R"({"scene": "dock", "from_steps": true})"));
    CHECK(ok(r) && r.get("result").get("eligible").boolean);
}

void previews() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("scene-test", "Scene test", "", json(kManifest));
    // the scene as the game started with it: its native version may play
    Value r = s.run("preview_scene", params(R"({"scene": "harbor"})"));
    CHECK(ok(r) && host.previews == 1 && host.last_declared && host.last_restore && host.last_scene.id == "harbor" && host.last_scene.steps.size() == 5);
    // edited: the edited steps play
    CHECK(ok(s.run("edit_scene_step", params(R"({"scene": "harbor", "step": 4, "fields": {"frames": 60}})"))));
    r = s.run("preview_scene", params(R"({"scene": "harbor", "restore": false})"));
    CHECK(ok(r) && !host.last_declared && !host.last_restore && host.last_scene.steps[3].frames == 60);
    // one step, a range: a scene of their own with an end
    r = s.run("preview_scene", params(R"({"scene": "harbor", "from": 3})"));
    CHECK(ok(r) && host.last_scene.id == "editor-preview" && host.last_scene.steps.size() == 2 && host.last_scene.steps[0].kind == story_notebook::SceneStep::Kind::Camera);
    r = s.run("preview_scene", params(R"({"scene": "harbor", "from": 2, "to": 4})"));
    CHECK(ok(r) && host.last_scene.steps.size() == 4 && r.get("result").get("from").number == 2 && r.get("result").get("to").number == 4);
    // a range inside a choice's branches: what leaves the range ends there
    CHECK(ok(s.run("add_scene_step", params(R"({"scene": "harbor", "type": "choice", "at": 4})"))));
    r = s.run("preview_scene", params(R"({"scene": "harbor", "from": 4, "to": 5})"));
    CHECK(ok(r) && host.last_scene.steps.front().kind == story_notebook::SceneStep::Kind::Choice);
    // the package is not changed by previews
    const size_t depth = s.undo_depth();
    CHECK(ok(s.run("preview_scene", params(R"({"scene": "harbor", "from": 1, "to": 2})"))) && s.undo_depth() == depth);
    // skip, stop, state
    CHECK(ok(s.run("skip_scene")) && host.skips == 1);
    CHECK(ok(s.run("scene_state")) && s.run("scene_state").get("result").get("playing").boolean);
    CHECK(ok(s.run("stop_scene")) && host.stops == 1);
    // not while playing
    host.playing_now = true;
    CHECK(!ok(s.run("preview_scene", params(R"({"scene": "harbor"})"))));
    host.playing_now = false;
    CHECK(!ok(s.run("preview_scene", params(R"({"scene": "nope"})"))));
    // without a game
    editor::Session offline(nullptr);
    offline.add_package("scene-test", "Scene test", "", json(kManifest));
    CHECK(!ok(offline.run("preview_scene", params(R"({"scene": "harbor"})"))));
}

void copy_paste_remove() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("scene-test", "Scene test", "", json(kManifest));
    // scenes: pasted under ids of their own, without a trigger
    CHECK(ok(s.run("set_scene_trigger", params(R"({"scene": "harbor", "type": "talk", "actor": "friend"})"))));
    CHECK(ok(s.run("copy", params(R"({"scenes": ["harbor"]})"))));
    Value r = s.run("paste");
    CHECK(ok(r) && r.get("result").get("ids").array[0].string() == "harbor-2" && scene_of(s, "harbor-2") && !scene_of(s, "harbor-2")->object.contains("trigger"));
    CHECK(ok(s.run("undo")) && !scene_of(s, "harbor-2"));
    // steps: before another scene's end
    CHECK(ok(s.run("add_scene", params(R"({"id": "other"})"))));
    CHECK(ok(s.run("copy", params(R"({"scene": "harbor", "steps": [3, 4]})"))));
    r = s.run("paste", params(R"({"scene": "other"})"));
    CHECK(ok(r) && steps(s, "other") == 3 && step_at(s, "other", 1).get("type").string() == "camera" && step_at(s, "other", 3).get("type").string() == "end");
    CHECK(!ok(s.run("copy", params(R"({"scene": "harbor", "steps": [5]})"))));  // an end
    // a choice only with its branches
    CHECK(ok(s.run("add_scene_step", params(R"({"scene": "harbor", "type": "choice", "at": 4})"))));
    CHECK(ok(s.run("copy", params(R"({"scene": "harbor", "steps": [4]})"))));
    CHECK(!ok(s.run("paste", params(R"({"scene": "other"})"))));
    CHECK(ok(s.run("copy", params(R"({"scene": "harbor", "steps": [4, 5, 6]})"))));
    r = s.run("paste", params(R"({"scene": "other", "before": 1})"));
    CHECK(ok(r) && steps(s, "other") == 6 && valid(s));
    // actors' copy and paste still work
    CHECK(ok(s.run("copy", params(R"({"ids": ["guide"]})"))) && ok(s.run("paste")));
    // a scene the sign starts can't be removed; another one can
    CHECK(!ok(s.run("remove_scene", params(R"({"id": "harbor", "confirm": true})"))));
    CHECK(!ok(s.run("remove_scene", params(R"({"id": "other"})"))));  // needs confirm
    const Value other_before = *scene_of(s, "other");
    CHECK(ok(s.run("remove_scene", params(R"({"id": "other", "confirm": true})"))) && !scene_of(s, "other"));
    // Undo brings the removed scene back as it was (its steps too); Redo removes it again
    CHECK(ok(s.run("undo")) && scene_of(s, "other") && *scene_of(s, "other") == other_before);
    CHECK(ok(s.run("redo")) && !scene_of(s, "other"));
    CHECK(ok(s.run("duplicate_scene", params(R"({"id": "harbor", "new_id": "harbor-copy"})"))) && scene_of(s, "harbor-copy") &&
          !scene_of(s, "harbor-copy")->object.contains("trigger"));
    // search finds a scene by a step's text
    CHECK(ok(s.run("edit_scene_step", params(R"({"scene": "harbor", "step": 4, "fields": {"text": ["Which lantern?"]}})"))));
    r = s.run("search", params(R"({"text": "lantern"})"));
    bool found = false;
    for (const auto& h : r.get("result").array) found = found || (h.get("kind").string() == "scene" && h.get("id").string() == "harbor");
    CHECK(found);
}

// A handle drag: the game shows the actor every step (in place where it can), the package keeps one
// change when the mouse is let go: one undo step for the whole drag.
void drag_is_one_undo_step() {
    FakeHost host;
    host.in_place = {"guide"};
    editor::Session s(&host);
    s.add_package("scene-test", "Scene test", "", json(kManifest));
    const Value before = *s.manifest("scene-test");
    for (int i = 1; i <= 60; i++) {
        Value poses;
        poses.type = Value::Array;
        Value pose;
        pose["id"] = "guide";
        pose["position"]["x"] = 100 + i * 5, pose["position"]["y"] = 1040, pose["position"]["z"] = -201500;
        poses.array.push_back(pose);
        std::vector<std::string> later;
        std::string why;
        CHECK(s.drag_preview("scene-test", poses, later, why));
    }
    CHECK(host.poses == 60 && host.recreated == 0);  // every step in place, nothing created again
    CHECK(s.undo_depth() == 0 && *s.manifest("scene-test") == before && s.log().empty());
    CHECK(ok(s.run("move_actor", params(R"({"id": "guide", "position": {"x": 400, "y": 1040, "z": -201400}})"))));
    CHECK(s.undo_depth() == 1);
    CHECK(ok(s.run("undo")) && *s.manifest("scene-test") == before);
    // a group: several actors per step, the ones that can't move in place are created again
    Value poses;
    poses.type = Value::Array;
    for (const char* id : {"guide", "friend"}) {
        Value pose;
        pose["id"] = id;
        pose["position"]["x"] = 10, pose["position"]["y"] = 1040, pose["position"]["z"] = -201500;
        poses.array.push_back(pose);
    }
    std::vector<std::string> later;
    std::string why;
    CHECK(s.drag_preview("scene-test", poses, later, why) && host.recreated == 1);
    CHECK(ok(s.run("move_actors", params(R"({"ids": ["guide", "friend"], "offset": {"x": 10, "y": 0, "z": 0}})"))) && s.undo_depth() == 1);
    // Esc: poses without places put the actors back where the package has them
    Value back;
    back.type = Value::Array;
    Value pose;
    pose["id"] = "guide";
    back.array.push_back(pose);
    CHECK(s.drag_preview("scene-test", back, later, why) && s.undo_depth() == 1);
    CHECK(!s.drag_preview("scene-test", params(R"([{"id": "nobody"}])"), later, why));
}


// A demo step built and edited key by key: lanes, poses at a frame (the preview), events, actors, curves,
// the game's own check, and undo / redo.
void demo_steps() {
    FakeHost host;
    editor::Session s(&host);
    s.add_package("scene-test", "Scene test", "", json(kManifest));
    CHECK(ok(s.run("add_scene", params(R"({"id": "flight"})"))));
    // a new demo step: the fly camera's view as its first camera key, 120 frames (240 on the timeline)
    Value r = s.run("add_scene_step", params(R"({"scene": "flight", "type": "demo"})"));
    CHECK(ok(r));
    const size_t n = r.get("result").get("step").number;
    CHECK(step_at(s, "flight", n).get("type").string() == "demo" && valid(s));
    const std::string at = std::to_string(n);
    auto run = [&](const std::string& cmd, const std::string& extra) { return s.run(cmd, params(("{\"scene\": \"flight\", \"step\": " + at + ", " + extra + "}").c_str())); };
    Value t = s.run("scene_timeline", params(R"({"scene": "flight"})")).get("result");
    CHECK(t.get("duration").number == 240);
    // actors: a model from the game's archives, a mod character (the placed uo2), Link
    CHECK(ok(run("add_demo_actor", R"("actor": {"id": "bird", "actor": "Dk"})")));
    CHECK(ok(run("add_demo_actor", R"("actor": {"id": "guide", "character": "guide"})")));
    CHECK(!ok(run("add_demo_actor", R"("actor": {"id": "bird", "model": {"archive": "Dk", "file": "dk.bdl"}})")));  // ids are unique
    // the bird's flight: three keys, smooth; a key replaced; one moved; the lane's curve
    CHECK(ok(run("set_demo_key", R"("lane": "bird.position", "frame": 0, "value": {"x": -2500, "y": 1800, "z": 3000})")));
    CHECK(ok(run("set_demo_key", R"("lane": "bird.position", "frame": 120, "value": {"x": 2500, "y": 900, "z": 3000}, "curve": "smooth")")));
    CHECK(ok(run("set_demo_key", R"("lane": "bird.position", "frame": 60, "value": {"x": 0, "y": 1500, "z": 3200})")));
    CHECK(ok(run("set_demo_key", R"("lane": "bird.position", "frame": 60, "value": {"x": 0, "y": 1400, "z": 3200})")));
    CHECK(ok(run("move_demo_key", R"("lane": "bird.position", "frame": 60, "to": 50)")));
    CHECK(!ok(run("move_demo_key", R"("lane": "bird.position", "frame": 50, "to": 120)")));  // a key is there
    CHECK(!ok(run("set_demo_key", R"("lane": "bird.position", "frame": 121, "value": {"x": 0, "y": 0, "z": 0})")));  // past the end
    CHECK(!ok(run("set_demo_key", R"("lane": "camera.fovy", "frame": 0, "value": {"x": 0, "y": 0, "z": 0})")));  // a number lane
    CHECK(ok(run("set_demo_key", R"("lane": "camera.fovy", "frame": 0, "value": 55)")));
    CHECK(ok(run("set_demo_curve", R"("lane": "bird.position", "curve": "bspline")")));
    CHECK(!ok(run("set_demo_curve", R"("lane": "bird.position", "curve": "wobbly")")));
    // the camera's target from the fly camera
    host.target = {100, 1200, -201300};
    CHECK(ok(run("set_demo_key", R"("lane": "camera.target", "frame": 120, "from": "camera")")));
    // events: a clip on the bird, a message, rain, the hour, the guide carried by the bird and let go
    CHECK(ok(run("add_demo_event", R"("list": "clips", "actor": "bird", "event": {"frame": 0, "archive": "Dk", "file": "fly1.bck"})")));
    CHECK(ok(run("add_demo_event", R"("list": "messages", "event": {"frame": 40, "text": "The Helmaroc King!"})")));
    CHECK(ok(run("add_demo_event", R"("list": "weather", "event": {"frame": 20, "rain": 1})")));
    CHECK(ok(run("add_demo_event", R"("list": "time", "event": {"frame": 10, "hour": 22})")));
    { Value a = run("add_demo_event", R"("list": "attach", "event": {"frame": 60, "actor": "guide", "to": "bird", "joint": "j_dk_ashi_r3"})"); if (!ok(a)) std::fprintf(stderr, "  attach: %s\n", err(a).c_str()); CHECK(ok(a)); }
    CHECK(ok(run("add_demo_event", R"("list": "detach", "event": {"frame": 100, "actor": "guide"})")));
    CHECK(!ok(run("add_demo_event", R"("list": "clips", "event": {"frame": 0, "archive": "Dk", "file": "fly1.bck"})")));  // whose clip?
    CHECK(!ok(run("add_demo_event", R"("list": "weather", "event": {"frame": 30, "rain": 5})")));  // the game's check: rain is 0..1
    CHECK(ok(run("edit_demo_event", R"("list": "messages", "index": 0, "fields": {"text": "Look up!"})")));
    CHECK(valid(s));
    // the lanes as the timeline draws them
    Value lanes = run("demo_lanes", "\"mod\": \"scene-test\"").get("result");
    if (lanes.object.contains("problem")) std::fprintf(stderr, "  problem: %s\n", lanes.get("problem").string().c_str());
    CHECK(lanes.get("frames").number == 120 && !lanes.object.contains("problem"));
    bool bird = false, fovy = false;
    for (const auto& l : lanes.get("lanes").array) {
        if (l.get("lane").string() == "bird.position") bird = l.get("keys").array.size() == 3 && l.get("keys").array[1].number == 50 && l.get("curve").string() == "bspline";
        if (l.get("lane").string() == "camera.fovy") fovy = l.get("kind").string() == "number";
    }
    CHECK(bird && fovy);
    int lists = 0;
    for (const auto& e : lanes.get("events").array) {
        const std::string l = e.get("list").string();
        if (l == "messages") lists += e.get("items").array[0].get("label").string() == "Look up!";
        if (l == "weather" || l == "time" || l == "attach" || l == "detach") ++lists;
        if (l == "clips") lists += e.get("actor").string() == "bird" && e.get("items").array[0].get("label").string() == "Dk/fly1.bck";
    }
    CHECK(lists == 6 && lanes.get("actors").array.size() == 2);
    // the pose at a frame: on the curve, in the world (no origin), the fly camera moved there
    Value pose = run("demo_pose", "\"frame\": 120").get("result");
    CHECK(near(pose.get("actors").array[0].get("position").get("x").number, 2500) && near(pose.get("camera").get("target").get("x").number, 100));
    CHECK(pose.get("camera").get("fovy").number == 55 && !host.looks.empty());
    pose = run("demo_pose", "\"frame\": 0, \"move\": false").get("result");
    CHECK(near(pose.get("actors").array[0].get("position").get("y").number, (5 * 1800 + 1400) / 6.0));  // bspline at the first key: (P0 + 4 P0 + P1) / 6
    // an origin at Link: keys from Link's place are relative to it, the pose is placed back at Link
    CHECK(ok(s.run("edit_scene_step", params(("{\"scene\": \"flight\", \"step\": " + at + ", \"fields\": {\"origin\": \"link\"}}").c_str()))));
    CHECK(ok(run("set_demo_key", R"("lane": "guide.position", "frame": 0, "from": "link")")));
    CHECK(near(step_at(s, "flight", n).get("actors").array[1].get("position").get("keys").array[0].get("value").get("x").number, 0));
    pose = run("demo_pose", "\"frame\": 0, \"move\": false").get("result");
    CHECK(near(pose.get("actors").array[1].get("position").get("x").number, 200) && near(pose.get("actors").array[1].get("position").get("z").number, -201400));
    // what the game refuses shows as the step's problem, and a refused edit changes nothing
    CHECK(!ok(run("remove_demo_actor", R"("id": "guide")")));  // the attachment names it
    CHECK(ok(run("remove_demo_event", R"("list": "detach", "index": 0)")));
    CHECK(ok(run("remove_demo_event", R"("list": "attach", "index": 0)")));
    CHECK(ok(run("remove_demo_actor", R"("id": "guide")")));
    // undo and redo of key edits
    CHECK(ok(run("remove_demo_key", R"("lane": "bird.position", "frame": 50)")));
    CHECK(step_at(s, "flight", n).get("actors").array[0].get("position").get("keys").array.size() == 2);
    CHECK(ok(s.run("undo")) && step_at(s, "flight", n).get("actors").array[0].get("position").get("keys").array.size() == 3);
    CHECK(ok(s.run("redo")) && step_at(s, "flight", n).get("actors").array[0].get("position").get("keys").array.size() == 2);
    // a lane goes with its last key
    CHECK(ok(run("remove_demo_key", R"("lane": "camera.fovy", "frame": 0)")) && !step_at(s, "flight", n).get("camera").object.contains("fovy"));
    CHECK(!ok(s.run("set_demo_key", params(R"({"scene": "flight", "step": 1, "lane": "camera.eye", "frame": 0, "value": {"x": 0, "y": 0, "z": 0}})"))) || step_at(s, "flight", 1).get("type").string() == "demo");
    CHECK(valid(s));
}

// The demo step's pickers: names for sounds, particles and actors, an archive's files and joints, the
// suggested ids, the sound preview; and the BFRES bone reader behind the joint lists.
void demo_pickers() {
    FakeHost host;
    editor::Session s(&host);
    auto items = [&](const char* json) {
        const Value r = s.run("demo_choices", params(json));
        CHECK(ok(r));
        return r.get("result").get("items");
    };
    // sounds and particles: ids with readable names, searchable
    const Value sounds = items(R"({"what": "sounds"})");
    CHECK(sounds.array.size() == editor::sound_names().size() && sounds.array.size() > 200);
    const Value splash = items(R"({"what": "sounds", "query": "fall water"})");
    CHECK(!splash.array.empty() && splash.array[0].get("code").string().find("FALL_WATER") != std::string::npos && splash.array[0].get("id").number > 0);
    CHECK(items(R"({"what": "particles", "query": "smoke"})").array.size() >= 3);
    CHECK(editor::sound_label(0x6918).find("fall water") != std::string::npos && editor::sound_label(0x12345) == "0x12345");
    // actors: ours, the editor's catalogues, then the game's other names; a listed name the game lacks is dropped
    const Value actors = items(R"({"what": "actors"})");
    auto actor = [&](const std::string& name) -> const Value* {
        for (const auto& a : actors.array)
            if (a.get("actor").string() == name) return &a;
        return nullptr;
    };
    CHECK(actor("Dk") && actor("Dk")->get("id").string() == "helmaroc" && actor("Dk")->get("archive").string() == "Dk");
    CHECK(actor("Tsubo") && actor("Tsubo")->get("label").string() == "Pot");
    CHECK(actor("Zz9") && actor("Zz9")->get("group").string() == "other");
    CHECK(!actor("Cb1"));  // ours, but not in this game's table
    CHECK(items(R"({"what": "actors", "query": "aryll"})").array.size() == 1);
    // an archive's files and joints; refused for an unknown one
    CHECK(items(R"({"what": "files", "archive": "Dk", "query": "bck"})").array.size() == 2);
    CHECK(items(R"({"what": "joints", "archive": "Dk"})").array.size() == 2);
    CHECK(!ok(s.run("demo_choices", params(R"({"what": "files", "archive": "Nope"})"))));
    CHECK(!ok(s.run("demo_choices", params(R"({"what": "colours"})"))));
    CHECK(items(R"({"what": "link_modes"})").array.size() == editor::link_mode_names().size());
    CHECK(items(R"({"what": "archives", "query": "kam"})").array.size() == 1);
    // the sound preview goes to the game
    CHECK(ok(s.run("play_sound", params(R"({"id": 26904})"))) && host.played == std::vector<uint32_t>{26904});
    CHECK(!ok(s.run("play_sound", params(R"({})"))));
    // suggested ids: from the choice, unique in the demo
    CHECK(editor::suggest_demo_id("helmaroc", {}) == "helmaroc");
    CHECK(editor::suggest_demo_id("Helmaroc King (the one)", {}) == "helmaroc-king-the");
    CHECK(editor::suggest_demo_id("link", {"link", "link-2"}) == "link-3");
    CHECK(editor::suggest_demo_id("!!", {}) == "actor");
    CHECK(editor::matches_query("voice: pig normal", "PIG voice") && !editor::matches_query("voice: pig normal", "pig turn"));
    // BFRES bones: a synthetic file with one model of two bones
    std::vector<uint8_t> b(0x200, 0);
    auto put32 = [&](size_t at, uint32_t v) { b[at] = uint8_t(v >> 24), b[at + 1] = uint8_t(v >> 16), b[at + 2] = uint8_t(v >> 8), b[at + 3] = uint8_t(v); };
    auto rel = [&](size_t at, size_t to) { put32(at, uint32_t(int32_t(to) - int32_t(at))); };
    auto text = [&](size_t at, const char* t) { std::memcpy(&b[at], t, std::strlen(t)); };
    text(0, "FRES");
    rel(0x20, 0x40);                       // the model group
    put32(0x44, 1);                        // one model (after the root entry)
    rel(0x40 + 8 + 16 + 8, 0x180);         // its name
    rel(0x40 + 8 + 16 + 12, 0x80);         // its data
    text(0x80, "FMDL");
    rel(0x80 + 12, 0xA0);                  // the skeleton
    text(0xA0, "FSKL");
    rel(0xA0 + 0x10, 0xC0);                // the bone group
    put32(0xC4, 2);
    rel(0xC0 + 8 + 16 + 8, 0x188);
    rel(0xC0 + 8 + 32 + 8, 0x190);
    text(0x180, "bird");
    text(0x188, "root");
    text(0x190, "j_wing");
    CHECK((editor::bfres_bones(b) == std::vector<std::string>{"root", "j_wing"}));
    CHECK(editor::bfres_bones(std::vector<uint8_t>(16, 0)).empty());
}

void docs_table(const char* path) {
    std::ifstream f(path);
    std::stringstream text;
    text << f.rdbuf();
    const std::string doc = text.str();
    CHECK(!doc.empty());
    for (const auto& c : editor::command_list()) {
        const bool listed = doc.find("| `" + c.name + "`") != std::string::npos || doc.find(", `" + c.name + "`") != std::string::npos;
        if (!listed) std::fprintf(stderr, "  not in the commands table: %s\n", c.name.c_str());
        CHECK(listed);
    }
}

}  // namespace

int main(int argc, char** argv) {
    scenes_and_steps();
    choices();
    camera_keys_timeline_scrub();
    walking_paths_and_triggers();
    native_version();
    previews();
    copy_paste_remove();
    drag_is_one_undo_step();
    demo_steps();
    demo_pickers();
    if (argc > 1) docs_table(argv[1]);
    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("editor scene test: ok");
    return 0;
}
