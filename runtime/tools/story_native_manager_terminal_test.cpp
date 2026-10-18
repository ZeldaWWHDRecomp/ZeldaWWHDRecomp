// SPDX-License-Identifier: MPL-2.0
#include "../src/mods/story_native_manager_terminal.h"
#include "../src/mods/story_native_event_observation.h"
#include <cassert>
#include <stdexcept>
#include <cstdint>
using namespace story_native_manager_terminal;
namespace {
struct Fixture {
 Identity expected;
 Observation live;
 Terminal* adapter=nullptr;
 unsigned original_calls=0,manager_calls=0;
 uint32_t foreign_status=kAllOffMask|0x400;
 unsigned recursive_original_calls=0;
 bool skip_second=false,skip_clear=false,recursive_callback=false;
 bool throw_manager=false,actor_mismatch=false,post_mismatch=false;
 Fixture(){
  expected.lease={0x10010000,0x18000000,0x1000,5,7,9,11};
  expected.event=expected.lease.image+0x40;
  expected.index=0;
  expected.actors={Actor{0x11000000,12,31,0x175,1,0x400},
                   Actor{0x11001000,13,32,0x175,1,0x400}};
  expected.actors[0].fingerprint[0]=0xA1;
  expected.actors[1].fingerprint[0]=0xB2;
  live.lease=expected.lease;live.event=expected.event;live.index=expected.index;
  live.event_state=kClose;live.camera_play=1;live.actors=expected.actors;
  live.actors[0].status|=0x8000; // native startProc FORCEMOVE
  live.actors[1].status|=0x1800;
  live.retained_allocation=live.entry_fence=live.idle_depths=live.manager_table_owned=true;
 }
 Terminal make(){
  return Terminal(expected,
   [this](Observation&o){o=live;return true;},
   [this](uint32_t address,Actor&a){
    for(const auto&actor:live.actors)if(actor.address==address){a=actor;if(actor_mismatch)++a.token;return true;}
    return false;
   },
   [this](uint32_t manager,int32_t index,uint32_t force){
    ++manager_calls;
    assert(adapter&&adapter->native_close_scope());
    assert(manager==expected.lease.manager&&index==expected.index&&force==0);
    auto original=[this](uint32_t address){return [this,address]{
     ++original_calls;
     if(recursive_callback&&address==expected.actors[0].address)
      adapter->route_all_off(address,[this]{++recursive_original_calls;return 0u;});
     if(address==0x12000000)foreign_status&=~kAllOffMask;
     else if(!skip_clear)for(auto& actor:live.actors)
      if(actor.address==address)actor.status&=~kAllOffMask;
     return 0u;
    };};
    assert(adapter->route_all_off(0x12000000,original(0x12000000))==0); // foreign suppressed
    assert(adapter->route_all_off(expected.actors[0].address,original(expected.actors[0].address))==0);
    if(!skip_second)assert(adapter->route_all_off(expected.actors[1].address,original(expected.actors[1].address))==0);
    if(throw_manager)throw std::runtime_error("native call failed");
    live.event_state=kNone;live.camera_play=0;
    if(post_mismatch)++live.actors[1].token;
   });
 }
 story_native_event_observation::Expected observation_expected()const{
  story_native_event_observation::Expected out;out.identity=expected.lease;
  for(unsigned n=0;n<2;++n){const auto&a=expected.actors[n];auto&b=out.actors[n];
   b.address=a.address;b.pid=a.pid;b.token=a.token;b.profile=a.profile;
   b.subtype=a.subtype;b.fingerprint=a.fingerprint;
  }
  return out;
 }
};
}
int main(){
 assert(!Completion{}.valid()&&Completion{}.serial()==0);
 {
  Fixture f;auto t=f.make();f.adapter=&t;
  assert(!t.native_close_scope());
  unsigned delegated=0;
  assert(t.route_all_off(f.expected.actors[0].address,[&]{++delegated;return 7u;})==7);
  assert(delegated==1);
  assert(!t.completed()&&!t.matches(f.observation_expected()));
  assert(t.completion()==Completion{});
  assert(t.close()==Result::Completed);
  assert(!t.native_close_scope());
  assert(t.completed()&&t.matches(f.observation_expected()));
  const auto receipt=t.completion();
  assert(receipt.valid()&&receipt.serial()!=0&&receipt.lease()==f.expected.lease);
  assert(t.completion()==receipt);
  auto mismatched=f.observation_expected();
  ++mismatched.identity.generation;assert(!t.matches(mismatched));
  mismatched=f.observation_expected();++mismatched.actors[0].pid;assert(!t.matches(mismatched));
  mismatched=f.observation_expected();++mismatched.actors[1].token;assert(!t.matches(mismatched));
  mismatched=f.observation_expected();mismatched.actors[1].fingerprint[0]^=1;assert(!t.matches(mismatched));
  assert(f.manager_calls==1&&f.original_calls==2&&!t.quarantined());
  assert(f.foreign_status==(kAllOffMask|0x400));
  assert(f.live.actors[0].status==0x400&&f.live.actors[1].status==0x400);
  f.live.event_state=kClose;f.live.camera_play=1; // even a forged CLOSE cannot repeat
  assert(t.close()==Result::Denied&&f.manager_calls==1);
  assert(t.completion()==receipt);
  Fixture other;auto u=other.make();other.adapter=&u;
  assert(u.close()==Result::Completed);
  assert(u.completion().valid()&&u.completion().serial()!=receipt.serial());
  assert(u.completion()!=receipt); // identical identity cannot reuse ordering receipt
 }
 {
  Fixture f;f.live.camera_play=0;auto t=f.make();f.adapter=&t;
  assert(t.close()==Result::Quarantined&&f.manager_calls==0&&!t.completed());
  assert(!t.completion().valid());
 }
 {
  Fixture f;f.live.actors[0].status|=0x200;auto t=f.make();f.adapter=&t;
  assert(t.close()==Result::Quarantined&&f.manager_calls==0&&t.quarantined());
 }
 {
  Fixture f;f.expected.event+=0xB0;f.live.event=f.expected.event;auto t=f.make();f.adapter=&t;
  assert(t.close()==Result::Quarantined&&f.manager_calls==0&&t.quarantined());
 }
 {
  Fixture f;f.live.entry_fence=false;auto t=f.make();f.adapter=&t;
  assert(t.close()==Result::Denied&&f.manager_calls==0&&!t.quarantined());
 }
 {
  Fixture f;f.skip_second=true;auto t=f.make();f.adapter=&t;
  assert(t.close()==Result::Quarantined&&f.original_calls==1&&t.quarantined());
 }
 {
  Fixture f;f.skip_clear=true;auto t=f.make();f.adapter=&t;
  assert(t.close()==Result::Quarantined&&t.quarantined());
 }
 {
  Fixture f;f.recursive_callback=true;auto t=f.make();f.adapter=&t;
  assert(t.close()==Result::Quarantined&&t.quarantined());
  assert(f.recursive_original_calls==0&&f.original_calls==1);
 }
 {
  Fixture f;f.actor_mismatch=true;auto t=f.make();f.adapter=&t;
  assert(t.close()==Result::Quarantined&&f.original_calls==0&&t.quarantined());
 }
 {
  Fixture f;f.post_mismatch=true;auto t=f.make();f.adapter=&t;
  assert(t.close()==Result::Quarantined&&f.original_calls==2&&t.quarantined());
 }
 {
  Fixture f;f.throw_manager=true;auto t=f.make();f.adapter=&t;
  assert(t.close()==Result::Quarantined&&t.quarantined());
  assert(!t.completed()&&!t.matches(f.observation_expected()));
  assert(!t.completion().valid());
  unsigned delegated=0;
  assert(t.route_all_off(0x12000000,[&]{++delegated;return 3u;})==3);
  assert(delegated==1); // scope ended after exception
 }
 {
  Fixture f;f.expected.index=1;f.expected.event=f.expected.lease.image+0x40+0xB0;
  f.live.index=1;f.live.event=f.expected.event;
  auto t=f.make();f.adapter=&t;
  assert(t.close()==Result::Completed&&!t.matches(f.observation_expected()));
 }
}
