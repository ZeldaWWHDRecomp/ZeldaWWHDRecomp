// SPDX-License-Identifier: MPL-2.0
// Mod stages: the editor's Check of a package's stages (check_stage, docs/mod-stages.md "Check"), as structured
// problems: the manifest's rules (mod_stages_manifest.h parse), whether every room can be reached in play with
// the keys the stage gives before each locked door, and what each room's ground model lacks (texture
// coordinates, named materials). Pure code, unit-tested in runtime/tools/mod_stages_test.cpp; the files are read
// by the caller.
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "mod_stages_manifest.h"

namespace mod_stages {

struct Problem {
    std::string severity;  // "error" (the stage can't be built or played through) or "warning"
    std::string stage, field, message;  // field: where in the stage ("rooms/1/ground", "doors/0", ...)
};

// What a room's ground model (a .glb) has: its visible parts' texture coordinates and materials. False when the
// bytes are not a readable GLB.
struct GlbFacts {
    unsigned visible_primitives = 0, without_uvs = 0;
    std::set<std::string> materials;  // the visible parts' material names ("" a material without a name or none)
};
inline bool glb_facts(const std::vector<uint8_t>& b, GlbFacts& out) {
    out = GlbFacts{};
    auto u32 = [&](size_t at) { return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24; };
    if (b.size() < 20 || std::memcmp(b.data(), "glTF", 4) != 0 || u32(4) != 2) return false;
    const uint32_t length = u32(12);
    if (std::memcmp(b.data() + 16, "JSON", 4) != 0 || 20 + uint64_t(length) > b.size()) return false;
    Value doc;
    try {
        doc = mods::json::parse(std::string(reinterpret_cast<const char*>(b.data() + 20), length));
    } catch (...) {
        return false;
    }
    std::vector<std::string> material_names;
    for (const auto& m : doc.get("materials").array) material_names.push_back(m.get("name").string());
    const auto& meshes = doc.get("meshes").array;
    std::vector<bool> collision(meshes.size(), false);
    for (size_t i = 0; i < meshes.size(); ++i) {
        const std::string n = meshes[i].get("name").string();
        collision[i] = n.size() >= 4 && n.substr(n.size() - 4) == "_col";
    }
    std::vector<bool> used(meshes.size(), false), visible(meshes.size(), false);
    for (const auto& node : doc.get("nodes").array) {
        const Value& mesh = node.get("mesh");
        if (mesh.type != Value::Number || mesh.number < 0 || size_t(mesh.number) >= meshes.size()) continue;
        const size_t i = size_t(mesh.number);
        const std::string n = node.get("name").string();
        const bool col = collision[i] || (n.size() >= 4 && n.substr(n.size() - 4) == "_col");
        used[i] = true;
        if (!col) visible[i] = true;
    }
    for (size_t i = 0; i < meshes.size(); ++i) {
        if (!visible[i]) continue;
        for (const auto& p : meshes[i].get("primitives").array) {
            ++out.visible_primitives;
            if (p.get("attributes").get("TEXCOORD_0").type == Value::Null) ++out.without_uvs;
            const Value& m = p.get("material");
            out.materials.insert(m.type == Value::Number && m.number >= 0 && size_t(m.number) < material_names.size() ? material_names[size_t(m.number)] : "");
        }
    }
    return true;
}

// The problems of one room's ground: missing texture coordinates; one material for everything, or materials
// without names, which the port can't tell apart (it draws them all with one kit material).
inline void ground_problems(const Stage& s, const Room& r, unsigned room_index, const GlbFacts& f, std::vector<Problem>& out) {
    const std::string field = "rooms/" + std::to_string(room_index) + "/ground";
    if (!f.visible_primitives) {
        out.push_back({"error", s.id, field, "room " + std::to_string(r.room) + ": the ground model " + r.ground + " has nothing visible (only _col meshes)"});
        return;
    }
    if (f.without_uvs)
        out.push_back({"warning", s.id, field,
                       "room " + std::to_string(r.room) + ": " + std::to_string(f.without_uvs) + " of the ground model's " + std::to_string(f.visible_primitives) +
                           " parts have no texture coordinates (UVs): they show one colour of the texture. Unwrap them in Blender (UV > Smart UV Project, or Cube "
                           "Projection) before exporting."});
    const bool unnamed = f.materials.count("") > 0;
    if (unnamed)
        out.push_back({"warning", s.id, field,
                       "room " + std::to_string(r.room) + ": parts of the ground model have no named material: name its materials (floor, wall, ceiling, ...) "
                           "and map them to kit materials with \"materials\"."});
    else if (f.materials.size() == 1 && !r.materials.count(*f.materials.begin()))
        out.push_back({"warning", s.id, field,
                       "room " + std::to_string(r.room) + ": the whole ground model uses one material (" + *f.materials.begin() +
                           "): floors, walls and ceilings all look the same. Give them their own named materials in Blender and map each with \"materials\"."});
}

// Whether every room can be reached in play: from where Link comes in, through open doors, joins and exits in
// the stage, opening a small-key door only with a small key found in a room already reached (each opens one),
// the boss door with the boss key, a shutter counted as open (its switch is the package's). The rooms that
// can't be reached that way are problems, with the door that keeps them shut.
inline void key_order_problems(const Stage& s, std::vector<Problem>& out) {
    const size_t n = s.rooms.size();
    std::vector<bool> reached(n, false), open(s.doors.size(), false);
    if (s.entries.empty()) reached[0] = true;
    for (const auto& e : s.entries)
        if (e.to_room < n) reached[e.to_room] = true;
    unsigned keys_used = 0;
    for (bool changed = true; changed;) {
        changed = false;
        auto reach = [&](unsigned room) {
            if (room < n && !reached[room]) reached[room] = changed = true;
        };
        for (size_t d = 0; d < s.doors.size(); ++d) {
            const Door& door = s.doors[d];
            if ((door.lock == "none" || door.lock == "shutter" || open[d]) && (reached[door.a] || reached[door.b])) {
                reach(door.a);
                reach(door.b);
            }
        }
        for (const auto& [a, b] : s.joins)
            if (reached[a] || reached[b]) reach(a), reach(b);
        for (unsigned room = 0; room < n; ++room)
            if (reached[room])
                for (const auto& e : s.rooms[room].exits)
                    if (e.to.own && e.to.stage.empty()) reach(e.to.room);
        if (changed) continue;
        // with nothing more to walk to: the keys found so far open the next locked door
        unsigned keys = 0;
        bool boss_key = false;
        for (unsigned room = 0; room < n; ++room)
            if (reached[room])
                for (const auto& a : s.rooms[room].actors) {
                    keys += a.actor == "chest" && a.item == "small_key";
                    boss_key |= a.actor == "chest" && a.item == "boss_key";
                }
        for (size_t d = 0; d < s.doors.size() && !changed; ++d) {
            const Door& door = s.doors[d];
            if (open[d] || !(reached[door.a] || reached[door.b]) || (reached[door.a] && reached[door.b])) continue;
            if ((door.lock == "small_key" && keys > keys_used) || (door.lock == "boss" && boss_key)) {
                keys_used += door.lock == "small_key";
                open[d] = changed = true;
            }
        }
    }
    for (unsigned room = 0; room < n; ++room) {
        if (reached[room]) continue;
        std::string behind;
        for (size_t d = 0; d < s.doors.size(); ++d)
            if ((s.doors[d].a == room || s.doors[d].b == room) && s.doors[d].lock != "none" && s.doors[d].lock != "shutter")
                behind = "doors/" + std::to_string(d);
        out.push_back({"error", s.id, behind.empty() ? "rooms/" + std::to_string(room) : behind,
                       "room " + std::to_string(room) + " can't be reached in play: " +
                           (behind.empty() ? std::string("no door, join or exit leads to it")
                                           : "it is behind a locked door, and the stage gives no key for it before that door (a small key in a chest of a "
                                             "room Link can reach first, or the boss key for the boss door)")});
    }
}

// Every problem of the package's stages. `ground`: a room's ground file's bytes (empty: missing).
inline std::vector<Problem> check(const Value& manifest, const std::function<std::vector<uint8_t>(const std::string& path)>& ground,
                                  const std::string& only = {}) {
    std::vector<Problem> out;
    std::vector<Stage> stages;
    std::string why;
    if (!parse(manifest, stages, why)) {
        // the manifest's own rule: its message names the stage, room and field
        out.push_back({"error", "", "", why});
        return out;
    }
    for (const auto& s : stages) {
        if (!only.empty() && s.id != only) continue;
        key_order_problems(s, out);
        for (size_t i = 0; i < s.rooms.size(); ++i) {
            const Room& r = s.rooms[i];
            if (r.kit_ground) continue;
            const auto bytes = ground(r.ground);
            if (bytes.empty()) {
                out.push_back({"error", s.id, "rooms/" + std::to_string(i) + "/ground", "room " + std::to_string(r.room) + ": the ground model " + r.ground + " is missing"});
                continue;
            }
            GlbFacts facts;
            if (!glb_facts(bytes, facts)) {
                out.push_back({"error", s.id, "rooms/" + std::to_string(i) + "/ground", "room " + std::to_string(r.room) + ": " + r.ground + " is not a GLB (glTF 2.0) file"});
                continue;
            }
            ground_problems(s, r, unsigned(i), facts, out);
        }
    }
    return out;
}

}  // namespace mod_stages
