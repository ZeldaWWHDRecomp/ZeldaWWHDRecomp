// World editor: the mod's room paths ("paths", story_room_path.h): lines and areas in a room that the
// mod's own actors name by id (a wall cannon's firing area). Added around Link, the fly camera or a
// position, their points added, moved and removed, closed or open, renamed (the actors that name it
// follow) and removed (refused while an actor names it). Each change is one undo step and goes to the
// game at once (a kept path keeps its record, so an actor on it sees the change).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "commands.h"

namespace editor {
namespace {
std::string text(const Value& p, const char* key) { return p.get(key).string(); }
double tenth(double v) { return std::round(v * 10) / 10; }
Value rounded_point(const WorldPoint& p) {
    Value v;
    v["x"] = tenth(p.x);
    v["y"] = tenth(p.y);
    v["z"] = tenth(p.z);
    return v;
}
bool point_of(const Value& v, WorldPoint& p) {
    if (v.type != Value::Object) return false;
    for (const char* k : {"x", "y", "z"})
        if (v.get(k).type != Value::Number || !std::isfinite(v.get(k).number)) return false;
    p = {float(v.get("x").number), float(v.get("y").number), float(v.get("z").number)};
    return true;
}
Value* path_by_id(Value& m, const std::string& id) {
    Value& list = m["paths"];
    if (list.type != Value::Array) return nullptr;
    for (auto& v : list.array)
        if (v.get("id").type == Value::String && v.get("id").text == id) return &v;
    return nullptr;
}
// the placed actors that name path `id`
std::vector<std::string> users(const Value& m, const std::string& id) {
    std::vector<std::string> out;
    for (const auto& a : m.get("actors").array)
        if (a.get("path").type == Value::String && a.get("path").text == id) out.push_back(a.get("id").string());
    return out;
}
std::string free_path_id(const Value& m, const std::string& base) {
    auto taken = [&](const std::string& id) {
        for (const auto& v : m.get("paths").array)
            if (v.get("id").string() == id) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int n = 2;; n++)
        if (!taken(base + "-" + std::to_string(n))) return base + "-" + std::to_string(n);
}
story_room_path::Path parsed(const Value& v) {
    story_room_path::Path p;
    p.id = v.get("id").string();
    p.stage = v.get("stage").string();
    p.room = unsigned(v.get("room").number);
    p.closed = v.get("closed").boolean;
    for (const auto& q : v.get("points").array) {
        WorldPoint w;
        if (point_of(q, w)) p.points.push_back({w.x, w.y, w.z});
    }
    return p;
}
}  // namespace

std::string room_path_use(const Value& actor) {
    if (actor.get("kind").type != Value::Null) return {};
    const std::string type = actor.get("actor").string(), variant = actor.get("parameters").get("variant").string();
    if (!story_enemy_variants::room_path_rule(type, variant)) return {};
    if (type == "wall_cannon") return "Its firing area: it turns to Link and fires only while he (and where it aims) is inside this area, by the game's own test.";
    if (type == "warship") return "Its route: it sails the path round and round at the sea's height (a line too: from the last point back to the first), slowing to fire at Link.";
    if (type == "wizzrobe") return "Its path: it appears at the points of this path and summons at the points of its next path (set the next path below).";
    if (type == "kargaroc") return "Its flight: it flies the path (round if it is an area, else there and back) carrying its rider, and lets the rider go at a point marked \"release\".";
    if (type == "floormaster") return "Its route: its hole glides along the path round and round and rises when Link comes near. Keep the points on the floor.";
    return "The path it uses.";
}

Value Session::dispatch_paths(const std::string& command, const Value& p, std::string& why, bool& ok, bool& handled) {
    Value r;
    ok = false;
    handled = true;
    // a change of path "id": one undo step
    auto path_edit = [&](const std::string& label, const std::function<bool(Value&, Value&, std::string&)>& set) {
        Package* pkg = package(p, why);
        if (!pkg) return;
        const std::string id = text(p, "id");
        ok = change(*pkg, label, [&](Value& m, std::string& w) {
            Value* path = path_by_id(m, id);
            if (!path) return w = "no path \"" + id + "\" in " + pkg->mod, false;
            return set(m, *path, w);
        }, r, why);
        if (ok && r.get("id").type == Value::Null) r["id"] = id;
    };
    // the point numbers are 1-based, as the editor shows them
    auto index_of = [&](const Value& path, const char* key, size_t& out, std::string& w) {
        const auto& pts = path.get("points").array;
        const Value& n = p.get(key);
        if (n.type != Value::Number || n.number < 1 || n.number > double(pts.size()) || std::floor(n.number) != n.number)
            return w = "\"" + std::string(key) + "\" is a point number from 1 to " + std::to_string(pts.size()), false;
        out = size_t(n.number) - 1;
        return true;
    };
    // where a new or moved point goes: at Link, at the fly camera or at a position, on the ground
    auto point_at = [&](WorldPoint& pos, Place& place, std::string& w) {
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

    if (command == "list_paths") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        r["paths"].type = Value::Array;
        for (const auto& v : pkg->manifest.get("paths").array) {
            Value e = v;
            e["used_by"].type = Value::Array;
            for (const auto& u : users(pkg->manifest, v.get("id").string())) e["used_by"].array.push_back(Value(u));
            if (host_) {
                const Value game = host_->path_state();
                for (const auto& g : game.array)
                    if (g.get("mod").string() == pkg->mod && g.get("id").string() == v.get("id").string()) e["game"] = g;
            }
            r["paths"].array.push_back(e);
        }
        ok = true;
        return r;
    }
    if (command == "add_path") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        Place place;
        WorldPoint at;
        if (!point_at(at, place, why)) return r;
        const bool closed = p.get("closed").type == Value::Bool ? p.get("closed").boolean : true;
        double size = p.get("size").type == Value::Number ? p.get("size").number : 800;
        if (!std::isfinite(size) || size < 50 || size > 20000) return why = "\"size\" is 50 to 20000 units", r;
        std::string id = text(p, "id");
        ok = change(*pkg, "Add a path", [&](Value& m, std::string& w) {
            if (id.empty()) id = free_path_id(m, closed ? "area" : "path");
            else if (path_by_id(m, id)) return w = "the mod already has a path \"" + id + "\"", false;
            Value v;
            v["id"] = id;
            v["stage"] = place.stage;
            v["room"] = double(place.room);
            v["closed"] = closed;
            v["points"].type = Value::Array;
            if (p.get("points").type == Value::Array) {
                for (const auto& q : p.get("points").array) {
                    WorldPoint w2;
                    if (!point_of(q, w2)) return w = "\"points\" are {x, y, z}", false;
                    v["points"].array.push_back(rounded_point(w2));
                    if (q.get("release").type == Value::Bool && q.get("release").boolean) v["points"].array.back()["release"] = true;
                }
            } else {
                // a square around the place (an area), or a line ahead along the heading
                const double h = size / 2, yaw = place.angle * 3.14159265358979323846 / 32768.0;
                std::vector<WorldPoint> pts;
                if (closed)
                    for (const auto& c : {std::pair{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}) pts.push_back({float(at.x + c.first * h), at.y, float(at.z + c.second * h)});
                else
                    pts = {at, {float(at.x + std::sin(yaw) * size), at.y, float(at.z + std::cos(yaw) * size)}};
                for (const auto& q : pts) v["points"].array.push_back(rounded_point(q));
            }
            if (m["paths"].type != Value::Array) m["paths"].type = Value::Array;
            m["paths"].array.push_back(v);
            return true;
        }, r, why);
        if (ok) r["id"] = id;
        return r;
    }
    if (command == "add_path_point") {
        Place place;
        WorldPoint pos;
        if (!point_at(pos, place, why)) return r;
        path_edit("Add a point to path " + text(p, "id"), [&](Value&, Value& path, std::string& w) {
            auto& pts = path["points"].array;
            if (pts.size() >= story_room_path::kMaxPoints) return w = "a path has at most 32 points", false;
            size_t after = pts.size();
            if (p.get("after").type == Value::Number) {
                const double n = p.get("after").number;
                if (n < 0 || n > double(pts.size()) || std::floor(n) != n) return w = "\"after\" is a point number from 0 to " + std::to_string(pts.size()), false;
                after = size_t(n);
            }
            pts.insert(pts.begin() + std::ptrdiff_t(after), rounded_point(pos));
            r["point"] = double(after + 1);
            return true;
        });
        return r;
    }
    if (command == "move_path_point") {
        WorldPoint pos;
        if (p.get("position").type == Value::Object && p.get("at").type == Value::Null) {
            if (!point_of(p.get("position"), pos)) return why = "\"position\" is {x, y, z}", r;
        } else {
            Place place;
            if (!point_at(pos, place, why)) return r;
        }
        path_edit("Move a point of path " + text(p, "id"), [&](Value&, Value& path, std::string& w) {
            size_t i = 0;
            if (!index_of(path, "point", i, w)) return false;
            const Value keep = path["points"].array[i].get("release");
            path["points"].array[i] = rounded_point(pos);
            if (keep.type == Value::Bool && keep.boolean) path["points"].array[i]["release"] = true;  // a rider's release point stays one
            return true;
        });
        return r;
    }
    if (command == "remove_path_point") {
        path_edit("Remove a point of path " + text(p, "id"), [&](Value&, Value& path, std::string& w) {
            size_t i = 0;
            if (!index_of(path, "point", i, w)) return false;
            auto& pts = path["points"].array;
            const size_t least = path.get("closed").boolean ? story_room_path::kMinAreaPoints : story_room_path::kMinPoints;
            if (pts.size() <= least) return w = std::string(path.get("closed").boolean ? "an area keeps at least 3 points" : "a path keeps at least 2 points") + " (remove the path instead)", false;
            pts.erase(pts.begin() + std::ptrdiff_t(i));
            return true;
        });
        return r;
    }
    if (command == "set_path_point") {
        // a point's marks: "release" (a Kargaroc lets its rider go there)
        path_edit("Change a point of path " + text(p, "id"), [&](Value&, Value& path, std::string& w) {
            size_t i = 0;
            if (!index_of(path, "point", i, w)) return false;
            if (p.get("release").type != Value::Bool) return w = "\"release\" is true or false", false;
            Value& pt = path["points"].array[i];
            if (p.get("release").boolean) pt["release"] = true;
            else pt.object.erase("release");
            return true;
        });
        return r;
    }
    if (command == "set_path") {
        path_edit("Change path " + text(p, "id"), [&](Value& m, Value& path, std::string& w) {
            if (p.get("closed").type == Value::Bool) path["closed"] = p.get("closed").boolean;
            if (p.object.contains("next")) {  // the path that follows it (a summoner's summon points); null: none
                if (p.get("next").type == Value::String) path["next"] = p.get("next").text;
                else path.object.erase("next");
            }
            if (p.get("new_id").type == Value::String) {
                const std::string to = p.get("new_id").text, from = path.get("id").string();
                if (to != from) {
                    if (!story_mail::valid_id(to)) return w = "\"" + to + "\" is not a valid id", false;
                    if (path_by_id(m, to)) return w = "the mod already has a path \"" + to + "\"", false;
                    path["id"] = to;
                    for (auto& a : m["actors"].array)  // the actors that name it follow
                        if (a.get("path").type == Value::String && a.get("path").text == from) a["path"] = to;
                    for (auto& q : m["paths"].array)  // and the paths it follows
                        if (q.get("next").type == Value::String && q.get("next").text == from) q["next"] = to;
                    r["id"] = to;
                }
            }
            return true;
        });
        return r;
    }
    if (command == "remove_path") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string id = text(p, "id");
        ok = change(*pkg, "Remove path " + id, [&](Value& m, std::string& w) {
            if (!path_by_id(m, id)) return w = "no path \"" + id + "\" in " + pkg->mod, false;
            for (const auto& q : m.get("paths").array)
                if (q.get("next").type == Value::String && q.get("next").text == id) return w = "path \"" + id + "\" follows path \"" + q.get("id").string() + "\" (its next path): change that first", false;
            const auto used = users(m, id);
            if (!used.empty()) {
                std::string list;
                for (const auto& u : used) list += (list.empty() ? "" : ", ") + u;
                return w = "path \"" + id + "\" is used by " + list + ": give " + (used.size() > 1 ? "them" : "it") + " another path or remove " +
                           (used.size() > 1 ? "them" : "it") + " first",
                       false;
            }
            auto& list = m["paths"].array;
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Value& v) { return v.get("id").string() == id; }), list.end());
            if (list.empty()) m.object.erase("paths");
            return true;
        }, r, why);
        if (ok) r["id"] = id;
        return r;
    }
    if (command == "check_path") {
        // a warning check for the actors that use the path (never a refusal): a warship's route over land
        // (ground above the sea's height under it), a Floormaster's route off the floor (no ground just
        // below a point of it, or a step of more than 50 units); sampled every 50 units, the route closed
        // (both sail or glide it round)
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string id = text(p, "id");
        Value* path = path_by_id(pkg->manifest, id);
        if (!path) return why = "no path \"" + id + "\" in " + pkg->mod, r;
        std::string kind = text(p, "for");
        if (kind.empty())
            for (const auto& a : pkg->manifest.get("actors").array)
                if (a.get("path").type == Value::String && a.get("path").text == id) kind = a.get("actor").string();
        if (kind != "warship" && kind != "floormaster") return why = "check_path checks a warship's or a Floormaster's route (\"for\")", r;
        if (!host_) return why = "the game is not running", r;
        const auto rp = parsed(*path);
        std::vector<WorldPoint> pts;
        for (const auto& q : rp.points) pts.push_back({q.x, q.y, q.z});
        if (pts.size() > 1) pts.push_back(pts.front());
        struct Sample { size_t segment; WorldPoint at; };
        std::vector<Sample> samples;
        for (size_t i = 1; i < pts.size(); i++) {
            const auto &f = pts[i - 1], &t = pts[i];
            const int n = std::max(1, int(std::ceil(std::hypot(double(t.x) - f.x, double(t.z) - f.z) / 50)));
            for (int k = 0; k <= n; k++) {
                const double u = double(k) / n;
                samples.push_back({i - 1, {float(f.x + (t.x - f.x) * u), float(f.y + (t.y - f.y) * u), float(f.z + (t.z - f.z) * u)}});
            }
        }
        const bool ship = kind == "warship";
        std::vector<WorldPoint> above;
        for (const auto& sm : samples) above.push_back({sm.at.x, ship ? 5000.f : sm.at.y + 100, sm.at.z});
        std::vector<float> heights;
        std::vector<bool> found;
        if (!host_->grounds(above, heights, found, why)) return r;
        r["for"] = kind;
        r["segments"].type = Value::Array;
        size_t k = 0;
        for (size_t i = 1; i < pts.size(); i++) {
            Value seg;
            seg["from"] = double(i);
            seg["to"] = double(i + 1 == pts.size() ? 1 : i + 1);
            bool good = true;
            for (; k < samples.size() && samples[k].segment == i - 1; k++) {
                if (!good) continue;
                const auto& at = samples[k].at;
                std::string problem;
                if (ship) {
                    if (found[k] && heights[k] > 0.f) problem = "land";
                } else if (!found[k] || std::fabs(heights[k] - at.y) > 50.f) problem = found[k] ? "off the floor (a step or a drop)" : "no floor";
                if (!problem.empty()) {
                    good = false;
                    seg["problem"] = problem;
                    seg["at"] = rounded_point(at);
                }
            }
            seg["ok"] = good;
            r["segments"].array.push_back(seg);
        }
        ok = true;
        return r;
    }
    if (command == "path_inside") {
        Package* pkg = package(p, why);
        if (!pkg) return r;
        const std::string id = text(p, "id");
        Value* path = path_by_id(pkg->manifest, id);
        if (!path) return why = "no path \"" + id + "\" in " + pkg->mod, r;
        if (!path->get("closed").boolean) return why = "path \"" + id + "\" is a line, not an area", r;
        Place place;
        WorldPoint pos;
        if (!point_at(pos, place, why)) return r;
        const auto rp = parsed(*path);
        r["inside"] = place.stage == rp.stage && place.room == int(rp.room) && story_room_path::inside(rp, pos.x, pos.z);
        r["position"] = rounded_point(pos);
        ok = true;
        return r;
    }
    handled = false;
    return r;
}

}  // namespace editor
