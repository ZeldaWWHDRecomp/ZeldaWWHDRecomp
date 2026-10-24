// World editor: the scene editor's pure helpers (scenes.h). No game dependencies.
#include "scenes.h"
#include "demo_tracks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../mods/story_native_scene_coverage.h"
#include "../mods/story_scene_motion.h"
#include "../mods/story_world_manifest.h"

namespace editor {
namespace {
namespace motion = story_scenes::motion;

Value arr() {
    Value v;
    v.type = Value::Array;
    return v;
}
double tenth(double v) { return std::round(v * 10) / 10; }
Value pt(const WorldPoint& p) {
    Value v;
    v["x"] = tenth(p.x);
    v["y"] = tenth(p.y);
    v["z"] = tenth(p.z);
    return v;
}
// {x, y, z} or [x, y, z]
bool pos(const Value& v, WorldPoint& p) {
    if (v.type == Value::Array && v.array.size() == 3) {
        for (const auto& n : v.array)
            if (n.type != Value::Number || !std::isfinite(n.number)) return false;
        p = {float(v.array[0].number), float(v.array[1].number), float(v.array[2].number)};
        return true;
    }
    if (v.type != Value::Object) return false;
    for (const char* k : {"x", "y", "z"})
        if (v.get(k).type != Value::Number || !std::isfinite(v.get(k).number)) return false;
    p = {float(v.get("x").number), float(v.get("y").number), float(v.get("z").number)};
    return true;
}
motion::Point mp(const WorldPoint& p) { return {p.x, p.y, p.z}; }
WorldPoint wp(const motion::Point& p) { return {float(p.x), float(p.y), float(p.z)}; }
WorldPoint mix(const WorldPoint& a, const WorldPoint& b, double t) {
    return {float(a.x + (b.x - a.x) * t), float(a.y + (b.y - a.y) * t), float(a.z + (b.z - a.z) * t)};
}
double dist(const WorldPoint& a, const WorldPoint& b) {
    const double x = double(a.x) - b.x, y = double(a.y) - b.y, z = double(a.z) - b.z;
    return std::sqrt(x * x + y * y + z * z);
}
std::string type_of(const Value& step) { return step.get("type").string(); }
const std::vector<Value>& steps_of(const Value& scene) { return scene.get("steps").array; }
long index_of_id(const Value& scene, const std::string& id, size_t after) {
    const auto& steps = steps_of(scene);
    for (size_t j = after + 1; j < steps.size(); j++)
        if (steps[j].get("id").string() == id) return long(j);
    return -1;
}
// the steps that name `id` (next, options)
std::vector<size_t> naming(const Value& scene, const std::string& id) {
    std::vector<size_t> out;
    if (id.empty()) return out;
    const auto& steps = steps_of(scene);
    for (size_t i = 0; i < steps.size(); i++) {
        bool names = steps[i].get("next").string() == id;
        for (const auto& o : steps[i].get("options").array) names = names || o.get("next").string() == id;
        if (names) out.push_back(i);
    }
    return out;
}
// The default path: from the first step, each choice's default option.
std::vector<size_t> default_path(const Value& scene) {
    std::vector<size_t> out;
    const auto& steps = steps_of(scene);
    size_t i = 0;
    std::set<size_t> seen;
    while (i < steps.size() && seen.insert(i).second) {
        out.push_back(i);
        const Value& s = steps[i];
        if (type_of(s) == "end") break;
        long next = -1;
        if (type_of(s) == "choice") {
            const int d = s.get("default").type == Value::Number ? int(s.get("default").number) : 0;
            const auto& options = s.get("options").array;
            if (d >= 0 && size_t(d) < options.size()) next = index_of_id(scene, options[size_t(d)].get("next").string(), i);
        } else if (s.get("next").type == Value::String)
            next = index_of_id(scene, s.get("next").text, i);
        else
            next = long(i + 1);
        if (next < 0) break;
        i = size_t(next);
    }
    return out;
}
// the first actor of the manifest that fits a step
std::string first_actor(const Value& m, const char* what) {
    for (const auto& a : m.get("actors").array) {
        const std::string id = a.get("id").string(), actor = a.get("actor").string(), kind = a.get("kind").string();
        const bool resident = (actor == "resident" || actor == "NpcPeople" || actor == "PROC_NPC_PEOPLE") && a.get("parameters").get("resident").string() != "ym1";
        bool skinned = false;
        if (kind == "mod")
            for (const auto& md : m.get("models").array)
                if (md.get("id").string() == a.get("model").string()) skinned = md.get("kind").string() == "skinned";
        const std::string w = what;
        if (w == "any" || (w == "mover" && (resident || kind == "mod")) || (w == "skinned" && skinned) || (w == "uo2" && resident && a.get("parameters").get("resident").string() == "uo2"))
            return id;
    }
    return {};
}
bool actor_position(const Value& m, const std::string& id, WorldPoint& p) {
    for (const auto& a : m.get("actors").array)
        if (a.get("id").string() == id) return pos(a.get("position"), p);
    return false;
}
unsigned clampf(double frames) { return unsigned(std::clamp(std::lround(frames), 1L, long(kSceneFramesMax))); }
}  // namespace

bool scene_step_timed(const std::string& t) {
    return t == "camera" || t == "say" || t == "move" || t == "wait" || t == "fade" || t == "camera_path" || t == "camera_follow" || t == "walk" ||
           t == "choice";
}
unsigned scene_step_frames(const Value& step) {
    const std::string t = type_of(step);
    if (t == "camera_path") {
        unsigned sum = 0;
        for (const auto& k : step.get("points").array)
            if (k.get("frames").type == Value::Number && k.get("frames").number > 0) sum += unsigned(k.get("frames").number);
        return sum;
    }
    // a demo's frames are the game's 30 Hz cutscene frames: twice as many timeline units
    if (t == "demo") return step.get("frames").type == Value::Number && step.get("frames").number > 0 ? unsigned(step.get("frames").number) * 2 : 0;
    if (!scene_step_timed(t)) return 0;
    if (step.get("seconds").type == Value::Number && step.get("seconds").number > 0) return unsigned(std::ceil(step.get("seconds").number * 60));
    if (step.get("frames").type == Value::Number && step.get("frames").number > 0) return unsigned(step.get("frames").number);
    return 0;
}

bool scene_step_index(const Value& scene, const Value& ref, size_t& index, std::string& why) {
    const auto& steps = steps_of(scene);
    if (ref.type == Value::Number) {
        if (std::floor(ref.number) != ref.number || ref.number < 1 || ref.number > double(steps.size()))
            return why = "the scene has steps 1.." + std::to_string(steps.size()), false;
        index = size_t(ref.number) - 1;
        return true;
    }
    if (ref.type == Value::String) {
        for (size_t i = 0; i < steps.size(); i++)
            if (steps[i].get("id").string() == ref.text) return index = i, true;
        return why = "the scene has no step \"" + ref.text + "\"", false;
    }
    return why = "a step is its number (1 = first) or its id", false;
}

std::vector<long> scene_successors(const Value& scene, size_t i) {
    std::vector<long> out;
    const auto& steps = steps_of(scene);
    if (i >= steps.size()) return out;
    const Value& s = steps[i];
    if (type_of(s) == "end") return out;
    if (type_of(s) == "choice") {
        for (const auto& o : s.get("options").array) out.push_back(index_of_id(scene, o.get("next").string(), i));
        return out;
    }
    if (s.get("next").type == Value::String) out.push_back(index_of_id(scene, s.get("next").text, i));
    else if (i + 1 < steps.size()) out.push_back(long(i + 1));
    return out;
}

Value scene_timeline(const Value& scene) {
    const auto& steps = steps_of(scene);
    const size_t n = steps.size();
    std::vector<double> start(n, -1), longest(n, -1);
    std::vector<std::string> branch(n);
    const auto path = default_path(scene);
    // the default path's times
    double t = 0;
    for (size_t i : path) {
        start[i] = t;
        t += scene_step_frames(steps[i]);
    }
    const double duration = t;
    // the other steps: the earliest arrival from what leads to them; branches remember their choice
    if (n) longest[0] = 0;
    for (size_t i = 0; i < n; i++) {
        const auto next = scene_successors(scene, i);
        for (size_t k = 0; k < next.size(); k++) {
            const long j = next[k];
            if (j < 0 || (start[i] < 0 && longest[i] < 0)) continue;
            const double from = start[i] >= 0 ? start[i] : longest[i];
            const double arrive = from + scene_step_frames(steps[i]);
            if (start[size_t(j)] < 0 && (longest[size_t(j)] < 0 || arrive < longest[size_t(j)])) longest[size_t(j)] = arrive;
            if (type_of(steps[i]) == "choice" && branch[size_t(j)].empty()) branch[size_t(j)] = std::to_string(i + 1) + ":" + std::to_string(k + 1);
            else if (branch[size_t(j)].empty() && !branch[i].empty() && type_of(steps[i]) != "choice") branch[size_t(j)] = branch[i];
        }
    }
    // the longest path (a choice deadline counted in full)
    std::vector<double> reach(n, -1);
    if (n) reach[0] = 0;
    double longest_end = 0;
    for (size_t i = 0; i < n; i++) {
        if (reach[i] < 0) continue;
        const double end = reach[i] + scene_step_frames(steps[i]);
        longest_end = std::max(longest_end, end);
        for (long j : scene_successors(scene, i))
            if (j >= 0) reach[size_t(j)] = std::max(reach[size_t(j)], end);
    }
    Value out;
    out["duration"] = duration;
    out["longest"] = longest_end;
    out["path"] = arr();
    for (size_t i : path) out["path"].array.push_back(Value(int(i + 1)));
    out["steps"] = arr();
    for (size_t i = 0; i < n; i++) {
        const Value& s = steps[i];
        Value v;
        v["step"] = int(i + 1);
        if (s.get("id").type == Value::String) v["id"] = s.get("id");
        v["type"] = type_of(s);
        const bool on_path = start[i] >= 0;
        v["start"] = on_path ? start[i] : longest[i];
        v["frames"] = int(scene_step_frames(s));
        v["timed"] = scene_step_timed(type_of(s));
        v["default_path"] = on_path;
        if (!branch[i].empty()) {
            // a step of a branch that rejoins the default path is on it: no branch label there
            if (!on_path) v["branch"] = branch[i];
        }
        if (type_of(s) == "camera_path") {
            v["keys"] = arr();
            double k = v.get("start").number;
            for (const auto& key : s.get("points").array) {
                v["keys"].array.push_back(Value(k));
                if (key.get("frames").type == Value::Number) k += key.get("frames").number;
            }
        }
        out["steps"].array.push_back(v);
    }
    return out;
}

bool scene_camera_at(const Value& scene, double time, WorldPoint& eye, WorldPoint& target, size_t& step, std::string& why) {
    const auto& steps = steps_of(scene);
    bool have = false;
    std::string unknown;  // the camera at that time follows or looks at an actor
    double t = 0;
    for (size_t i : default_path(scene)) {
        if (t > time) break;
        const Value& s = steps[i];
        const std::string type = type_of(s);
        const double frames = scene_step_frames(s);
        const double local = std::clamp(time - t, 0.0, std::max(frames, 0.0));
        const std::string label = "step " + std::to_string(i + 1);
        if (type == "camera") {
            WorldPoint e, g;
            if (!pos(s.get("eye"), e)) return why = label + ": the camera has no eye", false;
            if (!pos(s.get("target"), g)) {
                unknown = label + " looks at an actor: only the game knows where it is";
            } else {
                WorldPoint e2, g2;
                if (s.get("pan").boolean && frames > 0 && pos(s.get("eye_to"), e2) && pos(s.get("target_to"), g2)) {
                    const double k = local / frames;
                    e = mix(e, e2, k), g = mix(g, g2, k);
                }
                eye = e, target = g, step = i, have = true, unknown.clear();
            }
        } else if (type == "camera_path") {
            std::vector<motion::Key> keys;
            for (const auto& k : s.get("points").array) {
                WorldPoint e, g;
                if (!pos(k.get("eye"), e) || !pos(k.get("target"), g)) return why = label + ": a camera key without eye or target", false;
                keys.push_back({{mp(e), mp(g)}, k.get("frames").type == Value::Number ? unsigned(k.get("frames").number) : 0u});
            }
            motion::Path path;
            motion::Camera c;
            if (!path.set(keys, s.get("easing").string() == "smoothstep" ? motion::Easing::Smoothstep : motion::Easing::Linear) || !path.sample(local, c))
                return why = label + ": the camera path is not valid", false;
            eye = wp(c.eye), target = wp(c.target), step = i, have = true, unknown.clear();
        } else if (type == "camera_follow") {
            unknown = label + " follows an actor: only the game knows where it is";
        } else if (type == "demo" && s.get("camera").get("eye").type == Value::Object) {
            // a demo's camera at its own frame (30 a second); one placed at Link: demo_pose knows where he is
            if (s.get("origin").type == Value::String) {
                unknown = label + " is placed at Link: demo_pose shows it";
            } else {
                Value pose;
                std::string w;
                if (!demo_pose(s, local / 2, {}, 0, pose, w)) return why = label + ": " + w, false;
                if (pos(pose.get("camera").get("eye"), eye) && pos(pose.get("camera").get("target"), target)) step = i, have = true, unknown.clear();
            }
        }
        t += frames;
    }
    if (!unknown.empty()) return why = unknown, false;
    if (!have) return why = "no fixed camera before this time", false;
    return true;
}

std::string free_step_id(const Value& scene, const std::string& base) {
    std::set<std::string> used;
    for (const auto& s : steps_of(scene))
        if (s.get("id").type == Value::String) used.insert(s.get("id").text);
    if (!used.contains(base)) return base;
    for (int n = 2;; n++)
        if (!used.contains(base + "-" + std::to_string(n))) return base + "-" + std::to_string(n);
}

bool new_scene_steps(const std::string& type, const Value& m, const Value& scene, const SceneView& view, std::vector<Value>& out, std::string& why) {
    out.clear();
    Value s;
    s["type"] = type;
    WorldPoint eye{0, 1500, 0}, target{0, 1000, 500};
    if (view.valid) eye = view.eye, target = view.target;
    // a point a little in front of the view, on its height: where things happen
    WorldPoint ahead = target;
    auto mover = [&](const char* what, const char* label) -> std::string {
        const std::string id = first_actor(m, what);
        if (id.empty()) why = std::string("the package has no ") + label + " for a " + type + " step: place one first";
        return id;
    };
    if (type == "camera") {
        s["eye"] = pt(eye);
        s["target"] = pt(target);
        s["frames"] = 60;
    } else if (type == "camera_path") {
        Value a, b;
        a["eye"] = pt(eye);
        a["target"] = pt(target);
        a["frames"] = 60;
        // the second key: the view a step to the right (looking at the same point)
        const double dx = double(target.x) - eye.x, dz = double(target.z) - eye.z, len = std::max(1.0, std::hypot(dx, dz));
        const WorldPoint side{float(eye.x - dz / len * 200), eye.y, float(eye.z + dx / len * 200)};
        b["eye"] = pt(side);
        b["target"] = pt(target);
        s["points"] = arr();
        s["points"].array.push_back(a);
        s["points"].array.push_back(b);
        s["easing"] = "smoothstep";
    } else if (type == "say") {
        std::string who = first_actor(m, "mover");
        if (who.empty()) who = first_actor(m, "any");
        if (who.empty()) who = "link";
        s["actor"] = who;
        s["text"] = arr();
        s["text"].array.push_back(Value("..."));
        s["frames"] = 600;
    } else if (type == "move" || type == "face") {
        const std::string who = mover("mover", "placed resident or mod actor");
        if (who.empty()) return false;
        WorldPoint at = ahead;
        if (actor_position(m, who, at)) at.x += 100;
        s["actor"] = who;
        s["position"] = pt(at);
        if (type == "move") s["frames"] = 120;
    } else if (type == "wait") {
        s["frames"] = 30;
    } else if (type == "sound") {
        s["name"] = "SE_MENU_DECIDE";
    } else if (type == "fade") {
        s["direction"] = "out";
        s["frames"] = 30;
    } else if (type == "end") {
    } else if (type == "camera_follow") {
        const std::string who = mover("mover", "placed resident or mod actor");
        if (who.empty()) return false;
        s["actor"] = who;
        s["eye_offset"] = pt({0, 300, -600});
        s["target_offset"] = pt({0, 100, 0});
        s["frames"] = 120;
    } else if (type == "animation") {
        const std::string who = mover("skinned", "actor with a skinned model");
        if (who.empty()) return false;
        s["actor"] = who;
        s["clip"] = "idle";
    } else if (type == "walk") {
        const std::string who = mover("skinned", "actor with a skinned model");
        if (who.empty()) return false;
        WorldPoint a = ahead;
        actor_position(m, who, a);
        s["actor"] = who;
        s["points"] = arr();
        s["points"].array.push_back(pt(a));
        s["points"].array.push_back(pt({a.x + 200, a.y, a.z}));
        s["speed"] = 60;
        s["frames"] = 300;
    } else if (type == "music") {
        s["cue"] = "theme";
        s["action"] = "start";
    } else if (type == "demo") {
        // keyframe tracks played by the game's cutscene system (demo_tracks.h): a camera key to start
        s = new_demo_step(eye, target, 120);
    } else if (type == "particle") {
        s["effect"] = "auction-sparkle";
        s["position"] = pt(ahead);
    } else if (type == "choice") {
        const std::string id = free_step_id(scene, "choice");
        Value a, b;
        a["id"] = id + "-a";
        a["type"] = "wait";
        a["frames"] = 30;
        b["id"] = id + "-b";
        b["type"] = "wait";
        b["frames"] = 30;
        s["id"] = id;
        s["text"] = arr();
        s["text"].array.push_back(Value("Which one?"));
        Value o1, o2;
        o1["text"] = "Yes";
        o1["next"] = id + "-a";
        o2["text"] = "No";
        o2["next"] = id + "-b";
        s["options"] = arr();
        s["options"].array.push_back(o1);
        s["options"].array.push_back(o2);
        s["default"] = 0;
        s["frames"] = 900;
        out = {s, a, b};
        return true;
    } else
        return why = "\"" + type + "\" is not a scene step (camera, say, move, face, wait, sound, fade, end, camera_path, camera_follow, animation, walk, choice, music, particle)",
               false;
    out.push_back(s);
    return true;
}

bool insert_scene_steps(Value& scene, size_t at, std::vector<Value> add, std::string& why) {
    auto& steps = scene["steps"].array;
    if (at > steps.size()) return why = "no place " + std::to_string(at + 1) + " in the scene", false;
    if (steps.size() + add.size() > story_notebook::kMaxSceneSteps) return why = "a scene has up to 64 steps", false;
    // ids of the new steps must be free in the scene
    std::set<std::string> used;
    for (const auto& s : steps)
        if (s.get("id").type == Value::String) used.insert(s.get("id").text);
    std::map<std::string, std::string> renamed;
    for (auto& s : add)
        if (s.get("id").type == Value::String && used.contains(s.get("id").text)) {
            std::string id = s.get("id").text;
            for (int n = 2;; n++)
                if (!used.contains(id + "-" + std::to_string(n))) {
                    renamed[id] = id + "-" + std::to_string(n);
                    break;
                }
            s["id"] = renamed[id];
        }
    for (auto& s : add) {
        if (s.get("id").type == Value::String) used.insert(s.get("id").text);
        if (s.get("next").type == Value::String && renamed.contains(s.get("next").text)) s["next"] = renamed[s.get("next").text];
        if (s.get("options").type == Value::Array)
            for (auto& o : s["options"].array)
                if (renamed.contains(o.get("next").string())) o["next"] = renamed[o.get("next").string()];
    }
    // a choice's first branch goes on to the step after both branches: that step needs an id
    bool choice = false;
    for (const auto& s : add) choice = choice || type_of(s) == "choice";
    if (choice && !add.empty() && type_of(add.front()) == "choice" && add.size() == 3 && add[1].get("next").type == Value::Null) {
        if (at >= steps.size()) return why = "a choice needs a step after it (the scene's end)", false;
        Value& after = steps[at];
        if (after.get("id").type != Value::String) after["id"] = free_step_id(scene, add.front().get("id").string() + "-after");
        add[1]["next"] = after.get("id");
    }
    steps.insert(steps.begin() + long(at), add.begin(), add.end());
    return true;
}

bool remove_scene_step(Value& scene, size_t index, std::string& why) {
    auto& steps = scene["steps"].array;
    if (index >= steps.size()) return why = "no such step", false;
    const std::string id = steps[index].get("id").string();
    const auto names = naming(scene, id);
    if (!names.empty()) return why = "step " + std::to_string(names.front() + 1) + " leads to it (\"" + id + "\"): change that step first", false;
    if (steps.size() == 1) return why = "a scene keeps at least one step (its end)", false;
    steps.erase(steps.begin() + long(index));
    return true;
}

bool move_scene_step(Value& scene, size_t index, size_t to, std::string& why) {
    auto& steps = scene["steps"].array;
    if (index >= steps.size() || to >= steps.size()) return why = "the scene has steps 1.." + std::to_string(steps.size()), false;
    Value s = steps[index];
    steps.erase(steps.begin() + long(index));
    steps.insert(steps.begin() + long(to), s);
    return true;
}

bool retime_scene_step(Value& step, unsigned key, unsigned frames, std::string& why) {
    if (frames < 1 || frames > kSceneFramesMax) return why = "a duration is 1..18000 frames (60 per second)", false;
    const std::string type = type_of(step);
    if (type == "camera_path") {
        auto& points = step["points"].array;
        if (points.size() < 2) return why = "the camera path has no keys", false;
        if (key) {
            if (key >= points.size()) return why = "the keys with a time to the next are 1.." + std::to_string(points.size() - 1), false;
            points[key - 1]["frames"] = int(frames);
            return true;
        }
        // scale every segment, each at least one frame, the sum exact
        const unsigned old = scene_step_frames(step);
        const size_t segments = points.size() - 1;
        if (frames < segments) return why = "the path has " + std::to_string(segments) + " segments: at least one frame each", false;
        unsigned sum = 0;
        for (size_t i = 0; i < segments; i++) {
            const double f = old ? points[i].get("frames").number * frames / old : double(frames) / segments;
            const unsigned left = unsigned(segments - i - 1);
            unsigned v = i + 1 == segments ? frames - sum : std::clamp(unsigned(std::lround(f)), 1u, frames - sum - left);
            points[i]["frames"] = int(v);
            sum += v;
        }
        return true;
    }
    if (!scene_step_timed(type)) return why = "a " + type + " step has no duration", false;
    step.object.erase("seconds");
    step["frames"] = int(frames);
    return true;
}

bool add_camera_key(Value& step, const WorldPoint& eye, const WorldPoint& target, unsigned at, unsigned frames, std::string& why) {
    if (dist(eye, target) < 1) return why = "the camera's eye and target are the same point", false;
    if (!frames) frames = 60;
    if (frames > kSceneFramesMax) return why = "a duration is 1..18000 frames", false;
    Value key;
    key["eye"] = pt(eye);
    key["target"] = pt(target);
    const std::string type = type_of(step);
    if (type == "camera") {
        WorldPoint e, g;
        if (!pos(step.get("eye"), e) || !pos(step.get("target"), g) || step.get("pan").boolean)
            return why = "only a camera with a fixed eye and target (no pan) becomes a camera path", false;
        Value first;
        first["eye"] = pt(e);
        first["target"] = pt(g);
        first["frames"] = int(frames);
        Value path;
        path["type"] = "camera_path";
        if (step.get("id").type == Value::String) path["id"] = step.get("id");
        if (step.get("next").type == Value::String) path["next"] = step.get("next");
        path["easing"] = "smoothstep";
        path["points"] = arr();
        if (at == 1) {
            key["frames"] = int(frames);
            first.object.erase("frames");
            path["points"].array.push_back(key);
            path["points"].array.push_back(first);
        } else {
            path["points"].array.push_back(first);
            path["points"].array.push_back(key);
        }
        step = path;
        return true;
    }
    if (type != "camera_path") return why = "camera keys belong to a camera or camera_path step", false;
    auto& points = step["points"].array;
    if (points.size() >= 16) return why = "a camera path has up to 16 keys", false;
    const size_t place = at == 0 || at > points.size() ? points.size() : at - 1;
    if (place == points.size()) {
        points.back()["frames"] = int(frames);  // the old last key now leads to the new one
        points.push_back(key);
    } else {
        key["frames"] = int(frames);
        points.insert(points.begin() + long(place), key);
    }
    return true;
}

bool set_camera_key(Value& step, unsigned key, const WorldPoint* eye, const WorldPoint* target, unsigned frames, std::string& why) {
    const std::string type = type_of(step);
    if (type == "camera") {
        if (key > 1) return why = "a camera step has one view (key 1)", false;
        if (eye) step["eye"] = pt(*eye);
        if (target) {
            step.object.erase("target_actor");
            step["target"] = pt(*target);
        }
        if (frames) step["frames"] = int(frames), step.object.erase("seconds");
        return true;
    }
    if (type != "camera_path") return why = "camera keys belong to a camera or camera_path step", false;
    auto& points = step["points"].array;
    if (key < 1 || key > points.size()) return why = "the path has keys 1.." + std::to_string(points.size()), false;
    Value& k = points[key - 1];
    if (eye) k["eye"] = pt(*eye);
    if (target) k["target"] = pt(*target);
    if (frames) {
        if (key == points.size()) return why = "the last key has no time to a next key", false;
        k["frames"] = int(frames);
    }
    return true;
}

bool remove_camera_key(Value& step, unsigned key, std::string& why) {
    if (type_of(step) != "camera_path") return why = "camera keys belong to a camera_path step", false;
    auto& points = step["points"].array;
    if (key < 1 || key > points.size()) return why = "the path has keys 1.." + std::to_string(points.size()), false;
    if (points.size() == 2) {
        // the other key stays as a camera step of the path's length
        const unsigned frames = scene_step_frames(step);
        const Value other = points[key == 1 ? 1 : 0];
        Value cam;
        cam["type"] = "camera";
        if (step.get("id").type == Value::String) cam["id"] = step.get("id");
        if (step.get("next").type == Value::String) cam["next"] = step.get("next");
        cam["eye"] = other.get("eye");
        cam["target"] = other.get("target");
        cam["frames"] = int(std::max(1u, frames));
        step = cam;
        return true;
    }
    if (key == points.size()) {
        points.pop_back();
        points.back().object.erase("frames");
    } else
        points.erase(points.begin() + long(key - 1));
    return true;
}

bool scene_range(const Value& scene, size_t from, size_t to, const std::string& id, Value& out, std::string& why) {
    const auto& steps = steps_of(scene);
    if (from > to || to >= steps.size()) return why = "the range is steps 1.." + std::to_string(steps.size()) + ", from before to", false;
    std::vector<Value> list(steps.begin() + long(from), steps.begin() + long(to) + 1);
    std::set<std::string> inside;
    for (const auto& s : list)
        if (s.get("id").type == Value::String) inside.insert(s.get("id").text);
    bool need_alt_end = false;
    for (auto& s : list) {
        if (s.get("next").type == Value::String && !inside.contains(s.get("next").text)) s.object.erase("next");
        if (type_of(s) == "choice" && s.get("options").type == Value::Array) {
            auto& o = s["options"].array;
            for (size_t k = 0; k < o.size(); k++)
                if (!inside.contains(o[k].get("next").string())) {
                    o[k]["next"] = k == 0 ? "editor-end-a" : "editor-end";
                    need_alt_end = need_alt_end || k == 0;
                }
        }
    }
    // the range's own end
    if (list.empty() || type_of(list.back()) != "end" || need_alt_end) {
        if (need_alt_end) {
            Value alt;
            alt["id"] = "editor-end-a";
            alt["type"] = "end";
            list.push_back(alt);
        }
        Value end;
        end["id"] = "editor-end";
        end["type"] = "end";
        list.push_back(end);
    }
    // keep what the first step leads to
    Value tmp;
    tmp["steps"].type = Value::Array;
    tmp["steps"].array = list;
    std::vector<bool> reach(list.size(), false);
    reach[0] = true;
    for (size_t i = 0; i < list.size(); i++) {
        if (!reach[i]) continue;
        for (long j : scene_successors(tmp, i))
            if (j >= 0) reach[size_t(j)] = true;
    }
    Value result;
    result["id"] = id;
    result["steps"] = arr();
    for (size_t i = 0; i < list.size(); i++)
        if (reach[i]) result["steps"].array.push_back(list[i]);
    // the last kept step must be an end (a branch end may come last only after a choice)
    if (type_of(result["steps"].array.back()) != "end") {
        Value end;
        end["id"] = "editor-end-z";
        end["type"] = "end";
        result["steps"].array.push_back(end);
    }
    out = result;
    return true;
}

bool native_from_steps(const Value& m, const Value& scene, Value& native, std::string& why) {
    // the residents the face steps turn, in order; their positions
    std::vector<std::pair<std::string, WorldPoint>> turns;
    std::vector<const Value*> cameras;
    unsigned waits = 0, total = 0;
    const auto& steps = steps_of(scene);
    for (size_t i = 0; i < steps.size(); i++) {
        const Value& s = steps[i];
        const std::string t = type_of(s);
        if (t == "end") {
            if (i + 1 != steps.size()) return why = "the native version needs a scene without branches (one end, last)", false;
            continue;
        }
        if (s.get("next").type != Value::Null || t == "choice") return why = "the native version needs a scene without branches", false;
        if (t == "face") {
            WorldPoint p;
            if (s.get("target_actor").type != Value::Null || !pos(s.get("position"), p)) return why = "step " + std::to_string(i + 1) + ": a face step needs a fixed position", false;
            const std::string who = s.get("actor").string();
            bool known = false;
            for (auto& [id, at] : turns)
                if (id == who) known = true, at = p;
            if (!known) turns.push_back({who, p});
        } else if (t == "camera") {
            if (s.get("target_actor").type != Value::Null) return why = "step " + std::to_string(i + 1) + ": the native camera needs a fixed target", false;
            cameras.push_back(&s);
        } else if (t == "wait") {
            waits += scene_step_frames(s);
        } else
            return why = "step " + std::to_string(i + 1) + " (" + t + ") has no native counterpart: only face, camera, wait and end", false;
        total += scene_step_frames(s);
    }
    if (turns.size() != 2) return why = "the native version turns exactly two placed uo2 residents (face steps)", false;
    for (const auto& [id, at] : turns) {
        bool uo2 = false;
        for (const auto& a : m.get("actors").array)
            if (a.get("id").string() == id) uo2 = a.get("parameters").get("resident").string() == "uo2";
        if (!uo2) return why = id + " is not a placed uo2 resident", false;
    }
    if (cameras.empty()) return why = "the native version needs a camera step", false;
    WorldPoint se, st, ee, et;
    const Value& c0 = *cameras.front();
    pos(c0.get("eye"), se), pos(c0.get("target"), st);
    unsigned hold = 0, move = 0;
    if (c0.get("pan").boolean) {
        pos(c0.get("eye_to"), ee), pos(c0.get("target_to"), et);
        hold = 1;
        move = std::max(1u, scene_step_frames(c0) / 2);
    } else if (cameras.size() >= 2) {
        const Value& c1 = *cameras[1];
        pos(c1.get("eye"), ee), pos(c1.get("target"), et);
        if (c1.get("pan").boolean) pos(c1.get("eye_to"), ee), pos(c1.get("target_to"), et);
        hold = std::max(1u, scene_step_frames(c0) / 2);
        move = std::max(1u, scene_step_frames(c1) / 2);
    } else {
        ee = se, et = st;
        hold = std::max(1u, scene_step_frames(c0) / 2);
        move = 1;
    }
    if (hold + move > 9000) return why = "the native camera holds and moves for up to 9000 ticks (five minutes)", false;
    const unsigned wait = std::clamp(std::max(waits, total) / 2, 1u, 9000u);
    const unsigned keeper = std::max(wait, hold + move);
    if (keeper > 9000) return why = "the native version lasts up to 9000 ticks", false;
    Value n;
    n["type"] = "turn_camera";
    n["actors"] = arr();
    for (const auto& [id, at] : turns) {
        Value a;
        a["id"] = id;
        a["turn_to"] = pt(at);
        n["actors"].array.push_back(a);
    }
    n["wait_ticks"] = int(wait);
    Value cam;
    cam["hold_ticks"] = int(hold);
    cam["move_ticks"] = int(move);
    cam["start_eye"] = pt(se);
    cam["start_target"] = pt(st);
    cam["end_eye"] = pt(ee);
    cam["end_target"] = pt(et);
    cam["fovy"] = 45;
    n["camera"] = cam;
    n["timekeeper_ticks"] = int(keeper);
    n["watchdog_ticks"] = int(std::min(18000u, keeper * 2));
    native = n;
    return true;
}

bool native_eligible(const Value& m, const Value& scene, std::string& why) {
    if (scene.get("native").type == Value::Null) return why = "the scene has no native version", false;
    // parsed with the package's other scenes (actors and scenes name them)
    Value one = m;
    if (one.get("scenes").type != Value::Array) one["scenes"] = arr();
    bool found = false;
    for (auto& sc : one["scenes"].array)
        if (sc.get("id").string() == scene.get("id").string()) sc = scene, found = true;
    if (!found) one["scenes"].array.push_back(scene);
    std::vector<story_notebook::ManifestScene> parsed;
    if (!story_notebook::parse_scenes(one, parsed, why)) return false;
    const auto it = std::find_if(parsed.begin(), parsed.end(), [&](const story_notebook::ManifestScene& x) { return x.id == scene.get("id").string(); });
    if (it == parsed.end()) return why = "the scene can't be read", false;
    const auto c = story_native_scene_coverage::inspect(*it);
    if (c.selection != story_native_scene_coverage::Selection::NativeAlternative) return why = c.reason, false;
    return true;
}

bool scene_trigger(const std::string& type, const Value& p, const Place& place, Value& trigger, std::string& why) {
    trigger = Value();
    if (type == "none") return true;
    trigger["type"] = type;
    if (type == "talk") {
        if (p.get("actor").type != Value::String) return why = "a talk trigger names an actor (\"actor\")", false;
        trigger["actor"] = p.get("actor");
    } else if (type == "quest_step") {
        if (p.get("quest").type != Value::String || p.get("step").type != Value::Number) return why = "a quest_step trigger names a \"quest\" and its \"step\"", false;
        trigger["quest"] = p.get("quest");
        trigger["step"] = p.get("step");
    } else if (type == "salvage") {
        if (p.get("chart").type != Value::String) return why = "a salvage trigger names a \"chart\"", false;
        trigger["chart"] = p.get("chart");
    } else if (type == "room" || type == "area") {
        std::string stage = p.get("stage").string(place.stage);
        int room = p.get("room").type == Value::Number ? int(p.get("room").number) : place.room;
        WorldPoint at = place.position;
        if (p.get("position").type != Value::Null && !pos(p.get("position"), at)) return why = "\"position\" is {x, y, z}", false;
        if (stage.empty() || room < 0) return why = "a " + type + " trigger needs a stage and room (or Link in the world)", false;
        if (p.get("position").type != Value::Null && stage == "sea" && p.get("room").type != Value::Number) room = sea_room(at.x, at.z);
        trigger["stage"] = stage;
        trigger["room"] = room;
        if (type == "area") {
            trigger["position"] = pt(at);
            trigger["radius"] = p.get("radius").type == Value::Number ? p.get("radius").number : 500.0;
            if (p.get("again").type == Value::Bool && p.get("again").boolean) trigger["again"] = true;  // plays again after Link left the area
        } else if (p.get("again").type != Value::Null)
            return why = "\"again\" is for an area trigger", false;
    } else
        return why = "a trigger is none, talk, quest_step, room, area or salvage", false;
    return true;
}

}  // namespace editor
