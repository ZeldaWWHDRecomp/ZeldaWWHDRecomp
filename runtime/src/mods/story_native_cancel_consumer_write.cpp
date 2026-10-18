// SPDX-License-Identifier: MPL-2.0
#include "story_native_cancel_consumer_write.h"
#include <algorithm>
namespace story_native_cancel_consumer_write {namespace {
using Camera=story_native_event_observation::Camera;
uint32_t word(const auto&b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool pose(const P::Snapshot&a,const P::Snapshot&b){return a.identity==b.identity&&a.angles==b.angles&&a.shape==b.shape&&a.joint==b.joint&&a.cut==b.cut&&a.target==b.target&&a.velocity==b.velocity&&a.head_only==b.head_only&&a.noticed==b.noticed&&a.talk_ready==b.talk_ready&&a.target_mode==b.target_mode&&a.status==b.status;}
P::Snapshot native_pose(const P::Snapshot&baseline,const std::array<uint8_t,0x920>&raw){
 auto s=baseline;auto copy=[&](unsigned p,auto&out){std::copy_n(raw.begin()+p,out.size(),out.begin());};
 copy(0x320,s.angles);copy(0x328,s.shape);copy(0x3AC,s.joint);copy(0x3E0,s.cut);copy(0x884,s.target);copy(0x8F8,s.velocity);
 s.head_only=raw[0x8D8];s.noticed=raw[0x8FD];s.talk_ready=raw[0x8FF];s.target_mode=raw[0x90D];s.status=word(raw,0x2E0);return s;
}
bool boundary(const C::Boundary&a,const C::Boundary&b){return a.identity==b.identity&&a.logic_step==b.logic_step&&a.serial==b.serial&&a.exclusive&&a.fenced&&!a.foreign&&!a.fault&&a.manager_owned&&!a.manager_depth&&!a.camera_depth&&!a.npc_depth&&!a.callback_depth&&!a.foreign_depth&&a.control_mode==3&&a.partner1==~0u&&a.partner2==~0u&&a.talk_partner==~0u&&a.item_partner==~0u&&a.event_id==-1;}
void project(P::Snapshot&s,const P::Snapshot&b,unsigned offset){switch(offset){
 case 0x3E0:s.cut=b.cut;break;case 0x320:s.angles=b.angles;break;case 0x328:s.shape=b.shape;break;case 0x3AC:s.joint=b.joint;break;case 0x884:s.target=b.target;break;case 0x8F8:s.velocity=b.velocity;break;case 0x8D8:s.head_only=b.head_only;break;case 0x8FD:s.noticed=b.noticed;break;case 0x8FF:s.talk_ready=b.talk_ready;break;case 0x90D:s.target_mode=b.target_mode;break;}}
}
void Writer::quarantine(const Sources&s)noexcept{if(quarantined_)return;quarantined_=true;try{s.quarantine();}catch(...){}}
Result Writer::apply(Cpu*cpu,story_native_event_depth::Tracker&t,const story_native_outer_return::OuterReturn&outer,const threads::NativeQuiescence&q,const Expected&e,const X::Result&closed,const Sources&s){
 if(!cpu||q.read_only()||!q.current(cpu)||!outer.claimed_current(cpu,e,t)||!closed.current_binding(cpu,t,outer))return Result::Refused;
 if(busy_.exchange(true))return Result::Refused;
 struct Done{std::atomic<bool>&busy;~Done(){busy=false;}}done{busy_};
 if(quarantined_)return Result::Quarantined;
 if(attempted_||complete_||!cpu||!closed.matches(e)||closed.phase()!=X::Phase::Closed||!s.capture||!s.retained||!s.camera||!s.write||!s.quarantine)return Result::Refused;
 auto fail=[&]{if(attempted_){quarantine(s);return Result::Quarantined;}return Result::Refused;};
 try{
  const auto&baseline=closed.returned().cancel().baseline();
  auto live=[&]{auto d=t.snapshot();return !q.read_only()&&q.current(cpu)&&closed.current_binding(cpu,t,outer)&&outer.claimed_current(cpu,e,t)&&outer.root_serial()==closed.returned().after().outer_serial&&d.idle()&&d.coverage&&!d.fault&&d.state==story_native_event_depth::State::Draining&&d.serial==closed.serial()&&s.retained(e);};
  if(!live())return fail();
  State projected;projected.boundary=closed.boundary();projected.camera=closed.returned().camera();
  for(unsigned n=0;n<2;++n)projected.people[n]=native_pose(baseline[n],closed.returned().cancel().poststate().actors[n]);
  auto capture=[&](State&v){return live()&&s.capture(v)&&live()&&boundary(v.boundary,projected.boundary)&&v.camera==projected.camera&&pose(v.people[0],projected.people[0])&&pose(v.people[1],projected.people[1]);};
  State first;if(!capture(first))return fail();
  std::array<P::Plan,2>plans;
  constexpr std::array<uint32_t,10>offsets{0x3E0,0x320,0x328,0x3AC,0x884,0x8F8,0x8D8,0x8FD,0x8FF,0x90D};
  constexpr std::array<size_t,10>sizes{0x6C,6,6,0x34,12,2,1,1,1,1};
  for(unsigned n=0;n<2;++n){if(!P::make(baseline[n],first.people[n].identity,true,true,true,true,plans[n])||!plans[n].refresh_base||!plans[n].refresh_joints||plans[n].patches.size()!=10)return fail();for(unsigned i=0;i<10;++i)if(plans[n].patches[i].address!=e.actors[n].address+offsets[i]||plans[n].patches[i].bytes.size()!=sizes[i])return fail();}
  auto camera=C::prepare(e,first.camera,first.boundary,s.camera);if(!camera.valid()||camera.segments().size()!=12)return fail();
  State second;if(!capture(second))return fail(); // both fresh PIDs before first write
  auto write=[&](uint32_t at,std::span<const uint8_t>bytes){State now;if(!capture(now))return false;attempted_=true;return s.write(at,bytes);};
  for(unsigned n=0;n<2;++n){for(unsigned i=0;i<10;++i){const auto&w=plans[n].patches[i];if(!write(w.address,w.bytes))return fail();project(projected.people[n],baseline[n],offsets[i]);}
   const uint32_t merged=(projected.people[n].status&~0x9800u)|(baseline[n].status&0x9800u);std::array<uint8_t,4>bytes{};for(unsigned i=0;i<4;++i)bytes[i]=uint8_t(merged>>(24-8*i));if(!write(e.actors[n].address+0x2E0,bytes))return fail();projected.people[n].status=merged;
  }
  for(const auto&w:camera.segments()){
   if(!write(e.camera.address+w.offset,std::span(w.data).first(w.bytes)))return fail();
   auto&c=projected.camera;const auto&b=e.camera;
   switch(w.offset){
    case 0x10:c.initial_view010=b.initial_view010;break;
    case 0x3C:c.view03C=b.view03C;break;
    case 0x68:c.bg=b.bg;break;
    case 0xA4:c.positions0A4=b.positions0A4;break;
    case 0x100:c.engine100=b.engine100;c.done100=b.done100;c.counter108=b.counter108;c.counter118=b.counter118;c.elapsed11C=b.elapsed11C;break;
    case 0x13C:c.mode_phase13C=b.mode_phase13C;break;
    case 0x37C:c.work37C=b.work37C;c.timer=b.timer;break;
    case 0x3FC:c.event_data=b.event_data;break;
    case 0x510:c.flags=b.flags;break;
    case 0x514:c.style=b.style;c.type_gate518=b.type_gate518;c.current_type=b.current_type;c.next_type=b.next_type;break;
    case 0x600:c.trim=b.trim;break;
    case 0x8A8:c.param_pointer=b.param_pointer;c.param_index=b.param_index;break;
    default:return fail();
   }
  }
  State post;if(!capture(post)||!camera.verify_post(post.camera,post.boundary,s.camera))return fail();
  for(unsigned n=0;n<2;++n){auto wanted=baseline[n];wanted.status=projected.people[n].status;if(!pose(post.people[n],wanted))return fail();}
  complete_=true;return Result::Complete;
 }catch(...){return fail();}
}
}
