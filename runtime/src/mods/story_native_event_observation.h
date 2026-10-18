// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_lease.h"
#include "story_native_event_program.h"
#include <array>
#include <span>
namespace story_native_event_observation {
// Copied-byte validator for the exact WAIT or typed TURN-camera grammar. Collectors must
// independently bound all guest reads; this helper never dereferences guest EA.
enum class Operation {Prepare,Publish,Running,Restore,Drain,Release,
 // Native manager endProc has set NONE/cameraPlay0 while staff cursors and
 // consumers still belong to the retained image. These copied-byte checks
 // require a separate authenticated terminal/camera callback receipt.
 Terminal,TerminalDrain,ReleaseTerminal};
enum class Reason {None,Definition,Identity,Foreign,Image,Manager,Staff,Camera,Control,Busy};
struct Actor {
 uint32_t address=0,pid=0,token=0,profile=0,subtype=0;
 std::array<uint8_t,0x6C> cut{};
 std::array<uint8_t,32> fingerprint{}; // full immutable resident declaration binding
 friend bool operator==(const Actor&,const Actor&)=default;
};
struct Camera {
 uint32_t address=0,pid=0,flags=0,timer=0;
 // Typed event consumer context. +408 is saved return type, not action.
 uint32_t current_type=0,next_type=0,event_type=0,style=0,bg=0,trim=0;
 uint32_t counter108=0,counter118=0,elapsed11C=0;
 uint32_t style_table=0,style_count=0,param_pointer=0,param_index=0;
 uint32_t type_table=0,type_count=0;
 std::array<uint8_t,4> type_gate518{};
 std::array<uint8_t,0x20> engine100{}; // includes 10C/110/114 and elapsed counters
 std::array<uint8_t,0x14> mode_phase13C{}; // current/next mode, guard, angles,14C
 std::array<uint8_t,0x2C> initial_view010{}; // center/eye/up/bank/Fovy
 std::array<uint8_t,3> done100{};
 std::array<uint8_t,0x80> work37C{};
 std::array<uint8_t,0x40> positions0A4{};
 std::array<uint8_t,0x28> view03C{}; // direction globe + cached center/eye/bank/Fovy
 std::array<uint8_t,0x114> event_data{};
 friend bool operator==(const Camera&,const Camera&)=default;
};
// Valid idle or native entered tuple; refuses stale prior-type/partial unwind.
bool valid_camera_transition(const Camera& baseline,const Camera& current);
struct Expected {
 story_native_event_lease::Identity identity;
 uint32_t room_number=0,frames=30;
 std::array<uint8_t,8> stage_name{};
 std::array<uint32_t,8> retail_manager{};
 uint32_t retail_camera_play=0,retail_current_event=0;
 std::array<uint8_t,1280> retail_flags{};
 std::array<Actor,2> actors;
 Camera camera;
 // Absent retains the exact historical WAIT grammar. Explicit definitions
 // are copied into every opaque validation receipt, never inferred from bytes.
 std::optional<story_native_event_program::Program> program;
 friend bool operator==(const Expected&,const Expected&)=default;
};
struct Captured {
 story_native_event_lease::Identity identity;
 uint32_t room_number=0;
 std::array<uint8_t,8> stage_name{};
 std::span<const uint8_t> image;
 std::array<uint32_t,8> manager{};
 uint32_t camera_play=0,current_event=0;
 std::array<uint8_t,1280> flags{};
 std::array<Actor,2> actors;
 Camera camera;
 bool foreign=false,resource_generation_matches=false,control_owned=false,control_released=false;
 uint32_t manager_depth=0,camera_depth=0,npc_depth=0;
 uint32_t elapsed_ticks=0; // independently captured lease clock for TURN watchdog
};
class Result {
 bool valid_=false,release_=false;
 Reason reason_=Reason::Definition;
 Operation operation_=Operation::Prepare;
 uint32_t event_state_=0xFFFFFFFFu;
 Expected expected_{};
 story_native_event_lease::Identity identity_{};
 friend Result validate(Operation,const Expected&,const Captured&);
public:
 bool valid()const{return valid_;}
 bool restorable()const{return valid_&&operation_==Operation::Restore;}
 Operation operation()const{return operation_;}
 uint32_t event_state()const{return event_state_;}
 bool matches(const Expected&e)const{return valid_&&expected_==e;}
 bool same_definition(const Result&r)const{return valid_&&r.valid_&&expected_==r.expected_;}
 const story_native_event_program::Program* program()const{return valid_&&expected_.program?&*expected_.program:nullptr;}
 bool released()const{return valid_&&release_;}
 Reason reason()const{return reason_;}
 const story_native_event_lease::Identity& identity()const{return identity_;}
};
enum class CameraFailure {None,Definition,Identity,RawConsistency,Transition,BorrowedPointer,NonFinite,SelectedCut,SelectedCache,CurrentType,Debug,EventProperty,ReturnedStyle,ReturnedMode,Background,Trim,RetainedEventData,DoneBoolean,WorkCopied,WorkScalar,WorkByte,Radius,FollowTag,FollowCounter,FollowBoolean};
struct CameraDiagnostic {CameraFailure failure=CameraFailure::None;uint32_t offset=0,actual=0,expected=0;};
// Passive copied-byte diagnostic only: never produces a Result or authority.
CameraDiagnostic diagnose_camera(const Expected&,const Captured&,bool terminal) noexcept;
Result validate(Operation,const Expected&,const Captured&);
}
