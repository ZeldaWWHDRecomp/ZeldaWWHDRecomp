// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_consumer_write.h"
#include <algorithm>
namespace story_native_wait_consumer_write {namespace {
bool boundary(const C::Boundary&a,const C::Boundary&b){
 return a.identity==b.identity&&a.logic_step==b.logic_step&&a.serial==b.serial&&
 a.exclusive&&a.fenced&&!a.foreign&&!a.fault&&a.manager_owned&&
 !a.manager_depth&&!a.camera_depth&&!a.npc_depth&&!a.callback_depth&&!a.foreign_depth&&
 a.control_mode==3&&a.partner1==~0u&&a.partner2==~0u&&a.talk_partner==~0u&&a.item_partner==~0u&&a.event_id==-1;
}
bool pose(const P::Snapshot&a,const P::Snapshot&b){return a.identity==b.identity&&a.angles==b.angles&&a.shape==b.shape&&a.joint==b.joint&&a.target==b.target&&a.velocity==b.velocity&&a.head_only==b.head_only&&a.noticed==b.noticed&&a.talk_ready==b.talk_ready&&a.target_mode==b.target_mode&&a.status==b.status;}
}
void Writer::quarantine(const Source&s)noexcept{
 if(quarantined_)return;
 quarantined_=true;
 try{if(s.quarantine)s.quarantine();}catch(...){} // sticky even if notification fails
}
Result Writer::apply(const D::Plan&p,const Expected&e,const std::array<P::Snapshot,2>&baseline,const Source&s){
 if(quarantined_)return Result::Quarantined;
 if(attempted_||complete_||!p.normal()||!s.capture||!s.retained||!s.person||!s.camera_identity||!s.camera||!s.write||!s.quarantine)return Result::Refused;
 auto fail=[&](){if(attempted_){quarantine(s);return Result::Quarantined;}return Result::Refused;};
 try{
  State first;
  std::array<P::Snapshot,2> projected_people{};Camera projected_camera{};bool projecting=false;
  auto capture=[&](State&v){return s.capture(v)&&(!projecting||(v.people[0].cut==projected_people[0].cut&&v.people[1].cut==projected_people[1].cut&&pose(v.people[0],projected_people[0])&&pose(v.people[1],projected_people[1])&&v.camera==projected_camera))&&s.retained(e)&&boundary(v.boundary,first.boundary)&&v.boundary.identity==e.identity&&s.person(v.people[0],e.actors[0])&&s.person(v.people[1],e.actors[1])&&v.people[0].identity==first.people[0].identity&&v.people[1].identity==first.people[1].identity&&v.camera.address==e.camera.address&&v.camera.pid==e.camera.pid&&s.camera_identity(v.camera,e);};
  if(!s.capture(first)||!p.verify_pre(e,baseline,first.people,first.camera,first.boundary,s.camera))return fail();
  State checked;if(!capture(checked)||!p.verify_pre(e,baseline,checked.people,checked.camera,checked.boundary,s.camera))return fail();
  // Build an independent normal plan from the captured Expected baseline and
  // actual sources; match every payload and exact fixed whitelist before write.
  auto independent=D::prepare(e,baseline,first.people,first.camera,first.boundary,s.camera,true,true,true,false);
  if(!independent.valid())return fail();
  for(unsigned n=0;n<2;++n){const auto&v=p.people()[n];if(v.refresh_base||v.refresh_joints||v.patches.size()!=1)return fail();const auto&w=v.patches[0];if(w.address!=e.actors[n].address+0x3E0||w.bytes.size()!=0x6C||!std::equal(w.bytes.begin(),w.bytes.end(),baseline[n].cut.begin()))return fail();}
  constexpr std::array<uint32_t,12> offsets{0x10,0x3C,0x68,0xA4,0x100,0x13C,0x37C,0x3FC,0x510,0x514,0x600,0x8A8};
  constexpr std::array<uint32_t,12> lengths{0x2C,0x28,4,0x40,0x20,0x14,0x80,0x114,4,0x10,4,8};
  auto segments=p.camera().segments(),reference=independent.camera().segments();
  if(p.camera().camera()!=e.camera.address||segments.size()!=12||reference.size()!=12)return fail();
  for(unsigned n=0;n<12;++n){const auto&a=segments[n],&b=reference[n];if(a.offset!=offsets[n]||a.bytes!=lengths[n]||a.offset!=b.offset||a.bytes!=b.bytes||!std::equal(a.data.begin(),a.data.begin()+a.bytes,b.data.begin()))return fail();}
  projected_people=first.people;projected_camera=first.camera;projecting=true;
  auto write=[&](uint32_t address,std::span<const uint8_t>bytes){State now;if(!capture(now))return false;attempted_=true;return s.write(address,bytes);};
  for(unsigned n=0;n<2;++n){const auto&w=p.people()[n].patches[0];if(!write(w.address,w.bytes))return fail();projected_people[n].cut=baseline[n].cut;}
  for(const auto&w:segments){
   if(!write(e.camera.address+w.offset,std::span(w.data).first(w.bytes)))return fail();
   // Project only the whitelisted fields changed by this completed write. The
   // next actual capture must equal this full typed state, including untouched
   // tables/identity/cache metadata. This catches takeover between segments.
   auto&c=projected_camera;const auto&b=e.camera;
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
  State post;if(!capture(post)||!p.verify_post(post.people,post.camera,post.boundary,s.camera)||!pose(first.people[0],post.people[0])||!pose(first.people[1],post.people[1]))return fail();
  complete_=true;return Result::Complete;
 }catch(...){return fail();}
}
}
