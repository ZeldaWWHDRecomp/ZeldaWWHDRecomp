// SPDX-License-Identifier: MPL-2.0
#include "story_native_manager_terminal.h"
#include "story_native_event_observation.h"
#include <limits>

namespace story_native_manager_terminal {
namespace {
std::atomic<uint64_t> next_completion_serial{1};
uint64_t mint_serial(){
 uint64_t value=next_completion_serial.load(std::memory_order_relaxed);
 while(value!=std::numeric_limits<uint64_t>::max()){
  if(next_completion_serial.compare_exchange_weak(value,value+1,
       std::memory_order_relaxed,std::memory_order_relaxed))return value;
 }
 return 0;
}
}
thread_local Terminal* Terminal::scope_=nullptr;
Terminal::Terminal(Identity e,Probe p,ActorProbe a,Invoke i):
 expected_(e),probe_(std::move(p)),actor_probe_(std::move(a)),invoke_(std::move(i)){}
bool Terminal::same_actor(const Actor&a,const Actor&b){
 return a.address==b.address&&a.pid==b.pid&&a.token==b.token&&
        a.profile==b.profile&&a.subtype==b.subtype&&a.fingerprint==b.fingerprint;
}
bool Terminal::valid(const Observation&o,uint32_t state)const{
 const auto&i=expected_.lease;
 if(!i.manager||!i.image||!i.bytes||uint64_t(i.image)+i.bytes>0x100000000ull||
    !i.stage||!i.room||!i.generation||!i.program||
    !expected_.event||expected_.index<0||
    uint64_t(expected_.event)!=uint64_t(i.image)+0x40+uint64_t(expected_.index)*0xB0||
    uint64_t(expected_.event)+0xB0>uint64_t(i.image)+i.bytes||
    o.lease!=i||o.event!=expected_.event||o.index!=expected_.index||
    o.event_state!=state||(state==kClose&&o.camera_play!=1)||
    o.foreign_takeover||!o.retained_allocation||
    !o.entry_fence||!o.idle_depths||!o.manager_table_owned)return false;
 for(unsigned n=0;n<2;++n){
  const auto&want=expected_.actors[n],&got=o.actors[n];
  if(!want.address||!want.pid||!want.token||want.profile!=0x175||want.subtype!=1||
     want.fingerprint==std::array<uint8_t,32>{}||
     (want.status&kAllOffMask)||(got.status&~kAllOffMask)!=want.status||
     (state==kNone&&(got.status&kAllOffMask))||
     !same_actor(want,got))return false;
 }
 return expected_.actors[0].address!=expected_.actors[1].address&&
        expected_.actors[0].pid!=expected_.actors[1].pid&&
        expected_.actors[0].token!=expected_.actors[1].token;
}
bool Terminal::matches(const story_native_event_observation::Expected&e)const{
 if(!completed()||expected_.lease!=e.identity||expected_.index!=0||
    uint64_t(expected_.event)!=uint64_t(e.identity.image)+0x40)return false;
 for(unsigned n=0;n<2;++n){
  const auto&a=expected_.actors[n];const auto&b=e.actors[n];
  if(!a.address||!a.pid||!a.token||a.profile!=0x175||a.subtype!=1||
     (a.status&kAllOffMask)||a.fingerprint==std::array<uint8_t,32>{}||
     a.address!=b.address||a.pid!=b.pid||a.token!=b.token||
     a.profile!=b.profile||a.subtype!=b.subtype||a.fingerprint!=b.fingerprint)return false;
 }
 return expected_.actors[0].address!=expected_.actors[1].address&&
        expected_.actors[0].pid!=expected_.actors[1].pid&&
        expected_.actors[0].token!=expected_.actors[1].token;
}
Completion Terminal::completion()const{
 if(!completed())return {};
 return Completion(expected_,completion_serial_);
}
Result Terminal::close(){
 if(quarantined()||!probe_||!actor_probe_||!invoke_)return Result::Quarantined;
 if(completed_.load(std::memory_order_acquire))return Result::Denied;
 bool idle=false;
 if(!running_.compare_exchange_strong(idle,true,std::memory_order_acq_rel)){
  quarantined_.store(true,std::memory_order_release);return Result::Quarantined;
 }
 struct Running {Terminal*t;~Running(){t->running_.store(false,std::memory_order_release);}} running{this};
 if(scope_){quarantined_.store(true,std::memory_order_release);return Result::Quarantined;}
 if(completed_.load(std::memory_order_acquire))return Result::Denied;
 try{
  Observation before{};
  if(!probe_(before)){quarantined_.store(true,std::memory_order_release);return Result::Quarantined;}
  if(quarantined())return Result::Quarantined;
  if(!before.entry_fence||!before.idle_depths||before.event_state!=kClose)return Result::Denied;
  if(!valid(before,kClose)||quarantined()){
   quarantined_.store(true,std::memory_order_release);return Result::Quarantined;
  }
  seen_={};bad_callback_=false;
  for(unsigned n=0;n<2;++n)pre_status_[n]=before.actors[n].status;
  {
   struct Scope {Terminal*&slot;Terminal*prior;~Scope(){slot=prior;}} scope{scope_,scope_};
   scope_=this;
   invoke_(expected_.lease.manager,expected_.index,0);
  }
  if(quarantined()||bad_callback_||!seen_[0]||!seen_[1]){
   quarantined_.store(true,std::memory_order_release);return Result::Quarantined;
  }
  Observation after{};
  if(!probe_(after)||!valid(after,kNone)||after.camera_play!=0||quarantined()){
   quarantined_.store(true,std::memory_order_release);return Result::Quarantined;
  }
  completion_serial_=mint_serial();
  if(!completion_serial_){quarantined_.store(true,std::memory_order_release);return Result::Quarantined;}
  completed_.store(true,std::memory_order_release);
  return Result::Completed;
 }catch(...){quarantined_.store(true,std::memory_order_release);return Result::Quarantined;}
}
uint32_t Terminal::route_all_off(uint32_t address,const OriginalCallback& original){
 if(scope_!=this)return original();
 if(quarantined()){bad_callback_=true;return 0;}
 int owned=-1;
 for(unsigned n=0;n<2;++n)if(expected_.actors[n].address==address){owned=int(n);break;}
 if(owned<0)return 0; // native ALL iteration may visit retail actors
 try{
  Actor current{};
  if(seen_[owned]||!actor_probe_(address,current)||
     !same_actor(current,expected_.actors[owned])||current.status!=pre_status_[owned]){
   bad_callback_=true;quarantined_.store(true,std::memory_order_release);return 0;
  }
  seen_[owned]=true; // block duplicate or recursive delivery before invoking native body
  uint32_t result=original();
  if(result!=0){bad_callback_=true;quarantined_.store(true,std::memory_order_release);return result;}
  Actor after{};
  if(!actor_probe_(address,after)||!same_actor(after,expected_.actors[owned])||
     after.status!=(current.status&~kAllOffMask)){
   bad_callback_=true;quarantined_.store(true,std::memory_order_release);return result;
  }
  return result;
 }catch(...){bad_callback_=true;quarantined_.store(true,std::memory_order_release);throw;}
}
}
