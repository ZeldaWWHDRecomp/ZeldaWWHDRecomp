// SPDX-License-Identifier: MPL-2.0
// Game pieces in the running game (story_game_pieces_catalogue.h has the catalogue and the manifest
// rules). A set's pieces are built once from the player's own room archive into the player's model
// cache (<manager>/Data/.game-pieces, tools/story_mods/game_piece_model.py): one archive per set, with
// the set's textures once and one model per piece part, and one collision sidecar per piece. A placed
// piece is a static world object (mod_actor) whose model, second model (a window glow) and collision
// come from its set; it is drawn like the island draws it: the background light set of each part
// (settingTevStruct BG0..BG3 on the actor's own tevstr, which knows the room it is in) on the
// background draw lists. Nothing here runs without a package or editor placement that names a piece.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "story_game_pieces_catalogue.h"
#include "story_world_collision_mesh.h"
#include "story_world_manifest.h"

namespace story_game_pieces {

struct Built {
    std::string id;                   // the piece
    int body_index = 0;               // its first model's resource slot in the set archive
    int second_index = -1;            // its second model's slot (-1: none)
    int light[2] = {1, 4};            // settingTevStruct type of each part (0: an object's light, a kit model's own-texture part)
    int animated_part = 1;            // the part its texture animation plays on (0: the first, 1: the second)
    int animation_index = -1;         // its second model's texture animation slot (-1: none)
    story_notebook::WorldPoint minimum, maximum;
    unsigned triangles = 0;
    std::shared_ptr<const story_world_collision::mesh::Mesh> collision;
    // an island ground's distant model: an archive of its own (resource identity, cache file, its one model)
    std::string distant_archive, distant_path, distant_model;
    story_notebook::WorldPoint distant_minimum, distant_maximum;
};
struct BuiltSet {
    std::string set, archive, path;  // archive: the 31-character resource identity; path: the cache file
    std::vector<std::string> models;  // resource slot i+1 is models[i]
    std::vector<std::string> animations;  // slot models.size()+1+j is animations[j] (an animated part's model name)
    std::vector<Built> pieces;
    uint64_t archive_bytes = 0, texture_bytes = 0;
    const Built* piece(const std::string& id) const {
        for (const auto& p : pieces)
            if (p.id == id) return &p;
        return nullptr;
    }
};

// The set's resource identity: "gp" and 29 hex digits of the recipe's digest (a recipe change is a new archive).
std::string identity(const Set& s);
// Where the player's model cache keeps pieces, from a package's data directory (<manager>/Data/<mod>).
std::string cache_directory(const std::string& package_data_directory);

// Builds (or reuses) the set of piece `id` in `cache_dir` with the converter `tool`; any thread (it runs the
// converter). Throws std::runtime_error with a player-facing reason. The result is kept for later calls.
std::shared_ptr<const BuiltSet> build(const std::string& id, const std::string& cache_dir, const std::vector<std::string>& python,
                                      const std::string& tool, const std::string& game_dir);
// The set already built in this process for piece `id`, or null.
std::shared_ptr<const BuiltSet> built(const std::string& id);

// Makes piece `id` usable by `mod`'s placements: the archive's resource path, its model slots and part
// lights (mod_actor), the model resource (story_actors, key model_key(id)) and the collision; an island
// ground's distant archive too (key distant_key(id)). At start-up
// before hooks, or (editor) on the game thread. False with a reason.
bool register_for_mod(const std::string& mod, const BuiltSet& set, const std::string& id, bool editor, std::string& why);

// The pieces a manifest's actors use, each once.
std::vector<std::string> used(const std::vector<story_notebook::ManifestActor>& actors);

}  // namespace story_game_pieces
