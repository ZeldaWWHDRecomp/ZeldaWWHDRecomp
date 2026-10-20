// SPDX-License-Identifier: MPL-2.0
#include "story_game_pieces.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

#include "../platform/process.h"
#include "../runtime.h"
#include "actor_model_cache.h"
#include "mod_actor.h"
#include "mod_hash.h"
#include "mod_json.h"
#include "story_actors.h"

namespace story_game_pieces {
namespace {
std::mutex g_mutex, g_build_mutex;
std::map<std::string, std::shared_ptr<const BuiltSet>> g_sets;  // by set id

[[noreturn]] void fail(const std::string& why) { throw std::runtime_error(why); }
void need(bool ok, const std::string& why) {
    if (!ok) fail(why);
}
float number(const mods::json::Value& v) { return v.type == mods::json::Value::Number ? float(v.number) : NAN; }
}  // namespace

std::string identity(const Set& s) {
    return "gp" + mods::hash::sha256_text(s.id + std::string(1, '\0') + s.recipe).substr(0, 29);
}

std::string cache_directory(const std::string& package_data_directory) {
    if (package_data_directory.empty()) return {};
    return (std::filesystem::path(package_data_directory).parent_path() / ".game-pieces").string();
}

std::shared_ptr<const BuiltSet> built(const std::string& id) {
    const Piece* p = catalogue().find(id);
    if (!p) return nullptr;
    std::lock_guard guard(g_mutex);
    auto found = g_sets.find(p->set);
    return found == g_sets.end() ? nullptr : found->second;
}

std::shared_ptr<const BuiltSet> build(const std::string& id, const std::string& cache_dir, const std::vector<std::string>& python,
                                      const std::string& tool, const std::string& game_dir) {
    namespace fs = std::filesystem;
    const Piece* piece = catalogue().find(id);
    need(piece != nullptr, "\"piece:" + id + "\" is not in the game pieces catalogue");
    if (auto done = built(id)) return done;
    std::lock_guard building(g_build_mutex);
    if (auto done = built(id)) return done;
    const Set* set = catalogue().set(piece->set);
    need(set != nullptr, "the piece's set is missing");
    need(!cache_dir.empty() && !python.empty() && !tool.empty() && !game_dir.empty(), "the building converter is unavailable");
    const fs::path dir(cache_dir);
    need(!fs::is_symlink(dir), "the building cache cannot be a symbolic link");
    fs::create_directories(dir);
    const std::string archive = identity(*set);
    const fs::path out = dir / (archive + ".szs"), recipe = dir / (archive + ".recipe.json");
    for (const fs::path& p : {out, recipe, fs::path(out.string() + ".json")})
        need(!fs::is_symlink(p), "the building cache cannot use symbolic links");
    {
        std::ofstream f(recipe, std::ios::binary | std::ios::trunc);
        f << set->recipe;
        need(bool(f), "cannot write the building recipe into the model cache");
    }
    auto command = python;
    command.insert(command.end(), {tool, "--game", game_dir, "--recipe", recipe.string(), "--identity", archive, "--out", out.string()});
    const auto process = host::run_process(command);
    need(process.error.empty() && process.code == 0,
         "the buildings of \"" + set->label + "\" could not be built from your game files" +
             (process.output.empty() ? std::string() : ": " + process.output.substr(0, 200)));
    const auto end = process.output.find_last_not_of("\r\n");
    need(end != std::string::npos, "the building converter returned no result");
    const auto line = process.output.rfind('\n', end);
    const auto begin = line == std::string::npos ? 0 : line + 1;
    mods::json::Value meta;
    try {
        meta = mods::json::parse(process.output.substr(begin, end - begin + 1));
    } catch (...) {
        fail("the building converter returned invalid metadata");
    }
    need(meta.get("identity").string() == archive && meta.get("set").string() == set->id, "the building converter returned another set");
    need(fs::is_regular_file(out) && !fs::is_symlink(out) && fs::file_size(out) <= 16u * 1024 * 1024, "the built set archive is missing or oversized");
    {
        std::ifstream f(out, std::ios::binary);
        char magic[4] = {};
        f.read(magic, 4);
        need(bool(f) && std::string(magic, 4) == "Yaz0", "the built set archive is invalid");
    }
    auto result = std::make_shared<BuiltSet>();
    result->set = set->id;
    result->archive = archive;
    result->path = out.string();
    result->archive_bytes = uint64_t(meta.get("archive_bytes").number);
    result->texture_bytes = uint64_t(meta.get("texture_bytes").number);
    for (const auto& m : meta.get("models").array) result->models.push_back(m.string());
    need(!result->models.empty() && result->models.size() <= 16, "the built set has no models");
    for (const auto& p : meta.get("pieces").array) {
        Built b;
        b.id = p.get("id").string();
        const Piece* known = catalogue().find(b.id);
        need(known && known->set == set->id, "the built set names an unknown piece");
        const auto& parts = p.get("parts").array;
        need(parts.size() == known->parts.size(), "the built piece has another number of parts");
        for (size_t k = 0; k < parts.size(); ++k) {
            const auto name = parts[k].get("model").string();
            const auto at = std::find(result->models.begin(), result->models.end(), name);
            need(at != result->models.end(), "the built piece names a missing model");
            const int slot = int(at - result->models.begin()) + 1;
            if (k == 0)
                b.body_index = slot;
            else
                b.second_index = slot;
            b.light[k] = known->parts[k].light;
            need(unsigned(parts[k].get("triangles").number) == known->parts[k].triangles, "the built piece has another triangle count");
            b.triangles += known->parts[k].triangles;
        }
        if (known->distant_triangles) {
            const auto& d = p.get("distant");
            const std::string file = d.get("file").string(), model = d.get("model").string();
            b.distant_archive = d.get("archive").string();
            const fs::path path = dir / file;
            need(b.distant_archive.size() == 31 && b.distant_archive.rfind("gd", 0) == 0 && b.distant_archive != archive && !model.empty() &&
                     model.size() <= 31 && unsigned(d.get("triangles").number) == known->distant_triangles,
                 "the built island has another distant model");
            need(!file.empty() && file == fs::path(file).filename().string() && file.rfind(archive + ".szs.", 0) == 0 && fs::is_regular_file(path) &&
                     !fs::is_symlink(path) && fs::file_size(path) <= 1024 * 1024,
                 "the built island has no distant archive");
            {
                std::ifstream f(path, std::ios::binary);
                std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f), {}};
                need(bytes.size() >= 4 && std::string(bytes.begin(), bytes.begin() + 4) == "Yaz0" &&
                         mods::hash::sha256_text(std::string(bytes.begin(), bytes.end())) == d.get("archive_sha256").string(),
                     "the built island's distant archive changed on disk");
            }
            b.distant_path = path.string();
            b.distant_model = model;
            const auto& dl = d.get("minimum").array;
            const auto& dh = d.get("maximum").array;
            need(dl.size() == 3 && dh.size() == 3, "the built distant model has no bounds");
            b.distant_minimum = {number(dl[0]), number(dl[1]), number(dl[2])};
            b.distant_maximum = {number(dh[0]), number(dh[1]), number(dh[2])};
        }
        const auto& lo = p.get("minimum").array;
        const auto& hi = p.get("maximum").array;
        need(lo.size() == 3 && hi.size() == 3, "the built piece has no bounds");
        b.minimum = {number(lo[0]), number(lo[1]), number(lo[2])};
        b.maximum = {number(hi[0]), number(hi[1]), number(hi[2])};
        const auto name = p.get("collision_file").string();
        if (!known->collision_groups.empty()) {
            const fs::path path = dir / name;
            need(!name.empty() && name == fs::path(name).filename().string() && name.rfind(archive + ".szs.", 0) == 0 && fs::is_regular_file(path) &&
                     !fs::is_symlink(path) && fs::file_size(path) <= 200000,
                 "the built piece has no collision");
            std::ifstream f(path, std::ios::binary);
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f), {}};
            need(mods::hash::sha256_text(std::string(bytes.begin(), bytes.end())) == p.get("collision").get("collision_sha256").string(),
                 "the built piece's collision changed on disk");
            auto mesh = std::make_shared<story_world_collision::mesh::Mesh>();
            std::string why;
            need(story_world_collision::mesh::decode(bytes, *mesh, why), "the built piece's collision is invalid: " + why);
            need(mesh->triangles.size() == known->collision_triangles, "the built piece has another collision");
            b.collision = std::move(mesh);
        }
        result->pieces.push_back(std::move(b));
    }
    need(result->pieces.size() == set->pieces.size(), "the built set misses pieces");
    LOG("[game_pieces] set %s ready: %s, %zu models, archive %llu bytes (textures %llu)", set->id.c_str(), archive.c_str(), result->models.size(),
        static_cast<unsigned long long>(result->archive_bytes), static_cast<unsigned long long>(result->texture_bytes));
    std::lock_guard guard(g_mutex);
    g_sets[set->id] = result;
    return result;
}

