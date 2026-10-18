// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_wait_detach_plan.h"
#include <cassert>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
using namespace story_native_wait_detach_plan;
using story_native_event_observation::Camera;
using story_native_event_lease::Identity;
static void put(std::span<uint8_t>b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
static bool camera_policy(const Camera&c,const Identity&){return c.type_count==63&&c.style_count==145&&c.style<145&&c.param_index==c.style&&c.param_pointer==c.style_table+c.style*0x84;}
struct Fixture {
 story_native_event_observation::Expected e{};std::array<P::Snapshot,2> actors{},live{};
 C::Boundary boundary{};
 Fixture(){
  e.identity={0x10474374,0x60000000,0x520,77,88,3,4};
  for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x20001000+n*0x2000;a.pid=10+n;a.token=20+n;a.profile=0x175;a.subtype=1;a.cut[0x60]=1;
   auto&s=actors[n];s.identity={a.address,a.pid,a.token,77,88,3,0x30001000+n*0x2000,0x30002000+n*0x2000};s.cut=a.cut;live[n]=s;put(live[n].cut,0,e.identity.image+0xF0+n*0x50);put(live[n].cut,4,n);put(live[n].cut,8,a.address);put(live[n].cut,0x10,0);}
  auto&c=e.camera;c.address=0x20008248;c.pid=12;c.current_type=c.next_type=1;c.event_type=2;c.style=c.param_index=3;c.style_table=0x1004487C;c.style_count=145;c.param_pointer=c.style_table+3*0x84;c.type_table=0x10049350;c.type_count=63;c.timer=30;put(c.work37C,8,30);put(c.event_data,12,0xFFFFFFFFu);put(c.engine100,0x14,7);put(c.mode_phase13C,0,7);put(c.mode_phase13C,4,7);
  boundary.identity=e.identity;boundary.exclusive=boundary.fenced=boundary.manager_owned=true;boundary.control_mode=3;boundary.serial=9;boundary.logic_step=100;
 }
 Plan plan(bool cancel=false){return prepare(e,actors,live,e.camera,boundary,camera_policy,true,true,true,cancel);}
};
#include "mods/story_native_wait_consumer_write.h"
#include <algorithm>
#include <stdexcept>
#include <array>
#include <cstdint>
#include <span>
namespace W=story_native_wait_consumer_write;
static uint32_t get(std::span<const uint8_t>b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
struct Memory {
 Fixture f;Camera metadata=f.e.camera;Plan plan=f.plan();std::array<std::array<uint8_t,0x920>,2> actors{};std::array<uint8_t,0x8B8>camera{};
 unsigned writes=0,quarantines=0,captures=0;int fail_at=-1;bool throws=false,retained=true;int serial_before=-1,serial_after=-1;int change_pid_capture=-1,change_cut_capture=-1,lose_lease_after=-1;
 Memory(){
  for(unsigned n=0;n<2;++n){actors[n].fill(0xA5);std::copy(f.live[n].cut.begin(),f.live[n].cut.end(),actors[n].begin()+0x3E0);f.live[n].angles.fill(0x39);f.live[n].status=0xABCDEF00;}
  auto live=f.e.camera;live.current_type=live.next_type=2;live.style=live.param_index=4;live.param_pointer=live.style_table+4*0x84;put(live.event_data,12,1);live.flags=1;live.bg=8;live.trim=1;live.initial_view010.fill(0x66);live.view03C.fill(0x77);live.positions0A4.fill(0x88);live.work37C.fill(0x99);live.timer=10;put(live.work37C,8,10);live.engine100.fill(0);live.counter108=live.counter118=live.elapsed11C=0;live.mode_phase13C.fill(0);put(live.mode_phase13C,0,12);put(live.mode_phase13C,4,12);live.type_gate518.fill(0);
  plan=prepare(f.e,f.actors,f.live,live,f.boundary,camera_policy,true,true,true,false);assert(plan.valid());
  auto copy=[&](unsigned p,const auto&b){std::copy(b.begin(),b.end(),camera.begin()+p);};copy(0x10,live.initial_view010);copy(0x3C,live.view03C);copy(0xA4,live.positions0A4);copy(0x100,live.engine100);copy(0x13C,live.mode_phase13C);copy(0x37C,live.work37C);copy(0x3FC,live.event_data);copy(0x518,live.type_gate518);put(camera,0x68,live.bg);put(camera,0x600,live.trim);put(camera,0x510,live.flags);put(camera,0x514,live.style);put(camera,0x51C,live.current_type);put(camera,0x520,live.next_type);put(camera,0x8A8,live.param_pointer);put(camera,0x8AC,live.param_index);put(camera,0x8B4,live.event_type);
 }
 W::Source source(){W::Source s;
  s.capture=[&](W::State&v){++captures;v.people=f.live;for(unsigned n=0;n<2;++n)std::copy_n(actors[n].begin()+0x3E0,0x6C,v.people[n].cut.begin());
   auto&c=v.camera;c=metadata;c.event_type=get(camera,0x8B4);auto copy=[&](unsigned p,auto&b){std::copy_n(camera.begin()+p,b.size(),b.begin());};
   copy(0x10,c.initial_view010);copy(0x3C,c.view03C);copy(0xA4,c.positions0A4);copy(0x100,c.engine100);copy(0x13C,c.mode_phase13C);copy(0x37C,c.work37C);copy(0x3FC,c.event_data);copy(0x518,c.type_gate518);std::copy_n(c.engine100.begin(),3,c.done100.begin());c.bg=get(camera,0x68);c.trim=get(camera,0x600);c.flags=get(camera,0x510);c.style=get(camera,0x514);c.current_type=get(camera,0x51C);c.next_type=get(camera,0x520);c.param_pointer=get(camera,0x8A8);c.param_index=get(camera,0x8AC);c.timer=get(c.work37C,8);c.counter108=get(c.engine100,8);c.counter118=get(c.engine100,0x18);c.elapsed11C=get(c.engine100,0x1C);if(change_pid_capture>=0&&captures>=unsigned(change_pid_capture))++v.people[1].identity.pid;if(change_cut_capture>=0&&captures>=unsigned(change_cut_capture))v.people[1].cut[0]^=1;v.boundary=f.boundary;if(serial_before>=0&&writes==unsigned(serial_before))++v.boundary.serial;return true;};
  s.retained=[&](const auto&e){return retained&&(lose_lease_after<0||writes<unsigned(lose_lease_after))&&e==f.e;};
  s.person=[](const P::Snapshot&p,const story_native_event_observation::Actor&a){return p.identity.actor==a.address&&p.identity.pid==a.pid&&p.identity.token==a.token;};
  s.camera_identity=[](const Camera&c,const auto&e){return c.address==e.camera.address&&c.pid==e.camera.pid;};s.camera=camera_policy;
  s.write=[&](uint32_t a,std::span<const uint8_t>b){unsigned index=writes++;bool copied=false;for(unsigned n=0;n<2;++n)if(a==f.e.actors[n].address+0x3E0&&b.size()==0x6C){std::copy(b.begin(),b.end(),actors[n].begin()+0x3E0);copied=true;}if(a>=f.e.camera.address&&uint64_t(a)+b.size()<=uint64_t(f.e.camera.address)+camera.size()){std::copy(b.begin(),b.end(),camera.begin()+a-f.e.camera.address);copied=true;}assert(copied);if(serial_after>=0&&index==unsigned(serial_after))++f.boundary.serial;if(fail_at>=0&&index==unsigned(fail_at)){if(throws)throw std::runtime_error("after copy");return false;}return true;};
  s.quarantine=[&]{++quarantines;};return s;
 }
 W::Result run(W::Writer&w){return w.apply(plan,f.e,f.actors,source());}
};
int main(){
 {Memory m;W::Writer w;auto before=m.actors;assert(m.run(w)==W::Result::Complete&&m.writes==14&&!m.quarantines);for(unsigned n=0;n<2;++n)for(unsigned p=0;p<0x920;++p)if(p<0x3E0||p>=0x44C)assert(m.actors[n][p]==before[n][p]);assert(m.run(w)==W::Result::Refused&&m.writes==14);}
 {Memory m;W::Writer w;m.f.live[1].identity.pid++;assert(m.run(w)==W::Result::Refused&&!m.writes&&!m.quarantines);}
 {Memory m;W::Writer w;m.metadata.type_table+=4;assert(m.run(w)==W::Result::Refused&&!m.writes);}
 {Memory m;W::Writer w;m.retained=false;assert(m.run(w)==W::Result::Refused&&!m.writes);}
 {Memory m;W::Writer w;m.f.boundary.foreign=true;assert(m.run(w)==W::Result::Refused&&!m.writes);}
 {Memory m;W::Writer w;m.serial_before=0;assert(m.run(w)==W::Result::Refused&&!m.writes);}
 {Memory m;W::Writer w;auto cancel=m.f.plan(true);assert(w.apply(cancel,m.f.e,m.f.actors,m.source())==W::Result::Refused&&!m.writes);}
 {Memory m;W::Writer w;m.change_pid_capture=3;assert(m.run(w)==W::Result::Refused&&!m.writes);}
 {Memory m;W::Writer w;m.change_cut_capture=3;assert(m.run(w)==W::Result::Refused&&!m.writes);}
 {Memory m;W::Writer w;m.lose_lease_after=1;assert(m.run(w)==W::Result::Quarantined&&m.writes==1&&m.quarantines==1);}
 {Memory m;W::Writer w;auto s=m.source();s.quarantine=[&]{++m.quarantines;throw 3;};m.fail_at=0;assert(w.apply(m.plan,m.f.e,m.f.actors,s)==W::Result::Quarantined&&w.quarantined()&&m.quarantines==1);assert(w.apply(m.plan,m.f.e,m.f.actors,s)==W::Result::Quarantined&&m.quarantines==1);}
 for(int n=0;n<14;++n)for(bool throws:{false,true}){Memory m;W::Writer w;m.fail_at=n;m.throws=throws;assert(m.run(w)==W::Result::Quarantined&&m.writes==unsigned(n+1)&&m.quarantines==1);assert(m.run(w)==W::Result::Quarantined&&m.writes==unsigned(n+1)&&m.quarantines==1);}
 for(int n=0;n<14;++n){Memory m;W::Writer w;m.serial_after=n;assert(m.run(w)==W::Result::Quarantined&&m.writes==unsigned(n+1)&&m.quarantines==1);}
}
