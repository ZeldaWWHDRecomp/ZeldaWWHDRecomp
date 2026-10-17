#pragma once
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>
#include "story_world_manifest.h"
#include "mod_actor.h"
#include "story_actors_core.h"
#include "story_enemy_ownership.h"
struct Cpu;
namespace story_actors {
inline constexpr uint32_t kModScratchOffset=1280;
inline constexpr uint32_t kPropsScratchOffset=5376;
inline constexpr uint32_t kSwitchScratchOffset=kPropsScratchOffset+0x30;
inline constexpr uint32_t kDoorScratchOffset=kSwitchScratchOffset+0x30;
inline constexpr uint32_t kIdentityScratchOffset=5632;
static_assert(kDoorScratchOffset+0x30<=kIdentityScratchOffset);
// Preserve existing resident/prop/switch snapshot offsets when adding enemies.
inline constexpr uint32_t kEnemyProfilesScratchOffset=10752;
inline constexpr uint32_t kEnemyOwnershipScratchOffset=kEnemyProfilesScratchOffset+4*0x30;
inline constexpr uint32_t kEnemyEpochScratchOffset=kEnemyOwnershipScratchOffset+story_enemy_ownership::kStorageBytes;
inline constexpr uint32_t kScratchBytes=kEnemyEpochScratchOffset+128*4;
static_assert(kEnemyProfilesScratchOffset>=kIdentityScratchOffset+128*40);
static_assert((kEnemyOwnershipScratchOffset&15)==0);
bool load(const std::string& mod,const std::vector<story_notebook::ManifestActor>& actors,std::string& why);
// Scene flags commit or roll back with the complete actor declaration batch.
bool load(const std::string& mod,const std::vector<story_notebook::ManifestActor>& actors,std::string& why,const std::vector<std::string>& scene_flags);
bool declared();
bool mod_declared();
// Prepared player-local model archive; register before install.
bool register_model(const std::string& mod,const std::string& model,const std::string& archive,int resource_index,std::string& why,
                    story_notebook::WorldPoint minimum={-50,0,-50},story_notebook::WorldPoint maximum={50,200,50},
                    bool skinned=false,std::vector<std::string> clips={},mod_actor::ModelRig rig={});
bool register_flags(const std::string& mod,const std::vector<std::string>& flags,std::string& why);
void service(Cpu*);
void set_game_calls(void (*original)(Cpu*,uint32_t),uint32_t (*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>));
void set_scratch_memory(uint32_t guest);
void set_scene_start(std::function<bool(const std::string&,const std::string&)> start);
void install(const std::function<std::string(uint32_t)>& replaced_by,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
void room_loaded(Cpu*,int room);
// Call once after native room actor setup (including the shared treasure hook).
void native_room_loaded(Cpu*,int room);
void state_loaded();
// Pure host hydration after mail state clear, before restored guest threads resume.
void restore_talk_text();
uint32_t actor_address(Cpu*,const std::string& mod,const std::string& id);
// World editor (live reload, main game thread): `actors` are all of the mod's actors as the changed manifest
// declares them; `changed` the ids whose declaration differs. Changed actors are removed from the game and
// created again from their new spec (in their room, once it is the player's); new actors are created and
// actors missing from the list are removed (kept for an undo). Kinds whose state is declared at start-up
// (editor_restart_only) and models not prepared yet keep their old state: `pending` lists them with the
// reason, for a restart. False with a reason (a list the start-up checks refuse) leaves everything unchanged.
bool editor_update(const std::string& mod,const std::vector<story_notebook::ManifestActor>& actors,const std::vector<std::string>& changed,std::string& why,
                   std::vector<std::string>* pending=nullptr);
// true for kinds whose changes take effect only after a restart (native props, switches, doors and
// enemies, mod enemies and carried props)
bool editor_restart_only(const story_notebook::ManifestActor&);
// whether a new actor of this spec can be created while the game runs (else `why` says what waits)
bool editor_live_capable(const std::string& mod,const story_notebook::ManifestActor&,std::string& why);
// World editor (main game thread): a model imported while the game runs, prepared as `archive`
// (actor_model_cache): actors can use it from now on.
bool editor_register_model(const std::string& mod,const std::string& model,const std::string& archive,bool skinned,std::string& why);
// actors removed for the editor and not created again yet
unsigned editor_pending();
bool position(Cpu*,const std::string& mod,const std::string& id,story_notebook::WorldPoint& out);
// Resolve a scene particle's declared actor by exact live identity in the
// active stage and room, then copy its position. Never expose the raw search
// result to the particle service.
bool particle_position(Cpu*,const std::string& mod,const std::string& id,unsigned room,story_notebook::WorldPoint& out);
// Exact placed prop snapshot identity for owned texture binding. Returns the
// declaration token and current native stage/room generation, never a retail copy.
bool prop_texture_identity(uint32_t actor,uint32_t pid,uint32_t& token,uint32_t& epoch,uint32_t& generation);
bool actions(const std::string& mod,const std::vector<story_notebook::StoryAction>& actions);
// Notebook hooks delegate only actors created by this service. Returns true when handled.
bool people_message(Cpu*);
bool people_next_message(Cpu*);
bool people_execute(Cpu*,bool before);
}
