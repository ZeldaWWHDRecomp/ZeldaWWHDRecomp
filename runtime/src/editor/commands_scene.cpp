// World editor command layer: scenes (commands.h, scenes.h): steps, camera keys, the timeline, choices,
// walking paths, triggers, the native version, copy and paste of scenes and their steps, and previews in
// the game. No game dependencies: the fly camera and the preview go through Host.
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../mods/story_world_manifest.h"
#include "commands.h"
#include "scenes.h"

namespace editor {
namespace {

std::string text(const Value& p, const char* key) { return p.get(key).string(); }
Value arr() {
    Value v;
    v.type = Value::Array;
    return v;
}
Value* by_id(Value& list, const std::string& id) {
    if (list.type != Value::Array) return nullptr;
    for (auto& v : list.array)
        if (v.get("id").type == Value::String && v.get("id").text == id) return &v;
    return nullptr;
}
const Value* by_id(const Value& list, const std::string& id) { return by_id(const_cast<Value&>(list), id); }
bool point_of(const Value& v, WorldPoint& p) {
    if (v.type != Value::Object) return false;
    for (const char* k : {"x", "y", "z"})
        if (v.get(k).type != Value::Number || !std::isfinite(v.get(k).number)) return false;
    p = {float(v.get("x").number), float(v.get("y").number), float(v.get("z").number)};
    return true;
}
double tenth(double v) { return std::round(v * 10) / 10; }
Value rounded(const WorldPoint& p) {
    Value v;
    v["x"] = tenth(p.x);
    v["y"] = tenth(p.y);
    v["z"] = tenth(p.z);
    return v;
}
std::string free_scene_id(const Value& m, const std::string& base) {
    if (!by_id(m.get("scenes"), base)) return base;
    for (int n = 2;; n++)
        if (!by_id(m.get("scenes"), base + "-" + std::to_string(n))) return base + "-" + std::to_string(n);
}
// what starts a scene: actors' and scenes' scene actions
std::string started_by(const Value& m, const std::string& scene) {
    auto names = [&](const Value& then) {
        for (const auto& t : then.array)
            if (t.get("action").string() == "scene" && t.get("id").string() == scene) return true;
        return false;
    };
    for (const auto& a : m.get("actors").array)
        if (names(a.get("then"))) return "actor " + a.get("id").string();
    for (const auto& sc : m.get("scenes").array)
        for (const auto& st : sc.get("steps").array)
            if (sc.get("id").string() != scene && names(st.get("then"))) return "scene " + sc.get("id").string();
    for (const auto& it : m.get("items").array)
        if (it.get("use").get("action").string() == "scene" && it.get("use").get("id").string() == scene) return "item " + it.get("id").string();
    return {};
}

}  // namespace

Value Session::dispatch_scene(const std::string& command, const Value& p, std::string& why, bool& ok, bool& handled) {
    Value r;
    ok = false;
    handled = true;
    const std::string scene = text(p, "scene");
    // a change to one scene of the open mod: `edit` gets the manifest and the scene
    auto scene_change = [&](const std::string& label, const std::function<bool(Value& m, Value& s, std::string&)>& edit) {
        Package* pkg = package(p, why);
        if (!pkg) return;
        ok = change(*pkg, label, [&](Value& m, std::string& w) {
            Value* s = by_id(m["scenes"], scene);
            if (!s) return w = "no scene \"" + scene + "\" in " + pkg->mod, false;
            return edit(m, *s, w);
        }, r, why);
        if (ok) r["scene"] = scene;
    };
    // a change to one step of that scene
    auto step_change = [&](const std::string& label, const std::function<bool(Value& m, Value& s, size_t i, std::string&)>& edit) {
        scene_change(label, [&](Value& m, Value& s, std::string& w) {
            size_t i = 0;
            if (!scene_step_index(s, p.get("step"), i, w)) return false;
            if (!edit(m, s, i, w)) return false;
            r["step"] = int(i + 1);
            return true;
        });
    };
    // the fly camera's view (eye and target), or explicit eye and target
    auto view_from = [&](SceneView& v, bool explicit_allowed) {
        if (explicit_allowed && p.get("eye").type == Value::Object) {
            if (!point_of(p.get("eye"), v.eye) || !point_of(p.get("target"), v.target)) return why = "\"eye\" and \"target\" are {x, y, z}", false;
            v.valid = true;
            return true;
        }
        if (!host_) return why = "the game is not running (or give \"eye\" and \"target\")", false;
        const Value fly = host_->fly_state();
        if (!fly.get("on").boolean || !point_of(fly.get("position"), v.eye) || !point_of(fly.get("target"), v.target))
            return why = "the fly camera is off: its view is the camera key", false;
        v.valid = true;
        return true;
    };
    const std::string label_step = p.get("step").type == Value::Number ? std::to_string(int(p.get("step").number)) : text(p, "step");

    if (command == "add_scene") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        std::string id = p.get("id").type == Value::String ? p.get("id").text : free_scene_id(pkg->manifest, "scene");
        if (by_id(pkg->manifest.get("scenes"), id)) return why = "the mod has a scene \"" + id + "\" already", r;
        ok = change(*pkg, "Add scene " + id, [&](Value& m, std::string&) {
            if (m.get("scenes").type != Value::Array) m["scenes"] = arr();
            Value s, end;
            s["id"] = id;
            end["type"] = "end";
            s["steps"] = arr();
            s["steps"].array.push_back(end);
            m["scenes"].array.push_back(s);
            return true;
        }, r, why);
        if (ok) r["scene"] = id;
        return r;
    }
    if (command == "remove_scene") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string id = text(p, "id");
        ok = change(*pkg, "Remove scene " + id, [&](Value& m, std::string& w) {
            if (!by_id(m["scenes"], id)) return w = "no scene \"" + id + "\" in " + pkg->mod, false;
            const std::string who = started_by(m, id);
            if (!who.empty()) return w = who + " starts scene " + id + ": change that first", false;
            auto& list = m["scenes"].array;
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Value& s) { return s.get("id").string() == id; }), list.end());
            if (list.empty()) m.object.erase("scenes");
            return true;
        }, r, why);
        if (ok) r["scene"] = id;
        return r;
    }
    if (command == "duplicate_scene") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string id = text(p, "id");
        const Value* from = by_id(pkg->manifest.get("scenes"), id);
        if (!from) return why = "no scene \"" + id + "\"", r;
        const std::string to = p.get("new_id").type == Value::String ? p.get("new_id").text : free_scene_id(pkg->manifest, id);
        if (by_id(pkg->manifest.get("scenes"), to)) return why = "the mod has a scene \"" + to + "\" already", r;
        ok = change(*pkg, "Duplicate scene " + id, [&](Value& m, std::string&) {
            Value copy = *from;
            copy["id"] = to;
            copy.object.erase("trigger");  // two scenes on one trigger would both start
            m["scenes"].array.push_back(copy);
            return true;
        }, r, why);
        if (ok) r["scene"] = to;
        return r;
    }
    if (command == "set_scene_trigger") {
        Place place;
        const std::string type = text(p, "type");
        if ((type == "room" || type == "area") && host_ && (p.get("stage").type == Value::Null || p.get("position").type == Value::Null))
            place = text(p, "at") == "camera" ? host_->camera() : host_->link();
        Value trigger;
        if (!scene_trigger(type, p, place, trigger, why)) return r;
        scene_change("Trigger of scene " + scene, [&](Value&, Value& s, std::string&) {
            if (trigger.type == Value::Null) s.object.erase("trigger");
            else s["trigger"] = trigger;
            return true;
        });
        if (ok) r["trigger"] = trigger;
        return r;
    }
    if (command == "add_scene_step") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const Value* sc = by_id(pkg->manifest.get("scenes"), scene);
        if (!sc) return why = "no scene \"" + scene + "\"", r;
        SceneView view;
        std::string ignore;
        if (host_) {
            const Value fly = host_->fly_state();
            view.valid = fly.get("on").boolean && point_of(fly.get("position"), view.eye) && point_of(fly.get("target"), view.target);
            if (!view.valid) {
                const Place l = host_->link();
                if (l.valid) {
                    view.valid = true;
                    view.target = l.position;
                    view.eye = {l.position.x, l.position.y + 300, l.position.z - 600};
                }
            }
        }
        std::vector<Value> steps;
        if (!new_scene_steps(text(p, "type"), pkg->manifest, *sc, view, steps, why)) return r;
        for (const auto& [k, v] : p.get("fields").object) {
            if (v.type == Value::Null) steps.front().object.erase(k);
            else steps.front()[k] = v;
        }
        const size_t count = sc->get("steps").array.size();
        size_t at = count ? count - 1 : 0;  // before the scene's last step (its end)
        if (p.get("at").type == Value::Number) {
            const double a = p.get("at").number;
            if (a < 1 || a > double(count + 1)) return why = "\"at\" is 1.." + std::to_string(count + 1), r;
            at = size_t(a) - 1;
        }
        scene_change("Add a " + text(p, "type") + " step to " + scene, [&](Value&, Value& s, std::string& w) { return insert_scene_steps(s, at, steps, w); });
        if (ok) {
            r["step"] = int(at + 1);
            r["count"] = int(steps.size());
        }
        return r;
    }
    if (command == "edit_scene_step") {
        step_change("Scene " + scene + " step " + label_step, [&](Value&, Value& s, size_t i, std::string& w) {
            Value& st = s["steps"].array[i];
            const std::string old_id = st.get("id").string();
            for (const auto& [k, v] : p.get("fields").object) {
                if (v.type == Value::Null) st.object.erase(k);
                else st[k] = v;
            }
            // a renamed step: what leads to it follows
            const std::string new_id = st.get("id").string();
            if (!old_id.empty() && new_id != old_id)
                for (auto& other : s["steps"].array) {
                    if (other.get("next").string() == old_id) {
                        if (new_id.empty()) return w = "another step leads to \"" + old_id + "\": it keeps an id", false;
                        other["next"] = new_id;
                    }
                    if (other.get("options").type == Value::Array)
                        for (auto& o : other["options"].array)
                            if (o.get("next").string() == old_id) {
                                if (new_id.empty()) return w = "a choice leads to \"" + old_id + "\": it keeps an id", false;
                                o["next"] = new_id;
                            }
                }
            return true;
        });
        return r;
    }
    if (command == "remove_scene_step") {
        step_change("Remove step " + label_step + " of " + scene, [&](Value&, Value& s, size_t i, std::string& w) { return remove_scene_step(s, i, w); });
        return r;
    }
    if (command == "move_scene_step") {
        const double to = p.get("to").number;
        step_change("Move step " + label_step + " of " + scene, [&](Value&, Value& s, size_t i, std::string& w) {
            const size_t n = s.get("steps").array.size();
            if (to < 1 || to > double(n)) return w = "\"to\" is 1.." + std::to_string(n), false;
            if (!move_scene_step(s, i, size_t(to) - 1, w)) return false;
            return true;
        });
        if (ok) r["step"] = int(to);
        return r;
    }
    if (command == "duplicate_scene_step") {
        step_change("Duplicate step " + label_step + " of " + scene, [&](Value&, Value& s, size_t i, std::string& w) {
            Value copy = s["steps"].array[i];
            if (copy.get("type").string() == "end") return w = "an end is not copied (a scene ends once on each path)", false;
            if (copy.get("id").type == Value::String) copy["id"] = free_step_id(s, copy.get("id").text);
            return insert_scene_steps(s, i + 1, {copy}, w);
        });
        if (ok) r["step"] = r.get("step").number + 1;
        return r;
    }
    if (command == "add_camera_key") {
        SceneView v;
        if (!view_from(v, true)) return r;
        const unsigned frames = p.get("frames").type == Value::Number ? unsigned(std::max(0.0, p.get("frames").number)) : 60;
        const unsigned at = p.get("at").type == Value::Number ? unsigned(std::max(0.0, p.get("at").number)) : 0;
        if (p.get("step").type == Value::Null) {
            // a new camera step with this view, before the scene's end
            scene_change("Camera key for " + scene, [&](Value&, Value& s, std::string& w) {
                Value cam;
                cam["type"] = "camera";
                cam["eye"] = rounded(v.eye);
                cam["target"] = rounded(v.target);
                cam["frames"] = int(frames ? frames : 60);
                const size_t n = s.get("steps").array.size();
                if (!insert_scene_steps(s, n ? n - 1 : 0, {cam}, w)) return false;
                r["step"] = int(n ? n : 1);
                return true;
            });
            return r;
        }
        step_change("Camera key for " + scene + " step " + label_step, [&](Value&, Value& s, size_t i, std::string& w) {
            Value& st = s["steps"].array[i];
            if (!add_camera_key(st, v.eye, v.target, at, frames, w)) return false;
            r["keys"] = int(st.get("points").array.size());
            return true;
        });
        return r;
    }
    if (command == "set_camera_key") {
        SceneView v;
        const bool keep = text(p, "from") == "none";
        if (!keep && !view_from(v, true)) return r;
        const unsigned frames = p.get("frames").type == Value::Number ? unsigned(std::max(0.0, p.get("frames").number)) : 0;
        step_change("Camera key " + std::to_string(int(p.get("key").number)) + " of " + scene, [&](Value&, Value& s, size_t i, std::string& w) {
            return set_camera_key(s["steps"].array[i], unsigned(std::max(0.0, p.get("key").number)), keep ? nullptr : &v.eye, keep ? nullptr : &v.target, frames, w);
        });
        return r;
    }
    if (command == "remove_camera_key") {
        step_change("Remove camera key of " + scene, [&](Value&, Value& s, size_t i, std::string& w) {
            return remove_camera_key(s["steps"].array[i], unsigned(std::max(0.0, p.get("key").number)), w);
        });
        return r;
    }
    if (command == "retime_scene_step") {
        const double frames = p.get("frames").number;
        if (frames < 1 || frames > kSceneFramesMax) return why = "a duration is 1..18000 frames (60 a second)", r;
        step_change("Retime step " + label_step + " of " + scene, [&](Value&, Value& s, size_t i, std::string& w) {
            return retime_scene_step(s["steps"].array[i], unsigned(std::max(0.0, p.get("key").number)), unsigned(frames), w);
        });
        if (ok) r["frames"] = int(frames);
        return r;
    }
    if (command == "scene_timeline") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const Value* s = by_id(pkg->manifest.get("scenes"), scene);
        if (!s) return why = "no scene \"" + scene + "\"", r;
        r = scene_timeline(*s);
        r["scene"] = scene;
        ok = true;
        return r;
    }
    if (command == "scrub_scene") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const Value* s = by_id(pkg->manifest.get("scenes"), scene);
        if (!s) return why = "no scene \"" + scene + "\"", r;
        WorldPoint eye, target;
        size_t step = 0;
        if (!scene_camera_at(*s, p.get("time").number, eye, target, step, why)) return r;
        r["eye"] = rounded(eye);
        r["target"] = rounded(target);
        r["step"] = int(step + 1);
        r["time"] = p.get("time").number;
        const bool move = p.get("move").type == Value::Bool ? p.get("move").boolean : true;
        if (move) {
            if (!host_) return why = "the game is not running", r;
            if (!host_->look(eye, target, why)) return r;
            r["moved"] = true;
        }
        ok = true;
        return r;
    }
    if (command == "set_scene_choice") {
        step_change("Choice " + label_step + " of " + scene, [&](Value&, Value& s, size_t i, std::string& w) {
            Value& st = s["steps"].array[i];
            if (st.get("type").string() != "choice") return w = "step " + std::to_string(i + 1) + " is not a choice", false;
            if (p.get("default").type == Value::Number) {
                const double d = p.get("default").number;
                if (d != 1 && d != 2) return w = "\"default\" is 1 or 2", false;
                st["default"] = int(d) - 1;
            }
            if (p.get("label").type != Value::Null || p.get("next").type != Value::Null) {
                const double o = p.get("option").number;
                if (o != 1 && o != 2) return w = "\"option\" is 1 or 2", false;
                auto& options = st["options"].array;
                if (options.size() != 2) return w = "a choice has two options", false;
                Value& opt = options[size_t(o) - 1];
                if (p.get("label").type == Value::String) opt["text"] = p.get("label");
                if (p.get("next").type != Value::Null) {
                    size_t j = 0;
                    if (!scene_step_index(s, p.get("next"), j, w)) return false;
                    if (j <= i) return w = "an option leads to a later step", false;
                    Value& target = s["steps"].array[j];
                    if (target.get("id").type != Value::String) target["id"] = free_step_id(s, st.get("id").string("choice") + "-" + std::to_string(int(o)));
                    opt["next"] = target.get("id");
                }
            }
            return true;
        });
        return r;
    }
    if (command == "add_walk_point" || command == "set_walk_point" || command == "remove_walk_point") {
        WorldPoint at;
        if (command == "set_walk_point" && !point_of(p.get("position"), at)) return why = "\"position\" is {x, y, z}", r;
        if (command == "add_walk_point") {
            const std::string from = text(p, "from").empty() ? "camera" : text(p, "from");
            if (from == "position") {
                if (!point_of(p.get("position"), at)) return why = "from = position needs \"position\": {x, y, z}", r;
            } else if (from == "link" || from == "camera") {
                if (!host_) return why = "the game is not running", r;
                if (from == "link") {
                    const Place l = host_->link();
                    if (!l.valid) return why = "Link is not in the scene", r;
                    at = l.position;
                } else {
                    // the ground where the fly camera looks
                    SceneView v;
                    if (!view_from(v, false)) return r;
                    WorldPoint dir{v.target.x - v.eye.x, v.target.y - v.eye.y, v.target.z - v.eye.z};
                    const double len = std::sqrt(double(dir.x) * dir.x + double(dir.y) * dir.y + double(dir.z) * dir.z);
                    if (len < 1e-3) return why = "the fly camera has no direction", r;
                    dir = {float(dir.x / len), float(dir.y / len), float(dir.z / len)};
                    if (!host_->ray(v.eye, dir, 20000, at, why)) return r;
                }
            } else
                return why = "\"from\" is camera, link or position", r;
        }
        step_change((command == "add_walk_point" ? "Add a walk point to " : command == "set_walk_point" ? "Move a walk point of " : "Remove a walk point of ") + scene,
                    [&](Value&, Value& s, size_t i, std::string& w) {
                        Value& st = s["steps"].array[i];
                        if (st.get("type").string() != "walk") return w = "step " + std::to_string(i + 1) + " is not a walk", false;
                        if (st.get("points").type != Value::Array) return w = "this walk goes to a quest's place (quest_place), not along points", false;
                        auto& points = st["points"].array;
                        const double n = p.get(command == "add_walk_point" ? "at" : "point").number;
                        if (command == "add_walk_point") {
                            if (points.size() >= 16) return w = "a walk has up to 16 points", false;
                            const size_t place = p.get("at").type == Value::Number && n >= 1 && n <= double(points.size()) ? size_t(n) - 1 : points.size();
                            points.insert(points.begin() + long(place), rounded(at));
                            r["point"] = int(place + 1);
                            return true;
                        }
                        if (n < 1 || n > double(points.size())) return w = "the walk has points 1.." + std::to_string(points.size()), false;
                        if (command == "remove_walk_point") {
                            if (points.size() <= 2) return w = "a walk keeps at least two points", false;
                            points.erase(points.begin() + long(n) - 1);
                        } else
                            points[size_t(n) - 1] = rounded(at);
                        r["point"] = int(n);
                        return true;
                    });
        return r;
    }
    if (command == "native_scene") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const Value* s = by_id(pkg->manifest.get("scenes"), scene);
        if (!s) return why = "no scene \"" + scene + "\"", r;
        Value native;
        bool set = false;
        if (p.get("from_steps").boolean) {
            if (!native_from_steps(pkg->manifest, *s, native, why)) return r;
            set = true;
        } else if (p.object.contains("value")) {
            native = p.get("value");
            set = true;
        }
        if (set) {
            scene_change(native.type == Value::Null ? "Remove the native version of " + scene : "Native version of " + scene, [&](Value&, Value& sc, std::string&) {
                if (native.type == Value::Null) sc.object.erase("native");
                else sc["native"] = native;
                return true;
            });
            if (!ok) return r;
        } else
            ok = true;
        const Value* now = by_id(package(p, why)->manifest.get("scenes"), scene);
        std::string reason;
        r["native"] = now->get("native");
        r["eligible"] = native_eligible(pkg->manifest, *now, reason);
        if (!r.get("eligible").boolean) r["why"] = reason;
        r["scene"] = scene;
        return r;
    }
    if (command == "preview_scene") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (!host_) return why = "the game is not running", r;
        if (host_->playing()) return why = "Stop first: a scene preview starts from the editor's view", r;
        const Value* s = by_id(pkg->manifest.get("scenes"), scene);
        if (!s) return why = "no scene \"" + scene + "\"", r;
        Value play = *s;
        bool declared = false;
        if (p.get("from").type != Value::Null || p.get("to").type != Value::Null) {
            size_t from = 0, to = s->get("steps").array.size() - 1;
            if (p.get("from").type != Value::Null && !scene_step_index(*s, p.get("from"), from, why)) return r;
            if (p.get("to").type != Value::Null) {
                if (!scene_step_index(*s, p.get("to"), to, why)) return r;
            } else if (p.get("from").type != Value::Null)
                to = from;
            if (!scene_range(*s, from, to, "editor-preview", play, why)) return r;
            r["from"] = int(from + 1);
            r["to"] = int(to + 1);
        } else {
            const Value* started = by_id(pkg->started.get("scenes"), scene);
            declared = started && *started == *s;
        }
        // parsed with the package (the actors and scenes it names)
        Value m = pkg->manifest;
        if (m.get("scenes").type != Value::Array) m["scenes"] = arr();
        auto& list = m["scenes"].array;
        if (play.get("id").string() == "editor-preview") {
            play.object.erase("trigger");
            if (list.size() >= story_notebook::kMaxScenesPerMod)
                for (auto& sc : list)
                    if (sc.get("id").string() == scene) sc = play;
            if (!by_id(m["scenes"], "editor-preview")) list.push_back(play);
        }
        std::vector<story_notebook::ManifestScene> parsed;
        if (!story_notebook::parse_scenes(m, parsed, why)) return why = "the scene can't play: " + why, r;
        const auto it = std::find_if(parsed.begin(), parsed.end(), [&](const story_notebook::ManifestScene& x) { return x.id == play.get("id").string(); });
        if (it == parsed.end()) return why = "the scene can't play", r;
        const bool restore = p.get("restore").type == Value::Bool ? p.get("restore").boolean : true;
        if (!host_->preview_scene(pkg->mod, *it, declared, restore, why)) return r;
        r["requested"] = true;
        r["scene"] = scene;
        r["declared"] = declared;
        if (!declared && s->get("native").type != Value::Null)
            r["note"] = "the edited steps play; the native version plays after the package is saved and the game starts again";
        ok = true;
        return r;
    }
    if (command == "skip_scene" || command == "stop_scene") {
        if (!host_) return why = "the game is not running", r;
        if (!host_->scene_control(command == "stop_scene", why)) return r;
        r["requested"] = true;
        ok = true;
        return r;
    }
    if (command == "scene_state") {
        r = host_ ? host_->scene_state() : Value();
        if (r.type == Value::Null) r["playing"] = false;
        ok = true;
        return r;
    }
    // copy and paste of scenes and scene steps (actors and quest steps: commands_quest.cpp)
    if (command == "copy" && (p.get("scenes").type == Value::Array || (p.get("scene").type == Value::String && p.get("steps").type == Value::Array))) {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        Value clip;
        clip["mod"] = pkg->mod;
        clip["items"] = arr();
        if (p.get("scenes").type == Value::Array) {
            clip["kind"] = "scenes";
            for (const auto& id : p.get("scenes").array) {
                const Value* s = by_id(pkg->manifest.get("scenes"), id.string());
                if (!s) return why = "no scene \"" + id.string() + "\"", r;
                clip["items"].array.push_back(*s);
            }
        } else {
            clip["kind"] = "scene_steps";
            clip["scene"] = scene;
            const Value* s = by_id(pkg->manifest.get("scenes"), scene);
            if (!s) return why = "no scene \"" + scene + "\"", r;
            std::vector<size_t> picked;
            for (const auto& ref : p.get("steps").array) {
                size_t i = 0;
                if (!scene_step_index(*s, ref, i, why)) return r;
                if (s->get("steps").array[i].get("type").string() == "end") return why = "an end is not copied (a scene ends once on each path)", r;
                picked.push_back(i);
            }
            std::sort(picked.begin(), picked.end());
            picked.erase(std::unique(picked.begin(), picked.end()), picked.end());
            for (size_t i : picked) clip["items"].array.push_back(s->get("steps").array[i]);
        }
        if (clip["items"].array.empty()) return why = "nothing to copy", r;
        clipboard_ = clip;
        r["kind"] = clip["kind"];
        r["count"] = int(clip["items"].array.size());
        ok = true;
        return r;
    }
    if (command == "paste" && (clipboard_.get("kind").string() == "scenes" || clipboard_.get("kind").string() == "scene_steps")) {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        r["ids"] = arr();
        if (clipboard_.get("kind").string() == "scenes") {
            ok = change(*pkg, "Paste " + std::to_string(clipboard_.get("items").array.size()) + " scene(s)", [&](Value& m, std::string&) {
                if (m.get("scenes").type != Value::Array) m["scenes"] = arr();
                for (Value s : clipboard_.get("items").array) {
                    s["id"] = free_scene_id(m, s.get("id").string("scene"));
                    s.object.erase("trigger");  // the copy starts only when given a trigger of its own
                    r["ids"].array.push_back(s.get("id"));
                    m["scenes"].array.push_back(s);
                }
                return true;
            }, r, why);
            return r;
        }
        const std::string into = scene.empty() ? clipboard_.get("scene").string() : scene;
        ok = change(*pkg, "Paste " + std::to_string(clipboard_.get("items").array.size()) + " step(s) into " + into, [&](Value& m, std::string& w) {
            Value* s = by_id(m["scenes"], into);
            if (!s) return w = "no scene \"" + into + "\" in " + pkg->mod, false;
            std::vector<Value> steps = clipboard_.get("items").array;
            // links inside the copied steps stay; links out of them go (the steps follow each other)
            std::set<std::string> inside;
            for (const auto& st : steps)
                if (st.get("id").type == Value::String) inside.insert(st.get("id").text);
            for (auto& st : steps) {
                if (st.get("next").type == Value::String && !inside.contains(st.get("next").text)) st.object.erase("next");
                for (const auto& o : st.get("options").array)
                    if (!inside.contains(o.get("next").string())) return w = "a choice is pasted with the steps its options lead to", false;
            }
            const size_t n = s->get("steps").array.size();
            size_t at = n ? n - 1 : 0;
            if (p.get("before").type != Value::Null && !scene_step_index(*s, p.get("before"), at, w)) return false;
            if (!insert_scene_steps(*s, at, steps, w)) return false;
            for (size_t k = 0; k < steps.size(); k++) r["ids"].array.push_back(Value(int(at + k + 1)));
            return true;
        }, r, why);
        if (ok) r["scene"] = into;
        return r;
    }
    handled = false;
    return r;
}

}  // namespace editor
