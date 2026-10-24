// Game objects: the game's own static objects a mod places by reference ("actor": "game:palm").
// The port keeps only a recipe per group (runtime/assets/game_objects/<group>.recipe.json, compiled in by
// cmake/EmbedGameObjectRecipes.cmake): a neutral id, our own label, description and category, and
// references into the game (the stage actor name the game's object name table knows, its process, the
// archive it loads, the fixed parameter word of each variant, and what the placement may change). No
// game data is in the repository, in a package or in an export: the objects load from the player's own
// game files when the game creates them (tools/story_mods/game_objects.py --list shows which the
// player's game has).
//
// A manifest names an entry by its neutral id; native names never appear in manifests. Only catalogued
// ids are accepted. A game object takes no params, flags, then, paths or dialogue: its parameter word
// is the recipe's (switch, save bit and event fields "none"), and its rotation and scale stay inside
// what the recipe allows (default: a turn about the vertical axis only, scale 1). A manifest that uses
// game objects says which catalogue version it needs ("requires": {"game_objects": 1}); a newer one
// than this port's, or an id added after the version it names, is refused.
// Pure code: unit-tested in runtime/tools/story_game_objects_test.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mod_json.h"

namespace story_game_objects {

inline constexpr unsigned kFormat = 1;
// what the game objects of one room may cost in all (every mod's, game heap bytes: each instance, and
// each archive once while the room uses it)
inline constexpr uint64_t kRoomBudgetBytes = 3u * 1024 * 1024;

struct Variant {
    std::string id, label;
    uint32_t params = 0;  // the fixed parameter word written at creation and read back
    unsigned units = 0;   // a shared batch: how many units of the stage's batch it takes
    int angle_z = -1;     // a fixed home angle z word written at creation (a block's "no path": 0xFFFF); -1: none
};
// Grass, flowers and small trees are shared batches ("packets"), not actors: the game's placement actor
// adds units to the stage's batch and is gone at once; the units go when their room unloads. A batch has
// a fixed capacity per stage, shared with the game's own placements; mods together may use `mod_share`
// units of one kind per stage (the validator), and the game checks the free units before planting.
struct Packet {
    std::string kind;        // "grass", "flower", "tree", "wood" ("" an ordinary actor)
    unsigned capacity = 0;   // the stage's batch capacity (the game's own use included)
    unsigned mod_share = 0;  // what all mods together may plant per stage
};
inline bool packet_kind(const std::string& k) { return k == "grass" || k == "flower" || k == "tree" || k == "wood"; }
struct Entry {
    std::string id, group, category, label, description;
    unsigned since = 1;
    // references into the game
    std::string native_name;  // the stage actor name in the game's object name table ("Oyashi")
    std::string process;      // the process it is (documentation; the game's table resolves the name)
    std::string archive;      // the object archive the game loads for it
    std::vector<std::string> also_archives;  // more archives it loads (a flag's shared cloth)
    int subtype = 255;        // the object name table's argument for this name (checked at creation)
    std::vector<Variant> variants;
    bool yaw_only = true;     // rotation: a turn about the vertical axis only
    float scale_min = 1, scale_max = 1;  // uniform scale
    bool collision = false;
    bool own_heap = true;  // false: the object makes no heap of its own (particles only, a steam vent)
    uint64_t instance_bytes = 0, archive_bytes = 0;
    Packet packet;
    bool batch() const { return !packet.kind.empty(); }
    const Variant* variant(const std::string& name) const {
        if (name.empty()) return variants.empty() ? nullptr : &variants.front();
        for (const Variant& v : variants)
            if (v.id == name) return &v;
        return nullptr;
    }
};

inline bool neutral_id(const std::string& s) {
    if (s.empty() || s.size() > 32 || s.front() == '-' || s.back() == '-') return false;
    return s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos;
}
// a stage actor name: up to 7 characters (the game's names are 8 bytes with the terminator)
// an object archive's name: the game's are up to 8 characters ("Yfire_00"); 15 with the terminator in 16
inline bool archive_name_ok(const std::string& s) {
    if (s.empty() || s.size() > 15) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
inline bool native_name_ok(const std::string& s) {
    if (s.empty() || s.size() > 7) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

struct Catalogue {
    unsigned version = 0;  // the highest "since" of its entries
    std::vector<Entry> entries;
    const Entry* find(const std::string& id) const {
        for (const Entry& e : entries)
            if (e.id == id) return &e;
        return nullptr;
    }
    // adds one group's recipe, all or nothing; false with a reason for anything it doesn't accept
    bool add_table(const std::string& text, std::string& why);
};

inline bool Catalogue::add_table(const std::string& table, std::string& why) {
    using mods::json::Value;
    Value t;
    try {
        t = mods::json::parse(table);
    } catch (const std::exception& e) {
        return why = std::string("not JSON: ") + e.what(), false;
    }
    if (t.get("game_objects_format").type != Value::Number || t.get("game_objects_format").number != kFormat)
        return why = "unsupported game_objects_format", false;
    const std::string group = t.get("group").string();
    if (!neutral_id(group)) return why = "invalid group", false;
    if (t.get("objects").type != Value::Array || t.get("objects").array.empty()) return why = group + ": no objects", false;
    std::vector<Entry> added;
    unsigned newest = version;
    auto text = [](const Value& v) { return v.type == Value::String ? v.text : std::string(); };
    auto whole = [](const Value& v, double lo, double hi) {
        return v.type == Value::Number && std::isfinite(v.number) && v.number >= lo && v.number <= hi && std::floor(v.number) == v.number;
    };
    for (const Value& o : t.get("objects").array) {
        Entry e;
        e.group = group;
        e.id = text(o.get("id"));
        if (!neutral_id(e.id)) return why = group + ": the id \"" + e.id + "\" is not a neutral id", false;
        bool taken = find(e.id) != nullptr;
        for (const Entry& a : added) taken |= a.id == e.id;
        if (taken) return why = "duplicate game object id " + e.id, false;
        if (!whole(o.get("since"), 1, 1000)) return why = e.id + ": invalid since", false;
        e.since = unsigned(o.get("since").number);
        e.category = text(o.get("category"));
        e.label = text(o.get("label"));
        e.description = text(o.get("description"));
        if (e.category.empty() || e.category.size() > 32) return why = e.id + ": invalid category", false;
        if (e.label.empty() || e.label.size() > 48) return why = e.id + ": invalid label", false;
        if (e.description.empty() || e.description.size() > 160) return why = e.id + ": invalid description", false;
        const Value& n = o.get("native");
        e.native_name = text(n.get("name"));
        e.process = text(n.get("process"));
        e.archive = text(n.get("archive"));
        if (!native_name_ok(e.native_name)) return why = e.id + ": invalid native name", false;
        if (!archive_name_ok(e.archive)) return why = e.id + ": invalid archive", false;
        if (n.get("also_archives").type != Value::Null) {
            if (n.get("also_archives").type != Value::Array || n.get("also_archives").array.size() > 4) return why = e.id + ": also_archives must be a list of up to 4", false;
            for (const Value& a : n.get("also_archives").array) {
                if (a.type != Value::String || !archive_name_ok(a.text)) return why = e.id + ": invalid archive in also_archives", false;
                e.also_archives.push_back(a.text);
            }
        }
        if (e.process.empty() || e.process.size() > 48) return why = e.id + ": invalid process", false;
        if (!whole(n.get("subtype"), 0, 255)) return why = e.id + ": invalid subtype", false;
        e.subtype = int(n.get("subtype").number);
        if (o.get("variants").type != Value::Array || o.get("variants").array.empty() || o.get("variants").array.size() > 8)
            return why = e.id + ": 1 to 8 variants", false;
        for (const Value& v : o.get("variants").array) {
            Variant x;
            x.id = text(v.get("id"));
            x.label = text(v.get("label"));
            const std::string params = text(v.get("params"));
            char* end = nullptr;
            const unsigned long long word = params.size() == 10 && params.rfind("0x", 0) == 0 ? strtoull(params.c_str() + 2, &end, 16) : ~0ull;
            if (!neutral_id(x.id) || x.label.empty() || x.label.size() > 48) return why = e.id + ": invalid variant", false;
            if (word > 0xFFFFFFFFull || !end || *end) return why = e.id + ": a variant's params must be 0x and 8 hex digits", false;
            x.params = uint32_t(word);
            if (v.get("angle_z").type != Value::Null) {
                const std::string az = text(v.get("angle_z"));
                char* az_end = nullptr;
                const unsigned long long z = az.size() == 6 && az.rfind("0x", 0) == 0 ? strtoull(az.c_str() + 2, &az_end, 16) : ~0ull;
                if (z > 0xFFFFull || !az_end || *az_end) return why = e.id + ": a variant's angle_z must be 0x and 4 hex digits", false;
                x.angle_z = int(z);
            }
            if (v.get("units").type != Value::Null) {
                if (!whole(v.get("units"), 1, 64)) return why = e.id + ": a variant's units must be 1..64", false;
                x.units = unsigned(v.get("units").number);
            }
            for (const Variant& old : e.variants)
                if (old.id == x.id) return why = e.id + ": duplicate variant " + x.id, false;
            e.variants.push_back(std::move(x));
        }
        const Value& tr = o.get("transform");
        const std::string rotation = text(tr.get("rotation"));
        if (rotation != "yaw") return why = e.id + ": rotation must be \"yaw\"", false;  // the only kind so far
        e.yaw_only = true;
        const Value& scale = tr.get("scale");
        if (scale.type != Value::Array || scale.array.size() != 2 || scale.array[0].type != Value::Number || scale.array[1].type != Value::Number)
            return why = e.id + ": scale must be [min, max]", false;
        e.scale_min = float(scale.array[0].number);
        e.scale_max = float(scale.array[1].number);
        if (!(e.scale_min >= 0.5f && e.scale_max <= 2.0f && e.scale_min <= 1 && e.scale_max >= 1 && e.scale_min <= e.scale_max))
            return why = e.id + ": scale limits must lie within 0.5..2 and include 1", false;
        if (o.get("collision").type != Value::Bool) return why = e.id + ": collision must be true or false", false;
        e.collision = o.get("collision").boolean;
        if (o.get("own_heap").type != Value::Null) {
            if (o.get("own_heap").type != Value::Bool) return why = e.id + ": own_heap must be true or false", false;
            e.own_heap = o.get("own_heap").boolean;
        }
        const Value& mem = o.get("memory");
        if (!whole(mem.get("instance_bytes"), 1, 4 << 20) || !whole(mem.get("archive_bytes"), 0, 8 << 20))
            return why = e.id + ": memory needs instance_bytes and archive_bytes", false;
        e.instance_bytes = uint64_t(mem.get("instance_bytes").number);
        e.archive_bytes = uint64_t(mem.get("archive_bytes").number);
        const Value& pk = o.get("packet");
        if (pk.type != Value::Null) {
            e.packet.kind = text(pk.get("kind"));
            if (!packet_kind(e.packet.kind)) return why = e.id + ": packet kind must be grass, flower, tree or wood", false;
            if (!whole(pk.get("capacity"), 1, 5000) || !whole(pk.get("mod_share"), 1, 5000) || pk.get("mod_share").number > pk.get("capacity").number)
                return why = e.id + ": a packet needs its capacity and a mod_share within it", false;
            e.packet.capacity = unsigned(pk.get("capacity").number);
            e.packet.mod_share = unsigned(pk.get("mod_share").number);
            if (e.scale_min != 1 || e.scale_max != 1) return why = e.id + ": a packet keeps scale 1", false;
        }
        for (const Variant& x : e.variants)
            if (e.batch() != (x.units > 0)) return why = e.id + ": every variant of a packet names its units (and only a packet's)", false;
        newest = std::max(newest, e.since);
        added.push_back(std::move(e));
    }
    for (Entry& e : added) entries.push_back(std::move(e));
    version = newest;
    return true;
}

// ---- the port's recipes (generated at configure time; see cmake/EmbedGameObjectRecipes.cmake)
}  // namespace story_game_objects
#include "game_object_recipes.inc"
namespace story_game_objects {
// whether the port's catalogue was built (the inert-start check: a start without mods and without --editor never does)
inline bool& built_flag() {
    static bool built = false;
    return built;
}
inline bool built() { return built_flag(); }
// The port's catalogue, built once from the compiled-in recipes, on first use (never at start-up: a
// game without mods and without --editor doesn't build it).
inline const Catalogue& catalogue() {
    static const Catalogue c = [] {
        built_flag() = true;
        Catalogue built;
        for (const char* recipe : kRecipes) {
            std::string why;
            if (!*recipe) continue;
            if (!built.add_table(recipe, why)) fprintf(stderr, "[game_objects] recipe not used: %s\n", why.c_str());
        }
        return built;
    }();
    return c;
}

// "game:palm" -> "palm"; empty when it is not a game object reference
inline std::string reference(const std::string& actor) { return actor.rfind("game:", 0) == 0 ? actor.substr(5) : std::string(); }
inline bool is_reference(const std::string& actor) { return actor.rfind("game:", 0) == 0; }

// What a game object placement costs in its room: each instance, and each archive once.
struct RoomUse {
    uint64_t bytes = 0;
    unsigned count = 0;
    std::set<std::string> archives;
    void add(const Entry& e) {
        bytes += e.instance_bytes;
        ++count;
        if (archives.insert(e.archive).second) bytes += e.archive_bytes;
    }
};

// The manifest's catalogue version gate: 0 when it names none. False with a reason when it is not a
// version number or newer than `c`.
inline bool required_version(const mods::json::Value& manifest, unsigned have, unsigned& need, std::string& why);
inline bool required_version(const mods::json::Value& manifest, const Catalogue& c, unsigned& need, std::string& why) {
    return required_version(manifest, c.version, need, why);
}
// (`have`: the port's catalogue version; game pieces share it, story_game_pieces_catalogue.h)
inline bool required_version(const mods::json::Value& manifest, unsigned have, unsigned& need, std::string& why) {
    using mods::json::Value;
    need = 0;
    const Value& req = manifest.get("requires");
    if (req.type == Value::Null) return true;
    if (req.type != Value::Object) return why = "requires must be an object (\"requires\": {\"game_objects\": 1})", false;
    for (const auto& [key, value] : req.object)
        if (key != "game_objects") return why = "unknown requirement \"" + key + "\"", false;
    const Value& g = req.get("game_objects");
    if (g.type == Value::Null) return true;
    if (g.type != Value::Number || !std::isfinite(g.number) || g.number < 1 || g.number > 1000 || std::floor(g.number) != g.number)
        return why = "requires.game_objects must be a version number", false;
    need = unsigned(g.number);
    if (need > have)
        return why = "needs game objects catalogue version " + std::to_string(need) + "; this port has version " + std::to_string(have) +
                     " (update the port)",
               false;
    return true;
}

}  // namespace story_game_objects
