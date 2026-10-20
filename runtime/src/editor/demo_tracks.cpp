// World editor: demo step lanes, poses and edits (demo_tracks.h). No game dependencies.
#include "demo_tracks.h"

#include <algorithm>
#include <cmath>

#include "../mods/demo_scene.h"

namespace editor {
namespace {

Value arr() { Value v; v.type = Value::Array; return v; }
Value obj() { Value v; v.type = Value::Object; return v; }
bool fail(std::string& why, const std::string& text) { why = text; return false; }
bool point_of(const Value& v, WorldPoint& p) {
    if (v.type != Value::Object) return false;
    for (const char* k : {"x", "y", "z"}) if (v.get(k).type != Value::Number || !std::isfinite(v.get(k).number)) return false;
    p = {float(v.get("x").number), float(v.get("y").number), float(v.get("z").number)};
    return true;
}
Value demo_point(const WorldPoint& p) {
    Value v;
    v["x"] = std::round(p.x * 10) / 10;
    v["y"] = std::round(p.y * 10) / 10;
    v["z"] = std::round(p.z * 10) / 10;
    return v;
}
const char* const kLists[] = {"messages", "sounds", "particles", "attach", "detach", "time", "weather"};
const char* const kActorLists[] = {"clips", "modes", "data"};
bool step_list(const std::string& l) { return std::any_of(std::begin(kLists), std::end(kLists), [&](const char* n) { return l == n; }); }
bool actor_list(const std::string& l) { return std::any_of(std::begin(kActorLists), std::end(kActorLists), [&](const char* n) { return l == n; }); }

Value* actor_by_id(Value& step, const std::string& id) {
    if (step["actors"].type != Value::Array) return nullptr;
    for (auto& a : step["actors"].array) if (a.get("id").string() == id) return &a;
    return nullptr;
}
const Value* actor_by_id(const Value& step, const std::string& id) { return actor_by_id(const_cast<Value&>(step), id); }

// A lane name: the track object it names (created when `create`), and whether its values are numbers.
Value* lane_track(Value& step, const std::string& lane, bool create, bool& number, std::string& why) {
    const size_t dot = lane.rfind('.');
    if (dot == std::string::npos || dot == 0) return fail(why, "a lane is camera.eye, camera.target, camera.fovy, camera.roll or <actor>.position / .rotation / .scale"), nullptr;
    const std::string owner = lane.substr(0, dot), field = lane.substr(dot + 1);
    Value* holder = nullptr;
    if (owner == "camera") {
        if (field != "eye" && field != "target" && field != "fovy" && field != "roll") return fail(why, "the camera's lanes are eye, target, fovy and roll"), nullptr;
        number = field == "fovy" || field == "roll";
        if (step.get("camera").type != Value::Object) { if (!create) return fail(why, "the demo has no camera"), nullptr; step["camera"] = obj(); }
        holder = &step["camera"];
    } else {
        if (field != "position" && field != "rotation" && field != "scale") return fail(why, "an actor's lanes are position, rotation and scale"), nullptr;
        number = false;
        holder = actor_by_id(step, owner);
        if (!holder) return fail(why, "the demo has no actor \"" + owner + "\""), nullptr;
    }
    if (holder->get(field).type != Value::Object) {
        if (!create) return fail(why, "lane " + lane + " has no keys"), nullptr;
        (*holder)[field] = obj();
        (*holder)[field]["keys"] = arr();
    }
    Value& t = (*holder)[field];
    if (t.get("keys").type != Value::Array) t["keys"] = arr();
    return &t;
}
void drop_empty(Value& step, const std::string& lane) {
    const size_t dot = lane.rfind('.');
    const std::string owner = lane.substr(0, dot), field = lane.substr(dot + 1);
    Value* holder = owner == "camera" ? &step["camera"] : actor_by_id(step, owner);
    if (!holder || holder->get(field).get("keys").array.size()) return;
    holder->object.erase(field);
    if (owner == "camera" && holder->object.empty()) step.object.erase("camera");
}
Value* event_list(Value& step, const std::string& list, const std::string& actor, bool create, std::string& why) {
    Value* holder = nullptr;
    if (actor_list(list)) {
        holder = actor_by_id(step, actor);
        if (!holder) return fail(why, list + " belong to an actor: give \"actor\" (an id of the demo's actors)"), nullptr;
    } else if (step_list(list)) holder = &step;
    else return fail(why, "event lists are clips, modes, data (an actor's), messages, sounds, particles, attach, detach, time and weather"), nullptr;
    if (holder->get(list).type != Value::Array) {
        if (!create) return fail(why, "no " + list + " here"), nullptr;
        (*holder)[list] = arr();
    }
    return &(*holder)[list];
}
void sort_events(Value& list) {
    std::stable_sort(list.array.begin(), list.array.end(), [](const Value& a, const Value& b) { return a.get("frame").number < b.get("frame").number; });
}

// ---- evaluation ----
struct Key { double frame; double v[3]; };
std::vector<Key> keys_of(const Value& track, bool number) {
    std::vector<Key> out;
    for (const auto& k : track.get("keys").array) {
        Key key{k.get("frame").number, {0, 0, 0}};
        if (number) key.v[0] = k.get("value").number;
        else { WorldPoint p; if (!point_of(k.get("value"), p)) continue; key.v[0] = p.x; key.v[1] = p.y; key.v[2] = p.z; }
        out.push_back(key);
    }
    return out;
}
// The track's value at `f` (frames), each component: as the game plays the curve kinds.
void sample(const std::vector<Key>& k, const std::string& curve, double f, double out[3]) {
    const size_t n = k.size();
    // a B-spline starts off its first key (the game evaluates the curve from there); the end is held
    if (n == 1 || (f <= k.front().frame && curve != "bspline")) { std::copy(k.front().v, k.front().v + 3, out); return; }
    if (f < k.front().frame) f = k.front().frame;
    if (f >= k.back().frame) { std::copy(k.back().v, k.back().v + 3, out); return; }
    size_t s = 0;
    while (s + 1 < n && k[s + 1].frame <= f) ++s;
    const double span = k[s + 1].frame - k[s].frame, t = span > 0 ? (f - k[s].frame) / span : 0;
    for (int c = 0; c < 3; ++c) {
        const double a = k[s].v[c], b = k[s + 1].v[c];
        if (curve == "step") out[c] = a;
        else if (curve == "smooth") {
            // Hermite through the keys, slopes per second from the neighbours (demo_scene's compile)
            auto slope = [&](size_t i) {
                const size_t lo = i ? i - 1 : i, hi = i + 1 < n ? i + 1 : i;
                const double dt = (k[hi].frame - k[lo].frame) / kDemoFps;
                return dt > 0 ? (k[hi].v[c] - k[lo].v[c]) / dt : 0.0;
            };
            const double h = span / kDemoFps, t2 = t * t, t3 = t2 * t;
            out[c] = (2 * t3 - 3 * t2 + 1) * a + (t3 - 2 * t2 + t) * h * slope(s) + (-2 * t3 + 3 * t2) * b + (t3 - t2) * h * slope(s + 1);
        } else if (curve == "bspline") {
            // uniform cubic B-spline over the keys (ends held): rounder, passing near the inner keys
            auto P = [&](long i) { return k[size_t(std::clamp<long>(i, 0, long(n) - 1))].v[c]; };
            const long i = long(s);
            const double p0 = P(i - 1), p1 = P(i), p2 = P(i + 1), p3 = P(i + 2), t2 = t * t, t3 = t2 * t;
            out[c] = ((1 - 3 * t + 3 * t2 - t3) * p0 + (4 - 6 * t2 + 3 * t3) * p1 + (1 + 3 * t + 3 * t2 - 3 * t3) * p2 + t3 * p3) / 6;
        } else out[c] = a + (b - a) * t;
    }
}
bool track_at(const Value& track, bool number, double f, double out[3]) {
    const auto k = keys_of(track, number);
    if (k.empty()) return false;
    sample(k, track.get("curve").string("linear"), f, out);
    return true;
}
std::string lane_kind_label(const Value& a) {
    if (a.get("model").type == Value::Object) return "model";
    if (a.get("character").type == Value::String) return "character";
    return a.get("actor").string() == "Link" ? "link" : "game";
}
std::string event_label(const std::string& list, const Value& e) {
    if (list == "clips") return e.get("clip").type == Value::String ? e.get("clip").text : e.get("archive").string() + "/" + e.get("file").string();
    if (list == "modes") return e.get("clip").type == Value::String ? "clip " + e.get("clip").text : e.get("mode").type == Value::String ? e.get("mode").text : "mode " + std::to_string(int(e.get("mode").number));
    if (list == "messages") return e.get("text").string();
    if (list == "sounds") return "sound " + std::to_string(long(e.get("id").number));
    if (list == "particles") return "particle " + std::to_string(long(e.get("id").number));
    if (list == "attach") return e.get("actor").string() + " on " + e.get("to").string() + (e.get("joint").type == Value::String ? ":" + e.get("joint").text : "");
    if (list == "detach") return e.get("actor").string() + " off";
    if (list == "time") { char t[32]; std::snprintf(t, sizeof t, "%.2f h", e.get("hour").number); return t; }
    if (list == "weather") { char t[32]; std::snprintf(t, sizeof t, "rain %.2f", e.get("rain").number); return t; }
    return "data " + std::to_string(int(e.get("id").number));
}
}  // namespace

Value demo_lanes(const Value& manifest, const Value& step) {
    Value out;
    out["frames"] = step.get("frames").number;
    out["origin"] = step.get("origin");
    Value lanes = arr(), events = arr(), actors = arr();
    auto lane = [&](const std::string& name, const std::string& actor, const Value& track, bool number) {
        if (track.type != Value::Object) return;
        Value l;
        l["lane"] = name;
        if (!actor.empty()) l["actor"] = actor;
        l["kind"] = number ? "number" : "point";
        l["curve"] = track.get("curve").string("linear");
        Value keys = arr();
        for (const auto& k : track.get("keys").array) keys.array.push_back(Value(k.get("frame").number));
        l["keys"] = keys;
        lanes.array.push_back(l);
    };
    auto list = [&](const std::string& name, const std::string& actor, const Value& items) {
        if (items.type != Value::Array) return;
        Value e;
        e["list"] = name;
        if (!actor.empty()) e["actor"] = actor;
        Value it = arr();
        for (size_t i = 0; i < items.array.size(); ++i) {
            Value x;
            x["index"] = double(i);
            x["frame"] = items.array[i].get("frame").number;
            x["label"] = event_label(name, items.array[i]);
            it.array.push_back(x);
        }
        e["items"] = it;
        events.array.push_back(e);
    };
    const Value& cam = step.get("camera");
    for (const char* f : {"eye", "target", "fovy", "roll"}) lane(std::string("camera.") + f, "", cam.get(f), std::string(f) == "fovy" || std::string(f) == "roll");
    for (const auto& a : step.get("actors").array) {
        const std::string id = a.get("id").string();
        Value ac;
        ac["id"] = id;
        ac["kind"] = lane_kind_label(a);
        ac["name"] = a.get("model").type == Value::Object ? a.get("model").get("archive").string() + "/" + a.get("model").get("file").string()
                     : a.get("character").type == Value::String ? a.get("character").text : a.get("actor").string();
        actors.array.push_back(ac);
        for (const char* f : {"position", "rotation", "scale"}) lane(id + "." + f, id, a.get(f), false);
        for (const char* l : kActorLists) list(l, id, a.get(l));
    }
    for (const char* l : kLists) list(l, "", step.get(l));
    out["lanes"] = lanes;
    out["events"] = events;
    out["actors"] = actors;
    std::string why;
    if (!demo_check(manifest, step, why)) out["problem"] = why;
    return out;
}

bool demo_pose(const Value& step, double frame, const WorldPoint& link, double link_angle, Value& out, std::string& why) {
    if (step.get("type").string() != "demo") return fail(why, "not a demo step");
    // where the demo's coordinates are: Link at its start, a fixed origin, or the world
    const double angle = step.get("angle").number;
    (void)link_angle;  // the demo turns by its own "angle", not by Link's facing
    auto world = [&](const double v[3]) { return demo_world(step, {float(v[0]), float(v[1]), float(v[2])}, link); };
    out = obj();
    const Value& cam = step.get("camera");
    double e[3], t[3], n[3];
    if (track_at(cam.get("eye"), false, frame, e) && track_at(cam.get("target"), false, frame, t)) {
        Value c;
        c["eye"] = demo_point(world(e));
        c["target"] = demo_point(world(t));
        c["fovy"] = track_at(cam.get("fovy"), true, frame, n) ? n[0] : 60.0;
        c["roll"] = track_at(cam.get("roll"), true, frame, n) ? n[0] : 0.0;
        out["camera"] = c;
    }
    Value actors = arr();
    for (const auto& ac : step.get("actors").array) {
        Value x;
        x["id"] = ac.get("id");
        double v[3];
        if (track_at(ac.get("position"), false, frame, v)) x["position"] = demo_point(world(v));
        if (track_at(ac.get("rotation"), false, frame, v)) { v[1] += angle; x["rotation"] = demo_point({float(v[0]), float(v[1]), float(v[2])}); }
        if (track_at(ac.get("scale"), false, frame, v)) x["scale"] = demo_point({float(v[0]), float(v[1]), float(v[2])});
        actors.array.push_back(x);
    }
    out["actors"] = actors;
    out["frame"] = frame;
    return true;
}

bool demo_set_key(Value& step, const std::string& lane, unsigned frame, const Value& value, std::string& why) {
    if (step.get("type").string() != "demo") return fail(why, "not a demo step");
    if (frame > unsigned(step.get("frames").number)) return fail(why, "a key's frame is inside the demo (0.." + std::to_string(int(step.get("frames").number)) + ")");
    bool number = false;
    Value* t = lane_track(step, lane, true, number, why);
    if (!t) return false;
    WorldPoint p;
    if (number ? (value.type != Value::Number || !std::isfinite(value.number)) : !point_of(value, p)) return fail(why, number ? "this lane's value is a number" : "this lane's value is {x, y, z}");
    Value key;
    key["frame"] = double(frame);
    key["value"] = number ? value : demo_point(p);
    auto& keys = (*t)["keys"].array;
    auto it = std::find_if(keys.begin(), keys.end(), [&](const Value& k) { return unsigned(k.get("frame").number) == frame; });
    if (it != keys.end()) *it = key;
    else {
        keys.insert(std::upper_bound(keys.begin(), keys.end(), double(frame), [](double f, const Value& k) { return f < k.get("frame").number; }), key);
    }
    return true;
}
bool demo_move_key(Value& step, const std::string& lane, unsigned frame, unsigned to, std::string& why) {
    bool number = false;
    Value* t = lane_track(step, lane, false, number, why);
    if (!t) return false;
    if (to > unsigned(step.get("frames").number)) return fail(why, "a key's frame is inside the demo");
    auto& keys = (*t)["keys"].array;
    auto it = std::find_if(keys.begin(), keys.end(), [&](const Value& k) { return unsigned(k.get("frame").number) == frame; });
    if (it == keys.end()) return fail(why, "lane " + lane + " has no key at frame " + std::to_string(frame));
    if (to != frame && std::any_of(keys.begin(), keys.end(), [&](const Value& k) { return unsigned(k.get("frame").number) == to; }))
        return fail(why, "lane " + lane + " already has a key at frame " + std::to_string(to));
    Value key = *it;
    keys.erase(it);
    key["frame"] = double(to);
    keys.insert(std::upper_bound(keys.begin(), keys.end(), double(to), [](double f, const Value& k) { return f < k.get("frame").number; }), key);
    return true;
}
bool demo_remove_key(Value& step, const std::string& lane, unsigned frame, std::string& why) {
    bool number = false;
    Value* t = lane_track(step, lane, false, number, why);
    if (!t) return false;
    auto& keys = (*t)["keys"].array;
    auto it = std::find_if(keys.begin(), keys.end(), [&](const Value& k) { return unsigned(k.get("frame").number) == frame; });
    if (it == keys.end()) return fail(why, "lane " + lane + " has no key at frame " + std::to_string(frame));
    keys.erase(it);
    drop_empty(step, lane);
    return true;
}
bool demo_set_curve(Value& step, const std::string& lane, const std::string& curve, std::string& why) {
    if (curve != "linear" && curve != "smooth" && curve != "bspline" && curve != "step") return fail(why, "a curve is linear, smooth, bspline or step");
    bool number = false;
    Value* t = lane_track(step, lane, false, number, why);
    if (!t) return false;
    if (curve == "linear") t->object.erase("curve");
    else (*t)["curve"] = curve;
    return true;
}

bool demo_add_event(Value& step, const std::string& list, const std::string& actor, const Value& event, size_t& index, std::string& why) {
    if (event.type != Value::Object || event.get("frame").type != Value::Number) return fail(why, "an event is an object with a \"frame\"");
    Value* l = event_list(step, list, actor, true, why);
    if (!l) return false;
    l->array.push_back(event);
    sort_events(*l);
    // its place after sorting (the last of equal frames: it was added last)
    index = 0;
    for (size_t i = 0; i < l->array.size(); ++i) if (l->array[i].get("frame").number <= event.get("frame").number) index = i;
    return true;
}
bool demo_edit_event(Value& step, const std::string& list, const std::string& actor, size_t index, const Value& fields, std::string& why) {
    Value* l = event_list(step, list, actor, false, why);
    if (!l) return false;
    if (index >= l->array.size()) return fail(why, "no " + list + " event " + std::to_string(index));
    if (fields.type != Value::Object) return fail(why, "fields are {name: value} (null removes one)");
    Value& e = l->array[index];
    for (const auto& [k, v] : fields.object) {
        if (v.type == Value::Null) { if (k == "frame") return fail(why, "an event keeps its frame"); e.object.erase(k); }
        else e[k] = v;
    }
    sort_events(*l);
    return true;
}
bool demo_remove_event(Value& step, const std::string& list, const std::string& actor, size_t index, std::string& why) {
    Value* l = event_list(step, list, actor, false, why);
    if (!l) return false;
    if (index >= l->array.size()) return fail(why, "no " + list + " event " + std::to_string(index));
    l->array.erase(l->array.begin() + long(index));
    if (l->array.empty()) {
        Value* holder = actor_list(list) ? actor_by_id(step, actor) : &step;
        if (holder) holder->object.erase(list);
    }
    return true;
}

bool demo_add_actor(Value& step, const Value& actor, std::string& why) {
    if (actor.type != Value::Object || actor.get("id").type != Value::String || actor.get("id").text.empty()) return fail(why, "an actor needs an \"id\"");
    if (actor_by_id(step, actor.get("id").text)) return fail(why, "the demo already has an actor \"" + actor.get("id").text + "\"");
    if (step.get("actors").type != Value::Array) step["actors"] = arr();
    step["actors"].array.push_back(actor);
    return true;
}
bool demo_remove_actor(Value& step, const std::string& id, std::string& why) {
    if (!actor_by_id(step, id)) return fail(why, "the demo has no actor \"" + id + "\"");
    for (const char* l : {"sounds", "particles", "attach", "detach"})
        for (const auto& e : step.get(l).array)
            if (e.get("actor").string() == id || e.get("to").string() == id) return fail(why, std::string("a ") + l + " entry names " + id + ": remove it first");
    auto& a = step["actors"].array;
    a.erase(std::remove_if(a.begin(), a.end(), [&](const Value& x) { return x.get("id").string() == id; }), a.end());
    if (a.empty()) step.object.erase("actors");
    return true;
}

WorldPoint demo_world(const Value& step, const WorldPoint& l, const WorldPoint& link) {
    double ox = 0, oy = 0, oz = 0;
    if (step.get("origin").type == Value::String) ox = link.x, oy = link.y, oz = link.z;
    else if (step.get("origin").type == Value::Object) { WorldPoint o; if (point_of(step.get("origin"), o)) ox = o.x, oy = o.y, oz = o.z; }
    else return l;
    const double a = step.get("angle").number * 3.14159265358979 / 180, cs = std::cos(a), sn = std::sin(a);
    return {float(ox + l.x * cs + l.z * sn), float(oy + l.y), float(oz - l.x * sn + l.z * cs)};
}

WorldPoint demo_local(const Value& step, const WorldPoint& w, const WorldPoint& link) {
    double ox = 0, oy = 0, oz = 0;
    if (step.get("origin").type == Value::String) ox = link.x, oy = link.y, oz = link.z;
    else if (step.get("origin").type == Value::Object) { WorldPoint o; if (point_of(step.get("origin"), o)) ox = o.x, oy = o.y, oz = o.z; }
    else return w;
    const double a = step.get("angle").number * 3.14159265358979 / 180, cs = std::cos(a), sn = std::sin(a);
    const double dx = w.x - ox, dz = w.z - oz;
    return {float(dx * cs - dz * sn), float(w.y - oy), float(dx * sn + dz * cs)};
}

bool demo_check(const Value& manifest, const Value& step, std::string& why) {
    Value fields = step;
    fields.object.erase("id");
    fields.object.erase("next");
    demo_scene::Demo d;
    if (!demo_scene::parse(fields, d, why)) return false;
    for (const auto& a : d.actors)
        if (a.kind == demo_scene::Actor::Kind::Character) {
            bool found = false;
            for (const auto& m : manifest.get("actors").array) found |= m.get("id").string() == a.character;
            if (!found) return fail(why, "character " + a.character + " is not one of the mod's placed actors");
        }
    return true;
}

Value new_demo_step(const WorldPoint& eye, const WorldPoint& target, unsigned frames) {
    Value s;
    s["type"] = "demo";
    s["frames"] = double(frames ? frames : 120);
    Value cam = obj();
    for (const auto& [name, p] : {std::pair<const char*, WorldPoint>{"eye", eye}, {"target", target}}) {
        Value k;
        k["frame"] = 0.0;
        k["value"] = demo_point(p);
        Value t = obj();
        t["keys"] = arr();
        t["keys"].array.push_back(k);
        cam[name] = t;
    }
    s["camera"] = cam;
    return s;
}

}  // namespace editor
