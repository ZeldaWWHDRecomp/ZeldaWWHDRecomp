// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_backend.h"
#include "story_native_camera_policy.h"
#include "story_native_event_end_guard.h"
#include "guest_addr.h"
#include <utility>
namespace story_native_event_backend {namespace {
const story_native_camera_policy::Tables Tables{GD(0x10049348),GD(0x10049350),GD(0x10044878),GD(0x1004487C)};
uint32_t word(std::span<const uint8_t>b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
int32_t signed_half(std::span<const uint8_t>b,unsigned p){uint32_t v=uint32_t(b[p])<<8|b[p+1];return v&0x8000?int32_t(v)-65536:int32_t(v);}
}
Scaffold::Scaffold(Expected e,Config c,Sources s,story_native_event_depth::Tracker&d):expected_(std::move(e)),config_(c),sources_(std::move(s)),depth_(d){}
Report Scaffold::preflight(Operation op){
 Report report;auto fail=[&](Reason r){report.preflight=false;report.observation={};report.camera={};report.reason=r;return report;};
 if(busy_||!sources_.read||!sources_.session||!sources_.allocation||!sources_.actor||config_.memory.stamp!=config_.collector.stamp)return fail(Reason::Configuration);
 if(op!=Operation::Prepare&&op!=Operation::Running)return fail(Reason::Disabled);
 busy_=true;struct Reset{bool&b;~Reset(){b=false;}}reset{busy_};
 try{
  const auto before=depth_.snapshot();if(!depth_.ready()||!before.coverage||!before.idle()||before.state!=story_native_event_depth::State::Active)return fail(Reason::Depth);
  story_native_event_memory::Scope initial;
  if(!sources_.session(initial)||initial.identity!=expected_.identity||!initial.exclusive||!initial.bindings_verified||initial.foreign||initial.fault||initial.fenced||initial.manager_depth||initial.camera_depth||initial.npc_depth||initial.callback_depth||initial.foreign_depth||initial.serial!=before.serial)return fail(Reason::Session);
  auto query=[&](story_native_event_memory::Scope&s){if(!sources_.session(s))return false;auto d=depth_.snapshot();if(!d.coverage||!d.idle()||d.state!=story_native_event_depth::State::Active||d.serial!=before.serial)return false;return s.serial==d.serial&&s.logic_step==initial.logic_step&&!s.fenced;};
  // No external writer is supplied or reachable, even if later code accidentally
  // asks the memory capability to transition into a mutation phase.
  story_native_event_memory::Policy memory(expected_,config_.memory,query,sources_.allocation,sources_.read,[](uint32_t,std::span<const uint8_t>){return false;});
  auto read=[&](uint32_t a,std::span<uint8_t>b){return memory.read(a,b);};
  auto camera=[&](const Camera&c,const auto&i){return story_native_camera_policy::verify(Tables,read,c,i).valid;};
  auto allocation=[&](uint32_t image,uint32_t bytes,uint32_t stamp,uint32_t generation){return image==expected_.identity.image&&bytes==expected_.identity.bytes&&stamp==config_.memory.stamp&&generation==expected_.identity.generation&&sources_.allocation(expected_.identity,config_.memory);};
  auto guards=[&](story_native_event_collect::Guards&g){story_native_event_memory::Scope s;if(!query(s))return false;g.exclusive=s.exclusive&&s.bindings_verified;g.foreign=s.foreign||s.fault;g.manager_depth=s.manager_depth;g.camera_depth=s.camera_depth;g.npc_depth=s.npc_depth;return !s.callback_depth&&!s.foreign_depth;};
  story_native_event_collect::Collector collector(expected_,config_.collector,read,sources_.actor,allocation,camera,guards);
  Captured c;if(!collector.collect(c)){report.collector_error=collector.error();return fail(Reason::Collection);}
  report.observation=validate(op,expected_,c);if(!report.observation.valid())return fail(Reason::Observation);
  std::array<uint8_t,0xF4>control{};if(!read(expected_.identity.manager-0xF4,control))return fail(Reason::Control);
  const auto partner1=word(control,0xC4),partner2=word(control,0xC8),talk=word(control,0xCC),item=word(control,0xD0);
  if(signed_half(control,0xD8)!=-1)return fail(Reason::Control);
  if(op==Operation::Running){
   if(partner1!=0xFFFFFFFFu||partner2!=0xFFFFFFFFu||talk!=0xFFFFFFFFu||item!=0xFFFFFFFFu)return fail(Reason::Control);
   using namespace story_native_event_end_guard;
   story_native_event_end_guard::Identity identity{expected_.identity.manager,expected_.identity.image,expected_.identity.bytes,expected_.identity.generation,expected_.identity.program,0,expected_.identity.stage,expected_.identity.room};
   story_native_event_end_guard::Observation observation;observation.identity=identity;observation.table=c.manager[1];observation.event=c.current_event;observation.mode=control[0xC2];observation.partner1=partner1;observation.partner2=partner2;observation.talk_partner=talk;observation.item_partner=item;observation.control_event_id=signed_half(control,0xD8);observation.retained_claim=observation.identity_valid=true;
   Guard end(identity);if(end.before_control(observation)!=Decision::Queue)return fail(Reason::Control);
  // Passive retail partner caches are valid only before native entry. The
  // running branch above still requires our exact null-partner mode3 claim.
  }else if(control[0xC0]||control[0xC2])return fail(Reason::Control);
  const auto after=depth_.snapshot();story_native_event_memory::Scope final;if(!query(final)||after.serial!=before.serial||!after.idle()||final.identity!=initial.identity||final.link!=initial.link||final.link_pid!=initial.link_pid)return fail(Reason::Changed);
  report.camera=c.camera;report.serial=after.serial;report.preflight=true;report.reason=Reason::None;return report;
 }catch(...){return fail(Reason::Collection);}
}
}
