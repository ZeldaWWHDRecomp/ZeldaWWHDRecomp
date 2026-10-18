// SPDX-License-Identifier: MPL-2.0
#include "story_native_people_bridge.h"
#include "story_actors.h"
#include "story_native_event_image.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
#include <stdexcept>
namespace story_native_people_bridge {namespace {
thread_local Bridge*current=nullptr;
const uint32_t Common=GC(0x0259F858);
uint32_t word(std::span<const uint8_t>b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
bool span(story_native_event_invoke::Range r){return r.bytes&&!(r.address&3)&&((r.address>=mem::kMem2Start&&uint64_t(r.address)+r.bytes<=mem::kMem2End)||(r.address>=mem::kRuntimeStart&&uint64_t(r.address)+r.bytes<=mem::kHostStart));}
bool overlap(story_native_event_invoke::Range a,story_native_event_invoke::Range b){return uint64_t(a.address)<uint64_t(b.address)+b.bytes&&uint64_t(b.address)<uint64_t(a.address)+a.bytes;}
}
D::Call Bridge::call(D::Entry entry,unsigned index)const{const auto&a=expected_.actors[index];D::Call c;c.entry=entry;c.function=D::entry_address(entry);c.identity=expected_.identity;c.authority=D::Authority::Owned;c.subject=a.address;c.pid=a.pid;c.token=a.token;c.receiver=(entry==D::Entry::StaffLookup||entry==D::Entry::CutEnd)?expected_.identity.manager:a.address+((entry==D::Entry::PeopleEventMove)?0:0x3E0);return c;}
void Bridge::fail()noexcept{fault_.store(true);if(cancel_attempted_.exchange(true))return;try{if(sources_.queue_cancel)cancel_queued_.store(sources_.queue_cancel(expected_.identity));}catch(...){} }
bool Bridge::thread(const Cpu*c)const noexcept{try{return sources_.verify_owned_thread&&sources_.verify_owned_thread(c);}catch(...){return false;}}
bool Bridge::retained()const noexcept{try{return sources_.admission.lease&&sources_.admission.lease(expected_.identity);}catch(...){return false;}}
bool Bridge::identity(unsigned n)const{
 if(n>=2)return false;const auto&a=expected_.actors[n];story_actors::ResidentEventIdentity identity;
 return story_actors::resident_event_identity(references_[n].mod,references_[n].id,a.address,a.pid,identity)&&identity.actor==a.address&&identity.pid==a.pid&&identity.token==a.token&&identity.fingerprint==a.fingerprint&&identity.stage_pid==expected_.identity.stage&&identity.room_pid==expected_.identity.room&&identity.room==expected_.room_number&&identity.subtype==1;
}
bool Bridge::own(unsigned n)const{
 if(n>=2||fault_.load()||!sources_.admission.lease||!sources_.admission.current_scope)return false;
 const auto&a=expected_.actors[n];return tracker_.ready()&&sources_.verify_routes&&sources_.verify_routes()&&sources_.admission.lease(expected_.identity)&&sources_.admission.current_scope(a,expected_.identity)&&identity(n);
}
Bridge::Bridge(story_native_event_observation::Expected e,std::array<A::Reference,2>refs,Sources s,D::Tracker&t):expected_(std::move(e)),references_(refs),sources_(std::move(s)),tracker_(t){
 if(!sources_.stack_read||!sources_.stack_write||!sources_.guest_call||!sources_.verify_stack||!sources_.verify_routes||!sources_.verify_owned_thread||!sources_.queue_cancel||!span(sources_.stack.stack)||sources_.stack.protected_ranges.empty()||sources_.stack.protected_ranges.size()>16)return;
 for(const auto&r:sources_.stack.protected_ranges)if(!span(r))return;
 auto admission=sources_.admission;admission.invoke=[this](Cpu*c,uint32_t fn,std::initializer_list<uint32_t>r){return common(c,fn,r);};
 adapter_=std::make_unique<A::Adapter>(expected_,std::move(refs),std::move(admission));configured_=adapter_->configured();
}
uint32_t Bridge::common(Cpu*c,uint32_t fn,std::initializer_list<uint32_t>regs){
 auto reject=[&]()->uint32_t{fail();throw std::runtime_error("owned WAIT CommonCut refused");};
 if(!c||!thread(c)||fn!=Common||regs.size()!=1||current||active_!=-1||fault_||!sources_.verify_routes()||!tracker_.ready()||!sources_.verify_stack(c,sources_.stack))return reject();
 unsigned index=2;for(unsigned n=0;n<2;++n)if(*regs.begin()==expected_.actors[n].address+0x3E0)index=n;
 if(index==2||!own(index))return reject();
 const uint32_t sp=c->r[1];if((sp&15)||sp<0x40)return reject();story_native_event_invoke::Range frame{sp-0x40,0x40};
 if(frame.address<sources_.stack.stack.address||uint64_t(frame.address)+frame.bytes>uint64_t(sources_.stack.stack.address)+sources_.stack.stack.bytes)return reject();
 for(const auto&r:sources_.stack.protected_ranges)if(overlap(frame,r))return reject();
 if(overlap(frame,{expected_.identity.image,expected_.identity.bytes}))return reject();for(const auto&a:expected_.actors)if(overlap(frame,{a.address,0x920}))return reject();
 const Cpu saved=*c;std::array<uint8_t,0x40>bytes{};
 try{if(!sources_.stack_read(frame.address,bytes))return reject();}catch(...){*c=saved;return reject();}
 bool restored=false;auto restore=[&](){try{restored=sources_.stack_write(frame.address,bytes);}catch(...){restored=false;}*c=saved;};
 uint32_t result=0;bool okay=false;
 try{
  auto scope=tracker_.enter(call(D::Entry::CommonCut,index));if(!scope.allowed()){restore();return reject();}
  active_=int(index);current=this;struct Reset{Bridge&b;~Reset(){current=nullptr;b.active_=-1;}}reset{*this};
  result=sources_.guest_call(c,Common,regs);okay=!fault_&&own(index);
 }catch(...){restore();return reject();}
 restore();if(!okay||!restored)return reject();return result;
}
Decision Bridge::event_move(Cpu*c){
 if(!c)return Decision::RejectOwned;int index=-1;for(unsigned n=0;n<2;++n)if(c->r[3]==expected_.actors[n].address)index=int(n);if(index<0)return Decision::DelegateOwner;
 if(!thread(c)){fail();return Decision::RejectOwned;}
 // No lease means retail address reuse delegates WITHOUT cancellation.
 if(!identity(unsigned(index))){if(retained())fail();return Decision::DelegateOwner;}
 if(!configured_||fault_){fail();return Decision::RejectOwned;}
 try{if(!sources_.verify_routes()||!own(unsigned(index))){fail();return Decision::RejectOwned;}auto scope=tracker_.enter(call(D::Entry::PeopleEventMove,unsigned(index)));if(!scope.allowed()){fail();return Decision::RejectOwned;}const auto result=adapter_->event_move(c,c->r[3]);if(result!=story_native_people_events::Dispatch::Processed){fail();return Decision::RejectOwned;}return Decision::Handled;}catch(...){fail();return Decision::RejectOwned;}
}
bool Bridge::staff_id(Cpu*c){
 if(!configured_||!c)return false;
 if(!thread(c)){const bool claimed=c->r[3]==expected_.identity.manager&&(c->r[4]==expected_.identity.image+0xF0||c->r[4]==expected_.identity.image+0x140);if(claimed){fail();c->r[3]=0xFFFFFFFFu;return true;}return false;}
 if(current!=this||active_<0){const bool claimed=c->r[3]==expected_.identity.manager&&(c->r[4]==expected_.identity.image+0xF0||c->r[4]==expected_.identity.image+0x140);const bool handled=adapter_->staff_id(c);if(handled&&claimed&&c->r[3]==0xFFFFFFFFu)fail();return handled;}
 try{if(fault_||!own(unsigned(active_))){fail();c->r[3]=0xFFFFFFFFu;return true;}auto scope=tracker_.enter(call(D::Entry::StaffLookup,unsigned(active_)));if(!scope.allowed()){fail();c->r[3]=0xFFFFFFFFu;return true;}bool handled=adapter_->staff_id(c);if(!handled||c->r[3]!=uint32_t(active_)){fail();c->r[3]=0xFFFFFFFFu;}return true;}catch(...){fail();c->r[3]=0xFFFFFFFFu;return true;}
}
Decision Bridge::wait(Cpu*c,bool start,const Body&original){return action_entry(c,start,false,original);}
Decision Bridge::turn(Cpu*c,bool start,const Body&original){return action_entry(c,start,true,original);}
Decision Bridge::action_entry(Cpu*c,bool start,bool turn,const Body&original){
 if(!c||!original)return Decision::RejectOwned;int index=-1;for(unsigned n=0;n<2;++n)if(c->r[3]==expected_.actors[n].address+0x3E0)index=int(n);if(index<0)return Decision::DelegateOwner;
 if(!thread(c)){fail();return Decision::RejectOwned;}
 if(current!=this&&!identity(unsigned(index))){if(retained())fail();return Decision::DelegateOwner;}
 if(!configured_||fault_||current!=this||index!=active_){fail();return Decision::RejectOwned;}
 try{if(!own(unsigned(index))){fail();return Decision::RejectOwned;}
  if(turn&&!expected_.program){fail();return Decision::RejectOwned;}
  if(expected_.program){std::vector<uint8_t>image(expected_.identity.bytes);if(!sources_.admission.read(expected_.identity.image,image)||!expected_.program->immutable_matches(image)){fail();return Decision::RejectOwned;}const auto at=0xF0+unsigned(index)*0x50;const auto cut=expected_.program->cut(unsigned(index),word(image,at+0x38));if(!cut||(word(image,0xE4)!=2&&word(image,0xE4)!=4)||cut->action!=(turn?3u:0u)||(word(image,at+0x3C)!=0xFFFFFFFFu&&word(image,at+0x3C)!=cut->action)){fail();return Decision::RejectOwned;}
   const auto&a=expected_.actors[unsigned(index)];std::array<uint8_t,0x6C>consumer{};
   if(!sources_.admission.read(a.address+0x3E0,consumer)||word(consumer,0)!=expected_.identity.image+at||word(consumer,4)!=unsigned(index)||word(consumer,8)!=a.address||word(consumer,0x10)!=cut->action||(word(consumer,0xC)&&word(consumer,0xC)!=a.address)||(word(consumer,0x68)&&word(consumer,0x68)!=a.address+0x3AC)||consumer[0x60]>1||consumer[0x61]>1){fail();return Decision::RejectOwned;}
  }
  const auto entry=turn?(start?D::Entry::TurnStart:D::Entry::TurnProcess):(start?D::Entry::WaitStart:D::Entry::WaitProcess);
  auto scope=tracker_.enter(call(entry,unsigned(index)));if(!scope.allowed()){fail();return Decision::RejectOwned;}original();if(fault_||!own(unsigned(index))){fail();return Decision::RejectOwned;}return Decision::Handled;}catch(...){fail();return Decision::RejectOwned;}
}
Decision Bridge::cut_end(Cpu*c,const Body&original){
 if(!c||!original)return Decision::RejectOwned;
 if(c->r[3]!=expected_.identity.manager){if(current==this){fail();return Decision::RejectOwned;}return Decision::DelegateOwner;}
 if(!thread(c)){fail();return Decision::RejectOwned;}
 std::array<uint8_t,0x20>observed_pools{};std::array<uint8_t,4>observed_event{};
 bool read=false;try{read=sources_.admission.read&&sources_.admission.read(expected_.identity.manager,observed_pools)&&sources_.admission.read(expected_.identity.manager+0x53C,observed_event);}catch(...){}
 if(!read){if(current==this||retained()){fail();return Decision::RejectOwned;}return Decision::DelegateOwner;}
 const auto in_image=[&](uint32_t p){return p>=expected_.identity.image&&uint64_t(p)<uint64_t(expected_.identity.image)+expected_.identity.bytes;};
 bool claimed=in_image(word(observed_event,0));for(unsigned n=0;n<8;++n)claimed|=in_image(word(observed_pools,n*4));
 if(!claimed){if(current==this){fail();return Decision::RejectOwned;}return Decision::DelegateOwner;}
 bool owned_manager=false;try{owned_manager=sources_.admission.lease&&sources_.admission.lease(expected_.identity);}catch(...){}
 if(!owned_manager){fail();return Decision::RejectOwned;}
 std::array<uint32_t,8>expected_pools={expected_.identity.image,expected_.identity.image+0x40,expected_.identity.image+0xF0,expected_.identity.image+0x230,expected_.identity.image+0x410,0,expected_.identity.image+0x510,0};if(expected_.program){const auto offsets=expected_.program->pool_offsets();for(unsigned n=0;n<8;++n)expected_pools[n]=offsets[n]?expected_.identity.image+offsets[n]:n?0:expected_.identity.image;}
 for(unsigned n=0;n<8;++n)if(word(observed_pools,n*4)!=expected_pools[n]){fail();return Decision::RejectOwned;}
 // The manager is a retained owned image: native cutEnd bounds NO positive
 // index, so unknown staff can never be passed to the original on this route.
 if(c->r[4]>=4){fail();return Decision::RejectOwned;}
 if(c->r[4]>=2){if(current==this){fail();return Decision::RejectOwned;}return Decision::DelegateOwner;}
 // ManagerMain clears current_event before specialStaffProc (02543EA0),
 // then restores it after Timekeeper returns (02543ED8). The owning Routes
 // authenticate the genuine camera/timekeeper parent before delegation.
 const auto event_valid=[&](uint32_t event,uint32_t state){
  if(event==expected_.identity.image+0x40)return true;
  // CLOSE leaves the native actors forced to move until terminal allOff.
  // Their completed WAIT can repeat cutEnd while Main leaves current_event0.
  // Only the fully completed canonical fixture admits that idempotent call.
  if(event||state!=4)return false;
  std::array<uint8_t,0x500>flags{};
  if(!sources_.admission.read(expected_.identity.manager+0x3C,flags))return false;
  const uint32_t mask=expected_.program?expected_.program->completion_mask():0x3F;
  for(unsigned k=0;k<flags.size();++k)if(flags[k]!=(k<4?uint8_t(mask>>(24-k*8)):0))return false;
  return true;
 };
 std::array<uint8_t,4>event_state{};
 if(!sources_.admission.read(expected_.identity.image+0xE4,event_state)||!event_valid(word(observed_event,0),word(event_state,0))){fail();return Decision::RejectOwned;}

 if(!configured_||fault_||current!=this||active_<0||c->r[4]!=uint32_t(active_)){fail();return Decision::RejectOwned;}
 try{
  const unsigned n=unsigned(active_);if(!own(n)){fail();return Decision::RejectOwned;}
  std::vector<uint8_t>image(expected_.identity.bytes);std::array<uint8_t,0x6C>cut{};
  if(!sources_.admission.read(expected_.identity.image,image)||!sources_.admission.read(expected_.actors[n].address+0x3E0,cut)){fail();return Decision::RejectOwned;}
  std::array<uint8_t,0x20>manager{};std::array<uint8_t,4>event{};
  if(!sources_.admission.read(expected_.identity.manager,manager)||!sources_.admission.read(expected_.identity.manager+0x53C,event)){fail();return Decision::RejectOwned;}
  const auto&pools=expected_pools;
  for(unsigned j=0;j<8;++j)if(word(manager,j*4)!=pools[j]){fail();return Decision::RejectOwned;}
  if(!event_valid(word(event,0),word(image,0xE4))){fail();return Decision::RejectOwned;}
  const auto canonical=expected_.program?std::vector<uint8_t>(expected_.program->pristine().begin(),expected_.program->pristine().end()):story_native_event_image::make_wait_fixture(expected_.frames);for(unsigned p=0;p<image.size();++p){bool mutable_byte=p>=0xE4&&p<0xE8;for(unsigned staff=0;staff<4;++staff)mutable_byte|=p>=0xF0+staff*0x50+0x38&&p<0xF0+staff*0x50+0x48;if(!mutable_byte&&image[p]!=canonical[p]){fail();return Decision::RejectOwned;}}
  const uint32_t current=word(image,0xF0+n*0x50+0x38);const auto selected=expected_.program?expected_.program->cut(n,current):std::optional<story_native_event_program::Cut>{story_native_event_program::Cut{n,0,expected_.frames}};
  if(!selected||(word(image,0xE4)!=2&&word(image,0xE4)!=4)||(!expected_.program&&current!=n)||word(image,0x230+current*0x50+0x34)!=current||word(cut,0)!=expected_.identity.image+0xF0+n*0x50||word(cut,4)!=n||word(cut,8)!=expected_.actors[n].address||word(cut,0x10)!=selected->action||word(cut,0x18)!=0){fail();return Decision::RejectOwned;}
  auto scope=tracker_.enter(call(D::Entry::CutEnd,n));if(!scope.allowed()){fail();return Decision::RejectOwned;}original();if(fault_||!own(n)){fail();return Decision::RejectOwned;}return Decision::Handled;
 }catch(...){fail();return Decision::RejectOwned;}
}
}
