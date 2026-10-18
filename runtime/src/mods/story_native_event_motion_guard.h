// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
namespace story_native_event_motion_guard {
// Pure observations only. The native adapter must read and validate these values
// from current owned guest records; this module performs no guest/native calls.
struct Point {float x=0,y=0,z=0;};
struct Identity {
 uint32_t actor=0,pid=0,token=0,stage=0,room=0,epoch=0,lease=0,manager=0,image=0;
 uint64_t program=0;
 int32_t staff=-1;
 bool operator==(const Identity&)const=default;
};
struct Binding {
 uint32_t acch=0,circle=0,position=0,old_position=0,speed=0,angle=0,shape_angle=0;
 uint32_t actor=0,pid=0,count=0,flags=0;
 float circle_height=0,radius=0;
};
struct Native {
 uint32_t profile=0,parameters=0,stage_argument=0,class_subtype=0,ground_movement=0;
 int32_t action=-1;
 bool operator==(const Native&)const=default;
};
struct Observation {Identity identity;Binding binding;Native native;Point position;bool ownership_verified=false;};
struct Support {
 uint32_t index=256,poly_bgw=0,entry_bgw=0,entry_used=0,poly_pid=0,entry_pid=0,registration_id=256;
 uint32_t ride_callback=0,move_flags=0,room=64;
 float height=0;
};
// Convex XZ rectangle bounding the complete radius footprint. This is a spatial
// limit, not a certification that its terrain is empty or that rays sweep a body.
struct Limits {
 float min_x=0,max_x=0,min_z=0,max_z=0,max_step=0,max_ground_delta=0;
};
struct Preconditions {bool owned_scope=false,exclusive_lease=false,ride_guard_installed=false;};
enum class Phase {Empty,Running,Candidate,Validated,Committed,Aborted};
enum class Decision {Delegate,Defer,Suppress,Reject};
struct Completion {bool accepted=false,cut_end=false;uint32_t manager=0;int32_t staff=-1;};
class Guard {
 Identity identity_{};Binding binding_{};Native native_{};Point origin_{},validated_{};Limits limits_{};
 Phase phase_=Phase::Empty;bool end_=false;
 bool current(const Observation&)const;
 bool within(Point)const;
 bool support(const Support&,Point)const;
public:
 Phase phase()const{return phase_;}
 // Begin before native MOVE process (after its start function). Failed admission
 // does not create a scope; one Guard cannot start another transaction until reset.
 bool begin(const Observation&,const Support& old_ground,Limits,Preconditions);
 Decision cut_end(const Identity& caller,uint32_t manager,int32_t staff);
 Decision ride_callback(const Identity& rider);
 // Called after the native MOVE process and before native GroundCross/CrrPos.
 bool candidate(const Observation&,const Support& proposed_ground);
 // Requires actual CrrPos observations, including GROUND_HIT and corrected Y.
 bool accept(const Observation&,const Support& corrected_ground);
 // Fresh identity/position check; native adapter emits at most one deferred end.
 Completion commit(const Observation&);
 void abort();
 void reset();
};
}
