// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_lease.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
namespace story_native_event_observation {struct Expected;}

namespace story_native_manager_terminal {
// f_02542C9C clears these actor_status bits at actor+0x2E0.
inline constexpr uint32_t kAllOffMask=0x9800;
inline constexpr uint32_t kClose=4, kNone=0;
struct Actor {
 uint32_t address=0,pid=0,token=0,profile=0,subtype=0,status=0;
 std::array<uint8_t,32> fingerprint{};
 friend bool operator==(const Actor&,const Actor&)=default;
};
struct Identity {
 story_native_event_lease::Identity lease{};
 uint32_t event=0;
 int32_t index=-1;
 std::array<Actor,2> actors{}; // baseline status bits must be zero
 friend bool operator==(const Identity&,const Identity&)=default;
};
// Opaque proof that this exact Terminal instance completed its scoped native
// close. The serial is host-only ordering, not a persisted guest generation or
// authority to detach/free the image. Only Terminal can construct a valid one.
class Completion {
 Identity identity_{};
 uint64_t serial_=0;
 Completion(Identity identity,uint64_t serial):identity_(identity),serial_(serial){}
 friend class Terminal;
public:
 Completion()=default;
 bool valid()const{return serial_!=0;}
 const story_native_event_lease::Identity& lease()const{return identity_.lease;}
 uint64_t serial()const{return serial_;}
 friend bool operator==(const Completion&,const Completion&)=default;
};
struct Observation {
 story_native_event_lease::Identity lease{};
 uint32_t event=0,event_state=0,camera_play=0;
 int32_t index=-1;
 std::array<Actor,2> actors{};
 bool retained_allocation=false,entry_fence=false,idle_depths=false;
 bool manager_table_owned=false,foreign_takeover=false;
};
using Probe=std::function<bool(Observation&)>;
using ActorProbe=std::function<bool(uint32_t,Actor&)>;
// Must call the exact native endProc(manager,index,force=0) synchronously on
// this thread, with the composed allOff hook forwarding to route_all_off.
using Invoke=std::function<void(uint32_t,int32_t,uint32_t)>;
using OriginalCallback=std::function<uint32_t()>;
enum class Result {Denied,Completed,Quarantined};

// Dormant composition helper. A real adapter must independently authenticate
// the retained allocation, entry fence, manager table, and actor PID/token
// probes. This object does not install hooks, publish an event, detach camera,
// or authorize freeing the image. One instance belongs to one exact lease.
class Terminal {
 Identity expected_;
 Probe probe_;
 ActorProbe actor_probe_;
 Invoke invoke_;
 std::atomic<bool> running_{false},quarantined_{false},completed_{false};
 uint64_t completion_serial_=0; // published by completed_'s release store
 std::array<bool,2> seen_{};
 std::array<uint32_t,2> pre_status_{};
 bool bad_callback_=false;
 static thread_local Terminal* scope_;
 bool valid(const Observation&,uint32_t state)const;
 static bool same_actor(const Actor&,const Actor&);
public:
 Terminal(Identity,Probe,ActorProbe,Invoke);
 Result close();
 // Read-only attestation for the dedicated ABI capability. A caller flag or
 // completed receipt cannot substitute for this exact synchronous TLS scope.
 bool native_close_scope()const noexcept{
  return scope_==this&&running_.load(std::memory_order_acquire)&&
   !quarantined_.load(std::memory_order_acquire)&&!completed_.load(std::memory_order_acquire);
 }
 // Outside this thread's exact synchronous scope, delegate unchanged.
 // Inside it, suppress foreign actors and invoke the native body only for a
 // freshly revalidated owned actor. Its native return is zero.
 uint32_t route_all_off(uint32_t actor,const OriginalCallback& original);
 bool completed()const{return completed_.load(std::memory_order_acquire)&&!quarantined();}
 Completion completion()const;
 // Completed terminal receipt may bind only to the exact retained first event
 // and two immutable resident declarations. No camera or image-free authority.
 bool matches(const story_native_event_observation::Expected&)const;
 bool quarantined()const{return quarantined_.load(std::memory_order_acquire);}
};
}
