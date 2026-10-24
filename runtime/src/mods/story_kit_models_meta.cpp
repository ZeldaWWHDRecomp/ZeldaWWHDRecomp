// SPDX-License-Identifier: MPL-2.0
// Kit models: the recipe, the identity and the converter's metadata (pure apart from reading the collision file;
// unit-tested in runtime/tools/story_kit_models_test.cpp). Building and registering: story_kit_models.cpp.
#include "story_kit_models.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "mod_hash.h"

namespace story_kit_models {
namespace fs = std::filesystem;
namespace {
float number(const mods::json::Value& v) { return v.type == mods::json::Value::Number ? float(v.number) : NAN; }
bool point(const mods::json::Value& v, story_notebook::WorldPoint& out) {
    if (v.type != mods::json::Value::Array || v.array.size() != 3) return false;
    out = {number(v.array[0]), number(v.array[1]), number(v.array[2])};
    return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
}
}  // namespace

std::string recipe(const story_notebook::ManifestModel& model, const std::string& glb, bool distant_only) {
    using mods::json::quote;
    std::string out = "{\"kit_model_format\": 1, \"id\": " + quote(model.id) + ", \"glb\": " + quote(glb) + ", \"kit\": {\"stage\": " +
                      quote(model.kit_stage) + ", \"room\": " + std::to_string(model.kit_room) + "}";
    if (!model.kit_material.empty()) out += ", \"material\": " + quote(model.kit_material);
    if (!model.kit_materials.empty()) {
        out += ", \"materials\": {";
        for (size_t i = 0; i < model.kit_materials.size(); ++i)
            out += (i ? ", " : "") + quote(model.kit_materials[i].first) + ": " + quote(model.kit_materials[i].second);
        out += "}";
    }
    if (distant_only) out += ", \"far\": true";
    return out + "}\n";
}

std::string identity(const std::string& mod, const std::string& model_id, const std::string& recipe_text) {
    return "gk" + mods::hash::sha256_text(mod + std::string(1, '\0') + model_id + std::string(1, '\0') + recipe_text).substr(0, 29);
}

std::string cache_directory(const std::string& package_data_directory) {
    if (package_data_directory.empty()) return {};
    return (fs::path(package_data_directory).parent_path() / ".kit-models").string();
}

bool parse_built(const mods::json::Value& meta, const std::string& archive, const std::string& model_id, const std::string& archive_path,
                 story_game_pieces::BuiltSet& out, std::string& why) {
    out = {};
    if (meta.get("identity").string() != archive || meta.get("set").string() != "kit:" + model_id) return why = "the kit model converter returned another model", false;
    out.set = "kit:" + model_id;
    out.archive = archive;
    out.path = archive_path;
    out.archive_bytes = uint64_t(std::max(0.0, meta.get("archive_bytes").number));
    out.texture_bytes = uint64_t(std::max(0.0, meta.get("texture_bytes").number));
    for (const auto& m : meta.get("models").array) out.models.push_back(m.string());
    if (out.models.empty() || out.models.size() > 2) return why = "the built kit model has no parts or more than two", false;
    for (const auto& m : out.models)
        if (m.empty() || m.size() > 31) return why = "the built kit model names an invalid part", false;
    const auto& pieces = meta.get("pieces").array;
    if (pieces.size() != 1 || pieces[0].get("id").string() != model_id) return why = "the built kit model has another piece", false;
    const auto& p = pieces[0];
    story_game_pieces::Built b;
    b.id = model_id;
    const auto& parts = p.get("parts").array;
    if (parts.size() != out.models.size()) return why = "the built kit model has another number of parts", false;
    for (size_t k = 0; k < parts.size(); ++k) {
        if (parts[k].get("model").string() != out.models[k]) return why = "the built kit model's parts are out of order", false;
        const double light = parts[k].get("light").number;
        // a part of game materials has a background light set (1..4); the model's own-texture part (first) an object's light (0)
        const bool own = parts[k].get("kind").string() == "own";
        if (parts[k].get("light").type != mods::json::Value::Number || light < (own ? 0 : 1) || light > (own ? 0 : 4) || std::floor(light) != light ||
            (own && k != 0))
            return why = "the built kit model's part has no background light set", false;
        b.light[k] = int(light);
        const double triangles = parts[k].get("triangles").number;
        if (!(triangles >= 1 && triangles <= 20000)) return why = "the built kit model's part has no triangles", false;
        b.triangles += unsigned(triangles);
        const std::string animation = parts[k].get("animation").string();
        if (!animation.empty()) {
            if (own || animation != out.models[k] || !out.animations.empty()) return why = "only one part of game materials may be animated", false;
            out.animations.push_back(animation);
            b.animation_index = int(out.models.size() + out.animations.size());
            b.animated_part = int(k);
        }
    }
    if (b.triangles > 20000) return why = "the built kit model has more than 20000 triangles", false;
    b.body_index = 1;
    b.second_index = out.models.size() > 1 ? 2 : -1;
    if (!point(p.get("minimum"), b.minimum) || !point(p.get("maximum"), b.maximum) || b.minimum.x > b.maximum.x || b.minimum.y > b.maximum.y ||
        b.minimum.z > b.maximum.z)
        return why = "the built kit model has no bounds", false;
    const std::string name = p.get("collision_file").string();
    const fs::path dir = fs::path(archive_path).parent_path(), path = dir / name;
    std::error_code ec;
    if (name.empty() || name != fs::path(name).filename().string() || name.rfind(archive + ".szs.", 0) != 0 || !fs::is_regular_file(path, ec) ||
        fs::is_symlink(path, ec) || fs::file_size(path, ec) > 512u * 1024)
        return why = "the built kit model has no collision", false;
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f), {}};
    if (mods::hash::sha256_text(std::string(bytes.begin(), bytes.end())) != p.get("collision").get("collision_sha256").string())
        return why = "the built kit model's collision changed on disk", false;
    auto mesh = std::make_shared<story_world_collision::mesh::Mesh>();
    if (!story_world_collision::mesh::decode(bytes, *mesh, why)) return why = "the built kit model's collision is invalid: " + why, false;
    b.collision = std::move(mesh);
    out.pieces.push_back(std::move(b));
    return true;
}

}  // namespace story_kit_models
