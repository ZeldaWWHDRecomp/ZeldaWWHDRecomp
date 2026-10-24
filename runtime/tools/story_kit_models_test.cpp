// SPDX-License-Identifier: MPL-2.0
// Kit models (story_kit_models.h): the converter's recipe, the resource identity and the metadata a built
// model is read from (its parts, lights, animation, bounds and collision).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>

#include "mods/mod_hash.h"
#include "mods/mod_json.h"
#include "mods/story_kit_models.h"

using mods::json::Value;
namespace fs = std::filesystem;
using namespace story_world_collision::mesh;

static void be32(std::string& out, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(char((v >> s) & 0xFF));
}
static void be16(std::string& out, uint16_t v) {
    out.push_back(char(v >> 8));
    out.push_back(char(v & 0xFF));
}
static void bef(std::string& out, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    be32(out, v);
}
static std::string wcol(const Mesh& m) {
    std::string out = "WCOL";
    be32(out, 1);
    be32(out, uint32_t(m.vertices.size()));
    be32(out, uint32_t(m.triangles.size()));
    for (float f : {m.minimum.x, m.minimum.y, m.minimum.z, m.maximum.x, m.maximum.y, m.maximum.z}) bef(out, f);
    for (const auto& p : m.vertices)
        for (float f : {p.x, p.y, p.z}) bef(out, f);
    for (const auto& t : m.triangles) {
        for (auto i : t.indices) be16(out, i);
        be16(out, t.semantic);
    }
    return out;
}

