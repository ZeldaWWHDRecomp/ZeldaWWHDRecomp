// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_arena.h"
#include "story_native_event_observation.h"
#include "story_actors.h"
#include <functional>
#include "story_native_scene_request.h"
#include "story_native_scene_activation.h"
namespace story_native_wait_readiness {
struct Request {bool private_opt_in=false;std::string mod,scene,fixture_program;std::array<std::string,2>residents;uint32_t native_wait_timer=30;std::optional<story_native_event_program::Program> program;std::shared_ptr<const story_native_scene_request::DeclaredRequest> declared={};};
struct Boundary {
 uint32_t guest_thread=0,stack_begin=0,stack_end=0;uint64_t logic_step=0;bool exclusive_idle=false;
 // Actual source supplies this only while the native frontend owns this exact
 // activation. It allows read-only rearm inspection of that finishing scene;
 // it conveys no native publication or consumer lease authority.
 std::shared_ptr<const story_native_scene_playback::Activation>owned_scene={};
 bool operator==(const Boundary&)const=default;
};
struct Sources {
 // True only during final committed startup phase BEFORE guest threads start.
 std::function<bool()>startup_complete;
 std::function<bool(const Cpu*,Boundary&)>boundary; // actual designated guest game thread, not caller Expected
 std::function<bool(uint32_t,std::span<uint8_t>)>read;
 std::function<bool(uint32_t,std::span<const uint8_t>)>restore_frame; // only exact verified 64B outgoing frame
};
enum class Status {Disabled,Uninitialized,Definitions,Allocated,NotReady,Ready,Quarantined};
// Diagnostic-only: broad phase last attempted by inspect(). Never an admission
// or ownership receipt.
enum class Check {Allocation,Boundary,Search,Actors,Stage,Link,Control,Manager,Camera,Stable,Ready};
class Controller {
 Request request_;Sources sources_;Status status_=Status::Uninitialized;Check check_=Check::Allocation;
 story_native_event_arena::Layout arena_{};
 std::array<story_actors::ResidentEventDeclaration,2>definitions_;
 std::array<uint8_t,32>definition_hash_{};
 story_native_event_observation::Expected expected_{};
 std::vector<uint8_t>original_image_;bool initialized_=false,busy_=false,startup_attempted_=false;
 bool read(uint32_t,std::span<uint8_t>)const;
public:
 Controller(Request,Sources);
 Status startup(); // only once, after committed declarations; actual runtime_alloc below68000000
 Status inspect(Cpu*); // explicit native Search before pure bounded capture
 Status status()const{return status_;}
 Check check()const{return check_;}
 static constexpr bool publication_enabled(){return false;}
 const story_native_event_arena::Layout& arena()const{return arena_;}
 const story_native_event_observation::Expected* candidate()const{return status_==Status::Ready?&expected_:nullptr;}
 std::span<const uint8_t> original_image()const{return status_==Status::Ready?std::span<const uint8_t>(original_image_):std::span<const uint8_t>();}
 const std::array<uint8_t,32>& definition_hash()const{return definition_hash_;}
};
// Readiness candidates are HOST-only live baseline/config copies, NOT opaque
// Prepare/Restore certificates, active retained claims or snapshot generations.
// Candidate Expected.identity.generation=1 is a PROVISIONAL placeholder; a
// future controller must mint a snapshot-owned generation before publication.
// Never pass this candidate directly as a publish/restore or active-claim proof.
// Arena remains zero/unpublished; no image/stamp/record/hook/save/load writes.
}
