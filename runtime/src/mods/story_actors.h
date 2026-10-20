#pragma once
#include <array>
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
// One snapshot-owned selected dialogue record in the existing unused gap.
inline constexpr uint32_t kTalkProofScratchOffset=kDoorScratchOffset+0x30;
inline constexpr uint32_t kTalkProofBytes=64;
static_assert(kTalkProofScratchOffset>=kDoorScratchOffset+0x30);
static_assert(kTalkProofScratchOffset+kTalkProofBytes<=kIdentityScratchOffset);
// The Miniblin's private profile (added after the first four enemy kinds): the free 0x30 bytes
// between the talk proof and the identity rows, so no other offset moves.
inline constexpr uint32_t kMiniblinProfileScratchOffset=kTalkProofScratchOffset+kTalkProofBytes;
static_assert(kMiniblinProfileScratchOffset+0x30<=kIdentityScratchOffset&&(kMiniblinProfileScratchOffset&3)==0);
// Preserve existing resident/prop/switch snapshot offsets when adding enemies.
inline constexpr uint32_t kEnemyProfilesScratchOffset=10752;
inline constexpr uint32_t kEnemyOwnershipScratchOffset=kEnemyProfilesScratchOffset+4*0x30;
inline constexpr uint32_t kEnemyEpochScratchOffset=kEnemyOwnershipScratchOffset+story_enemy_ownership::kStorageBytes;
inline constexpr uint32_t kGrappleScratchOffset=(kEnemyEpochScratchOffset+128*4+15)&~15u;
inline constexpr uint32_t kVisualScratchOffset=kGrappleScratchOffset+0x4000;
// Private profiles of the enemy kinds added after the Miniblin: appended at the end, so no earlier offset
// moves; one 0x30 profile each. Slot 2 is the Darknut's (slots 0 and 1 are kept for other kinds).
inline constexpr uint32_t kExtraEnemyProfilesScratchOffset=kVisualScratchOffset+0x3000;
inline constexpr uint32_t kExtraEnemyProfiles=8;
inline constexpr uint32_t kDarknutProfileScratchOffset=kExtraEnemyProfilesScratchOffset+2*0x30;
inline constexpr uint32_t kRatHoleProfileScratchOffset=kExtraEnemyProfilesScratchOffset+3*0x30;
// Batch 3: slot 6 the Octorok's.
// Batch 3: slot 5 the Bubble's.
// Batch 3: slot 4 the Boko Baba's.
// the table-driven enemies' private profiles (story_enemies_more.h, ids 0x7FD0..0x7FE7), after the extra ones
inline constexpr uint32_t kMoreEnemyProfilesScratchOffset=kExtraEnemyProfilesScratchOffset+kExtraEnemyProfiles*0x30;
inline constexpr uint32_t kMoreEnemyProfiles=20;
inline constexpr uint32_t kScratchBytes=kMoreEnemyProfilesScratchOffset+kMoreEnemyProfiles*0x30;
static_assert(kEnemyProfilesScratchOffset>=kIdentityScratchOffset+128*40);
static_assert((kEnemyOwnershipScratchOffset&15)==0);
static_assert(kTalkProofScratchOffset+kTalkProofBytes<=kScratchBytes);
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
// the "stage" action: into one of the mod's own stages (mod_stages::enter_stage)
void set_stage_enter(std::function<bool(const std::string&,const std::string&,unsigned,unsigned)> enter);
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
// With `cpu`, an actor a handle drag moved in place (editor_pose) whose change is only that move keeps its
// copy in the game: the drag's end checks it exists, is ours and stands at the new place (solid kinds,
// switches, doors and chests, are always created again).
bool editor_update(const std::string& mod,const std::vector<story_notebook::ManifestActor>& actors,const std::vector<std::string>& changed,std::string& why,
                   std::vector<std::string>* pending=nullptr,Cpu* cpu=nullptr);