bool register_for_mod(const std::string& mod, const BuiltSet& set, const std::string& id, bool editor, std::string& why) {
    const Built* b = set.piece(id);
    if (!b) return why = "the piece is not in its built set", false;
    if (!actor_model_cache::register_external(set.archive, set.path)) return why = "the set archive could not be registered", false;
    if (!mod_actor::register_piece_set(set.archive, set.models)) return why = "the set's models could not be registered", false;
    mod_actor::PieceDraw draw;
    draw.second_index = b->second_index;
    draw.light[0] = b->light[0];
    draw.light[1] = b->light[1];
    if (!mod_actor::set_piece_draw(set.archive, b->body_index, draw)) return why = "the piece's parts could not be registered", false;
    const std::string key = model_key(id);
    if (b->collision && !mod_actor::register_collision(mod, key, b->collision)) return why = "the piece's collision could not be registered", false;
    if (!b->distant_archive.empty()) {
        // the distant model, in its own small archive, draws like the island's own distant view: background light
        if (!actor_model_cache::register_external(b->distant_archive, b->distant_path)) return why = "the island's distant archive could not be registered", false;
        if (!mod_actor::register_piece_set(b->distant_archive, {b->distant_model})) return why = "the island's distant model could not be registered", false;
        mod_actor::PieceDraw distant_draw;
        distant_draw.second_index = -1;
        distant_draw.light[0] = distant_draw.light[1] = 1;
        if (!mod_actor::set_piece_draw(b->distant_archive, 1, distant_draw)) return why = "the island's distant model could not be registered", false;
        if (!story_actors::register_piece_model(mod, distant_key(id), b->distant_archive, 1, b->distant_minimum, b->distant_maximum, editor, why))
            return false;
    }
    return story_actors::register_piece_model(mod, key, set.archive, b->body_index, b->minimum, b->maximum, editor, why);
}

std::vector<std::string> used(const std::vector<story_notebook::ManifestActor>& actors) {
    std::vector<std::string> out;
    for (const auto& a : actors)
        if (a.game_piece() && std::find(out.begin(), out.end(), a.piece) == out.end()) out.push_back(a.piece);
    return out;
}

}  // namespace story_game_pieces
