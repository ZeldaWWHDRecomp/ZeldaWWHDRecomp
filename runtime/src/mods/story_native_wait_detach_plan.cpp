// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_detach_plan.h"
#include <algorithm>
namespace story_native_wait_detach_plan {namespace {
bool overlap(uint32_t a,uint32_t na,uint32_t b,uint32_t nb){return uint64_t(a)<uint64_t(b)+nb&&uint64_t(b)<uint64_t(a)+na;}
bool mem2(uint32_t a,uint32_t n){return a>=0x10000000u&&uint64_t(a)+n<=0x50000000u;}
uint32_t word(std::span<const uint8_t>b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool patches(const P::Plan&p,uint32_t base){
 for(size_t i=0;i<p.patches.size();++i){const auto&a=p.patches[i];if(a.bytes.empty()||a.address<base||uint64_t(a.address)+a.bytes.size()>uint64_t(base)+0x920)return false;
  for(size_t j=0;j<i;++j){const auto&b=p.patches[j];if(overlap(a.address,uint32_t(a.bytes.size()),b.address,uint32_t(b.bytes.size())))return false;}}
 return true;
}
}
Plan prepare(const story_native_event_observation::Expected&e,
 const std::array<P::Snapshot,2>&baseline,const std::array<P::Snapshot,2>&live_people,
 const story_native_event_observation::Camera&live,const C::Boundary&boundary,
 C::VerifyCamera verify,bool exact_uo2,bool idle,bool detached,bool cancel){
 Plan plan;
 if(!mem2(e.actors[0].address,0x920)||!mem2(e.actors[1].address,0x920)||!mem2(e.camera.address,0x8B8)||
    overlap(e.actors[0].address,0x920,e.actors[1].address,0x920)||
    overlap(e.actors[0].address,0x920,e.camera.address,0x8B8)||
    overlap(e.actors[1].address,0x920,e.camera.address,0x8B8))return plan;
 for(unsigned n=0;n<2;++n){const auto&a=e.actors[n];const auto&i=baseline[n].identity;
  if(a.profile!=0x175||a.subtype!=1||i.actor!=a.address||i.pid!=a.pid||i.token!=a.token||
     i.stage!=e.identity.stage||i.room!=e.identity.room||i.generation!=e.identity.generation||
     baseline[n].cut!=a.cut||live_people[n].identity!=i||
     word(live_people[n].cut,0)!=e.identity.image+0xF0+n*0x50||
     word(live_people[n].cut,4)!=n||word(live_people[n].cut,8)!=a.address||
     (word(live_people[n].cut,0x10)!=0&&word(live_people[n].cut,0x10)!=0xFFFFFFFFu)||
     !P::make(baseline[n],live_people[n].identity,exact_uo2,idle,detached,cancel,plan.people_[n])||
     !patches(plan.people_[n],a.address))return Plan{};
 }
 plan.camera_=C::prepare(e,live,boundary,verify);if(!plan.camera_.valid())return Plan{};
 plan.baseline_=baseline;plan.cancel_=cancel;plan.expected_=e;plan.before_people_=live_people;plan.before_camera_=live;plan.boundary_=boundary;plan.valid_=true;return plan;
}
bool Plan::verify_pre(const story_native_event_observation::Expected&e,
 const std::array<P::Snapshot,2>&baseline,const std::array<P::Snapshot,2>&fresh,
 const story_native_event_observation::Camera&camera,const C::Boundary&b,C::VerifyCamera verify)const{
 auto same=[](const P::Snapshot&a,const P::Snapshot&c){return a.identity==c.identity&&a.cut==c.cut&&a.angles==c.angles&&a.shape==c.shape&&a.joint==c.joint&&a.target==c.target&&a.velocity==c.velocity&&a.head_only==c.head_only&&a.noticed==c.noticed&&a.talk_ready==c.talk_ready&&a.target_mode==c.target_mode&&a.status==c.status;};
 if(!valid_||e!=expected_||camera!=before_camera_||b.logic_step!=boundary_.logic_step||b.serial!=boundary_.serial)return false;
 for(unsigned n=0;n<2;++n)if(!same(baseline[n],baseline_[n])||!same(fresh[n],before_people_[n]))return false;
 // Revalidate the actual supplied boundary, identities, source policy and native
 // transitions, rather than accepting an old plan as current authority.
 return prepare(e,baseline,fresh,camera,b,verify,true,true,true,cancel_).valid();
}
bool Plan::verify_post(const std::array<P::Snapshot,2>&fresh,
 const story_native_event_observation::Camera&camera,const C::Boundary&boundary,
 C::VerifyCamera verify)const{
 if(!valid_||!camera_.verify_post(camera,boundary,verify))return false;
 for(unsigned n=0;n<2;++n){const auto&b=baseline_[n],&c=fresh[n];
  if(c.identity!=b.identity||c.cut!=b.cut)return false;
  if(cancel_&&(c.angles!=b.angles||c.shape!=b.shape||c.joint!=b.joint||
    c.target!=b.target||c.velocity!=b.velocity||c.head_only!=b.head_only||
    c.noticed!=b.noticed||c.talk_ready!=b.talk_ready||c.target_mode!=b.target_mode))return false;
 }
 return true;
}
}
