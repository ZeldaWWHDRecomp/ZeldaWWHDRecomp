// SPDX-License-Identifier: MPL-2.0
#include "story_native_cancel_pose_capture.h"
#include <algorithm>
namespace story_native_cancel_pose_capture {namespace {
namespace X=story_native_cancel_observation;
namespace P=story_native_people_pose_plan;
uint32_t word(std::span<const uint8_t>b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
void put(std::span<uint8_t>b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
}
static bool capture_impl(const story_native_people_pose_capture::Read&read,const story_native_people_pose_capture::VerifyActor&verify,const story_native_people_pose_capture::Layout&layout,const story_native_event_observation::Expected&e,unsigned n,const story_native_camera_cancel_barrier::Completion&receipt,const X::Result*r,Cpu*c,story_native_event_depth::Tracker&t,const story_native_outer_return::OuterReturn&outer,const threads::NativeQuiescence&q,P::Snapshot&out){
 out={};if(!read||!verify||n>=2||!receipt.matches(e)||!receipt.cancel().matches(e)||(r&&(!r->matches(e)||r->phase()!=X::Phase::Closed))||!c)return false;
 auto boundary=[&]{if(!q.current(c)||(r&&!r->current_binding(c,t,outer))||!outer.claimed_current(c,e,t)||outer.root_serial()!=receipt.after().outer_serial)return false;const auto d=t.snapshot();return d.idle()&&d.coverage&&d.state==story_native_event_depth::State::Draining&&d.serial>=(receipt.after().boundary.depth.serial)&&(r?d.serial==r->serial():true);};
 auto bounded=[&](uint32_t a,std::span<uint8_t>b){return boundary()&&read(a,b)&&boundary();};
 try{
  if(!boundary())return false;
  const auto&native=receipt.cancel().poststate();const auto&baseline=receipt.cancel().baseline()[n];
  std::array<uint8_t,0x540>manager{};std::array<uint8_t,0xE60>image{};std::array<uint8_t,0x920>actor{},again{};
  if(!bounded(e.identity.manager,manager)||manager!=native.manager||!bounded(e.identity.image,image)||image!=native.image||!bounded(e.actors[n].address,actor))return false;
  if(!r&&actor!=native.actors[n])return false;
  auto allowed=native.actors[n];P::Plan plan;if(!P::make(baseline,baseline.identity,true,true,true,true,plan))return false;
  for(const auto&p:plan.patches){const uint32_t offset=p.address-e.actors[n].address;if(uint64_t(offset)+p.bytes.size()>allowed.size())return false;const auto actual=std::span<const uint8_t>(actor).subspan(offset,p.bytes.size());const auto previous=std::span<const uint8_t>(native.actors[n]).subspan(offset,p.bytes.size());if(!std::equal(actual.begin(),actual.end(),previous.begin())&&!std::equal(actual.begin(),actual.end(),p.bytes.begin()))return false;std::copy(actual.begin(),actual.end(),allowed.begin()+offset);}
  const uint32_t status=word(actor,0x2E0),before=word(native.actors[n],0x2E0),merged=(before&~0x9800u)|(baseline.status&0x9800u);if(status!=before&&status!=merged)return false;put(allowed,0x2E0,status);if(allowed!=actor)return false;
  // Reuse the existing bounded Uo2 accessor/model/NpcDat double-sample
  // primitive. Its Baseline mode constrains only the cut byte equality; here
  // that exact cut was independently restricted to the two authentic receipt
  // segments above. No event state, flags or staff cursors are projected.
  auto constraint=e;std::copy_n(actor.begin()+0x3E0,0x6C,constraint.actors[n].cut.begin());
  auto resident=[&](const auto&a,const auto&i){auto original=a;original.cut=e.actors[n].cut;return original==e.actors[n]&&i==e.identity&&boundary()&&verify(e.actors[n],e.identity)&&boundary();};
  P::Snapshot sample;if(!story_native_people_pose_capture::capture(bounded,resident,layout,constraint,n,sample)||sample.identity!=baseline.identity||!bounded(e.actors[n].address,again)||again!=actor||!bounded(e.identity.manager,manager)||manager!=native.manager||!bounded(e.identity.image,image)||image!=native.image||!boundary())return false;
  out=sample;return true;
 }catch(...){return false;}
}
bool capture(const story_native_people_pose_capture::Read&read,const story_native_people_pose_capture::VerifyActor&verify,const story_native_people_pose_capture::Layout&layout,const story_native_event_observation::Expected&e,unsigned n,const X::Result&r,Cpu*c,story_native_event_depth::Tracker&t,const story_native_outer_return::OuterReturn&outer,const threads::NativeQuiescence&q,P::Snapshot&out){return capture_impl(read,verify,layout,e,n,r.returned(),&r,c,t,outer,q,out);}
bool capture_closed(const story_native_people_pose_capture::Read&read,const story_native_people_pose_capture::VerifyActor&verify,const story_native_people_pose_capture::Layout&layout,const story_native_event_observation::Expected&e,unsigned n,const story_native_camera_cancel_barrier::Completion&r,Cpu*c,story_native_event_depth::Tracker&t,const story_native_outer_return::OuterReturn&outer,const threads::NativeQuiescence&q,P::Snapshot&out){return capture_impl(read,verify,layout,e,n,r,nullptr,c,t,outer,q,out);}
}
