// SPDX-License-Identifier: MPL-2.0
// Game pieces: buildings of the game's islands that a mod places by reference ("actor": "piece:<id>").
// A piece is a part of an island's background model that is already separate there (whole connected
// meshes, never a cut), with its collision. The port keeps only a recipe per set of pieces that share
// their textures (runtime/assets/game_pieces/<set>.recipe.json, compiled in by
// cmake/EmbedGamePieceRecipes.cmake): neutral ids, our own label, description and category, and
// references into the game (the room archive, its background models and nodes, a room-local volume,
// the light set each part is drawn with, the collision groups it keeps, the triangle counts it must
// have). No game data is in the repository, in a package or in an export: the pieces are built from
// the player's own room archive into the player's model cache (tools/story_mods/game_piece_model.py).
//
// A manifest names a piece by its neutral id and gives only its id, stage, room, position and a turn
// about the vertical axis (scale 1: the baked light and the collision are the island's own). Pieces
// share the game objects catalogue version ("requires": {"game_objects": N}). What the pieces of one
// room cost (each set's archive once, each placement's instance) stays within kRoomBudgetBytes.
// Pure code: unit-tested in runtime/tools/story_game_pieces_test.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mod_json.h"
#include "story_game_pieces_budget.h"

namespace story_game_pieces {

inline constexpr unsigned kFormat = 1;
// kRoomBudgetBytes (story_game_pieces_budget.h): what the game pieces of one room may cost in all
inline constexpr unsigned kMaxPiecesPerSet = 16;

// The light set a part is drawn with: the island's background light of BG model 0..3 (the game's
// TEV_TYPE_BG0..BG3, settingTevStruct types 1..4).
inline int light_type(const std::string& s) {
    if (s.size() == 3 && s[0] == 'b' && s[1] == 'g' && s[2] >= '0' && s[2] <= '3') return 1 + (s[2] - '0');
    return 0;
}

struct Part {
    unsigned model = 0;              // the room's BG model (0 "model", 1..3 "model1".."model3")
    std::vector<std::string> nodes;  // its nodes whose whole components inside the volume are taken
    int light = 1;                   // settingTevStruct type (1..4)
    unsigned triangles = 0;          // what the player's game must give (another version is refused)
    bool animated = false;           // a second part keeps the room's texture animation (an island's moving water)
};
struct Piece {
    std::string id, set, category, label, description;
    unsigned since = 1;
    float volume[6] = {};  // room-local, min xyz then max xyz
    std::vector<Part> parts;
    std::vector<std::string> collision_groups;
    unsigned collision_triangles = 0;
    uint64_t instance_bytes = 0;
    // an island ground's distant model (the room's own sea_LODNN.szs, drawn in the neighbouring squares):
    // its triangle count, 0 when the piece has none
    unsigned distant_triangles = 0;
    std::string distant_archive;
};
struct Set {
    std::string id, label, archive;  // archive: the room archive, "sea_RoomNN.szs"
    unsigned room = 0;
    uint64_t archive_bytes = 0;  // the built set archive in game memory (measured; the budget's estimate)
    std::string recipe;          // the recipe text (the converter's input)
    std::vector<std::string> pieces;
};

inline bool neutral_id(const std::string& s) {
    if (s.empty() || s.size() > 32 || s.front() == '-' || s.back() == '-' || s.find("--") != std::string::npos) return false;
    return s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos;
}
inline bool node_name_ok(const std::string& s) {
    if (s.empty() || s.size() > 31) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

struct Catalogue {
    unsigned version = 0;  // the highest "since" of its pieces
    std::vector<Piece> pieces;
    std::vector<Set> sets;
    const Piece* find(const std::string& id) const {
        for (const Piece& p : pieces)
            if (p.id == id) return &p;
        return nullptr;
    }
    const Set* set(const std::string& id) const {
        for (const Set& s : sets)
            if (s.id == id) return &s;
        return nullptr;
    }
    // adds one set's recipe, all or nothing; false with a reason for anything it doesn't accept
    bool add_set(const std::string& text, std::string& why);
};

inline bool Catalogue::add_set(const std::string& recipe, std::string& why) {
    using mods::json::Value;
    Value r;
    try {
        r = mods::json::parse(recipe);
    } catch (const std::exception& e) {
        return why = std::string("not JSON: ") + e.what(), false;
    }
    auto text = [](const Value& v) { return v.type == Value::String ? v.text : std::string(); };
    auto whole = [](const Value& v, double lo, double hi) {
        return v.type == Value::Number && std::isfinite(v.number) && v.number >= lo && v.number <= hi && std::floor(v.number) == v.number;
    };
    if (!whole(r.get("game_pieces_format"), kFormat, kFormat)) return why = "unsupported game_pieces_format", false;
    Set s;
    s.id = text(r.get("set"));
    if (!neutral_id(s.id) || set(s.id)) return why = "invalid or duplicate set \"" + s.id + "\"", false;
    s.label = text(r.get("label"));
    if (s.label.empty() || s.label.size() > 48) return why = s.id + ": invalid label", false;
    const Value& src = r.get("source");
    s.archive = text(src.get("archive"));
    const bool archive_ok = s.archive.size() >= 13 && s.archive.size() <= 14 && s.archive.rfind("sea_Room", 0) == 0 &&
                            s.archive.compare(s.archive.size() - 4, 4, ".szs") == 0 &&
                            s.archive.find_first_not_of("0123456789", 8) == s.archive.size() - 4;
    if (!archive_ok || !whole(src.get("room"), 1, 49)) return why = s.id + ": invalid source", false;
    s.room = unsigned(src.get("room").number);
    if (s.archive != "sea_Room" + std::to_string(s.room) + ".szs") return why = s.id + ": the source archive is not the room's", false;
    if (!whole(r.get("memory").get("archive_bytes"), 1, kRoomBudgetBytes)) return why = s.id + ": memory.archive_bytes (the set's measured archive) is needed", false;
    s.archive_bytes = uint64_t(r.get("memory").get("archive_bytes").number);
    s.recipe = recipe;
    const Value& list = r.get("pieces");
    if (list.type != Value::Array || list.array.empty() || list.array.size() > kMaxPiecesPerSet) return why = s.id + ": 1 to 16 pieces", false;
    std::vector<Piece> added;
    unsigned newest = version;
    for (const Value& o : list.array) {
        Piece p;
        p.set = s.id;
        p.id = text(o.get("id"));
        bool taken = !neutral_id(p.id) || find(p.id);
        for (const Piece& a : added) taken |= a.id == p.id;
        if (taken) return why = s.id + ": invalid or duplicate piece id \"" + p.id + "\"", false;
        if (!whole(o.get("since"), 1, 1000)) return why = p.id + ": invalid since", false;
        p.since = unsigned(o.get("since").number);
        p.category = text(o.get("category"));
        p.label = text(o.get("label"));
        p.description = text(o.get("description"));
        if (p.category.empty() || p.category.size() > 32) return why = p.id + ": invalid category", false;
        if (p.label.empty() || p.label.size() > 48) return why = p.id + ": invalid label", false;
        if (p.description.empty() || p.description.size() > 160) return why = p.id + ": invalid description", false;
        const Value& v = o.get("volume");
        if (v.type != Value::Array || v.array.size() != 6) return why = p.id + ": invalid volume", false;
        for (unsigned k = 0; k < 6; ++k) {
            if (v.array[k].type != Value::Number || !std::isfinite(v.array[k].number) || std::fabs(v.array[k].number) > 60000)
                return why = p.id + ": invalid volume", false;
            p.volume[k] = float(v.array[k].number);
        }
        for (unsigned k = 0; k < 3; ++k)
            if (!(p.volume[k] < p.volume[k + 3]) || p.volume[k + 3] - p.volume[k] > 20000) return why = p.id + ": invalid volume", false;
        const Value& parts = o.get("parts");
        const Value& expect = o.get("expect");
        const Value& tris = expect.get("triangles");
        if (parts.type != Value::Array || parts.array.empty() || parts.array.size() > 2) return why = p.id + ": 1 or 2 parts", false;
        if (tris.type != Value::Array || tris.array.size() != parts.array.size()) return why = p.id + ": expect.triangles gives each part's count", false;
        for (size_t i = 0; i < parts.array.size(); ++i) {
            const Value& x = parts.array[i];
            Part part;
            if (!whole(x.get("model"), 0, 3)) return why = p.id + ": invalid BG model", false;
            part.model = unsigned(x.get("model").number);
            part.light = light_type(text(x.get("light")));
            if (!part.light) return why = p.id + ": light must be bg0, bg1, bg2 or bg3", false;
            const Value& animated = x.get("animated");
            if (animated.type != Value::Null && (animated.type != Value::Bool || (animated.boolean && i != 1)))
                return why = p.id + ": only a piece's second part may be animated", false;
            part.animated = animated.boolean;
            if (x.get("nodes").type != Value::Array || x.get("nodes").array.empty() || x.get("nodes").array.size() > 32)
                return why = p.id + ": invalid nodes", false;
            for (const Value& n : x.get("nodes").array) {
                if (!node_name_ok(text(n))) return why = p.id + ": invalid node name", false;
                part.nodes.push_back(n.text);
            }
            if (!whole(tris.array[i], 1, 8192)) return why = p.id + ": invalid expected triangles", false;
            part.triangles = unsigned(tris.array[i].number);
            p.parts.push_back(std::move(part));
        }
        const Value& groups = o.get("collision").get("groups");
        if (groups.type != Value::Array || groups.array.size() > 64) return why = p.id + ": collision.groups must be a list", false;
        for (const Value& g : groups.array) {
            if (!node_name_ok(text(g))) return why = p.id + ": invalid collision group", false;
            p.collision_groups.push_back(g.text);
        }
        if (!whole(expect.get("collision_triangles"), 0, 4096)) return why = p.id + ": invalid expect.collision_triangles", false;
        p.collision_triangles = unsigned(expect.get("collision_triangles").number);
        if (p.collision_groups.empty() != (p.collision_triangles == 0)) return why = p.id + ": collision groups and their count go together", false;
        if (!whole(o.get("memory").get("instance_bytes"), 1, 1 << 20)) return why = p.id + ": memory.instance_bytes is needed", false;
        const Value& distant = o.get("distant");
        if (distant.type != Value::Null) {
            char lod[16];
            std::snprintf(lod, sizeof lod, "sea_LOD%02u.szs", s.room);
            if (text(distant.get("archive")) != lod || !whole(distant.get("triangles"), 1, 2048))
                return why = p.id + ": distant names the room's own distant-view archive and its 1 to 2048 triangles", false;
            p.distant_archive = lod;
            p.distant_triangles = unsigned(distant.get("triangles").number);
        }
        p.instance_bytes = uint64_t(o.get("memory").get("instance_bytes").number);
        newest = std::max(newest, p.since);
        s.pieces.push_back(p.id);
        added.push_back(std::move(p));
    }
    for (Piece& p : added) pieces.push_back(std::move(p));
    sets.push_back(std::move(s));
    version = newest;
    return true;
}

}  // namespace story_game_pieces
#include "game_piece_recipes.inc"
namespace story_game_pieces {
inline bool& built_flag() {
    static bool built = false;
    return built;
}
// whether the catalogue was built (the inert-start check: a start without mods and without --editor never does)
inline bool built() { return built_flag(); }
// The port's catalogue, built once from the compiled-in recipes on first use.
inline const Catalogue& catalogue() {
    static const Catalogue c = [] {
        built_flag() = true;
        Catalogue built;
        for (const char* recipe : kRecipes) {
            std::string why;
            if (!*recipe) continue;
            if (!built.add_set(recipe, why)) fprintf(stderr, "[game_pieces] recipe not used: %s\n", why.c_str());
        }
        return built;
    }();
    return c;
}

// "piece:outset-house-small" -> "outset-house-small"; empty when it is not a piece reference
inline std::string reference(const std::string& actor) { return actor.rfind("piece:", 0) == 0 ? actor.substr(6) : std::string(); }
inline bool is_reference(const std::string& actor) { return actor.rfind("piece:", 0) == 0; }
// the manifest model and collision key a placement uses inside the runtime (not a manifest model id)
inline std::string model_key(const std::string& id) { return "piece:" + id; }
// the key of a piece's distant model (an island ground's view from the neighbouring squares)
inline std::string distant_key(const std::string& id) { return "piece:" + id + ".distant"; }

// What the pieces of one room cost: each placement's instance, and each set's archive once.
struct RoomUse {
    uint64_t bytes = 0;
    unsigned count = 0;
    std::set<std::string> sets;
    void add(const Catalogue& c, const Piece& p) {
        bytes += p.instance_bytes;
        ++count;
        if (sets.insert(p.set).second)
            if (const Set* s = c.set(p.set)) bytes += s->archive_bytes;
    }
};

}  // namespace story_game_pieces
