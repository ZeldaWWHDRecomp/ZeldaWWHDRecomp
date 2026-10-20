// World editor: a placed actor's route ("path"), for the mod's own skinned actors and for placed
// residents alike: points added at Link, at the fly camera or at a position, moved, reordered and
// removed; speed, mode and pauses; the ground check of its segments; a walking resident's live state.
// Each change is one undo step and goes to the game at once (a route edit keeps the walker in place).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "../mods/story_resident_types.h"
#include "../mods/story_world_manifest.h"
#include "commands.h"

namespace editor {
namespace {
std::string text(const Value& p, const char* key) { return p.get(key).string(); }
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
Value* by_id(Value& list, const std::string& id) {
    if (list.type != Value::Array) return nullptr;
    for (auto& v : list.array)
        if (v.get("id").type == Value::String && v.get("id").text == id) return &v;
    return nullptr;
}
// a Bokoblin patrols its route with the game's patrol code: it starts at its position like a resident
bool patroller(const Value& a) { return (a.get("actor").string() == "bokoblin" || a.get("actor").string() == "moblin") && a.get("kind").type == Value::Null; }
// starts at its position (residents, Bokoblins); a mod model's route lists every point
bool resident(const Value& a) { return a.get("actor").string() == "resident" || patroller(a); }
// a resident's own pace (game units per second), 0 when it can't walk
double pace_of(const Value& a) {
    const auto& r = a.get("parameters").get("resident");
    const auto* t = r.type == Value::String ? story_resident_types::find(r.text) : nullptr;
    const uint8_t subtype = t ? t->subtype : (r.type == Value::Null || (r.type == Value::Number && r.number == 9)) ? 9 : 255;
    const auto* w = story_resident_types::walker(subtype);
    return w ? w->pace : 0;
}
// why this actor can't have a route (empty: it can)
std::string no_route(const Value& a) {
    if (resident(a)) {
        const auto& r = a.get("parameters").get("resident");
        const auto* t = r.type == Value::String ? story_resident_types::find(r.text) : nullptr;
        const uint8_t subtype = t ? t->subtype : 9;
        if (const char* no = story_resident_types::no_walk(subtype)) return no;
        return {};
    }
    if (a.get("kind").string() == "mod") return {};  // the manifest check says when its model isn't skinned
    if (patroller(a)) return {};
    return "only residents, Bokoblins, Moblins and the mod's own models walk routes";
}
}  // namespace

Value Session::dispatch_route(const std::string& command, const Value& p, std::string& why, bool& ok, bool& handled) {
    Value r;
    ok = false;
    handled = true;
    // a change of the route of actor "id": one undo step
    auto route_edit = [&](const std::string& label, const std::function<bool(Value&, std::string&)>& set) {
        Package* pkg = package(p, why);
        if (!pkg) return;
        const std::string id = text(p, "id");
        ok = change(*pkg, label, [&](Value& m, std::string& w) {
            Value* a = by_id(m["actors"], id);
            if (!a) return w = "no placed actor \"" + id + "\" in " + pkg->mod, false;
            if (const auto no = no_route(*a); !no.empty()) return w = no, false;
            return set(*a, w);
        }, r, why);
        if (ok) r["id"] = id;
    };
    // the point numbers are 1-based, as the editor shows them
    auto index_of = [&](const Value& a, const char* key, size_t& out, std::string& w) {
        const auto& pts = a.get("path").get("points").array;
        const Value& n = p.get(key);
        if (n.type != Value::Number || n.number < 1 || n.number > double(pts.size()) || std::floor(n.number) != n.number)
            return w = "\"" + std::string(key) + "\" is a point number from 1 to " + std::to_string(pts.size()), false;
        out = size_t(n.number) - 1;
        return true;
    };
    // where a new or moved point goes: at Link, at the fly camera or at a position, on the ground
    auto point_at = [&](WorldPoint& pos, std::string& w) {
        Place place;
        if (!where(p, place, w)) return false;
        pos = place.position;
        if ((p.get("snap").type != Value::Bool || p.get("snap").boolean) && host_) {
            float h = 0;
            std::string note;
            if (host_->ground({pos.x, pos.y + 50, pos.z}, h, note)) pos.y = h;
            else r["snap_note"] = "not on the ground: " + note;
        }
        return true;
    };

    if (command == "add_route_point") {
        WorldPoint pos;
        if (!point_at(pos, why)) return r;
        route_edit("Add a route point to " + text(p, "id"), [&](Value& a, std::string& w) {
            Value& path = a["path"];
            if (path.type != Value::Object) {
                path = Value();
                path["points"].type = Value::Array;
                path["mode"] = patroller(a) ? "back_and_forth" : "once";  // a patrol goes round or back and forth
                // a mod actor's route lists every point: it starts where the actor stands
                if (!resident(a)) {
                    WorldPoint start;
                    if (!point_of(a.get("position"), start)) return w = "the actor has no position", false;
                    path["points"].array.push_back(rounded(start));
                    path["speed"] = 60.0;
                }
            }
            auto& pts = path["points"].array;
            size_t at = pts.size();
            if (p.get("after").type == Value::Number) {
                const double n = p.get("after").number;
                if (n < 0 || n > double(pts.size()) || std::floor(n) != n) return w = "\"after\" is a point number from 0 to " + std::to_string(pts.size()), false;
                at = size_t(n);
            }
            pts.insert(pts.begin() + long(at), rounded(pos));
            r["point"] = int(at + 1);
            return true;
        });
        return r;
    }
    if (command == "move_route_point") {
        WorldPoint pos;
        const bool given = point_of(p.get("position"), pos);
        if (!given) {
            if (p.get("at").type == Value::Null) return why = "say where: \"position\" or \"at\" (link or camera)", r;
            if (!point_at(pos, why)) return r;
        }
        route_edit("Move route point " + std::to_string(int(p.get("point").number)) + " of " + text(p, "id"), [&](Value& a, std::string& w) {
            size_t i = 0;
            if (!index_of(a, "point", i, w)) return false;
            a["path"]["points"].array[i] = rounded(pos);
            return true;
        });
        return r;
    }
    if (command == "remove_route_point") {
        route_edit("Remove route point " + std::to_string(int(p.get("point").number)) + " of " + text(p, "id"), [&](Value& a, std::string& w) {
            size_t i = 0;
            if (!index_of(a, "point", i, w)) return false;
            auto& pts = a["path"]["points"].array;
            pts.erase(pts.begin() + long(i));
            // fewer than a route needs: no route
            if (pts.size() < (resident(a) ? 1u : 2u)) a.object.erase("path"), r["route_removed"] = true;
            return true;
        });
        return r;
    }
    if (command == "reorder_route_point") {
        route_edit("Reorder the route of " + text(p, "id"), [&](Value& a, std::string& w) {
            size_t from = 0, to = 0;
            if (!index_of(a, "point", from, w) || !index_of(a, "to", to, w)) return false;
            auto& pts = a["path"]["points"].array;
            Value moved = pts[from];
            pts.erase(pts.begin() + long(from));
            pts.insert(pts.begin() + long(to), moved);
            return true;
        });
        return r;
    }
    if (command == "set_route") {
        route_edit("Route settings of " + text(p, "id"), [&](Value& a, std::string& w) {
            Value& path = a["path"];
            if (path.type != Value::Object) return w = "the actor has no route yet: add_route_point adds its first point", false;
            if (patroller(a) && (p.object.contains("speed") || p.object.contains("pauses")))
                return w = "a Bokoblin or Moblin patrols at its own pace, without pauses: only the mode (loop or back_and_forth)", false;
            if (p.object.contains("speed")) {
                if (p.get("speed").type == Value::Null) {
                    if (!resident(a)) return w = "a model's route needs a speed", false;
                    path.object.erase("speed");  // the character's own pace
                } else
                    path["speed"] = p.get("speed");
            }
            if (p.object.contains("mode")) path["mode"] = p.get("mode");
            if (p.object.contains("pauses")) {
                if (p.get("pauses").type == Value::Bool && !p.get("pauses").boolean) path.object.erase("pauses");
                else path["pauses"] = p.get("pauses");
            }
            for (const char* clip : {"walk", "idle"})
                if (p.object.contains(clip)) {
                    if (resident(a)) return w = "a resident walks with its own walk and stands with its own idle", false;
                    if (p.get(clip).type == Value::Null || p.get(clip).string().empty()) path.object.erase(clip);
                    else path[clip] = p.get(clip);
                }
            return true;
        });
        return r;
    }
    if (command == "remove_route") {
        route_edit("Remove the route of " + text(p, "id"), [&](Value& a, std::string&) {
            a.object.erase("path");
            return true;
        });
        return r;
    }
    if (command == "check_route") {
        // the ground under each segment, sampled every 25 units: none, or a step of more than 50 units
        // between samples (a wall, an edge), marks the segment
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const Value* a = nullptr;
        for (const auto& x : pkg->manifest.get("actors").array)
            if (x.get("id").string() == text(p, "id")) a = &x;
        if (!a) return why = "no placed actor \"" + text(p, "id") + "\"", r;
        std::vector<WorldPoint> pts;
        WorldPoint q;
        if (resident(*a) && point_of(a->get("position"), q)) pts.push_back(q);
        for (const auto& v : a->get("path").get("points").array)
            if (point_of(v, q)) pts.push_back(q);
        const std::string mode = a->get("path").get("mode").string("once");
        if (mode == "loop" && pts.size() > 1) pts.push_back(pts.front());
        if (!host_) return why = "the game is not running", r;
        // every sample of every segment, then one ground check for all of them
        struct Sample { size_t segment; WorldPoint at; };
        std::vector<Sample> samples;
        for (size_t i = 1; i < pts.size(); i++) {
            const auto& f = pts[i - 1];
            const auto& t = pts[i];
            const int n = std::max(1, int(std::ceil(std::hypot(double(t.x) - f.x, double(t.z) - f.z) / 25)));
            for (int k = 0; k <= n; k++) {
                const double s = double(k) / n;
                samples.push_back({i - 1, {float(f.x + (t.x - f.x) * s), float(f.y + (t.y - f.y) * s), float(f.z + (t.z - f.z) * s)}});
            }
        }
        std::vector<WorldPoint> above;
        for (const auto& s : samples) above.push_back({s.at.x, s.at.y + 100, s.at.z});
        std::vector<float> heights;
        std::vector<bool> found;
        if (!host_->grounds(above, heights, found, why)) return r;
        r["segments"].type = Value::Array;
        size_t k = 0;
        for (size_t i = 1; i < pts.size(); i++) {
            Value seg;
            // point numbers as the editor shows them: a resident's 0 is its position (the start)
            const int offset = resident(*a) ? 0 : 1;
            seg["from"] = int(i - 1) + offset;
            seg["to"] = mode == "loop" && i + 1 == pts.size() ? offset : int(i) + offset;
            bool good = true, first = true;
            std::string problem;
            float last = 0;
            for (; k < samples.size() && samples[k].segment == i - 1; k++) {
                if (!good) continue;
                const auto& at = samples[k].at;
                if (!found[k]) good = false, problem = "no ground";
                else if (!first && std::fabs(heights[k] - last) > 50) good = false, problem = heights[k] < last ? "a drop" : "a step up too high (a wall)";
                else if (std::fabs(heights[k] - at.y) > 150) good = false, problem = "the ground is far from the route's height";
                if (!good) seg["at"] = rounded(at);
                last = heights[k];
                first = false;
            }
            seg["ok"] = good;
            if (!good) seg["problem"] = problem;
            r["segments"].array.push_back(seg);
        }
        ok = true;
        return r;
    }
    if (command == "route_status") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        if (!host_) return why = "the game is not running", r;
        r = host_->route_state(pkg->mod, text(p, "id"));
        if (r.type == Value::Null) return why = "no live route for \"" + text(p, "id") + "\"", r;
        ok = true;
        return r;
    }
    handled = false;
    return r;
}

double resident_pace(const Value& actor) { return pace_of(actor); }
std::string route_refusal(const Value& actor) { return no_route(actor); }

}  // namespace editor