// World editor, a handle drag (main game thread): the actor's copy in the game moves to `position` and
// turns to `rotation` (degrees) in place, without being created again. False when it has no copy now.
bool editor_pose(Cpu*,const std::string& mod,const std::string& id,const story_notebook::WorldPoint& position,const story_notebook::WorldPoint& rotation);
// --editor, before install: native props, switches, doors, enemies and residents are installed even
// without declarations, so the editor places them while the game runs.
void editor_live_kinds();
// Whether actors of a world-editor type ("pot", "door", "bokoblin", "model_enemy"...) appear while the
// game runs (`fallback` for the others).
bool editor_kind_live(const std::string& type,bool fallback);
// true for kinds whose changes take effect only after a restart (mod enemies and carried props; native
// props, switches, doors and enemies unless the editor installed them for live placement)
bool editor_restart_only(const story_notebook::ManifestActor&);
// placed residents walking a mod route ("path"): whether any is declared (their record memory is
// needed), and one's state for the editor (declared: it has a route; live: its copy in the game
// follows it; status: walking, skipped a blocked point, stopped, finished)
bool resident_routes_declared();
// a walking resident needs borrowed walk `kind` (story_resident_types::kBorrowedWalks)
bool borrowed_walk_declared(unsigned kind);
struct ResidentRoute {bool declared=false,live=false;std::string status;};
bool resident_route(const std::string& mod,const std::string& id,ResidentRoute& out);
// whether a new actor of this spec can be created while the game runs (else `why` says what waits)
bool editor_live_capable(const std::string& mod,const story_notebook::ManifestActor&,std::string& why);
// World editor (main game thread): a model imported while the game runs, prepared as `archive`
// (actor_model_cache): actors can use it from now on.
bool editor_register_model(const std::string& mod,const std::string& model,const std::string& archive,bool skinned,std::string& why);
// Game pieces (story_game_pieces.h): piece `model` ("piece:<id>") of `mod` is resource slot `index` of a
// set archive that its other pieces share. Before install, or (editor) on the game thread.
// whether `mod` has the prepared model `model`
bool has_model(const std::string& mod,const std::string& model);
bool register_piece_model(const std::string& mod,const std::string& model,const std::string& archive,int index,
                          story_notebook::WorldPoint minimum,story_notebook::WorldPoint maximum,bool editor,std::string& why);
// actors removed for the editor and not created again yet
unsigned editor_pending();
bool position(Cpu*,const std::string& mod,const std::string& id,story_notebook::WorldPoint& out);
// Resolve a scene particle's declared actor by exact live identity in the
// active stage and room, then copy its position. Never expose the raw search
// result to the particle service.
bool particle_position(Cpu*,const std::string& mod,const std::string& id,unsigned room,story_notebook::WorldPoint& out);
// Exact placed prop snapshot identity for owned texture binding. Returns the
// declaration token and current native stage/room generation, never a retail copy.
// Pure resident ownership for native event Capture. No native search or writes.
// Existing 40-byte resident row persists magic/token/full fingerprint; zero old
// rows deny this accessor while ordinary resident talk remains unchanged.
// Returns declaration + restored row identity only. Native leases must
// independently match saved config fingerprint/token in the native-only arena;
// this getter alone never authorizes a restored event. Historical scratch unchanged.
struct ResidentEventIdentity {
 uint32_t token=0,actor=0,pid=0,stage_pid=0,room_pid=0,room=0,subtype=0;
 std::array<uint8_t,32> fingerprint{};
};
struct ResidentEventDeclaration {
 uint32_t token=0,room=0,subtype=0;std::string stage;
 std::array<uint8_t,32> fingerprint{};
};
// Pure startup declaration authority, independent of any snapshot/native address.
bool resident_event_declaration(const std::string& mod,const std::string& id,ResidentEventDeclaration&);
// Data-only placed process lineage. No address lookup, guest call or entry
// authority. Absent is the private unplaced sentinel corroborated by scratch;
// pending deletion/quarantine and incomplete rows refuse instead.
struct ResidentEventProcess {
 enum class Presence {Absent,Present};Presence presence=Presence::Absent;
 ResidentEventDeclaration declaration;uint32_t pid=0;
 uint32_t pid_slot=0,identity_row=0,scratch_window=0;
 std::array<uint8_t,4>pid_bytes{};std::array<uint8_t,40>identity_bytes{};
 // Contains the actual stage name, stage PID and all room PID clocks. Other
 // bytes are retained verbatim, never interpreted as ownership predicates.
 std::array<uint8_t,0x200>scratch_bytes{};
};
bool resident_event_process(const std::string& mod,const std::string& id,ResidentEventProcess&);
// Explicit native query BEFORE pure Capture only. Restores its scratch output;
// caller must verify game-thread boundary and preserve outgoing stack/Cpu.
// Candidate address is NOT ownership; resident_event_identity must follow.
uint32_t query_resident_event_address(Cpu*,const std::string& mod,const std::string& id);
bool resident_event_identity(const std::string& mod,const std::string& id,
 uint32_t actor,uint32_t pid,ResidentEventIdentity&);
// Exact same identity predicates, with no diagnostic allocation/log mutex.
// Required for bounded reads while other guest threads are physically parked.
bool resident_event_identity_quiet(const std::string& mod,const std::string& id,
 uint32_t actor,uint32_t pid,ResidentEventIdentity&);
bool prop_texture_identity(uint32_t actor,uint32_t pid,uint32_t& token,uint32_t& epoch,uint32_t& generation);
bool actions(const std::string& mod,const std::vector<story_notebook::StoryAction>& actions);
// A placed chest holding one of its mod's items ("parameters": {"item": "item:<id>"}): the chest actor at `address`.
bool placed_chest_item(uint32_t address,std::string& mod,std::string& item);
// Notebook hooks delegate only actors created by this service. Returns true when handled.
bool people_message(Cpu*);
bool people_next_message(Cpu*);
bool people_execute(Cpu*,bool before);
// The resident at `actor` is one this service created for a mod (not one of the game's own).
bool owns_resident(uint32_t actor);
}
