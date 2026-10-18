// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include <functional>
#include <initializer_list>
struct Cpu;
namespace story_native_people_events {
using namespace story_native_event_observation;
using Read=std::function<bool(uint32_t,std::span<uint8_t>)>;
using Write=std::function<bool(uint32_t,std::span<const uint8_t>)>;
using Invoke=std::function<uint32_t(Cpu*,uint32_t,std::initializer_list<uint32_t>)>;
struct Binding {story_native_event_lease::Identity lease;Actor actor;uint32_t name=0;int32_t staff=-1;};
using VerifyActor=std::function<bool(const Actor&,const story_native_event_lease::Identity&)>;
using VerifyLease=std::function<bool(const story_native_event_lease::Identity&)>;
// Must validate actual current cut/parameters in the owned mutable image, not
// trust a trace/cached action. Return only native index WAIT0, TURN3 or MOVE4.
using VerifyAction=std::function<bool(const Binding&,uint32_t&)>;
struct Capabilities {
 bool staff_lookup=false,common_event_move=false;
 // MOVE is refused unless all three scoped safeguards are wired by the caller.
 bool move_process=false,cut_end=false,ride_callback=false;
};
struct Backend {Read read;Write write;Invoke invoke;VerifyActor actor;VerifyLease lease;VerifyAction action;Capabilities capabilities;};
enum class Dispatch {Foreign,Processed,Refused,Quarantined};
// Caller serializes routing/detachment on the game logic thread; staff authority
// itself is thread-local. Only the active exclusive Router may own native hooks.
// Isolated adapter: no hooks installed, no manager publication, no camera writes,
// no freeing. Native invocation must use real runtime hook dispatch so the scoped
// getMyStaffId and (for MOVE) motion/cutEnd/ride guards actually run.
class Router {
 Backend backend_;Expected expected_;bool configured_=false,quarantined_=false,detached_=false;
 int active_=-1;uint32_t depth_=0;
 bool read(uint32_t,std::span<uint8_t>);
 bool capture(unsigned,Actor&);
 bool own_cut(unsigned,const Actor&)const;
 Binding binding(unsigned,const Actor&)const;
 bool scope();
public:
 Router(Backend,Expected);
 bool configured()const{return configured_;}
 bool quarantined()const{return quarantined_;}
 uint32_t depth()const{return depth_;}
 bool people_detached()const{return detached_;} // NOT camera detach/image-free authority
 Dispatch event_move(Cpu*,uint32_t actor);
 // Return true if handled; writes only r3 exactly like native getMyStaffId.
 // Other camera/timekeeper/retail names delegate. Matching owned names outside
 // the exact common-cut scope return -1, even if supplied at a different address.
 bool staff_id(Cpu*);
 // Restore only exact current owned People cut backups; partial failures retain
 // image references and quarantine. Caller separately detaches camera and uses
 // the authoritative collector/lease release validation before freeing anything.
 bool detach_people(Cpu*);
};
}
