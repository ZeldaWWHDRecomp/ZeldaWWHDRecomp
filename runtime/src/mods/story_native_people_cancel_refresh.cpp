// SPDX-License-Identifier: MPL-2.0
#include "story_native_people_cancel_refresh.h"
#include "guest_addr.h"
#include "ppc.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace story_native_people_cancel_refresh {namespace {
bool pose(const P::Snapshot&a,const P::Snapshot&b){return a.identity==b.identity&&a.angles==b.angles&&a.shape==b.shape&&a.joint==b.joint&&a.cut==b.cut&&a.target==b.target&&a.velocity==b.velocity&&a.head_only==b.head_only&&a.noticed==b.noticed&&a.talk_ready==b.talk_ready&&a.target_mode==b.target_mode&&a.status==b.status;}
bool boundary(const C::Boundary&a,const C::Boundary&b){return a.identity==b.identity&&a.exclusive==b.exclusive&&a.fenced==b.fenced&&a.foreign==b.foreign&&a.fault==b.fault&&a.manager_owned==b.manager_owned&&a.manager_depth==b.manager_depth&&a.camera_depth==b.camera_depth&&a.npc_depth==b.npc_depth&&a.callback_depth==b.callback_depth&&a.foreign_depth==b.foreign_depth&&a.control_mode==b.control_mode&&a.partner1==b.partner1&&a.partner2==b.partner2&&a.talk_partner==b.talk_partner&&a.item_partner==b.item_partner&&a.event_id==b.event_id&&a.logic_step==b.logic_step&&a.serial==b.serial;}
bool finite(std::span<const uint8_t>b){for(unsigned p=0;p<b.size();p+=4){uint32_t bits=uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];float f;std::memcpy(&f,&bits,4);if(!std::isfinite(f))return false;}return true;}
bool range(I::Range r){return r.address&&r.bytes&&uint64_t(r.address)+r.bytes<=0x100000000ull;}
bool overlap(I::Range a,I::Range b){return uint64_t(a.address)<uint64_t(b.address)+b.bytes&&uint64_t(b.address)<uint64_t(a.address)+a.bytes;}
}
bool Completion::matches(const Expected&e,const std::array<P::Snapshot,2>&p,const C::Boundary&b)const{return valid_&&e==expected_&&pose(p[0],poses_[0])&&pose(p[1],poses_[1])&&boundary(b,boundary_);}
Invoker::Invoker(Expected e,std::array<P::Snapshot,2>p,C::Boundary b,std::array<ModelIdentity,2>m,std::array<Transform,2>t,I::Config c,Sources s):expected_(std::move(e)),baseline_(std::move(p)),boundary_(b),models_(m),transforms_(t),stack_(std::move(c)),sources_(std::move(s)){}
void Invoker::fail()noexcept{if(fault_.exchange(true))return;try{if(sources_.quarantine)sources_.quarantine();}catch(...){} }
bool Invoker::capture(Cpu*c,State&v,const std::array<bool,2>&refreshed){
 if(fault_||!sources_.capture(v)||!sources_.retained(expected_)||!sources_.boundary(c,expected_,v.boundary)||!boundary(v.boundary,boundary_)||v.boundary.identity!=expected_.identity||!v.boundary.exclusive||!v.boundary.fenced||v.boundary.foreign||v.boundary.fault||!v.boundary.manager_owned||v.boundary.manager_depth||v.boundary.camera_depth||v.boundary.npc_depth||v.boundary.callback_depth||v.boundary.foreign_depth||v.boundary.control_mode!=3||v.boundary.partner1!=~0u||v.boundary.partner2!=~0u||v.boundary.talk_partner!=~0u||v.boundary.item_partner!=~0u||v.boundary.event_id!=-1||!v.boundary.serial)return false;
 for(unsigned n=0;n<2;++n){const auto&a=expected_.actors[n];const auto&p=baseline_[n];const auto&m=models_[n];const auto&t=transforms_[n];
  if(!pose(v.poses[n],p)||p.identity.actor!=a.address||p.identity.pid!=a.pid||p.identity.token!=a.token||p.identity.stage!=expected_.identity.stage||p.identity.room!=expected_.identity.room||p.identity.generation!=expected_.identity.generation||p.cut!=a.cut||p.status&0x9800||a.profile!=0x175||a.subtype!=1||p.identity.morf!=m.morf||p.identity.model!=m.model||m.user_area!=a.address||!m.morf||!m.model||!m.model_data||!m.material_table||!m.joint_tree||v.models[n]!=m||!sources_.model(expected_,n,m,t)||v.transforms[n].position!=t.position||v.transforms[n].scale!=t.scale||t.scale!=t.model_scale||!finite(t.position)||!finite(t.scale)||!finite(t.base)||!finite(v.transforms[n].model_scale)||!finite(v.transforms[n].base))return false;
  if(refreshed[n]&&v.transforms[n]!=t)return false;
 }return true;
}
bool Invoker::invoke(Cpu*c,uint32_t fn,uint32_t receiver){
 if(c->gqr[0]||c->gqr[1]||c->gqr[2]!=0x40004||c->gqr[3]!=0x50005||c->gqr[4]!=0x60006||c->gqr[5]!=0x70007)return false;
 if((fn!=GC(0x022BF3A0)&&fn!=GC(0x025E55A0))||!sources_.stack(c,stack_)||!range(stack_.stack)||stack_.protected_ranges.empty()||stack_.protected_ranges.size()>16||(c->r[1]&7)||c->r[1]<I::kFrameBytes)return false;
 const I::Range frame{c->r[1]-I::kFrameBytes,I::kFrameBytes};
 if(frame.address<stack_.stack.address||uint64_t(frame.address)+frame.bytes>uint64_t(stack_.stack.address)+stack_.stack.bytes)return false;
 for(auto p:stack_.protected_ranges)if(!range(p)||overlap(frame,p))return false;
 const Cpu saved=*c;std::array<uint8_t,I::kFrameBytes>bytes{};try{if(!sources_.read(frame.address,bytes)){*c=saved;return false;}}catch(...){*c=saved;return false;}
 bool called=false,restored=false;try{sources_.call(c,fn,{receiver});called=true;}catch(...){}
 // Frame callbacks observe the restored caller even after a native exception.
 *c=saved;
 try{restored=sources_.write(frame.address,bytes);std::array<uint8_t,I::kFrameBytes>check{};restored=restored&&sources_.read(frame.address,check)&&check==bytes;}catch(...){restored=false;}
 *c=saved;return called&&restored;
}
Result Invoker::run(Cpu*c){
 if(fault_)return Result::Quarantined;
 if(!c||!sources_.capture||!sources_.retained||!sources_.boundary||!sources_.model||!sources_.stack||!sources_.read||!sources_.write||!sources_.call||!sources_.quarantine)return Result::Refused;
 // Denied threads cannot inspect the mutable invocation state.
 try{if(!sources_.stack(c,stack_))return Result::Refused;}catch(...){return Result::Refused;}
 if(busy_.exchange(true)){fail();return Result::Quarantined;}
 struct Reset{std::atomic<bool>&v;~Reset(){v.store(false);}}reset{busy_};
 if(attempted_)return Result::Refused;
 auto reject=[&]{if(attempted_){fail();return Result::Quarantined;}return Result::Refused;};
 try{std::array<bool,2>refreshed{};State state;
  if(!capture(c,state,refreshed))return reject();
  const auto animations=state.animations;
  auto animation_stable=[&](){for(unsigned k=0;k<2;++k){const auto&a=state.animations[k];const auto&b=animations[k];if(a.address!=b.address||a.frame_control!=b.frame_control||a.morph!=b.morph||!finite(std::span(a.frame_control).first(8))||!finite(a.morph)||!finite(a.cached_frame))return false;}return true;};
  for(unsigned n=0;n<2;++n){
   if(!capture(c,state,refreshed)||!animation_stable())return reject();attempted_=true;
   if(!invoke(c,GC(0x022BF3A0),expected_.actors[n].address))return reject();refreshed[n]=true;
   if(!capture(c,state,refreshed)||!animation_stable())return reject();
   if(!invoke(c,GC(0x025E55A0),models_[n].morf)||!capture(c,state,refreshed)||!animation_stable()||state.current_model!=models_[n].model||state.root_calc[n]!=models_[n].morf||state.mtx_calc[n]!=models_[n].morf)return reject();
   if(state.animations[n].address&&!std::equal(state.animations[n].cached_frame.begin(),state.animations[n].cached_frame.end(),state.animations[n].frame_control.begin()+4))return reject();
  }
  completion_.expected_=expected_;completion_.poses_=baseline_;completion_.boundary_=state.boundary;completion_.valid_=true;complete_.store(true);return Result::Complete;
 }catch(...){return reject();}
}
}
