// SPDX-License-Identifier: MPL-2.0
#define STORY_NATIVE_WAIT_SESSION_FIXTURE_ONLY
#include "story_native_wait_session_test.cpp"
#include "mods/story_native_wait_controller.h"
#include "mods/story_native_event_record.h"
#include <iostream>
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
#include <cstring>
namespace C=story_native_wait_controller;
static void run(unsigned fail,bool typed_turn=false,unsigned later_fault=0){
 auto*guest=map_arena();Readiness f;auto req=request();if(typed_turn){f.put(0x1004487C+4,1);f.half(0x10049350+2*0x40+0x18,1);f.put(0x1004487C+0x84+4,11);story_native_turn_camera_image::Program p;p.camera.start_eye={0,10,20};p.camera.end_eye={10,10,20};req.program=story_native_event_program::Program::turn(p,301);req.fixture_program="sdk-private-turn-v1";}S::Session session(req,f.source());assert(session.startup());Cpu cpu{};cpu.r[1]=0x600300C0;assert(session.inspect(&cpu));auto hold=session.try_fence();auto e=*session.expected();auto layout=session.arena();const auto selected=typed_turn?layout.turn:layout.wait;
 story_native_people_pose_record::Poses poses;for(unsigned n=0;n<2;++n){const auto&a=e.actors[n];poses[n].identity={a.address,a.pid,a.token,e.identity.stage,e.identity.room,e.identity.generation,0x30000000+n*0x2000,0x30001000+n*0x2000};poses[n].cut=a.cut;}if(typed_turn)assert(session.stage_image(poses));else assert(session.stage_image()&&session.stage_poses(poses));
 D::Tracker depth(e,{{1,1,true},{2,2,true},{3,3,true},true});
 auto ptr=[&](uint32_t a,size_t n)->uint8_t*{if(a>=layout.allocation.address&&uint64_t(a)+n<=uint64_t(layout.allocation.address)+layout.allocation.bytes)return mem::ptr(a);return f.pointer(a,n);};
 auto read=[&](uint32_t a,std::span<uint8_t>b){auto*p=ptr(a,b.size());if(!p)return false;std::copy_n(p,b.size(),b.begin());return true;};
 auto write=[&](uint32_t a,std::span<const uint8_t>b){auto*p=ptr(a,b.size());if(!p)return false;std::copy(b.begin(),b.end(),p);return true;};
 auto w=[&](uint32_t a,uint32_t v){std::array<uint8_t,4>b{};for(unsigned n=0;n<4;++n)b[n]=uint8_t(v>>(24-8*n));assert(write(a,b));};
 bool source_fault=false;uint64_t live_step=100;C::Sources sources;sources.validation.read=read;sources.validation.actor=[&](const Actor&a,const auto&i){return i==e.identity&&a.pid==e.actors[(a.address-e.actors[0].address)/0x1000].pid;};sources.validation.allocation=[](const auto&,const auto&){return true;};
 sources.validation.session=[&](story_native_event_memory::Scope&s){if(source_fault)return false;auto d=depth.snapshot();s={};s.identity=e.identity;s.link=link_actor;s.link_pid=55;s.exclusive=s.bindings_verified=true;s.fenced=d.state==D::State::Draining;s.logic_step=live_step;s.serial=d.serial;return true;};
 sources.camera_tables={0x10049348,0x10049350,0x10044878,0x1004487C};sources.write=[&](uint32_t at,std::span<const uint8_t>bytes){if(at==selected.record.address&&fail==18){auto*p=ptr(at,bytes.size());assert(p);std::copy_n(bytes.begin(),19,p);return false;}const bool copied=write(at,bytes);if(at==selected.record.address){if(fail==15)return false;if(fail==16)throw std::runtime_error("checkpoint false-after-copy");if(fail==17)++live_step;}return copied;};unsigned calls=0,claims=0,quarantines=0,arms=0;bool claim=false;
 sources.claim=[&](const auto&,const auto&,const auto&){claim=true;++claims;if(fail==9)throw std::runtime_error("claim callback");return fail!=10;};sources.retained=[&](const auto&){return claim&&(fail!=11||calls==0);};sources.quarantine=[&]{++quarantines;};sources.detach=[](Cpu*,const auto&){return false;};sources.stack=[](Cpu*,story_native_event_invoke::Config&c){c.stack={0x60030000,0x1000};return true;};
 sources.call=[&](Cpu*,uint32_t fn,std::initializer_list<uint32_t>regs){++calls;std::vector<uint32_t>a(regs);switch(fn){
 case 0x0253EC0C:f.byte(play+0x51D0+0xC0,1);return 1u;
 case 0x0253FD1C:f.byte(play+0x51D0+0xC0,0);f.byte(play+0x51D0+0xC2,3);for(unsigned p:{0xC4,0xC8,0xCC,0xD0})w(play+0x51D0+p,~0u);f.half(play+0x51D0+0xD8,0xFFFF);f.half(play+0x51D0+0xDE,0xFFFF);return 1u;
 case 0x0254273C:for(unsigned n=0;n<8;++n)w(e.identity.manager+n*4,0);return 0u;
 case 0x025443B4:{auto u=[&](uint32_t at){const auto*p=ptr(at,4);assert(p);return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};w(e.identity.manager,e.identity.image);for(unsigned n=0;n<7;++n){const auto offset=u(e.identity.image+n*8),count=u(e.identity.image+n*8+4);w(e.identity.manager+(n+1)*4,count?e.identity.image+offset:0);}return 0u;}
 case 0x0259F7D4:w(a[0],a[1]);w(a[0]+8,a[2]);w(a[0]+0x10,~0u);return 0u;
 case 0x02544534:w(e.identity.image+0xE4,1);return 1u;
 default:assert(false);return 0u;}};
 auto native=sources.call;sources.call=[&,native](Cpu*c,uint32_t fn,std::initializer_list<uint32_t>regs){auto v=native(c,fn,regs);c->r[10]=0xBAD;c->lr=44;if(fail<=7&&fail&&calls==fail)throw std::runtime_error("synthetic after native mutation");return v;};if(fail==8)sources.detach={};
 C::B::Config cfg;cfg.collector=f.config;cfg.collector.stamp=selected.stamp.address;cfg.memory={selected.stamp.address,link_actor,55,{selected.record.address,selected.record.bytes}};
 if(typed_turn)cfg.collector.clock=[&](story_native_event_collect::Clock&out){out={e.identity,live_step,depth.snapshot().serial,uint32_t(live_step-100)};return true;};
 sources.arm_clock=[&](const story_native_event_observation::Result&publish){++arms;assert(typed_turn&&calls==7&&claims==1&&claim&&publish.valid()&&publish.operation()==story_native_event_observation::Operation::Publish&&publish.event_state()==1&&publish.matches(e));assert(session.journal()->phase==story_native_wait_journal::Phase::Published&&session.baseline()==e);const auto snapshot=depth.snapshot();assert(snapshot.state==D::State::Active&&snapshot.idle()&&snapshot.coverage&&!snapshot.fault);const auto live_record=depth.record();story_native_event_depth::Record stored{};std::copy_n(mem::ptr(selected.depth.address),stored.size(),stored.begin());assert(stored==live_record&&story_native_event_depth::Tracker::valid_idle_record(stored,e));assert(mem::ptr(selected.image.address)[0xE7]==1);if(fail==13)throw std::runtime_error("clock arm callback");return fail!=12;};if(fail==14)sources.arm_clock={};
 C::Controller controller(session,depth,e,layout,{},cfg,sources);const Cpu cpu_before=cpu;auto result=controller.start(&cpu);assert(!std::memcmp(&cpu,&cpu_before,sizeof cpu));story_native_wait_journal::Record journal;assert(story_native_wait_journal::decode(std::span(mem::ptr(layout.wait.header.address),story_native_wait_journal::kBytes),journal));std::cerr<<"state="<<unsigned(result)<<" calls="<<calls<<" claims="<<claims<<" quarantine="<<quarantines<<" journal="<<unsigned(journal.phase)<<"\n";if(fail==8||fail==14){assert(!arms);assert(result==C::State::Refused&&!calls&&!claims&&!quarantines&&!controller.retained(e));}
 else if(fail){assert(result==C::State::Quarantined&&calls==(fail<=7?fail:fail==11?1:fail==12||fail==13||fail>=15?7:0)&&claims==1&&quarantines==1&&controller.retained(e)&&depth.snapshot().state==D::State::Draining&&journal.phase==story_native_wait_journal::Phase::Quarantined);const auto before_calls=calls;assert(controller.start(&cpu)==C::State::Quarantined&&quarantines==1&&calls==before_calls);assert(arms==unsigned(typed_turn&&(fail==12||fail==13||fail>=15)));}
 else {assert(result==C::State::Started&&calls==7&&claims==1&&!quarantines&&controller.retained(e)&&depth.snapshot().state==D::State::Active&&session.journal()->phase==story_native_wait_journal::Phase::Published);assert(controller.observe(&cpu)==C::State::Started);assert(arms==unsigned(typed_turn));
 auto check_record=[&](story_native_event_lease::Phase expected_phase,uint32_t elapsed,uint32_t flags){
  const auto*p=mem::ptr(selected.record.address);const uint32_t size=uint32_t(p[8])<<24|uint32_t(p[9])<<16|uint32_t(p[10])<<8|p[11];assert(size<=selected.record.bytes);
  if(typed_turn){story_native_turn_record::Record record;assert(story_native_turn_record::decode(std::span(p,size),record)&&record.expected==e&&record.phase==expected_phase&&record.elapsed_ticks==elapsed&&record.completion_flags==flags);for(unsigned n=0;n<2;++n)assert(record.poses[n].identity==poses[n].identity&&record.poses[n].cut==poses[n].cut&&record.poses[n].joint==poses[n].joint);}
  else{Expected saved;story_native_event_lease::Phase phase;assert(story_native_event_record::decode(std::span(p,size),saved,phase)&&saved==e&&phase==expected_phase);}
  assert(session.baseline()==e&&session.baseline_poses());
 };
 check_record(story_native_event_lease::Phase::Published,0,0);assert(controller.checkpoint(&cpu));check_record(story_native_event_lease::Phase::Published,0,0);
 // A parked save requester must use the genuine start CPU and cannot run
 // cleanup or mutate checkpoint storage for a transient native NONE/ORDER.
 const auto initial_calls=calls;const Cpu saved_cpu=cpu;Cpu other=cpu;
 assert(!controller.checkpoint(&other)&&controller.state()==C::State::Started&&!quarantines&&calls==initial_calls);
 auto unchanged_snapshot=[&](const auto&fn){
  std::vector<uint8_t> prior(mem::ptr(layout.allocation.address),mem::ptr(layout.allocation.address)+layout.allocation.bytes);
  assert(!fn()&&controller.state()==C::State::Started&&!quarantines&&calls==initial_calls);
  assert(std::equal(prior.begin(),prior.end(),mem::ptr(layout.allocation.address)));
 };
 w(e.identity.image+0xE4,0);unchanged_snapshot([&]{return controller.checkpoint(&cpu);});w(e.identity.image+0xE4,1);
 w(e.identity.manager+0x20,1);unchanged_snapshot([&]{return controller.checkpoint(&cpu);});w(e.identity.manager+0x20,0);
 assert(!std::memcmp(&cpu,&saved_cpu,sizeof cpu));


 if(!typed_turn){
 w(e.identity.image+0xE4,2);w(e.identity.manager+0x20,1);w(e.identity.manager+0x53C,e.identity.image+0x40);
 for(unsigned n=0;n<4;++n){w(e.identity.image+0xF0+n*0x50+0x38,n);*ptr(e.identity.image+0xF0+n*0x50+0x46,1)=2;}
 for(unsigned n=0;n<2;++n){w(e.actors[n].address+0x3E0+4,n);w(e.actors[n].address+0x3E0+0x10,0);}
 w(cam_process+0x248+0x400,2);w(cam_process+0x248+0x408,0);w(cam_process+0x248+0x51C,2);w(cam_process+0x248+0x520,2);
 const auto before_checkpoint_calls=calls;assert(controller.state()==C::State::Started&&controller.checkpoint(&cpu)&&calls==before_checkpoint_calls&&!std::memcmp(&cpu,&saved_cpu,sizeof cpu));auto observed=controller.state();assert(story_native_wait_journal::decode(std::span(mem::ptr(layout.wait.header.address),story_native_wait_journal::kBytes),journal));std::cerr<<"running_state="<<unsigned(observed)<<" journal="<<unsigned(journal.phase)<<"\n";assert(observed==C::State::Running&&session.journal()->phase==story_native_wait_journal::Phase::Running);check_record(story_native_event_lease::Phase::Running,0,0);
 }else {
  assert(calls==7&&arms==1&&session.journal()->phase==story_native_wait_journal::Phase::Published);
  D::Call call{D::Entry::ManagerRun,D::entry_address(D::Entry::ManagerRun),e.identity.manager,e.identity.manager,0,0,e.identity,D::Authority::Owned};{auto scope=depth.enter(call);assert(scope.allowed());}
  live_step=105;w(e.identity.image+0xE4,2);w(e.identity.manager+0x20,1);w(e.identity.manager+0x53C,e.identity.image+0x40);
  for(unsigned n=0;n<4;++n)w(e.identity.image+0xF0+n*0x50+0x38,e.program->initial_cut(n));
  for(unsigned n=0;n<2;++n){w(e.actors[n].address+0x3E4,n);w(e.actors[n].address+0x3F0,3);}
  w(e.identity.image+0x190+0x3C,4);const auto camera=e.camera.address;
  w(camera+0x51C,2);w(camera+0x520,2);w(camera+0x514,1);w(camera+0x8A8,0x1004487C+0x84);w(camera+0x8AC,1);w(camera+0x68,9);w(camera+0x600,2);w(camera+0x400,2);w(camera+0x408,e.camera.current_type);w(camera+0x418,2);w(camera+0x420,1);
  const auto pristine=e.program->pristine();auto copy=[&](uint32_t at,unsigned offset,size_t count){assert(write(at,pristine.subspan(offset,count)));};
  f.byte(camera+0x37C,1);copy(camera+0x380,0xD80,12);copy(camera+0x38C,0xD8C,12);copy(camera+0x398,0xD98,4);f.byte(camera+0x3A4,'-');f.byte(camera+0x3A5,'-');w(camera+0x3A8,e.program->turn_definition()->camera.hold_timer);f.byte(camera+0x3AC,1);
  const auto before_checkpoint_calls=calls;assert(controller.state()==C::State::Started&&controller.checkpoint(&cpu)&&controller.state()==C::State::Running&&calls==before_checkpoint_calls&&!std::memcmp(&cpu,&saved_cpu,sizeof cpu));check_record(story_native_event_lease::Phase::Running,5,0);
  w(e.identity.manager+0x3C,4);live_step=106;assert(controller.checkpoint(&cpu));check_record(story_native_event_lease::Phase::Running,6,4);
  if(later_fault){
   if(later_fault==1)live_step=105;
   if(later_fault==2){live_step=107;w(e.identity.manager+0x3C,0);}
   if(later_fault==3)mem::ptr(selected.record.address)[20]^=1;
   if(later_fault==4)source_fault=true;
   const auto*p=mem::ptr(selected.record.address);std::vector<uint8_t> prior(p,p+selected.record.bytes);const auto prior_calls=calls;
   assert(!controller.checkpoint(&cpu)&&controller.state()==C::State::Quarantined&&controller.retained(e)&&claim&&quarantines==1&&calls==prior_calls);
   assert(std::equal(prior.begin(),prior.end(),mem::ptr(selected.record.address))&&!controller.checkpoint(&cpu)&&calls==prior_calls);
  }

 }
 }
 hold.reset();unmap_arena(guest);
}

int main(){for(unsigned n=0;n<=11;++n)run(n);for(unsigned n:{0u,1u,11u,12u,13u,14u,15u,16u,17u,18u})run(n,true);for(unsigned n:{15u,16u,17u,18u})run(n);for(unsigned n=1;n<=4;++n)run(0,true,n);}
