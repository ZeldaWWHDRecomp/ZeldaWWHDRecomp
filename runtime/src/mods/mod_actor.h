#pragma once
#include "story_world_manifest.h"
#include <functional>
#include <cstddef>
#include <array>
#include <vector>
#include <initializer_list>
#include <string>
#include <memory>
#include "story_world_collision_mesh.h"
#include "story_world_placement.h"
struct Cpu;
namespace mod_actor {
inline constexpr uint16_t kProfile=0x7FFE,kWorldProfile=0x7FF6;
inline constexpr bool own_profile(uint16_t profile){return profile==kProfile||profile==kWorldProfile;}
inline constexpr uint32_t kScratchBytes=4096;
inline constexpr uint32_t kInstanceBytes=0x1120;
using PlacementProvider=std::function<story_world_placement::Index(const std::string&,int)>;
void configure_world_objects(const std::string& game_directory,PlacementProvider={});
bool world_object_query(const std::string& mod,const story_notebook::ManifestActor&,bool wireframe,mods::json::Value&,std::string& why);
bool register_collision(const std::string& mod,const std::string& model,std::shared_ptr<const story_world_collision::mesh::Mesh>);
struct ModelRig {std::vector<std::string> joint_names;std::vector<int> joint_parents;std::vector<bool> joint_look_compatible;};
struct Identity {std::string mod,id;uint32_t token=0,process_id=0;};
struct State {
    story_notebook::WorldPoint position,rotation;float collision_radius=0,collision_height=0;
    bool collision_enabled=true,pushable=false,scene_owned=false;std::string animation;float animation_frame=0;
};
struct MotionState {
    std::array<story_notebook::WorldPoint,16> points{};unsigned count=0,next=0;
    float speed=100;bool loop=false,finished=false,enabled=false;
};
enum class BehaviorEvent:uint32_t {CreateNew,CreateRestored,Execute,Draw,TalkBegin,TalkEnd,TalkCancel,Hit,Destroy};
struct Hit {uint32_t sequence=0,attack_type=0,attack_power=0,source_kind=0,source_process_id=0;story_notebook::WorldPoint position,reaction;};
struct CombatState {
    uint32_t health=0,max_health=0,phase=0,flags=0,last_damage_sequence=0,reward_status=0,native_drop_status=0;
    uint32_t invulnerability_frames=0,reaction_frames=0,attack_active_frames=0,attack_cooldown_frames=0;
};
bool combat_snapshot(Cpu*,const Identity&,CombatState&);
bool attack_begin(Cpu*,const Identity&);
bool attack_cancel(Cpu*,const Identity&);
void quarantine_combat(Cpu*,const Identity&);
enum class CombatReward {Committed,AlreadyDone,Failed};
using CombatRewardSink=std::function<CombatReward()>;
// Called only by the actor's existing owned native talk start/resume branch.
// The returned text and completion are latched until its genuine native END.
struct TalkSelection {std::string lines;std::function<void()> completed;};
using TalkProvider=std::function<bool(uint32_t actor,bool resumed,TalkSelection&)>;
bool set_talk_provider(const std::string&mod,const std::string&id,TalkProvider);

bool set_combat_reward(const std::string& mod,const std::string& id,CombatRewardSink);
bool set_carry_break(const std::string& mod,const std::string& id,std::function<bool()>);
using BehaviorSink=std::function<void(Cpu*,uint32_t,BehaviorEvent,const Hit*)>;
void set_behavior_sink(BehaviorSink);
uint32_t declaration_token(const std::string& mod,const std::string& id);
bool identify(Cpu*,uint32_t actor,Identity&);
uint32_t resolve(Cpu*,const Identity&);
bool snapshot(Cpu*,const Identity&,State&);
bool set_position(Cpu*,const Identity&,story_notebook::WorldPoint);
bool set_rotation(Cpu*,const Identity&,story_notebook::WorldPoint);
bool set_collision(Cpu*,const Identity&,float radius,float height,bool enabled,bool pushable);
bool has_animation(Cpu*,const Identity&,const std::string& clip);
bool set_animation(Cpu*,const Identity&,const std::string& clip,float blend_frames=6,bool loop=true);
uint32_t hit_event_address(Cpu*,const Identity&);
uint32_t userdata_address(Cpu*,const Identity&);
uint32_t behavior_handle(Cpu*,const Identity&);
bool set_behavior_handle(Cpu*,const Identity&,uint32_t);
bool set_hit_notifications(const std::string& mod,const std::string& id,bool);
bool get_motion(Cpu*,const Identity&,MotionState&);
bool set_motion(Cpu*,const Identity&,const MotionState&);
bool start_path(Cpu*,const Identity&,const std::vector<story_notebook::WorldPoint>&,float speed,bool loop=false);
bool stop_path(Cpu*,const Identity&);
bool begin_scene(Cpu*,const Identity&,uint32_t lease);
bool end_scene(Cpu*,const Identity&,uint32_t lease);
bool scene_path(Cpu*,const Identity&,uint32_t lease,const std::vector<story_notebook::WorldPoint>&,float speed);
bool scene_rotation(Cpu*,const Identity&,uint32_t lease,story_notebook::WorldPoint degrees);
bool scene_animation(Cpu*,const Identity&,uint32_t lease,const std::string&,float blend_frames=6,bool loop=true);
// Startup-private provisional declarations cannot escape through installed services.
size_t checkpoint();
void rollback(size_t) noexcept;
void configure(void(*original)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>),uint32_t scratch,uint32_t grapple_storage=0);
// Declaration tokens go into native create parameters; they remain stable across state loads.
uint32_t declare(const std::string& mod,const story_notebook::ManifestActor&,const std::string& archive,int resource_index,std::function<void()> talked,
                 story_notebook::WorldPoint minimum={-50,0,-50},story_notebook::WorldPoint maximum={50,200,50},
                 bool skinned=false,std::vector<std::string> clips={},ModelRig rig={});
// World editor (live reload): the declaration `token` takes a changed spec and model; the actor is
// created again by its owner. Same checks as declare; combat and carry actors are refused (they take
// effect after a restart). False leaves the declaration unchanged; apply=false only checks.
bool redeclare(uint32_t token,const story_notebook::ManifestActor&,const std::string& archive,int resource_index,
               story_notebook::WorldPoint minimum,story_notebook::WorldPoint maximum,bool skinned,std::vector<std::string> clips,ModelRig rig,bool apply=true);
// Internal collision-free distant/coast variants; never exposed as behaviour callbacks.
bool visual_role(uint32_t token,bool distant);
bool owns_visual(uint32_t actor,uint32_t token);
// World editor: the profile is installed, so declare() may add actors while the game runs (their
// records are in place for all 128 declarations).
bool live_ready();
bool install(const std::function<std::string(uint32_t)>& replaced_by,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
void state_loaded();
bool talking(uint32_t actor);
bool failed(uint32_t declaration_token);
void retry(uint32_t declaration_token);
}
