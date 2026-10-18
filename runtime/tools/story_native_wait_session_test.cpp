// SPDX-License-Identifier: MPL-2.0
#define STORY_NATIVE_WAIT_READINESS_FIXTURE_ONLY
#include "story_native_wait_readiness_test.cpp"
#include "mods/story_native_wait_session.h"
#include "mods/story_native_turn_record.h"
#include <cstring>
#include <thread>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <span>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <span>
#endif

namespace S=story_native_wait_session;
static constexpr uint32_t arena_at=0x60010000;
static uint8_t* map_arena(){
#ifdef _WIN32
 auto* base=static_cast<uint8_t*>(VirtualAlloc(PPC_MEM_BASE+arena_at,
  story_native_event_arena::kBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
 auto* base=static_cast<uint8_t*>(mmap(PPC_MEM_BASE+arena_at,
  story_native_event_arena::kBytes,PROT_READ|PROT_WRITE,
  MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0));
#endif
 assert(base==PPC_MEM_BASE+arena_at);
 return base;
}
static void unmap_arena(uint8_t* base){
#ifdef _WIN32
 assert(VirtualFree(base,0,MEM_RELEASE));
#else
 assert(munmap(base,story_native_event_arena::kBytes)==0);
#endif
}
#ifndef STORY_NATIVE_WAIT_SESSION_FIXTURE_ONLY
int main(){
 {
  Readiness f;S::Session a(request(),f.source()),b(request(),f.source()),c(request(),f.source());
  assert(!a.fence_held()&&!b.fence_held()&&!c.fence_held());
  auto ah=a.try_fence();auto bh=b.try_fence();auto ch=c.try_fence();
  assert(ah&&bh&&ch&&a.fence_held()&&b.fence_held()&&c.fence_held());
  assert(!a.try_fence()&&!b.try_fence()&&!c.try_fence());
  std::thread foreign([&]{
   assert(!a.fence_held()&&!b.fence_held()&&!c.fence_held());
   assert(!a.try_fence()&&!b.try_fence()&&!c.try_fence());
  });foreign.join();
  // Removing a middle guard must preserve the other genuine held locks.
  bh.reset();assert(a.fence_held()&&!b.fence_held()&&c.fence_held());
  bh=b.try_fence();assert(bh&&a.fence_held()&&b.fence_held()&&c.fence_held());
  ah.reset();assert(!a.fence_held()&&b.fence_held()&&c.fence_held());
  bh.reset();ch.reset();assert(!a.fence_held()&&!b.fence_held()&&!c.fence_held());
 }
 auto* guest=map_arena();
 uint32_t first_generation=0;
 {
  Readiness f;S::Session session(request(),f.source());
  auto hold=session.try_fence();assert(hold&&!session.try_fence());
  assert(!session.startup());hold.reset();
  assert(session.startup()&&allocation_calls==1);
  assert(!session.startup());
  hold=session.try_fence();assert(hold);
  const auto initial=session.journal();assert(initial);
  const auto generation=session.generation();
  first_generation=generation;
  assert(generation>1&&initial->generation==generation&&initial->serial==1&&
   initial->phase==story_native_wait_journal::Phase::Unpublished&&
   initial->arena==arena_at);
  assert(session.unexposed()&&!session.expected()&&
   session.canonical_image().empty()&&!session.publication_enabled());
  hold.reset();
  Cpu cpu{};cpu.r[1]=0x600300C0;cpu.r[3]=3;cpu.lr=4;cpu.f[2].ps0=1;
  const Cpu before=cpu;
  assert(session.inspect(&cpu)&&!memcmp(&before,&cpu,sizeof cpu));
  hold=session.try_fence();assert(hold);
  auto candidate=session.expected();assert(candidate&&
   candidate->identity.generation==generation&&
   candidate->identity.image==arena_at+0x100&&
   session.canonical_image().size()==0x520&&session.unexposed());
  assert(session.begin_restore()&&!session.unexposed()&&!session.expected());
  // The saved guest allocation is restored exactly, but admission stays
  // deferred until the designated logic thread observes the live boundary.
  assert(session.finish_restore(*initial)&&session.unexposed()&&!session.expected());
  hold.reset();
  assert(session.inspect(&cpu));
  hold=session.try_fence();assert(hold&&session.expected());
  session.begin_progress_load();
  assert(!session.unexposed()&&!session.expected());
  hold.reset();
  assert(!session.inspect(&cpu));
 }
 std::memset(guest,0,story_native_event_arena::kBytes);
 for(unsigned damage=0;damage<6;++damage){
  Readiness f;S::Session session(request(),f.source());assert(session.startup());
  assert(!session.stage_image()&&!session.staged_expected()); // no fence/candidate
  {auto hold=session.try_fence();assert(hold&&!session.stage_image()&&session.unexposed());}
  Cpu cpu{};cpu.r[1]=0x600300C0;assert(session.inspect(&cpu));
  const auto native_before=f.memory;const auto queries_before=queries;
  auto hold=session.try_fence();assert(hold);
  const auto baseline=session.expected();const auto image=session.canonical_image();
  assert(baseline&&session.stage_image()&&!session.stage_image());
  const auto journal=session.journal();const auto layout=session.arena();
  assert(journal&&journal->phase==story_native_wait_journal::Phase::Preparing&&journal->serial==2);
  assert(!session.unexposed()&&!session.expected()&&!session.begin_restore()&&
      session.canonical_image().empty()&&!session.publication_enabled());
  const auto staged=session.staged_expected();assert(staged&&staged->identity==baseline->identity);
  story_native_people_pose_record::Poses poses;
  for(unsigned n=0;n<2;++n){const auto&a=baseline->actors[n];
   poses[n].identity={a.address,a.pid,a.token,baseline->identity.stage,baseline->identity.room,
       baseline->identity.generation,0x30000000+n*0x2000,0x30001000+n*0x2000};
   poses[n].cut=a.cut;poses[n].angles.fill(uint8_t(10+n));
  }
  auto wrong_pose=poses;++wrong_pose[1].identity.pid;
  assert(!session.stage_poses(wrong_pose)&&!session.staged_poses());
  assert(session.stage_poses(poses)&&!session.stage_poses(poses));
  const auto saved_poses=session.staged_poses();assert(saved_poses&&saved_poses->at(1).angles==poses[1].angles);
  namespace Depth=story_native_event_depth;
  Depth::Coverage coverage{{1,1,true},{2,2,true},{3,3,true},true};
  Depth::Tracker observed(*baseline,coverage);const auto depth_record=observed.record();
  auto corrupt_depth=depth_record;corrupt_depth[0]^=1;
  assert(!session.stage_depth(corrupt_depth)&&session.stage_depth(depth_record));
  {Depth::Call call{Depth::Entry::PeopleExecute,Depth::entry_address(Depth::Entry::PeopleExecute),
       0,0,0,0,{},Depth::Authority::Foreign};auto native=observed.enter(call);
   assert(native.allowed()&&!session.stage_depth(observed.record()));}
  assert(!std::memcmp(mem::ptr(layout.wait.depth.address),depth_record.data(),depth_record.size()));
  assert(!std::memcmp(mem::ptr(layout.wait.image.address),image.data(),image.size()));
  assert(f.memory==native_before&&queries==queries_before); // no native writes/calls
  const uint32_t at=damage==0?layout.wait.image.address:damage==1?layout.wait.stamp.address:
      damage==2?layout.wait.record.address:damage==3?layout.wait.header.address:
      damage==4?layout.wait.depth.address:layout.wait.record.address+S::Session::kPoseRecordOffset;
  *mem::ptr(at)^=1;
  assert(!session.staged_expected()&&session.quarantined());
  hold.reset();assert(!session.inspect(&cpu));
  std::memset(guest,0,story_native_event_arena::kBytes);
 }
 std::memset(guest,0,story_native_event_arena::kBytes);
 {
  Readiness f;S::Session session(request(),f.source());
  assert(session.startup());
  {auto hold=session.try_fence();assert(hold&&session.generation()>first_generation);}
  guest[story_native_wait_journal::kBytes+1]=1;
  auto hold=session.try_fence();assert(hold&&!session.unexposed()&&
   session.quarantined()&&!session.journal());
 }
 std::memset(guest,0,story_native_event_arena::kBytes);
 {
  Readiness f;S::Session session(request(),f.source());
  assert(session.startup());
  guest[0]^=1;
  auto hold=session.try_fence();assert(hold&&!session.journal()&&
   session.quarantined()&&!session.unexposed());
 }
 std::memset(guest,0,story_native_event_arena::kBytes);
 {
  Readiness f;S::Session session(request(),f.source());
  assert(session.startup());
  auto hold=session.try_fence();assert(hold);
  const auto saved=session.journal();assert(saved&&session.begin_restore());
  guest[story_native_wait_journal::kBytes+1]=1;
  assert(!session.finish_restore(*saved)&&session.quarantined()&&
   !session.unexposed());
 }
 // Native journal evolution is separate from the controller's retained claim.
 // Certificates here come from copied fixtures and grant no native authority.
 for(unsigned terminal_path=0;terminal_path<2;++terminal_path){
  std::memset(guest,0,story_native_event_arena::kBytes);
  Readiness f;S::Session session(request(),f.source());assert(session.startup());
  Cpu cpu{};cpu.r[1]=0x600300C0;assert(session.inspect(&cpu));
  auto hold=session.try_fence();const auto expected=session.expected();assert(expected&&session.stage_image());
  const auto layout=session.arena();auto image=story_native_event_image::make_wait_fixture(expected->frames);
  namespace O=story_native_event_observation;namespace P=story_native_wait_journal;
  auto put_bytes=[](auto&b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));};
  auto copied=[&](O::Operation op,unsigned state){
   put_bytes(image,0xE4,state);
   if(state>=2||op==O::Operation::Terminal||op==O::Operation::TerminalDrain||op==O::Operation::ReleaseTerminal)
    for(unsigned n=0;n<4;++n)put_bytes(image,0xF0+n*0x50+0x38,n);
   std::memcpy(mem::ptr(layout.wait.image.address),image.data(),image.size());
   O::Captured c;c.identity=expected->identity;c.room_number=expected->room_number;c.stage_name=expected->stage_name;c.image=image;
   c.manager=expected->retail_manager;c.camera_play=expected->retail_camera_play;c.current_event=expected->retail_current_event;c.flags=expected->retail_flags;c.actors=expected->actors;c.camera=expected->camera;c.resource_generation_matches=true;c.control_released=true;
   const bool idle=op==O::Operation::Prepare||op==O::Operation::Release||op==O::Operation::ReleaseTerminal;
   const bool terminal=op==O::Operation::Terminal||op==O::Operation::TerminalDrain||op==O::Operation::ReleaseTerminal;
   const bool live=(state>=2||terminal)&&op!=O::Operation::TerminalDrain&&op!=O::Operation::ReleaseTerminal;
   if(!idle){const auto base=expected->identity.image;c.manager={base,base+0x40,base+0xF0,base+0x230,base+0x410,0,base+0x510,0};c.camera_play=terminal?0:state>=2?1:0;c.current_event=terminal?0:state>=2?base+0x40:0;c.control_owned=true;c.control_released=false;
    if(op!=O::Operation::TerminalDrain&&op!=O::Operation::Drain){
     for(unsigned n=0;n<2;++n){auto&cut=c.actors[n].cut;put_bytes(cut,0,base+0xF0+n*0x50);put_bytes(cut,8,c.actors[n].address);put_bytes(cut,0x10,state==1?0xFFFFFFFFu:0);if(state>=2||terminal)put_bytes(cut,4,n);}
    }
   }
   if(live){put_bytes(c.camera.event_data,4,2);put_bytes(c.camera.event_data,12,expected->camera.current_type);c.camera.current_type=c.camera.next_type=c.camera.event_type;}
   auto result=O::validate(op,*expected,c);assert(result.valid());return result;
  };
  const auto prepare=copied(O::Operation::Prepare,0);
  const auto wrongop=copied(O::Operation::Release,0);
  assert(!session.begin_publication(wrongop)&&!session.begin_publication(prepare)); // missing baselines
  story_native_people_pose_record::Poses poses;
  for(unsigned n=0;n<2;++n){const auto&a=expected->actors[n];poses[n].identity={a.address,a.pid,a.token,expected->identity.stage,expected->identity.room,expected->identity.generation,0x30000000+n*0x2000,0x30001000+n*0x2000};poses[n].cut=a.cut;}
  assert(session.stage_poses(poses)&&!session.begin_publication(prepare)); // missing depth
  story_native_event_depth::Tracker depth(*expected,{{1,1,true},{2,2,true},{3,3,true},true});
  assert(session.stage_depth(depth.record()));
  auto stale=*expected;++stale.identity.generation;
  O::Captured stale_copy;stale_copy.identity=stale.identity;stale_copy.room_number=stale.room_number;stale_copy.stage_name=stale.stage_name;stale_copy.image=image;stale_copy.manager=stale.retail_manager;stale_copy.flags=stale.retail_flags;stale_copy.camera=stale.camera;stale_copy.actors=stale.actors;stale_copy.resource_generation_matches=stale_copy.control_released=true;
  const auto stale_prepare=O::validate(O::Operation::Prepare,stale,stale_copy);assert(stale_prepare.valid()&&!session.begin_publication(stale_prepare));
  hold.reset();assert(!session.begin_publication(prepare)&&!session.baseline()&&!session.baseline_poses()&&!session.persist_depth(depth.record()));
  hold=session.try_fence();assert(session.begin_publication(prepare)&&!session.begin_publication(prepare));
  assert(session.baseline()&&session.baseline_poses()&&!session.staged_expected()&&!session.staged_poses());
  assert(!session.stage_image()&&!session.stage_poses(poses)&&!session.stage_depth(depth.record()));
  auto published=copied(O::Operation::Publish,1);
  assert(session.baseline()&&!session.transition_native(P::Phase::Running,published));
  assert(!session.transition_native(P::Phase::Published,prepare));
  assert(session.transition_native(P::Phase::Published,published)&&session.journal()->serial==3);
  assert(!session.transition_native(P::Phase::Published,published));
  const auto old_depth=depth.record();
  {story_native_event_depth::Call call{story_native_event_depth::Entry::PeopleExecute,story_native_event_depth::entry_address(story_native_event_depth::Entry::PeopleExecute),0,0,0,0,{},story_native_event_depth::Authority::Foreign};auto scope=depth.enter(call);assert(scope.allowed()&&!session.persist_depth(depth.record()));}
  assert(session.persist_depth(depth.record())&&session.baseline());
  assert(!session.persist_depth(old_depth)&&session.baseline());
  O::Result draining;
  if(terminal_path){const auto running=copied(O::Operation::Running,2);assert(session.transition_native(P::Phase::Running,running));draining=copied(O::Operation::Terminal,0);}
  else draining=published;
  assert(!session.transition_native(P::Phase::Detaching,draining));
  assert(session.transition_native(P::Phase::Draining,draining));
  if(terminal_path){const auto post_restore=copied(O::Operation::TerminalDrain,0);assert(!session.transition_native(P::Phase::Detaching,post_restore));draining=copied(O::Operation::Terminal,0);}
  assert(session.transition_native(P::Phase::Detaching,draining));
  const auto released=copied(terminal_path?O::Operation::ReleaseTerminal:O::Operation::Release,terminal_path?0:1);
  assert(session.transition_native(P::Phase::Unpublished,released)&&session.baseline_poses());
  assert(!session.unexposed()&&!session.persist_depth(depth.record())); // no clean bootstrap resurrection
  const auto retained_image=std::vector<uint8_t>(mem::ptr(layout.wait.image.address),mem::ptr(layout.wait.image.address)+layout.wait.image.bytes);
  assert(session.quarantine_native()&&session.quarantined()&&!session.baseline());
  story_native_wait_journal::Record quarantined;assert(story_native_wait_journal::decode(std::span(mem::ptr(layout.wait.header.address),story_native_wait_journal::kBytes),quarantined)&&quarantined.phase==P::Phase::Quarantined);
  assert(!std::memcmp(mem::ptr(layout.wait.image.address),retained_image.data(),retained_image.size()));
 }
 // Sealing permits only native mutable image fields. Every immutable private
 // byte and the owned depth receipt remain guarded after the seal.
 for(unsigned damage=0;damage<6;++damage){
  std::memset(guest,0,story_native_event_arena::kBytes);Readiness f;S::Session session(request(),f.source());assert(session.startup());Cpu cpu{};cpu.r[1]=0x600300C0;assert(session.inspect(&cpu));auto hold=session.try_fence();auto expected=session.expected();assert(expected&&session.stage_image());const auto layout=session.arena();
  story_native_people_pose_record::Poses poses;for(unsigned n=0;n<2;++n){const auto&a=expected->actors[n];poses[n].identity={a.address,a.pid,a.token,expected->identity.stage,expected->identity.room,expected->identity.generation,0x30000000+n*0x2000,0x30001000+n*0x2000};poses[n].cut=a.cut;}assert(session.stage_poses(poses));story_native_event_depth::Tracker depth(*expected,{{1,1,true},{2,2,true},{3,3,true},true});assert(session.stage_depth(depth.record()));
  story_native_event_observation::Captured c;c.identity=expected->identity;c.room_number=expected->room_number;c.stage_name=expected->stage_name;c.image=std::span(mem::ptr(layout.wait.image.address),layout.wait.image.bytes);c.manager=expected->retail_manager;c.flags=expected->retail_flags;c.actors=expected->actors;c.camera=expected->camera;c.resource_generation_matches=c.control_released=true;auto prepare=story_native_event_observation::validate(story_native_event_observation::Operation::Prepare,*expected,c);assert(prepare.valid()&&session.begin_publication(prepare));
  const uint32_t at=damage==0?layout.wait.image.address+0xF0:damage==1?layout.wait.stamp.address:damage==2?layout.wait.record.address:damage==3?layout.wait.header.address:damage==4?layout.wait.depth.address:layout.wait.record.address+S::Session::kPoseRecordOffset;*mem::ptr(at)^=1;
  assert(!session.baseline()&&session.quarantined()&&!session.baseline_poses());
 }
 // TURN stages its distinct full record with poses captured before the first
 // write. The WAIT instance stays zero except its shared journal header.
 for(unsigned damage=0;damage<7;++damage){
  std::memset(guest,0,story_native_event_arena::kBytes);Readiness f;f.put(0x1004487C+4,1);f.half(0x10049350+2*0x40+0x18,1);f.put(0x1004487C+0x84+4,11);auto req=request();story_native_turn_camera_image::Program def;def.camera.start_eye={0,10,20};def.camera.end_eye={10,10,20};req.program=story_native_event_program::Program::turn(def,301);req.fixture_program="sdk-private-turn-v1";S::Session session(req,f.source());assert(session.startup());Cpu cpu{};cpu.r[1]=0x600300C0;assert(session.inspect(&cpu));auto hold=session.try_fence();const auto expected=session.expected();const auto layout=session.arena();assert(expected&&expected->program==req.program&&expected->identity.image==layout.turn.image.address);
  const auto all_before=std::vector<uint8_t>(guest,guest+story_native_event_arena::kBytes);assert(!session.stage_image()&&session.unexposed()&&std::equal(all_before.begin(),all_before.end(),guest));
  story_native_people_pose_record::Poses poses;for(unsigned n=0;n<2;++n){const auto&a=expected->actors[n];poses[n].identity={a.address,a.pid,a.token,expected->identity.stage,expected->identity.room,expected->identity.generation,0x30000000+n*0x2000,0x30001000+n*0x2000};poses[n].cut=a.cut;poses[n].angles[0]=uint8_t(n+1);}
  auto wrong=poses;++wrong[1].identity.pid;assert(!session.stage_image(wrong)&&session.unexposed()&&std::equal(all_before.begin(),all_before.end(),guest));assert(session.stage_image(poses)&&!session.stage_image(poses)&&!session.stage_poses(poses));assert(session.staged_expected()==expected&&session.staged_poses()->at(1).angles==poses[1].angles);
  assert(std::all_of(mem::ptr(layout.wait.image.address),mem::ptr(layout.wait.record.address+layout.wait.record.bytes),[](uint8_t b){return !b;}));
  const auto*record=mem::ptr(layout.turn.record.address);const uint32_t bytes=uint32_t(record[8])<<24|uint32_t(record[9])<<16|uint32_t(record[10])<<8|record[11];story_native_turn_record::Record decoded;assert(story_native_turn_record::decode(std::span(record,bytes),decoded)&&decoded.expected==*expected&&decoded.expected.program==req.program&&decoded.poses[1].angles==poses[1].angles&&bytes>S::Session::kPoseRecordOffset);
  story_native_event_depth::Tracker depth(*expected,{{1,1,true},{2,2,true},{3,3,true},true});assert(session.stage_depth(depth.record()));assert(std::all_of(mem::ptr(layout.wait.depth.address),mem::ptr(layout.wait.depth.address+layout.wait.depth.bytes),[](uint8_t b){return !b;}));
  namespace O=story_native_event_observation;O::Captured captured;captured.identity=expected->identity;captured.room_number=expected->room_number;captured.stage_name=expected->stage_name;captured.image=std::span(mem::ptr(layout.turn.image.address),layout.turn.image.bytes);captured.manager=expected->retail_manager;captured.flags=expected->retail_flags;captured.actors=expected->actors;captured.camera=expected->camera;captured.resource_generation_matches=captured.control_released=true;auto prepare=O::validate(O::Operation::Prepare,*expected,captured);assert(prepare.valid());auto other=*expected;other.program=story_native_event_program::Program::turn(def,302);assert(!session.begin_publication(O::validate(O::Operation::Prepare,other,captured)));assert(session.begin_publication(prepare)&&session.baseline()==expected&&session.baseline_poses()->at(1).angles==poses[1].angles);
  if(damage==0){assert(session.persist_depth(depth.record()));continue;}
  const uint32_t at=damage==1?layout.turn.image.address+0xF0:damage==2?layout.turn.stamp.address:damage==3?layout.turn.record.address:damage==4?layout.wait.header.address:damage==5?layout.turn.depth.address:layout.turn.record.address+S::Session::kPoseRecordOffset;*mem::ptr(at)^=1;assert(!session.baseline()&&session.quarantined()&&!session.baseline_poses());
 }
 unmap_arena(guest);
 return 0;
}
#endif
