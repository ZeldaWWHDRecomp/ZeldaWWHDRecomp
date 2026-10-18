// SPDX-License-Identifier: MPL-2.0
#define log_msg fixture_silenced_log
#define STORY_NATIVE_WAIT_SESSION_FIXTURE_ONLY
#include "story_native_wait_session_test.cpp"
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
#include <utility>
#include <functional>
#include <cstring>
#include <memory>
#undef log_msg
#include <cstdarg>
#include <cstdio>
void log_msg(const char*fmt,...){va_list args;va_start(args,fmt);std::vfprintf(stderr,fmt,args);std::fputc('\n',stderr);va_end(args);}
#include "mods/story_native_wait_controller.h"
#include "mods/story_native_event_record.h"
#include <iostream>
#include "mods/story_native_cancel_pose_capture.h"
#include "mods/story_native_camera_capture.h"
#include "mods/story_native_cancel_consumer_write.h"
#include <algorithm>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
#include <utility>
#include <functional>
#include <cstring>
#include <memory>
// Synthetic scheduler/outer-return issuer primitives only. Actual production
// issuers live exclusively in threads.cpp/guest_mods.cpp; this test exercises
// real Controller/Session journal and genuine helper receipts, not a scheduler.
namespace threads {class NativeQuiescenceIssuer {public:static NativeQuiescence test(){return NativeQuiescence([](const Cpu*){return true;},[]{});}static QuiescenceWitness witness(Cpu*c,bool&live){return QuiescenceWitness([c,&live](const Cpu*x){return live&&x==c;});}};}
namespace guestmods {class NativeOuterReturnIssuer {public:static void test(Cpu*c,story_native_event_depth::Tracker&t,story_native_event_depth::FrameWitness w,const std::function<void(const story_native_outer_return::OuterReturn&)>&f){story_native_outer_return::OuterReturn token(c,t,std::move(w));f(token);}};}
namespace CO=story_native_cancel_observation;namespace MC=story_native_manager_cancel;namespace CB=story_native_camera_cancel_barrier;namespace RF=story_native_people_cancel_refresh;
namespace C=story_native_wait_controller;
static void run(unsigned cancel_fault){
 const unsigned fail=0;const bool typed_turn=true;
 auto*guest=map_arena();Readiness f;auto req=request();if(typed_turn){f.put(0x1004487C+4,1);f.half(0x10049350+2*0x40+0x18,1);f.put(0x1004487C+0x84+4,11);story_native_turn_camera_image::Program p;p.camera.start_eye={0,10,20};p.camera.end_eye={10,10,20};req.program=story_native_event_program::Program::turn(p,301);req.fixture_program="sdk-private-turn-v1";}S::Session session(req,f.source());assert(session.startup());Cpu cpu{};cpu.r[1]=0x600300C0;assert(session.inspect(&cpu));auto hold=session.try_fence();auto e=*session.expected();auto layout=session.arena();const auto selected=typed_turn?layout.turn:layout.wait;
 story_native_people_pose_record::Poses poses;for(unsigned n=0;n<2;++n){const auto&a=e.actors[n];poses[n].identity={a.address,a.pid,a.token,e.identity.stage,e.identity.room,e.identity.generation,0x30000000+n*0x2000,0x30001000+n*0x2000};poses[n].cut=a.cut;}f.add(0x101C41D4,0x28);f.put(0x101C41D4+0x1C,0x432A0000);f.put(0x101C41D4+0x24,0x43160000);
 for(unsigned n=0;n<2;++n){const auto&a=e.actors[n];f.add(poses[n].identity.morf,0xC8);f.add(poses[n].identity.model,0x170);f.put(a.address+0x44C,poses[n].identity.morf);f.put(poses[n].identity.morf+0x90,poses[n].identity.model);f.put(poses[n].identity.model+0xB8,a.address);f.put(a.address+0xB4,0x100209C4);f.put(a.address+0x8A0,0x101C41D4);}
 if(typed_turn)assert(session.stage_image(poses));else assert(session.stage_image()&&session.stage_poses(poses));
 D::Tracker depth(e,{{1,1,true},{2,2,true},{3,3,true},true});
 auto ptr=[&](uint32_t a,size_t n)->uint8_t*{if(a>=layout.allocation.address&&uint64_t(a)+n<=uint64_t(layout.allocation.address)+layout.allocation.bytes)return mem::ptr(a);return f.pointer(a,n);};
 auto read=[&](uint32_t a,std::span<uint8_t>b){auto*p=ptr(a,b.size());if(!p)return false;std::copy_n(p,b.size(),b.begin());return true;};
 auto write=[&](uint32_t a,std::span<const uint8_t>b){auto*p=ptr(a,b.size());if(!p)return false;std::copy(b.begin(),b.end(),p);return true;};
 auto w=[&](uint32_t a,uint32_t v){std::array<uint8_t,4>b{};for(unsigned n=0;n<4;++n)b[n]=uint8_t(v>>(24-8*n));assert(write(a,b));};
 bool control_restore_fault=false;bool source_fault=false;uint64_t live_step=100;C::Sources sources;sources.validation.read=read;sources.validation.actor=[&](const Actor&a,const auto&i){return i==e.identity&&a.pid==e.actors[(a.address-e.actors[0].address)/0x1000].pid;};sources.validation.allocation=[](const auto&,const auto&){return true;};
 sources.validation.session=[&](story_native_event_memory::Scope&s){if(source_fault)return false;auto d=depth.snapshot();s={};s.identity=e.identity;s.link=link_actor;s.link_pid=55;s.exclusive=s.bindings_verified=true;s.fenced=d.state==D::State::Draining;s.logic_step=live_step;s.serial=d.serial;return true;};
 sources.camera_tables={0x10049348,0x10049350,0x10044878,0x1004487C};sources.write=[&](uint32_t at,std::span<const uint8_t>bytes){if(at==selected.record.address&&fail==18){auto*p=ptr(at,bytes.size());assert(p);std::copy_n(bytes.begin(),19,p);return false;}const bool copied=write(at,bytes);if(control_restore_fault&&at==cpu.r[1]-0x40&&bytes.size()==0x40){if(cancel_fault==11)throw std::runtime_error("control frame restore throw");return false;}if(at==selected.record.address){if(fail==15)return false;if(fail==16)throw std::runtime_error("checkpoint false-after-copy");if(fail==17)++live_step;}return copied;};unsigned calls=0,claims=0,quarantines=0,arms=0;bool claim=false;
 sources.claim=[&](const auto&,const auto&,const auto&){claim=true;++claims;if(fail==9)throw std::runtime_error("claim callback");return fail!=10;};sources.retained=[&](const auto&){return claim&&(fail!=11||calls==0);};sources.quarantine=[&]{++quarantines;};sources.detach=[](Cpu*,const auto&){return false;};sources.stack=[](Cpu*,story_native_event_invoke::Config&c){c.stack={0x60030000,0x1000};return true;};
 sources.call=[&](Cpu*,uint32_t fn,std::initializer_list<uint32_t>regs){++calls;std::vector<uint32_t>a(regs);switch(fn){
 case 0x0253EFA8:if(cancel_fault==4){cpu.r[10]=0xBAD;cpu.lr=0xBAD;w(cpu.r[1]-0x40,0xBAD);throw std::runtime_error("cancel EndControl native exception");}f.byte(play+0x51D0+0xC2,0);return 1u;
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
 sources.release=[&](const auto&x){assert(x==e&&session.journal()->phase==story_native_wait_journal::Phase::Unpublished);claim=false;return true;};
#ifdef STORY_NATIVE_CANCEL_REARM_BEFORE_CONTROLLER
 STORY_NATIVE_CANCEL_REARM_BEFORE_CONTROLLER(f,session,depth,cpu,e,layout,cfg,sources,source_fault,live_step);
#endif
 auto controller_owner=std::make_unique<C::Controller>(session,depth,e,layout,story_native_stage_owner_observer::Snapshot{},cfg,sources);auto&controller=*controller_owner;
 assert(controller.start(&cpu)==C::State::Started&&calls==7&&claim&&arms==1);
 {D::Call manager_entry{D::Entry::ManagerRun,D::entry_address(D::Entry::ManagerRun),e.identity.manager,e.identity.manager,0,0,e.identity,D::Authority::Owned};auto manager_scope=depth.enter(manager_entry);assert(manager_scope.allowed());
 // Synthetic genuine native manager/cast side effects: both SDK actors are
 // selected by the original engine and its StartProc sets FORCEMOVE.
 w(e.identity.image+0xE4,2);w(e.identity.manager+0x20,1);w(e.identity.manager+0x53C,e.identity.image+0x40);
 for(unsigned n=0;n<4;++n){w(e.identity.image+0xF0+n*0x50+0x38,e.program->initial_cut(n));w(e.identity.image+0xF0+n*0x50+0x3C,e.program->cut(n,e.program->initial_cut(n))->action);}
 for(unsigned n=0;n<2;++n){w(e.actors[n].address+0x3E4,n);w(e.actors[n].address+0x3F0,3);w(e.actors[n].address+0x2E0,0x8000);f.half(e.actors[n].address+0x320,0x1234+n);}
 live_step=105;const auto cam=e.camera.address;
 w(cam+0x51C,2);w(cam+0x520,2);w(cam+0x514,1);w(cam+0x8A8,0x1004487C+0x84);w(cam+0x8AC,1);w(cam+0x68,9);w(cam+0x600,2);w(cam+0x400,2);w(cam+0x408,e.camera.current_type);w(cam+0x418,2);w(cam+0x420,1);
 const auto pristine=e.program->pristine();auto copy=[&](uint32_t at,unsigned offset,size_t count){assert(write(at,pristine.subspan(offset,count)));};
 f.byte(cam+0x37C,1);copy(cam+0x380,0xD80,12);copy(cam+0x38C,0xD8C,12);copy(cam+0x398,0xD98,4);f.byte(cam+0x3A4,'-');f.byte(cam+0x3A5,'-');w(cam+0x3A8,e.program->turn_definition()->camera.hold_timer);f.byte(cam+0x3AC,1);
 }
 const auto cam=e.camera.address;
 const story_native_camera_capture::Layout camera_layout{play,0x101D55F0,0x101D5634,0x101D5538,{0x025021A4,0x02502288,0x024FFA3C,0x0250235C,0x024FFC40},{0x10049348,0x10049350,0x10044878,0x1004487C}};
 auto camera_read=[&](Camera&out){return story_native_camera_capture::capture(read,camera_layout,e,out);};
 auto camera_verify=[&](const auto&camera,const auto&i){return story_native_camera_policy::verify(sources.camera_tables,read,camera,i).valid;};
 if(cancel_fault!=7)assert(controller.checkpoint(&cpu)&&controller.state()==C::State::Running);
 MC::Sources ms;ms.read=read;ms.write=write;ms.resident=[&](const auto&x,unsigned){return x==e&&claim;};ms.routes=[]{return true;};ms.arena=[&](const auto&x,auto&out){out={layout.allocation.address,layout.allocation.bytes};return x==e;};ms.stack=[&](Cpu*,auto&out){out.stack={0x60030000,0x1000};out.protected_ranges={{layout.allocation.address,layout.allocation.bytes}};return true;};ms.quarantine=[&]{++quarantines;};
 uint64_t outer_serial=0;MC::Boundary boundary;
 auto frame=[&](Cpu*,MC::Boundary&out){out={};out.expected=e;out.parent={D::Entry::CameraExecute,D::entry_address(D::Entry::CameraExecute),cam_process,cam_process,e.camera.pid,0,e.identity,D::Authority::Owned};out.depth=depth.snapshot();out.logic_step=live_step;out.elapsed=uint32_t(live_step-100);out.retained=claim;out.fenced=session.fence_held();out.stage=out.exclusive=true;return true;};ms.boundary=frame;
 MC::Cancel*active=nullptr;unsigned force_calls=0,all_off=0;
 ms.call=[&](Cpu*,uint32_t fn,std::initializer_list<uint32_t>args){assert(active->native_cancel_scope()&&fn==0x0254465C&&std::vector<uint32_t>(args)==std::vector<uint32_t>({e.identity.manager,0,1}));++force_calls;for(unsigned n=0;n<2;++n)assert(active->route_all_off(e.actors[n].address,[&,n]{++all_off;w(e.actors[n].address+0x2E0,0);return 0u;})==0);w(e.identity.image+0xE4,0);w(e.identity.manager+0x20,0);return 0u;};
 MC::Cancel manager(e,poses,ms);active=&manager;
 CB::Sources bs;bs.frame=[&](Cpu*c,auto&out){out.outer_serial=outer_serial;return frame(c,out.boundary);};bs.owned_thread=[&](Cpu*c){return c==&cpu;};bs.read=read;bs.resident=ms.resident;bs.camera=camera_read;bs.verify_camera=[&](const auto&x,const auto&camera){return x==e&&camera_verify(camera,e.identity);};bs.original_route=[](Cpu*,uint32_t fn){return fn==0x024FE3E8;};bs.quarantine=ms.quarantine;
 CB::Barrier barrier(e,bs);D::FrameWitness witness;
 {D::Call call{D::Entry::CameraExecute,D::entry_address(D::Entry::CameraExecute),cam_process,cam_process,e.camera.pid,0,e.identity,D::Authority::Owned};auto scope=depth.enter(call);assert(scope.allowed());witness=*depth.current_owned_frame();outer_serial=witness.root_serial();cpu.r[3]=cam;
  const auto ready=barrier.ready(&cpu,0x024FE3E8);assert(ready);assert(manager.run(&cpu)==MC::Result::Completed&&force_calls==1&&all_off==2);
  assert(barrier.run(&cpu,0x024FE3E8,*ready,manager.completion(),[&]{w(cam+0x51C,e.camera.current_type);w(cam+0x520,e.camera.next_type);w(cam+0x514,e.camera.style);w(cam+0x8A8,e.camera.param_pointer);w(cam+0x8AC,e.camera.param_index);w(cam+0x68,e.camera.bg);w(cam+0x600,e.camera.trim);w(cam+0x408,~0u);std::array<uint8_t,128>work{};assert(write(cam+0x37C,work));w(cam+0x37C,0x464C4C57);w(cam+0x380,1);})==CB::Result::Returned);
 }
 const auto returned=barrier.completion();assert(returned.matches(e));
 unsigned consumer_writes=0,refresh_calls=0;bool released=false;
 const story_native_people_pose_capture::Layout people_layout{0x101C4CD0,0x100209C4};
 guestmods::NativeOuterReturnIssuer::test(&cpu,depth,witness,[&](const auto&outer){
  assert(outer.claim(&cpu,e)&&controller.prepare_cancel(&cpu,outer));
  std::shared_ptr<const threads::NativeQuiescence>q=std::make_shared<threads::NativeQuiescence>(threads::NativeQuiescenceIssuer::test());
  const CO::Result*active_closed=nullptr;CO::Sources cs;cs.quiescence=q;cs.retained=[&](const auto&x){return x==e&&claim;};cs.resident=ms.resident;cs.verify_camera=bs.verify_camera;
  cs.sample=[&](Cpu*c,CO::Sample&out){out={};if(c!=&cpu||!q||!q->current(c)||!read(e.identity.manager,out.state.manager)||!read(e.identity.image,out.state.image))return false;
   auto&o=out.observation;o.identity=e.identity;o.room_number=e.room_number;o.stage_name=e.stage_name;o.resource_generation_matches=true;o.image=out.state.image;o.control_owned=!released;o.control_released=released;o.elapsed_ticks=uint32_t(live_step-100);
   for(unsigned n=0;n<8;++n){const auto*b=out.state.manager.data()+n*4;o.manager[n]=uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];}auto word=[&](unsigned at){const auto*b=out.state.manager.data()+at;return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];};o.camera_play=word(0x20);o.current_event=word(0x53C);std::copy_n(out.state.manager.begin()+0x3C,0x500,o.flags.begin());if(!camera_read(o.camera))return false;
   for(unsigned n=0;n<2;++n){if(!read(e.actors[n].address,out.state.actors[n]))return false;o.actors[n]=e.actors[n];std::copy_n(out.state.actors[n].begin()+0x3E0,0x6C,o.actors[n].cut.begin());
    if(released){if(!story_native_people_pose_capture::capture(read,sources.validation.actor,people_layout,e,n,out.poses[n]))return false;}
    else if(active_closed){if(!story_native_cancel_pose_capture::capture(read,sources.validation.actor,people_layout,e,n,*active_closed,c,depth,outer,*q,out.poses[n]))return false;}
    else if(!story_native_cancel_pose_capture::capture_closed(read,sources.validation.actor,people_layout,e,n,returned,c,depth,outer,*q,out.poses[n]))return false;
   }
   auto&b=out.boundary;b.identity=e.identity;b.exclusive=b.fenced=b.manager_owned=true;b.logic_step=live_step;b.serial=depth.snapshot().serial;b.control_mode=released?0:3;return true;
  };
  auto validate=[&]{return CO::Validator(e,cs);};auto closed=validate().closed(&cpu,depth,outer,returned);assert(closed.valid());active_closed=&closed;
  const auto before_calls=calls;
  if(cancel_fault==6){Cpu wrong=cpu;assert(controller.begin_cancel(&wrong,outer,closed)==C::State::Quarantined&&controller.retained(e)&&calls==before_calls&&!consumer_writes);return;}
  if(cancel_fault==3){assert(controller.begin_cancel(&cpu,outer,{})==C::State::Quarantined&&controller.retained(e)&&calls==before_calls&&!consumer_writes);return;}
  if(cancel_fault==5)mem::ptr(layout.wait.header.address)[0]^=1;
  const auto begin=controller.begin_cancel(&cpu,outer,closed);
  if(cancel_fault==5){assert(begin==C::State::Quarantined&&controller.retained(e)&&calls==before_calls&&!consumer_writes);return;}
  assert(begin==C::State::Cancelling&&session.journal()->phase==story_native_wait_journal::Phase::Detaching&&calls==before_calls);
  // The callback reads the ACTUAL durable guest header before EVERY segment.
  // A fake host phase boolean is not accepted as write-ahead evidence.
  story_native_cancel_consumer_write::Sources ws;ws.retained=cs.retained;ws.camera=[&](const auto&camera,const auto&i){return camera_verify(camera,i);};ws.quarantine=[&]{++quarantines;assert(session.quarantine_native());};
  ws.capture=[&](auto&out){CO::Sample sample;if(!cs.sample(&cpu,sample))return false;out.people=sample.poses;out.camera=sample.observation.camera;out.boundary=sample.boundary;return true;};
  ws.write=[&](uint32_t at,std::span<const uint8_t>bytes){const auto journal=session.journal();assert(journal&&journal->phase==story_native_wait_journal::Phase::Detaching);++consumer_writes;const bool copied=write(at,bytes);return copied&&cancel_fault!=1;};
  story_native_cancel_consumer_write::Writer writer;const auto wrote=writer.apply(&cpu,depth,outer,*q,e,closed,ws);
  if(cancel_fault==1){assert(wrote==story_native_cancel_consumer_write::Result::Quarantined&&consumer_writes==1&&controller.retained(e)&&session.quarantined()&&calls==before_calls);assert(writer.apply(&cpu,depth,outer,*q,e,closed,ws)==story_native_cancel_consumer_write::Result::Quarantined&&consumer_writes==1);return;}
  assert(wrote==story_native_cancel_consumer_write::Result::Complete&&consumer_writes==34);
  CO::Sample after;assert(cs.sample(&cpu,after));RF::State live;live.poses=after.poses;live.boundary=after.boundary;
  std::array<RF::ModelIdentity,2>models;std::array<RF::Transform,2>transforms{};
  for(unsigned n=0;n<2;++n){models[n]={poses[n].identity.morf,poses[n].identity.model,e.actors[n].address,0x31000000+n*0x1000,0x32000000+n*0x1000,0x33000000+n*0x1000,0};}live.models=models;live.transforms=transforms;
  RF::Sources rs;rs.capture=[&](auto&out){out=live;return true;};rs.retained=cs.retained;rs.boundary=[&](const Cpu*c,const auto&x,const auto&b){return c==&cpu&&x==e&&q&&q->current(c)&&b.serial==depth.snapshot().serial&&outer.claimed_current(c,e,depth);};rs.model=[&](const auto&x,unsigned n,const auto&m,const auto&t){return x==e&&m==models[n]&&t==transforms[n];};rs.stack=[&](const Cpu*c,const auto&){return c==&cpu&&q&&q->current(c);};rs.read=read;rs.write=write;rs.quarantine=ms.quarantine;
  rs.call=[&](Cpu*,uint32_t fn,std::initializer_list<uint32_t>a){const unsigned n=refresh_calls/2;const bool matrix=(refresh_calls++%2)==0;assert(n<2&&fn==(matrix?0x022BF3A0u:0x025E55A0u)&&*a.begin()==(matrix?e.actors[n].address:models[n].morf));if(!matrix){live.current_model=models[n].model;live.root_calc[n]=live.mtx_calc[n]=models[n].morf;}return 0u;};
  cpu.gqr[2]=0x40004;cpu.gqr[3]=0x50005;cpu.gqr[4]=0x60006;cpu.gqr[5]=0x70007;
  RF::Invoker refresh(e,poses,live.boundary,models,transforms,{{0x60030000,0x1000},{{layout.allocation.address,layout.allocation.bytes}}},rs);assert(refresh.run(&cpu)==RF::Result::Complete&&refresh_calls==4);
  auto detached=validate().detached(&cpu,depth,outer,closed,refresh.completion());assert(detached.valid());
  if(cancel_fault==2){assert(controller.release_cancel_native(&cpu,outer,detached)==C::State::Quarantined&&calls==before_calls&&controller.retained(e));return;}
  cs.quiescence.reset();q.reset();assert(!threads::current_thread_holds_native_quiescence());
  cpu.r[1]=cancel_fault==9?0x600300B4:0x600300B8; // real post-CameraExecute ABI is eight aligned
  if(cancel_fault==8)f.byte(play+0x51D0+0xC2,1);
  control_restore_fault=cancel_fault==10||cancel_fault==11;
  const Cpu before_release=cpu;const auto status=controller.release_cancel_native(&cpu,outer,detached);assert(!std::memcmp(&cpu,&before_release,sizeof cpu));
  if(cancel_fault==8||cancel_fault==9){assert(status==C::State::Quarantined&&calls==before_calls&&controller.retained(e));return;}
  if(cancel_fault==4||cancel_fault==10||cancel_fault==11){assert(status==C::State::Quarantined&&calls==before_calls+1&&controller.retained(e));assert(controller.release_cancel_native(&cpu,outer,detached)==C::State::Quarantined&&calls==before_calls+1);return;}
  assert(status==C::State::Cancelling&&calls==before_calls+2&&claim);
  q=std::make_shared<threads::NativeQuiescence>(threads::NativeQuiescenceIssuer::test());cs.quiescence=q;released=true;
  auto result=validate().released(&cpu,depth,outer,detached);assert(result.released());assert(controller.finish_cancel(&cpu,outer,result)==C::State::Released&&!claim&&!controller.retained(e)&&session.journal()->phase==story_native_wait_journal::Phase::Unpublished);
  assert(!controller.take_rearm_certificate());const auto final_calls=calls;assert(controller.finish_cancel(&cpu,outer,result)==C::State::Released&&calls==final_calls);
 });
#ifdef STORY_NATIVE_CANCEL_REARM_AFTER_OUTER
 STORY_NATIVE_CANCEL_REARM_AFTER_OUTER(f,session,controller,depth,cpu,e,layout,cfg,sources,poses,calls,claims,arms,claim,live_step,read,write,w,source_fault,hold,controller_owner);
#endif
 hold.reset();unmap_arena(guest);
}
#ifndef STORY_NATIVE_CANCEL_REARM_FIXTURE_ONLY
int main(){for(unsigned fault=0;fault<=11;++fault)run(fault);}
#endif
