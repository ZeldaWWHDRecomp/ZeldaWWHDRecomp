// SPDX-License-Identifier: MPL-2.0
#include "story_kit_models.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>

#include "../platform/process.h"
#include "../runtime.h"
#include "actor_model_cache.h"
#include "mod_actor.h"
#include "mod_hash.h"
#include "story_actors.h"

namespace story_kit_models {
namespace fs = std::filesystem;
namespace {
std::mutex g_build_mutex;
}  // namespace

std::shared_ptr<const story_game_pieces::BuiltSet> build(const std::string& mod, const story_notebook::ManifestModel& model,
                                                         const std::string& package_dir, const std::string& cache_dir,
                                                         const std::vector<std::string>& python, const std::string& tool,
                                                         const std::string& game_dir, bool distant_only) {
    auto fail = [](const std::string& why) { throw std::runtime_error(why); };
    if (model.kind != "kit" || model.kit_stage.empty() || model.kit_room < 0) fail("not a kit model");
    if (cache_dir.empty() || python.empty() || tool.empty() || game_dir.empty() || package_dir.empty()) fail("the kit model converter is unavailable");
    fs::path glb = fs::path(package_dir);
    for (const auto& part : fs::path(model.source)) {
        glb /= part;
        if (fs::is_symlink(glb)) fail("the model's path may not use symbolic links");
    }
    std::error_code ec;
    if (!fs::is_regular_file(glb, ec) || fs::file_size(glb, ec) > 16u * 1024 * 1024) fail("the model " + model.source + " is missing or larger than 16 MiB");
    const std::string text = recipe(model, fs::absolute(glb).string(), distant_only);
    // the identity: the recipe without the file's place (a moved package keeps its built models) and the file's content
    // (a changed model is a new archive, so the editor can take it while the game runs)
    const std::string archive = identity(mod, model.id, recipe(model, model.source, distant_only) + mods::hash::sha256_file(glb.string()));
    const fs::path dir(cache_dir);
    if (fs::is_symlink(dir, ec)) fail("the model cache cannot be a symbolic link");
    std::lock_guard building(g_build_mutex);
    fs::create_directories(dir);
    const fs::path out = dir / (archive + ".szs"), recipe_path = dir / (archive + ".recipe.json");
    for (const fs::path& p : {out, recipe_path, fs::path(out.string() + ".json")})
        if (fs::is_symlink(p, ec)) fail("the model cache cannot use symbolic links");
    {
        std::ofstream f(recipe_path, std::ios::binary | std::ios::trunc);
        f << text;
        if (!f) fail("cannot write the kit model's recipe into the model cache");
    }
    auto command = python;
    command.insert(command.end(), {tool, "--game", game_dir, "--recipe", recipe_path.string(), "--identity", archive, "--out", out.string()});
    const auto process = host::run_process(command);
    if (!process.error.empty() || process.code != 0)
        fail("the model could not be built from your game files" + (process.output.empty() ? std::string() : ": " + process.output.substr(0, 1500)));
    const auto end = process.output.find_last_not_of("\r\n");
    if (end == std::string::npos) fail("the kit model converter returned no result");
    const auto line = process.output.rfind('\n', end);
    const auto begin = line == std::string::npos ? 0 : line + 1;
    mods::json::Value meta;
    try {
        meta = mods::json::parse(process.output.substr(begin, end - begin + 1));
    } catch (...) {
        fail("the kit model converter returned invalid metadata");
    }
    if (!fs::is_regular_file(out, ec) || fs::is_symlink(out, ec) || fs::file_size(out, ec) > 16u * 1024 * 1024) fail("the built model archive is missing or oversized");
    auto result = std::make_shared<story_game_pieces::BuiltSet>();
    std::string why;
    if (!parse_built(meta, archive, model.id, out.string(), *result, why)) fail(why);
    LOG("[kit_models] %s:%s ready: %s, %zu parts, %u triangles, archive %llu bytes (textures %llu), collision %zu triangles", mod.c_str(), model.id.c_str(),
        archive.c_str(), result->models.size(), result->pieces[0].triangles, static_cast<unsigned long long>(result->archive_bytes),
        static_cast<unsigned long long>(result->texture_bytes), result->pieces[0].collision ? result->pieces[0].collision->triangles.size() : size_t(0));
    return result;
}

bool register_for_mod(const std::string& mod, const std::string& model_id, const story_game_pieces::BuiltSet& set, bool editor, std::string& why) {
    if (set.pieces.size() != 1) return why = "the kit model is not built", false;
    const auto& b = set.pieces[0];
    if (!actor_model_cache::register_external(set.archive, set.path, b.triangles)) return why = "the kit model's archive could not be registered", false;
    if (!mod_actor::register_piece_set(set.archive, set.models, set.animations)) return why = "the kit model's parts could not be registered", false;
    mod_actor::PieceDraw draw;
    draw.second_index = b.second_index;
    draw.animation = b.animation_index;
    draw.light[0] = b.light[0];
    draw.light[1] = b.light[1];
    draw.animated = b.animated_part;
    if (!mod_actor::set_piece_draw(set.archive, b.body_index, draw)) return why = "the kit model's parts could not be registered", false;
    if (b.collision && !mod_actor::register_collision(mod, model_id, b.collision)) return why = "the kit model's collision could not be registered", false;
    return story_actors::register_piece_model(mod, model_id, set.archive, b.body_index, b.minimum, b.maximum, editor, why);
}

}  // namespace story_kit_models
