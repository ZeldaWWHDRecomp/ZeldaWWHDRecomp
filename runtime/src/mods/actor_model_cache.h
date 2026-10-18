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
                    const std::vector<std::string>& python,const std::string& tool,const std::string& game_dir,bool collision=false,bool shoreline=false);
std::shared_ptr<const story_world_collision::mesh::Mesh> collision_for(const std::string& archive);
// Validated render triangles; zero means the archive is not prepared.
unsigned triangles_for(const std::string& archive);
bool shoreline_for(const std::string& archive);
std::string archive_for(const std::string& guest_path);
bool bounds_for(const std::string& archive,story_notebook::WorldPoint& minimum,story_notebook::WorldPoint& maximum);
mod_actor::ModelRig rig_for(const std::string& archive);
std::vector<std::string> clips_for(const std::string& archive);
}
