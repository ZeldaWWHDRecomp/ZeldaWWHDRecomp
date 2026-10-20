// World editor: the game objects palette (story_game_objects_catalogue.h). The game's own static objects a
// mod places by reference: list_game_objects lists the catalogue with whether the player's game files have
// each one, add_actor takes "game:<id>" as its type, and set_actor sets a game object's "variant". No game
// data passes through here: the manifest holds the neutral id, the variant and the placement only.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <string>

#include "../mods/story_game_objects_catalogue.h"
#include "../mods/story_game_pieces_catalogue.h"
#include "commands.h"

namespace editor {
namespace {
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
double tenth(double v) { return std::round(v * 10) / 10; }
Value point(double x, double y, double z) {
    Value v;
    v["x"] = tenth(x), v["y"] = tenth(y), v["z"] = tenth(z);
    return v;
}
}  // namespace

Value game_object_info(const story_game_objects::Entry& e) {
    Value v;
    v["type"] = "game:" + e.id;
    v["id"] = e.id;
    v["label"] = e.label;
    v["category"] = e.category;
    v["description"] = e.description;
    v["since"] = double(e.since);
    v["collision"] = e.collision;
    if (e.batch()) {
        v["batch"] = e.packet.kind;  // a shared batch: removed or changed when the room loads again
        v["note"] = "grass, flowers and small trees go or change when their room loads again (Reload room)";
    }
    v["rotation"] = "yaw";
    Value scale;
    scale["min"] = double(e.scale_min), scale["max"] = double(e.scale_max);
    v["scale"] = scale;
    Value variants;
    variants.type = Value::Array;
    for (const auto& x : e.variants) {
        Value w;
        w["id"] = x.id, w["label"] = x.label;
        if (x.units) w["units"] = double(x.units);
        variants.array.push_back(w);
    }
    v["variants"] = variants;
    return v;
}

bool new_game_object(const std::string& type, const std::string& id, const Place& place, const Value& options, Value& a, std::string& why) {
    const auto* e = story_game_objects::catalogue().find(story_game_objects::reference(type));
    if (!e) return why = "\"" + type + "\" is not in the game objects catalogue (list_game_objects)", false;
    if (!place.valid) return why = "no place to put it (Link is not in the scene)", false;
    a = Value();
    a["id"] = id;
    a["actor"] = "game:" + e->id;
    const std::string variant = options.get("variant").string();
    if (!variant.empty()) {
        if (!e->variant(variant)) return why = "\"" + type + "\" has no variant \"" + variant + "\"", false;
        if (variant != e->variants.front().id) a["variant"] = variant;
    }
    a["stage"] = place.stage;
    a["room"] = place.room;
    a["position"] = point(place.position.x, place.position.y, place.position.z);
    const double yaw = tenth(std::fmod(place.angle * 360.0 / 65536.0 + 360.0, 360.0));
    if (yaw != 0) a["rotation"] = point(0, yaw, 0);
    return true;
}

void require_game_objects(Value& manifest, const std::string& type) {
    const auto* e = story_game_objects::catalogue().find(story_game_objects::reference(type));
    if (!e) return;
    Value& req = manifest["requires"];
    const Value& had = req.get("game_objects");
    const unsigned have = had.type == Value::Number && had.number >= 1 ? unsigned(had.number) : 0;
    if (have < e->since) req["game_objects"] = double(e->since);
}

Value Session::dispatch_game_objects(const std::string& command, const Value& p, std::string& why, bool& ok, bool& handled) {
    Value r;
    handled = true;
    if (command == "list_game_objects") {
        const auto& c = story_game_objects::catalogue();
        const std::string category = p.get("category").string(), query = lower(p.get("query").string());
        Value list, categories;
        list.type = categories.type = Value::Array;
        std::set<std::string> seen;
        for (const auto& e : c.entries) {
            if (seen.insert(e.category).second) categories.array.push_back(e.category);
            if (!category.empty() && lower(e.category) != lower(category)) continue;
            if (!query.empty() && lower(e.id + " " + e.label + " " + e.description + " " + e.category).find(query) == std::string::npos) continue;
            Value v = game_object_info(e);
            std::string where = host_ ? host_->game_archive(e.archive) : std::string("?");
            for (const auto& more : e.also_archives)
                if (!where.empty() && where != "?" && host_->game_archive(more).empty()) where.clear();
            if (where == "?") v["available"] = Value();  // unknown: the game is not running
            else {
                v["available"] = !where.empty();
                if (where.empty()) v["why"] = "not available in your game files";
            }
            v["live"] = host_ ? host_->live_kind(v.get("type").string(), true) : false;
            list.array.push_back(v);
        }
        // island buildings (commands_game_pieces.cpp): built from the player's room archive when placed
        const auto& pieces = story_game_pieces::catalogue();
        const std::string mod = p.get("mod").string().empty() ? open_ : p.get("mod").string();
        for (const auto& piece : pieces.pieces) {
            if (seen.insert(piece.category).second) categories.array.push_back(piece.category);
            if (!category.empty() && lower(piece.category) != lower(category)) continue;
            if (!query.empty() && lower(piece.id + " " + piece.label + " " + piece.description + " " + piece.category).find(query) == std::string::npos) continue;
            Value v = game_piece_info(piece);
            const std::string state = host_ ? host_->piece_state(mod, piece.id) : std::string();
            v["state"] = state;  // "" not prepared, "preparing", "ready", "failed: ..."
            v["preparing"] = state == "preparing";
            if (state.rfind("failed", 0) == 0) {
                v["available"] = false;
                v["why"] = state;
            } else {
                v["available"] = Value();  // known once it is built (the player's room archive)
            }
            v["live"] = host_ ? host_->live_kind("model", true) : false;
            list.array.push_back(v);
        }
        r["catalogue"] = double(std::max(c.version, pieces.version));
        r["categories"] = categories;
        r["objects"] = list;
        ok = true;
        return r;
    }
    handled = false;
    (void)why;
    return r;
}

}  // namespace editor
