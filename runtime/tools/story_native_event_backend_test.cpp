// SPDX-License-Identifier: MPL-2.0
// Reuse the bounded native class byte fixture, not its test entrypoint.
#define main collector_fixture_contract_unused
#include "story_native_event_collect_test.cpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#undef main
#include "mods/story_native_event_backend.h"
#include <memory>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
namespace B=story_native_event_backend;
namespace D=story_native_event_depth;
struct RuntimeFixture:Fake {
 bool session_valid=true,actor_valid=true;uint64_t logic=100;D::Tracker*tracker=nullptr;
 RuntimeFixture(){
  memory[0x10044878].resize(4+145*0x84);memory[0x10049348].resize(8+63*0x40);put(0x10044878,145);put(0x10049348,63);e.camera.style_count=145;e.camera.type_count=63;
  const char name[]="Event";copy(0x10049350+2*0x40,std::span(reinterpret_cast<const uint8_t*>(name),sizeof(name)));put(0x1004487C+4,11);
  for(unsigned offset:{0xC4u,0xC8u,0xCCu,0xD0u})put(play+0x51D0+offset,0xFFFFFFFFu);half(play+0x51D0+0xD8,0xFFFF);
 }
 B::Scaffold scaffold(D::Tracker&d){tracker=&d;B::Config cfg;cfg.collector=config;cfg.memory={stamp,link_actor,55,{0x60002000,4096}};B::Sources sources;
  sources.read=[&](uint32_t a,std::span<uint8_t>b){++reads;auto*p=pointer(a,b.size());if(!p)return false;std::copy_n(p,b.size(),b.begin());return true;};
  sources.session=[&](story_native_event_memory::Scope&s){s.identity=e.identity;s.link=link_actor;s.link_pid=55;s.exclusive=session_valid;s.bindings_verified=owned;s.logic_step=logic;s.serial=tracker->snapshot().serial;return true;};
  sources.allocation=[&](const auto&,const auto&){return allocated;};sources.actor=[&](const Actor&a,const auto&){const auto n=(a.address-e.actors[0].address)/0x1000;return actor_valid&&n<2&&a.pid==e.actors[n].pid&&a.fingerprint==actual_fingerprints[n];};return B::Scaffold(e,cfg,sources,d);
 }
};
static D::Coverage covered(){return {{11,11,true},{0,0,true},{22,22,true},true};}
int main(){
 RuntimeFixture f;D::Tracker d(f.e,covered());auto scaffold=f.scaffold(d);auto r=scaffold.preflight(Operation::Prepare);assert(r.preflight&&!r.publication_enabled&&r.observation.valid()&&r.camera==f.e.camera);assert(!B::Scaffold::publication_enabled());assert(scaffold.preflight(Operation::Release).reason==B::Reason::Disabled);
 for(unsigned n=0;n<10;++n){RuntimeFixture x;if(n==9){x.e.actors[0].fingerprint[0]^=1;x.copy(stamp,make_stamp(x.e));}D::Tracker depth(x.e,covered());auto s=x.scaffold(depth);if(n==0)x.session_valid=false;if(n==1)x.owned=false;if(n==2)x.allocated=false;if(n==3)x.actor_valid=false;if(n==4)x.put(cam_process+4,99);if(n==5)x.byte(x.e.actors[0].address+0x2DD,9);if(n==6)x.put(0x1004487C+4,19);if(n==7)x.half(play+0x51D0+0xD8,0);if(n==8)x.put(stamp+0x34,999);assert(!s.preflight(Operation::Prepare).preflight);}
 for(unsigned fault=0;fault<6;++fault){RuntimeFixture x;for(unsigned n=0;n<4;++n)x.put(play+0x51D0+0xC4+n*4,n?0x16C:0x110);
  if(fault==1)x.byte(play+0x51D0+0xC0,1);if(fault>=2&&fault<=4)x.byte(play+0x51D0+0xC2,fault-1);if(fault==5)x.half(play+0x51D0+0xD8,0);
  const auto original=x.memory;D::Tracker depth(x.e,covered());auto s=x.scaffold(depth);assert(s.preflight(Operation::Prepare).preflight==(fault==0)&&x.memory==original);
 }
 // Inspect a synthetic already-published owned WAIT without publishing it.
 RuntimeFixture running;const auto image=running.e.identity.image,manager=running.e.identity.manager;
 running.put(image+0x40+0xA4,2);
 for(unsigned n=0;n<4;++n){running.put(image+0xF0+n*0x50+0x38,n);running.put(image+0xF0+n*0x50+0x3C,n<2?0:n==2?1:0);}
 const std::array<uint32_t,8>published={image,image+0x40,image+0xF0,image+0x230,image+0x410,0,image+0x510,0};for(unsigned n=0;n<8;++n)running.put(manager+n*4,published[n]);running.put(manager+0x20,1);running.put(manager+0x53C,image+0x40);
 for(unsigned n=0;n<2;++n){auto actor=running.e.actors[n].address;running.put(actor+0x3E0,image+0xF0+n*0x50);running.put(actor+0x3E4,n);running.put(actor+0x3E8,actor);running.put(actor+0x3F0,0);}
 running.put(cam_process+0x248+0x51C,2);running.put(cam_process+0x248+0x520,2);running.put(cam_process+0x248+0x408,0);running.put(cam_process+0x248+0x400,2);running.byte(play+0x51D0+0xC2,3);running.byte(play+0x51D0+0xDE,0xFF);running.byte(play+0x51D0+0xDF,0xFF);
 D::Tracker live_depth(running.e,covered());auto inspection=running.scaffold(live_depth);assert(inspection.preflight(Operation::Running).preflight);for(unsigned offset:{0xC4u,0xC8u,0xCCu,0xD0u}){running.put(play+0x51D0+offset,123);auto refused_partner=inspection.preflight(Operation::Running);assert(!refused_partner.preflight&&!refused_partner.observation.valid());running.put(play+0x51D0+offset,~0u);}
 RuntimeFixture busy;D::Tracker busy_depth(busy.e,covered());auto busy_scaffold=busy.scaffold(busy_depth);D::Call c;c.entry=D::Entry::PeopleExecute;c.function=D::entry_address(c.entry);c.receiver=c.subject=busy.e.actors[0].address;c.pid=busy.e.actors[0].pid;c.token=busy.e.actors[0].token;c.identity=busy.e.identity;c.authority=D::Authority::Owned;{auto scope=busy_depth.enter(c);assert(scope.allowed());unsigned reads=busy.reads;assert(busy_scaffold.preflight(Operation::Prepare).reason==B::Reason::Depth&&busy.reads==reads);}assert(busy_scaffold.preflight(Operation::Prepare).preflight);
 RuntimeFixture no_coverage;D::Tracker missing(no_coverage.e,{});auto refused=no_coverage.scaffold(missing);assert(refused.preflight(Operation::Prepare).reason==B::Reason::Depth&&no_coverage.reads==0);
 RuntimeFixture fenced;D::Tracker drain(fenced.e,covered());auto denied=fenced.scaffold(drain);assert(drain.begin_drain());assert(denied.preflight(Operation::Prepare).reason==B::Reason::Depth);
}
