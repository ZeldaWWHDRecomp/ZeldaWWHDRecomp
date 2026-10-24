// SPDX-License-Identifier: MPL-2.0
// Kit models: a package's own rigid model drawn with the materials of a room of the game (a ManifestModel of
// kind "kit", docs/world-objects.md "Kit models"). Like a mod stage's ground, the model brings no pictures:
// tools/story_mods/kit_model.py builds it on the player's machine from the player's own game files (the kit
// room's materials, textures, shader archives and texture animation) into the player's model cache
// (<manager>/Data/.kit-models), as a game piece set of one piece. The placement is a static world object drawn
// like a game piece (mod_actor's draw_piece): each part with a background light set of the room it stands in,
// so it has that place's light, fog and time of day; its collision is the model's own *_col meshes.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "mod_json.h"
#include "story_game_pieces.h"
#include "story_world_manifest.h"

namespace story_kit_models {

// The converter's recipe for `model` (kit_model.py's format 1), with the model file's absolute path.
// `distant_only`: the model is only a distant model (its game textures are drawn at most 256 texels a side).
std::string recipe(const story_notebook::ManifestModel& model, const std::string& glb, bool distant_only = false);
// The built model's resource identity: "gk" and 29 hex digits of the mod, the model id and the recipe (a
// changed model or mapping is a new archive). Kit identities never meet game piece ("gp"/"gd") or mod model ones.
std::string identity(const std::string& mod, const std::string& model_id, const std::string& recipe);
// Where the player's model cache keeps kit models, from a package's data directory (<manager>/Data/<mod>).
std::string cache_directory(const std::string& package_data_directory);

// The converter's metadata (its last output line) as a built set of one piece named `model_id`: its parts
// (one or two models, the second may be animated), bounds and collision (read from `directory`). False with
// a reason. Pure apart from reading the collision file; unit-tested.
bool parse_built(const mods::json::Value& meta, const std::string& identity, const std::string& model_id, const std::string& archive_path,
                 story_game_pieces::BuiltSet& out, std::string& why);

// Builds (or reuses) the kit model with the converter `tool`; any thread. Throws std::runtime_error with a
// player-facing reason.
std::shared_ptr<const story_game_pieces::BuiltSet> build(const std::string& mod, const story_notebook::ManifestModel& model,
                                                         const std::string& package_dir, const std::string& cache_dir,
                                                         const std::vector<std::string>& python, const std::string& tool,
                                                         const std::string& game_dir, bool distant_only = false);

// Makes the built model usable by `mod`'s placements of model `model_id`: the archive, its parts' lights and
// animation (mod_actor), the model resource (story_actors) and the collision. At start-up before hooks.
bool register_for_mod(const std::string& mod, const std::string& model_id, const story_game_pieces::BuiltSet& set, bool editor, std::string& why);

}  // namespace story_kit_models
