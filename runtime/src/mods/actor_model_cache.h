// Player-local generated models, resolved only at their exact native archive path.
#pragma once
#include "story_world_manifest.h"
#include "mod_actor.h"
#include <string>
#include <memory>
#include "story_world_collision_mesh.h"
#include <vector>
namespace actor_model_cache {
std::string prepare(const std::string& mod,const story_notebook::ManifestModel& model,
                    const std::string& package_dir,const std::string& data_dir,
                    const std::vector<std::string>& python,const std::string& tool,const std::string& game_dir,bool collision=false,bool shoreline=false,const std::string& material={},
                    unsigned generation=0);
// `generation` (the editor's replace_model, issue #287): a model's file replaced while the game runs gets an archive of
// its own (0: the model's usual archive, as the start-up prepares it), so the actors drawing it load the new version.
std::shared_ptr<const story_world_collision::mesh::Mesh> collision_for(const std::string& archive);
// Validated render triangles; zero means the archive is not prepared.
unsigned triangles_for(const std::string& archive);
bool shoreline_for(const std::string& archive);
// a shoreline prepared with the still-water material ("material": "water")
bool water_for(const std::string& archive);
// a shoreline's material as prepared: "" (foam), "water", "pond", "river", "waterfall" or "lava"; "" for anything else
std::string material_for(const std::string& archive);
std::string archive_for(const std::string& guest_path);
// A game piece set (story_game_pieces.h): its archive, built elsewhere into the player's model cache, is
// resolved like a prepared model's. The identity is 31 characters; an identity already in use for another
// path is refused.
// `triangles`: its render triangles where they are known (a kit model's; triangles_for)
bool register_external(const std::string& identity,const std::string& path,unsigned triangles=0);
bool bounds_for(const std::string& archive,story_notebook::WorldPoint& minimum,story_notebook::WorldPoint& maximum);
mod_actor::ModelRig rig_for(const std::string& archive);
std::vector<std::string> clips_for(const std::string& archive);
// Why a model of `mod` could not be prepared (prepare's last failure, or one note_failure recorded for a model built
// elsewhere, a kit model): empty once it is prepared. The editor says this instead of "appears after a restart",
// because a restart prepares it the same way and fails again (issue #280).
std::string failure_for(const std::string& mod,const std::string& model);
void note_failure(const std::string& mod,const std::string& model,const std::string& why);
void clear_failure(const std::string& mod,const std::string& model);
// prepare is running for this model now (the editor prepares on a thread of its own)
bool preparing(const std::string& mod,const std::string& model);
// The editor asked for this model (Host::prepare_model): preparing() holds from the request until its result is in the
// game (registered or failed), not only while the converter runs, so a placement waiting for it says "being prepared".
void hold(const std::string& mod,const std::string& model);
void release(const std::string& mod,const std::string& model);
// A converter's reason as one plain line: terminal colour sequences (Python 3.13+ colours its tracebacks) and
// control characters removed, at most 200 characters.
std::string plain_reason(std::string text);
}
