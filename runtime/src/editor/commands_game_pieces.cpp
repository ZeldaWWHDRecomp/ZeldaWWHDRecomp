// SPDX-License-Identifier: MPL-2.0
// World editor: island buildings, island parts and island grounds ("piece:<id>", story_game_pieces_catalogue.h) in
// the Game objects palette. list_game_objects lists them under their category ("Buildings", "Island parts",
// "Islands") with whether this mod's copy is prepared; an island ground is placed at the sea's level below Link; add_actor takes
// "piece:<id>" as its type and starts building the piece's set from the player's room archive when it is
// not ready yet ("preparing…"): the placement appears once the converter is done. No game data passes
// through here: the manifest holds the neutral id and the placement only.
#include <cmath>
#include <string>

#include "commands.h"

namespace editor {
namespace {
double tenth(double v) { return std::round(v * 10) / 10; }
Value point(double x, double y, double z) {
    Value v;
    v["x"] = tenth(x), v["y"] = tenth(y), v["z"] = tenth(z);
    return v;
}
}  // namespace

Value game_piece_info(const story_game_pieces::Piece& p) {
    Value v;
    v["type"] = "piece:" + p.id;
    v["id"] = p.id;
    v["label"] = p.label;
    v["category"] = p.category;
    v["description"] = p.description;
    v["since"] = double(p.since);
    v["collision"] = !p.collision_groups.empty();
    v["building"] = true;
    v["rotation"] = "yaw";
    Value scale;
    scale["min"] = 1.0, scale["max"] = 1.0;
    v["scale"] = scale;
    Value variants;
    variants.type = Value::Array;
    v["variants"] = variants;
    if (const auto* s = story_game_pieces::catalogue().set(p.set)) {
        v["set"] = s->label;
        v["memory_bytes"] = double(s->archive_bytes);  // the set's archive, once per room
    }
    v["instance_bytes"] = double(p.instance_bytes);
    if (p.distant_triangles) v["island"] = true;  // an island ground: distant view from the neighbouring squares
    return v;
}

bool new_game_piece(const std::string& type, const std::string& id, const Place& place, Value& a, std::string& why) {
    const auto* p = story_game_pieces::catalogue().find(story_game_pieces::reference(type));
    if (!p) return why = "\"" + type + "\" is not in the game pieces catalogue (list_game_objects, categories Buildings, Island parts and Islands)", false;
    if (!place.valid) return why = "no place to put it (Link is not in the scene)", false;
    a = Value();
    a["id"] = id;
    a["actor"] = "piece:" + p->id;
    a["stage"] = place.stage;
    a["room"] = place.room;
    // an island ground's origin is its sea level: on the sea it is placed at the sea's height under Link
    const bool island = p->distant_triangles && place.stage == "sea";
    a["position"] = point(place.position.x, island ? 0.0 : place.position.y, place.position.z);
    const double yaw = tenth(std::fmod(place.angle * 360.0 / 65536.0 + 360.0, 360.0));
    if (yaw != 0) a["rotation"] = point(0, yaw, 0);
    return true;
}

bool sea_level_piece(const Value& a) {
    const auto* p = story_game_pieces::catalogue().find(story_game_pieces::reference(a.get("actor").string()));
    return p && p->distant_triangles && a.get("stage").string() == "sea";
}

void require_game_piece(Value& manifest, const std::string& type) {
    const auto* p = story_game_pieces::catalogue().find(story_game_pieces::reference(type));
    if (!p) return;
    Value& req = manifest["requires"];
    const Value& had = req.get("game_objects");
    const unsigned have = had.type == Value::Number && had.number >= 1 ? unsigned(had.number) : 0;
    if (have < p->since) req["game_objects"] = double(p->since);
}

}  // namespace editor
