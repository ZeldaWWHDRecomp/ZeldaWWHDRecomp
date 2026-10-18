// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_install.h"
#include "runtime.h"
#include "guest_addr.h"
#include "story_native_stage_owner.h"
namespace story_native_event_install {namespace {
const uint32_t EndControl=GC(0x0253EFA8),ClearBase=GC(0x0254273C);
struct CpuGuard {Cpu*c;Cpu saved;explicit CpuGuard(Cpu*p):c(p),saved(*p){}~CpuGuard(){*c=saved;}};
bool identity(const Expected&e,const Captured&c){const auto&a=e.identity;const auto&b=c.identity;return a==b&&e.stage_name==c.stage_name&&e.room_number==c.room_number&&c.resource_generation_matches&&!c.foreign&&!c.manager_depth&&!c.camera_depth&&!c.npc_depth;}
}

namespace {
bool cancellation_current(Cpu*c,story_native_event_depth::Tracker&depth,const story_native_outer_return::OuterReturn&outer,const Expected&e,const story_native_cancel_observation::Result&r){
 const auto d=depth.snapshot();
 return c&&r.matches(e)&&r.current_binding(c,depth,outer)&&outer.claimed_current(c,e,depth)&&d.coverage&&d.idle()&&d.state==story_native_event_depth::State::Draining&&d.serial==r.serial();
}
}
Status Install::begin_cancel(Cpu*c,story_native_event_depth::Tracker&depth,const story_native_outer_return::OuterReturn&outer,const story_native_cancel_observation::Result&r){
 namespace C=story_native_cancel_observation;
 try{
  if((status_!=Status::Started&&status_!=Status::Running)||cancel_closed_||cancel_detached_||r.phase()!=C::Phase::Closed||!cancellation_current(c,depth,outer,expected_,r)||!backend_.scope(expected_)||!lease_.finish({expected_.identity,false,false}))throw false;
  cancel_closed_=r;return status_=Status::Draining;
 }catch(...){lease_.invalidate();return status_=Status::Quarantined;}
}
Status Install::release_cancel_native(Cpu*c,story_native_event_depth::Tracker&depth,const story_native_outer_return::OuterReturn&outer,const story_native_cancel_observation::Result&r){
 namespace C=story_native_cancel_observation;
 if(!c){lease_.invalidate();return status_=Status::Quarantined;}
 CpuGuard cpu(c);try{
  if(threads::current_thread_holds_native_quiescence()||story_native_stage_owner::current_thread_holds_lifetime_pin()||status_!=Status::Draining||!cancel_closed_||cancel_detached_||r.phase()!=C::Phase::Detached||!r.same_cancellation(*cancel_closed_)||!cancellation_current(c,depth,outer,expected_,r)||!backend_.scope(expected_))throw false;
  // Consume before native execution; exceptions and partial writes cannot be
  // replayed. The caller releases physical parking and Stage pins first.
  cancel_detached_=r;
  if(!backend_.invoke(c,EndControl,{control()},{}))throw false;
  Captured o;if(!capture(c,o,true)||!identity(expected_,o)||!outer.claimed_current(c,expected_,depth))throw false;
  backend_.invoke(c,ClearBase,{expected_.identity.manager},{});
  if(!restore_manager()||!outer.claimed_current(c,expected_,depth)||!backend_.scope(expected_))throw false;
  return status_;
 }catch(...){lease_.invalidate();return status_=Status::Quarantined;}
}
Status Install::retire_cancel(Cpu*c,story_native_event_depth::Tracker&depth,const story_native_outer_return::OuterReturn&outer,const story_native_cancel_observation::Result&r){
 namespace C=story_native_cancel_observation;
 try{
  if(status_!=Status::Draining||!cancel_closed_||!cancel_detached_||r.phase()!=C::Phase::Released||!r.same_cancellation(*cancel_detached_)||!cancellation_current(c,depth,outer,expected_,r)||!backend_.scope(expected_)||!lease_.retire({expected_.identity,false,true})||!lease_.can_free(r))throw false;
  return status_=Status::Released;
 }catch(...){lease_.invalidate();return status_=Status::Quarantined;}
}

}
