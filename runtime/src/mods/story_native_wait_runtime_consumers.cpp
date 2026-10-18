// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_runtime_consumers.h"
#include "guest_addr.h"
#include <algorithm>
namespace story_native_wait_runtime_consumers {namespace {
namespace P=story_native_people_pose_capture;
namespace C=story_native_camera_capture;
const P::Layout People{GD(0x101C4CD0),GD(0x100209C4)};
const C::Layout Camera{GD(0x1046F0B0),GD(0x101D55F0),GD(0x101D5634),GD(0x101D5538),
 {GC(0x025021A4),GC(0x02502288),GC(0x024FFA3C),GC(0x0250235C),GC(0x024FFC40)},
 {GD(0x10049348),GD(0x10049350),GD(0x10044878),GD(0x1004487C)}};
bool boundary(const story_native_camera_restore::Boundary&a,const story_native_camera_restore::Boundary&b){
 return a.identity==b.identity&&a.logic_step==b.logic_step&&a.serial==b.serial&&
 a.exclusive&&b.exclusive&&a.fenced&&b.fenced&&!a.foreign&&!b.foreign&&!a.fault&&!b.fault&&
 a.manager_owned&&b.manager_owned&&!a.manager_depth&&!b.manager_depth&&!a.camera_depth&&!b.camera_depth&&
 !a.npc_depth&&!b.npc_depth&&!a.callback_depth&&!b.callback_depth&&!a.foreign_depth&&!b.foreign_depth&&
 a.control_mode==3&&b.control_mode==3&&a.partner1==~0u&&b.partner1==~0u&&
 a.partner2==~0u&&b.partner2==~0u&&a.talk_partner==~0u&&b.talk_partner==~0u&&
 a.item_partner==~0u&&b.item_partner==~0u&&a.event_id==-1&&b.event_id==-1;
}
void put(std::span<uint8_t>b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
}
Adapter::Adapter(Expected e,Poses p,Sources s):expected_(std::move(e)),baseline_(std::move(p)),sources_(std::move(s)){
 if(!sources_.read||!sources_.write||!sources_.retained||!sources_.resident||!sources_.boundary||!sources_.quarantine)return;
 for(unsigned n=0;n<2;++n){const auto&a=expected_.actors[n];const auto&v=baseline_[n];
  if(v.identity.actor!=a.address||v.identity.pid!=a.pid||v.identity.token!=a.token||
     v.identity.stage!=expected_.identity.stage||v.identity.room!=expected_.identity.room||
     v.identity.generation!=expected_.identity.generation||!v.identity.morf||!v.identity.model||v.cut!=a.cut)return;
 }
 configured_=true;
}
bool Adapter::retained()const{return configured_&&sources_.retained(expected_);}
bool Adapter::person(const story_native_people_pose_plan::Snapshot&p,const story_native_event_observation::Actor&a)const{
 if(!retained())return false;
 for(unsigned n=0;n<2;++n)if(a==expected_.actors[n])return p.identity==baseline_[n].identity&&sources_.resident(a,expected_.identity);
 return false;
}
bool Adapter::capture(W::State&out)const{
 out={};if(!configured_)return false;
 try{
  W::State value;story_native_camera_restore::Boundary after;
  if(!retained()||!sources_.boundary(value.boundary)||value.boundary.identity!=expected_.identity||!boundary(value.boundary,value.boundary))return false;
  for(unsigned n=0;n<2;++n){
   // Select independently for each actor, permitting a completed first cut
   // restoration while the other actor still retains its exact owned consumer.
   std::array<uint8_t,0x6C> cut{};
   if(!sources_.read(expected_.actors[n].address+0x3E0,cut))return false;
   auto mode=cut==expected_.actors[n].cut?P::Mode::Baseline:P::Mode::Owned;
   if(mode==P::Mode::Owned&&expected_.program&&expected_.program->kind()==story_native_event_program::Kind::TurnCamera){std::array<uint8_t,4>state{};if(!sources_.read(expected_.identity.image+0xE4,state))return false;if(std::all_of(state.begin(),state.end(),[](uint8_t b){return b==0;}))mode=P::Mode::Exhausted;}
   if(!P::capture(sources_.read,sources_.resident,People,expected_,n,value.people[n],mode)||
      value.people[n].cut!=cut||!person(value.people[n],expected_.actors[n]))return false;
  }
  if(!C::capture(sources_.read,Camera,expected_,value.camera)||!retained()||
     !sources_.boundary(after)||!boundary(value.boundary,after))return false;
  // Both residents must remain current after the camera's two raw samples.
  for(unsigned n=0;n<2;++n)if(!person(value.people[n],expected_.actors[n]))return false;
  out=std::move(value);return true;
 }catch(...){return false;}
}
bool Adapter::write(uint32_t at,std::span<const uint8_t>bytes)const{
 if(!retained())return false;
 bool allowed=false;
 for(unsigned n=0;n<2;++n)if(at==expected_.actors[n].address+0x3E0&&bytes.size()==baseline_[n].cut.size()&&std::equal(bytes.begin(),bytes.end(),baseline_[n].cut.begin()))allowed=true;
 const auto&c=expected_.camera;
 auto match=[&](uint32_t offset,std::span<const uint8_t>b){if(at==c.address+offset&&bytes.size()==b.size()&&std::equal(bytes.begin(),bytes.end(),b.begin()))allowed=true;};
 auto scalar=[&](uint32_t offset,uint32_t v){std::array<uint8_t,4>b{};put(b,0,v);match(offset,b);};
 match(0x10,c.initial_view010);match(0x3C,c.view03C);scalar(0x68,c.bg);match(0xA4,c.positions0A4);match(0x100,c.engine100);match(0x13C,c.mode_phase13C);match(0x37C,c.work37C);match(0x3FC,c.event_data);scalar(0x510,c.flags);
 std::array<uint8_t,16>types{};put(types,0,c.style);std::copy(c.type_gate518.begin(),c.type_gate518.end(),types.begin()+4);put(types,8,c.current_type);put(types,12,c.next_type);match(0x514,types);
 scalar(0x600,c.trim);std::array<uint8_t,8>cache{};put(cache,0,c.param_pointer);put(cache,4,c.param_index);match(0x8A8,cache);
 if(!allowed)return false;
 // Independent actual capture immediately before raw mutation catches loss of
 // either actor PID/allocation, even for a patch targeting only the first one.
 W::State before;if(!capture(before))return false;
 return sources_.write(at,bytes)&&retained();
}
W::Source Adapter::source(){
 W::Source s;s.capture=[this](auto&v){return capture(v);};
 s.retained=[this](const auto&e){return e==expected_&&retained();};
 s.person=[this](const auto&p,const auto&a){return person(p,a);};
 s.camera_identity=[this](const auto&c,const auto&e){C::Camera actual;return e==expected_&&retained()&&C::capture(sources_.read,Camera,expected_,actual)&&actual==c;};
 s.camera=[this](const auto&c,const auto&i){return i==expected_.identity&&retained()&&story_native_camera_policy::verify(Camera.tables,sources_.read,c,i).valid;};
 s.write=[this](auto at,auto bytes){return write(at,bytes);};
 s.quarantine=[this]{sources_.quarantine();};return s;
}
}
