// SPDX-License-Identifier: MPL-2.0
#define STORY_NATIVE_WAIT_SESSION_FIXTURE_ONLY
#include "story_native_wait_session_test.cpp"
#include "mods/story_native_wait_controller.h"
#include <iostream>
#include "mods/story_native_wait_runtime_consumers.h"
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
#include <utility>
#include <functional>
#include <cstring>
#include <memory>
namespace C=story_native_wait_controller;
static void terminal_run(unsigned mode){
 const bool before_return=mode==1;
 auto*guest=map_arena();Readiness f;std::function<void()> during_readiness;auto readiness_sources=f.source();auto original_boundary=readiness_sources.boundary;readiness_sources.boundary=[&](const Cpu*c,auto&out){const bool ok=original_boundary(c,out);if(during_readiness){auto hook=std::move(during_readiness);during_readiness={};hook();}return ok;};S::Session session(request(),readiness_sources);assert(session.startup());Cpu cpu{};cpu.r[1]=0x600300C0;assert(session.inspect(&cpu));auto hold=session.try_fence();auto e=*session.expected();
#ifdef STORY_NATIVE_SESSION_PRISTINE_BEFORE_STAGE
 STORY_NATIVE_SESSION_PRISTINE_BEFORE_STAGE(f,session,cpu,hold);
#endif
 assert(session.stage_image());auto layout=session.arena();
 f.add(0x101C41D4,0x28);f.put(0x101C41D4+0x1C,0x432A0000);f.put(0x101C41D4+0x24,0x43160000);
 for(unsigned n=0;n<2;++n){const auto&a=e.actors[n];const uint32_t morf=0x30000000+n*0x2000,model=morf+0x1000;f.add(morf,0xC8);f.add(model,0xBC);f.put(a.address+0x8A0,0x101C41D4);f.put(a.address+0xB4,0x100209C4);f.put(a.address+0x44C,morf);f.put(morf+0x90,model);f.put(model+0xB8,a.address);f.put(a.address+0x2E0,0x40000);}
 story_native_people_pose_record::Poses poses;for(unsigned n=0;n<2;++n){const auto&a=e.actors[n];poses[n].identity={a.address,a.pid,a.token,e.identity.stage,e.identity.room,e.identity.generation,0x30000000+n*0x2000,0x30001000+n*0x2000};poses[n].cut=a.cut;poses[n].status=0x40000;}assert(session.stage_poses(poses));
 D::Tracker depth(e,{{1,1,true},{2,2,true},{3,3,true},true});
 auto ptr=[&](uint32_t a,size_t n)->uint8_t*{if(a>=layout.allocation.address&&uint64_t(a)+n<=uint64_t(layout.allocation.address)+layout.allocation.bytes)return mem::ptr(a);return f.pointer(a,n);};
 auto read=[&](uint32_t a,std::span<uint8_t>b){auto*p=ptr(a,b.size());if(!p)return false;std::copy_n(p,b.size(),b.begin());return true;};
 auto write=[&](uint32_t a,std::span<const uint8_t>b){auto*p=ptr(a,b.size());if(!p)return false;std::copy(b.begin(),b.end(),p);return true;};
 auto w=[&](uint32_t a,uint32_t v){std::array<uint8_t,4>b{};for(unsigned n=0;n<4;++n)b[n]=uint8_t(v>>(24-8*n));assert(write(a,b));};
 std::unique_ptr<story_native_wait_runtime_consumers::Adapter> consumers;story_native_wait_consumer_write::Writer writer;unsigned consumer_writes=0;
 C::Sources sources;sources.validation.read=read;sources.validation.actor=[&](const Actor&a,const auto&i){return i==e.identity&&a.pid==e.actors[(a.address-e.actors[0].address)/0x1000].pid;};sources.validation.allocation=[](const auto&,const auto&){return true;};
 sources.validation.session=[&](story_native_event_memory::Scope&s){auto d=depth.snapshot();s={};s.identity=e.identity;s.link=link_actor;s.link_pid=55;s.exclusive=s.bindings_verified=true;s.fenced=d.state==D::State::Draining;s.logic_step=100;s.serial=d.serial;return true;};
 sources.camera_tables={0x10049348,0x10049350,0x10044878,0x1004487C};sources.write=write;unsigned calls=0,claims=0,quarantines=0,releases=0;bool claim=false;
 sources.claim=[&](const auto&,const auto&,const auto&){claim=true;++claims;return true;};sources.retained=[&](const auto&){return claim;};sources.quarantine=[&]{++quarantines;};sources.detach=[&](Cpu*,const auto&expected){
  assert(expected==e&&session.journal()->phase==story_native_wait_journal::Phase::Detaching);
  auto source=consumers->source();story_native_wait_consumer_write::State state;assert(source.capture(state));
  auto plan=story_native_wait_detach_plan::prepare(e,poses,state.people,state.camera,state.boundary,source.camera,true,true,true,false);
  assert(plan.valid());return writer.apply(plan,e,poses,source)==story_native_wait_consumer_write::Result::Complete;
 };sources.stack=[](Cpu*,story_native_event_invoke::Config&c){c.stack={0x60030000,0x1000};return true;};
 sources.call=[&](Cpu*,uint32_t fn,std::initializer_list<uint32_t>regs){++calls;std::vector<uint32_t>a(regs);switch(fn){
 case 0x0253EFA8:f.byte(play+0x51D0+0xC2,0);return 1u;
 case 0x0253EC0C:f.byte(play+0x51D0+0xC0,1);return 1u;
 case 0x0253FD1C:f.byte(play+0x51D0+0xC0,0);f.byte(play+0x51D0+0xC2,3);for(unsigned p:{0xC4,0xC8,0xCC,0xD0})w(play+0x51D0+p,~0u);f.half(play+0x51D0+0xD8,0xFFFF);f.half(play+0x51D0+0xDE,0xFFFF);return 1u;
 case 0x0254273C:for(unsigned n=0;n<8;++n)w(e.identity.manager+n*4,0);return 0u;
 case 0x025443B4:{std::array<uint32_t,8>p={e.identity.image,e.identity.image+0x40,e.identity.image+0xF0,e.identity.image+0x230,e.identity.image+0x410,0,e.identity.image+0x510,0};for(unsigned n=0;n<8;++n)w(e.identity.manager+n*4,p[n]);return 0u;}
 case 0x0259F7D4:w(a[0],a[1]);w(a[0]+8,a[2]);w(a[0]+0x10,~0u);return 0u;
 case 0x02544534:w(e.identity.image+0xE4,1);return 1u;
 default:assert(false);return 0u;}};
 auto native=sources.call;sources.call=[&,native](Cpu*c,uint32_t fn,std::initializer_list<uint32_t>regs){auto v=native(c,fn,regs);c->r[10]=0xBAD;c->lr=44;return v;};
 C::B::Config cfg;cfg.collector=f.config;cfg.collector.stamp=layout.wait.stamp.address;cfg.memory={layout.wait.stamp.address,link_actor,55,{layout.wait.record.address,layout.wait.record.bytes}};
 sources.release=[&](const auto&x){assert(x==e&&session.journal()->phase==story_native_wait_journal::Phase::Unpublished);claim=false;++releases;return true;};
 auto controller_owner=std::make_unique<C::Controller>(session,depth,e,layout,C::Stage{},cfg,sources);auto&controller=*controller_owner;
 story_native_wait_runtime_consumers::Sources cs;cs.read=read;cs.retained=[&](const auto&x){return claim&&controller.retained(x);};cs.resident=sources.validation.actor;cs.quarantine=[&]{++quarantines;};
 cs.boundary=[&](auto&b){auto d=depth.snapshot();b={};b.identity=e.identity;b.exclusive=claim;b.fenced=d.state==D::State::Draining;b.manager_owned=true;b.control_mode=*ptr(play+0x51D0+0xC2,1);b.logic_step=100;b.serial=d.serial;b.foreign=d.foreign_depth!=0;b.fault=d.fault;b.manager_depth=d.manager_depth;b.camera_depth=d.camera_depth;b.npc_depth=d.npc_depth;b.callback_depth=d.callback_depth;b.foreign_depth=d.foreign_depth;return session.fence_held();};
 cs.write=[&](uint32_t a,std::span<const uint8_t>b){assert(session.journal()->phase==story_native_wait_journal::Phase::Detaching);++consumer_writes;return write(a,b);};
 consumers=std::make_unique<story_native_wait_runtime_consumers::Adapter>(e,poses,cs);assert(consumers->configured());
 const Cpu before_start=cpu;assert(controller.start(&cpu)==C::State::Started&&calls==7&&claims==1);assert(!std::memcmp(&cpu,&before_start,sizeof cpu));
 w(e.identity.image+0xE4,2);w(e.identity.manager+0x20,1);w(e.identity.manager+0x53C,e.identity.image+0x40);
 for(unsigned n=0;n<4;++n){w(e.identity.image+0xF0+n*0x50+0x38,n);*ptr(e.identity.image+0xF0+n*0x50+0x46,1)=2;}
 for(unsigned n=0;n<2;++n){w(e.actors[n].address+0x3E4,n);w(e.actors[n].address+0x3F0,0);w(e.actors[n].address+0x2E0,0x49800);f.half(e.actors[n].address+0x320,0x1234+n);f.half(e.actors[n].address+0x328,0x2345+n);w(e.actors[n].address+0x314,0x3F800000+n);f.byte(e.actors[n].address+0x3AD,0x41+n);}
 w(cam_process+0x248+0x400,2);w(cam_process+0x248+0x408,0);w(cam_process+0x248+0x51C,2);w(cam_process+0x248+0x520,2);
 assert(controller.observe(&cpu)==C::State::Running);
 w(e.identity.image+0xE4,4);w(e.identity.manager+0x53C,0);
 namespace T=story_native_manager_terminal;
 T::Identity ti;ti.lease=e.identity;ti.event=e.identity.image+0x40;ti.index=0;
 auto actor_probe=[&](uint32_t address,T::Actor&out){for(unsigned n=0;n<2;++n){const auto&a=e.actors[n];if(a.address!=address)continue;std::array<uint8_t,4>status{};if(!read(address+0x2E0,status))return false;out={a.address,a.pid,a.token,a.profile,a.subtype,uint32_t(status[0])<<24|uint32_t(status[1])<<16|uint32_t(status[2])<<8|status[3],a.fingerprint};return true;}return false;};
 for(unsigned n=0;n<2;++n){assert(actor_probe(e.actors[n].address,ti.actors[n]));ti.actors[n].status=poses[n].status;}
 T::Terminal*active_terminal=nullptr;unsigned all_off=0;
 T::Terminal terminal(ti,[&](T::Observation&o){auto d=depth.snapshot();o.lease=e.identity;o.event=ti.event;o.index=0;std::array<uint8_t,4>b{};assert(read(e.identity.image+0xE4,b));o.event_state=uint32_t(b[3]);assert(read(e.identity.manager+0x20,b));o.camera_play=b[3];for(unsigned n=0;n<2;++n)if(!actor_probe(e.actors[n].address,o.actors[n]))return false;o.retained_allocation=controller.retained(e);o.entry_fence=session.fence_held()&&d.state==D::State::Draining;o.idle_depths=d.idle();o.manager_table_owned=true;return true;},actor_probe,[&](uint32_t manager,int32_t index,uint32_t force){
  assert(manager==e.identity.manager&&index==0&&force==0);
  for(uint32_t address:{e.actors[0].address,0x22220000u,e.actors[1].address})active_terminal->route_all_off(address,[&]{++all_off;T::Actor current;assert(actor_probe(address,current));w(address+0x2E0,current.status&~T::kAllOffMask);return 0u;});
  w(e.identity.image+0xE4,0);w(e.identity.manager+0x20,0);
 });active_terminal=&terminal;
 assert(depth.begin_drain());assert(terminal.close()==T::Result::Completed&&all_off==2);assert(depth.cancel_drain());
 const story_native_camera_capture::Layout camera_layout{play,0x101D55F0,0x101D5634,0x101D5538,{0x025021A4,0x02502288,0x024FFA3C,0x0250235C,0x024FFC40},{0x10049348,0x10049350,0x10044878,0x1004487C}};
 story_native_camera_return::Sources bs;bs.capture=[&](auto*,auto&v){v.identity=e.identity;v.event_state=v.camera_play=0;v.terminal_serial=terminal.completion().serial();v.retained=controller.retained(e);v.exclusive=claim;return story_native_camera_capture::capture(read,camera_layout,e,v.camera);};bs.verify_camera=[&](const auto&camera,const auto&i){return story_native_camera_policy::verify(sources.camera_tables,read,camera,i).valid;};bs.owned_thread=[&](auto*c){return c==&cpu;};bs.route=[]{return true;};
 story_native_camera_return::Barrier barrier(e.identity,e.camera,bs);assert(barrier.arm(terminal.completion()));
 if(before_return){assert(controller.stop_terminal(&cpu,terminal,barrier)==C::State::Quarantined&&controller.retained(e)&&claim&&!consumer_writes&&quarantines==1&&!writer.attempted()&&!releases&&session.quarantined());story_native_wait_journal::Record saved;assert(story_native_wait_journal::decode(std::span(mem::ptr(layout.wait.header.address),story_native_wait_journal::kBytes),saved)&&saved.phase==story_native_wait_journal::Phase::Quarantined);}
 else {
  cpu.r[3]=e.camera.address;unsigned runs=0;assert(barrier.run(&cpu,0x024FE3E8,[&]{++runs;w(e.camera.address+0x51C,e.camera.current_type);w(e.camera.address+0x520,e.camera.next_type);w(e.camera.address+0x408,~0u);})==story_native_camera_return::Decision::Observed&&runs==1&&barrier.returned());
  assert(controller.observe(&cpu)==C::State::TerminalReady);
  std::array<std::array<uint8_t,0x920>,2> native_final;for(unsigned n=0;n<2;++n)assert(read(e.actors[n].address,native_final[n]));
  auto released=controller.stop_terminal(&cpu,terminal,barrier);std::cerr<<"terminal release="<<unsigned(released)<<" writes="<<consumer_writes<<" quarantine="<<quarantines<<"\n";
  assert(released==C::State::Released&&!controller.retained(e)&&!claim&&consumer_writes==14&&!quarantines&&releases==1&&session.journal()->phase==story_native_wait_journal::Phase::Unpublished);
  for(unsigned n=0;n<2;++n){std::array<uint8_t,0x920> after{};assert(read(e.actors[n].address,after));for(unsigned p=0;p<after.size();++p)if(p<0x3E0||p>=0x44C)assert(after[p]==native_final[n][p]);assert(std::equal(poses[n].cut.begin(),poses[n].cut.end(),after.begin()+0x3E0));}
  const auto old_journal=*session.journal();const auto old_expected=e;
  assert(!session.unexposed()&&!session.expected());
  auto certificate=controller.take_rearm_certificate();assert(certificate&&!controller.take_rearm_certificate());
  if(mode==2){hold.reset();assert(!session.rearm(std::move(*certificate),&cpu));hold=session.try_fence();assert(!session.rearm(std::move(*certificate),&cpu));}
  else if(mode==3){controller_owner.reset();assert(!session.rearm(std::move(*certificate),&cpu));}
  else if(mode==4){auto call=D::Call{};call.entry=D::Entry::PeopleExecute;call.function=D::entry_address(call.entry);call.receiver=e.actors[0].address;call.authority=D::Authority::Foreign;{auto scope=depth.enter(call);assert(!scope.allowed());}assert(!session.rearm(std::move(*certificate),&cpu));}
  else if(mode==5){w(e.actors[1].address+4,e.actors[1].pid+1);assert(!session.rearm(std::move(*certificate),&cpu));}
  else if(mode==6){Cpu other=cpu;assert(!session.rearm(std::move(*certificate),&other));assert(!session.rearm(std::move(*certificate),&cpu));}
  else if(mode==7){during_readiness=[&]{assert(depth.cancel_drain());};assert(!session.rearm(std::move(*certificate),&cpu));assert(!during_readiness);}
  else if(mode==8){S::Session other(request(),f.source());assert(!other.rearm(std::move(*certificate),&cpu));assert(!session.rearm(std::move(*certificate),&cpu));}
  else {
   assert(session.rearm(std::move(*certificate),&cpu));assert(!session.rearm(std::move(*certificate),&cpu));
   const auto next_journal=*session.journal();assert(next_journal.generation>old_journal.generation&&next_journal.serial==old_journal.serial+1&&next_journal.definition==old_journal.definition);
   assert(!session.unexposed()&&!session.baseline()&&!session.staged_expected()&&!session.quarantined());
#ifdef STORY_NATIVE_SESSION_IDLE_TEST_HOOK
   STORY_NATIVE_SESSION_IDLE_TEST_HOOK(f,session,cpu,hold);
#endif
   e=*session.expected();assert(e.identity.generation==next_journal.generation&&e.identity.image==old_expected.identity.image&&e!=old_expected);
   for(unsigned n=0;n<2;++n){poses[n].identity.generation=e.identity.generation;poses[n].cut=e.actors[n].cut;}
   assert(session.stage_image()&&session.stage_poses(poses));
   D::Tracker next_depth(e,{{1,1,true},{2,2,true},{3,3,true},true});
   auto next_sources=sources;next_sources.validation.session=[&](story_native_event_memory::Scope&out){auto d=next_depth.snapshot();out={};out.identity=e.identity;out.link=link_actor;out.link_pid=55;out.exclusive=out.bindings_verified=true;out.fenced=d.state==D::State::Draining;out.logic_step=101;out.serial=d.serial;return true;};
   const auto old_calls=calls;C::Controller next(session,next_depth,e,layout,{},cfg,next_sources);
   assert(!next.take_rearm_certificate()&&next.start(&cpu)==C::State::Started&&calls==old_calls+7&&claims==2);
   assert(session.journal()->phase==story_native_wait_journal::Phase::Published&&!session.unexposed());
  }
  if(mode>=2){assert(session.journal()==old_journal&&!session.unexposed()&&!session.expected());}
 }
 assert(story_native_event_arena::owns(layout,mem::runtime_alloc_log(),mem::runtime_top()));
 hold.reset();unmap_arena(guest);
}
#ifndef STORY_NATIVE_WAIT_TERMINAL_FIXTURE_ONLY
int main(){for(unsigned mode=0;mode<=8;++mode)terminal_run(mode);}
#endif