int main() {
    std::string why;
    story_notebook::ManifestModel model;
    model.id = "island";
    model.kind = "kit";
    model.source = "assets/island.glb";
    model.kit_stage = "sea";
    model.kit_room = 41;
    model.kit_materials = {{"grass", "m_kusa_v"}, {"spray", "SC_00_m_2_"}};
    const std::string text = story_kit_models::recipe(model, "/packages/my-mod/assets/island.glb");
    const Value r = mods::json::parse(text);
    assert(r.get("kit_model_format").number == 1 && r.get("id").string() == "island" && r.get("glb").string() == "/packages/my-mod/assets/island.glb");
    assert(r.get("kit").get("stage").string() == "sea" && r.get("kit").get("room").number == 41 && r.get("material").type == Value::Null);
    assert(r.get("materials").get("spray").string() == "SC_00_m_2_" && r.get("materials").object.size() == 2);
    model.kit_material = "m_iwa_v";
    assert(mods::json::parse(story_kit_models::recipe(model, "/g.glb")).get("material").string() == "m_iwa_v");
    // the identity: 31 characters, "gk", stable, and another for another mod, model or recipe
    const std::string id = story_kit_models::identity("my-mod", "island", text);
    assert(id.size() == 31 && id.rfind("gk", 0) == 0 && id == story_kit_models::identity("my-mod", "island", text));
    assert(id != story_kit_models::identity("other", "island", text) && id != story_kit_models::identity("my-mod", "ruins", text) &&
           id != story_kit_models::identity("my-mod", "island", text + " "));
    assert(std::filesystem::path(story_kit_models::cache_directory("/m/Data/my-mod")) == std::filesystem::path("/m/Data/.kit-models") && story_kit_models::cache_directory("").empty());
    // a distant model's recipe says so (its game textures are drawn smaller)
    assert(mods::json::parse(story_kit_models::recipe(model, "/g.glb", true)).get("far").boolean &&
           mods::json::parse(story_kit_models::recipe(model, "/g.glb")).get("far").type == Value::Null);

    // the metadata of a built model with two parts, the second animated
    const fs::path dir = fs::temp_directory_path() / ("kit-models-test-" + std::to_string(std::hash<std::string>{}(id) & 0xFFFFFF));
    fs::create_directories(dir);
    Mesh box;
    assert(make_shape("box", {400, 100, 400}, 0, 0, 1, box, why));
    const std::string body = wcol(box);
    const std::string archive_path = (dir / (id + ".szs")).string(), col_name = id + ".szs.island.wcol";
    std::ofstream(dir / col_name, std::ios::binary) << body;
    auto meta = mods::json::parse(R"({"identity":"","set":"kit:island","models":["gk0001a","gk0001b"],"animations":["gk0001b"],"archive_bytes":4481024,
        "texture_bytes":844800,"pieces":[{"id":"island","origin":[0,0,0],"minimum":[-2868,-2869,-2620],"maximum":[3209,1108,2955],
        "parts":[{"model":"gk0001a","light":1,"triangles":12639},{"model":"gk0001b","light":2,"triangles":2184,"animation":"gk0001b"}],
        "collision":{"collision_sha256":""},"collision_file":""}]})");
    meta["identity"] = id;
    meta["pieces"].array[0]["collision"]["collision_sha256"] = mods::hash::sha256_text(body);
    meta["pieces"].array[0]["collision_file"] = col_name;
    story_game_pieces::BuiltSet set;
    assert(story_kit_models::parse_built(meta, id, "island", archive_path, set, why));
    assert(set.archive == id && set.models.size() == 2 && set.animations == std::vector<std::string>{"gk0001b"} && set.pieces.size() == 1);
    const auto& b = set.pieces[0];
    assert(b.body_index == 1 && b.second_index == 2 && b.animation_index == 3 && b.light[0] == 1 && b.light[1] == 2 && b.triangles == 14823);
    assert(b.collision && b.collision->triangles.size() == box.triangles.size() && b.maximum.y == 1108);
    // refused: another model, a third part, an animated first part, a light out of range, too many triangles,
    // a changed collision file, a collision file outside the cache folder
    auto bad = meta;
    assert(!story_kit_models::parse_built(bad, id, "ruins", archive_path, set, why));
    bad = meta;
    bad["models"].array.push_back("gk0001c");
    assert(!story_kit_models::parse_built(bad, id, "island", archive_path, set, why));
    bad = meta;  // two animated parts
    bad["pieces"].array[0]["parts"].array[0]["animation"] = "gk0001a";
    bad["animations"].array.insert(bad["animations"].array.begin(), Value("gk0001a"));
    assert(!story_kit_models::parse_built(bad, id, "island", archive_path, set, why));
    {  // the first part animated alone: the animation plays on part 0
        auto first = meta;
        first["pieces"].array[0]["parts"].array[0]["animation"] = "gk0001a";
        first["pieces"].array[0]["parts"].array[1].object.erase("animation");
        first["animations"].array = {Value("gk0001a")};
        assert(story_kit_models::parse_built(first, id, "island", archive_path, set, why) && set.pieces[0].animated_part == 0 &&
               set.pieces[0].animation_index == 3);
    }
    {  // a mixed model: its own-texture part first, lit as an object (light 0); never second, never animated
        auto mixed = meta;
        mixed["pieces"].array[0]["parts"].array[0]["light"] = 0;
        mixed["pieces"].array[0]["parts"].array[0]["kind"] = "own";
        assert(story_kit_models::parse_built(mixed, id, "island", archive_path, set, why) && set.pieces[0].light[0] == 0 && set.pieces[0].animated_part == 1);
        auto second = meta;
        second["pieces"].array[0]["parts"].array[1]["light"] = 0;
        second["pieces"].array[0]["parts"].array[1]["kind"] = "own";
        assert(!story_kit_models::parse_built(second, id, "island", archive_path, set, why));
        auto unlit = meta;
        unlit["pieces"].array[0]["parts"].array[0]["light"] = 0;  // light 0 without being the own part
        assert(!story_kit_models::parse_built(unlit, id, "island", archive_path, set, why));
    }
    bad = meta;
    bad["pieces"].array[0]["parts"].array[1]["light"] = 5;
    assert(!story_kit_models::parse_built(bad, id, "island", archive_path, set, why));
    bad = meta;
    bad["pieces"].array[0]["parts"].array[0]["triangles"] = 19000;
    assert(!story_kit_models::parse_built(bad, id, "island", archive_path, set, why));
    bad = meta;
    bad["pieces"].array[0]["collision"]["collision_sha256"] = std::string(64, '0');
    assert(!story_kit_models::parse_built(bad, id, "island", archive_path, set, why) && why.find("changed") != std::string::npos);
    bad = meta;
    bad["pieces"].array[0]["collision_file"] = "../" + col_name;
    assert(!story_kit_models::parse_built(bad, id, "island", archive_path, set, why));
    // one part: no second model, no animation
    auto one = meta;
    one["models"].array.resize(1);
    one["animations"].array.clear();
    one["pieces"].array[0]["parts"].array.resize(1);
    assert(story_kit_models::parse_built(one, id, "island", archive_path, set, why) && set.pieces[0].second_index == -1 && set.pieces[0].animation_index == -1);
    fs::remove_all(dir);
    std::puts("story kit models tests passed");
}
