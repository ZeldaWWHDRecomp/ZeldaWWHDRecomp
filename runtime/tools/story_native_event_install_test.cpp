// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_install.h"
#include "mods/story_native_event_image.h"
#include "mods/story_native_event_record.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <utility>
using namespace story_native_event_install;
static void put(auto&b,size_t p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
static uint32_t word(std::span<const uint8_t>b){return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];}
struct Fake {
 Expected e;Captured o;std::vector<uint8_t> image;std::array<uint8_t,0xF4> control{};std::vector<uint32_t> calls;
 bool scope=true,detach_ok=true,detach_lies=false,alias=false,foreign_on_failure=false,fault_after_set=false;uint32_t fail=0,throw_after_set=0;unsigned writes=0;
 Fake(){e.identity={0x20000000,0x60000000,0x520,1,2,3,4};e.stage_name={'s','e','a'};
  for(unsigned n=0;n<2;++n){e.actors[n].address=0x20001000+n*0x1000;e.actors[n].pid=10+n;e.actors[n].token=1+n;e.actors[n].profile=0x175;e.actors[n].subtype=1;for(unsigned k=0;k<32;++k)e.actors[n].fingerprint[k]=uint8_t(k+n+1);put(e.actors[n].cut,4,0xFFFFFFFFu);put(e.actors[n].cut,0x10,0xFFFFFFFFu);}
  e.camera.address=0x20003000;e.camera.pid=12;e.camera.event_type=2;e.camera.style_table=e.camera.param_pointer=0x1004487C;e.camera.style_count=4;e.camera.type_table=0x10049350;e.camera.type_count=3;put(e.camera.event_data,0x0C,0xFFFFFFFFu);put(e.camera.event_data,4,0xFFFFFFFFu);
  e.retail_manager={0x30000000,0x30000040,0x300000F0,0x30000230,0x30000410,0,0x30000510,0};
  image=story_native_event_image::make_wait_fixture();o.identity=e.identity;o.stage_name=e.stage_name;o.image=image;o.manager=e.retail_manager;o.resource_generation_matches=true;o.actors=e.actors;o.camera=e.camera;
 }
 Backend backend(){return {
  [this](Cpu*c,uint32_t fn,std::initializer_list<uint32_t> regs,std::initializer_list<uint32_t> stack){
   calls.push_back(fn);std::vector<uint32_t>a(regs);c->r[10]=0xBAD;c->f[7].ps0=123;c->lr=99;
   if(fn==fail){if(foreign_on_failure){o.foreign=true;control[0xC2]=1;}return uint32_t(0);}
   switch(fn){
    case 0x0253EC0C:assert(a==std::vector<uint32_t>({e.identity.manager-0xF4,3,1,0,0,0,0,0xFFFFFFFFu})&&std::vector<uint32_t>(stack)==std::vector<uint32_t>({0xFF}));control[0]=0;control[1]=3;control[16]=control[17]=control[20]=control[21]=0xFF;control[19]=1;control[0xC0]=1;return uint32_t(1);
    case 0x0253FD1C:control[0xC0]=0;control[0xC2]=3;for(unsigned p:{0xC4u,0xC8u,0xCCu,0xD0u})put(control,p,0xFFFFFFFFu);control[0xD8]=control[0xD9]=control[0xDE]=control[0xDF]=0xFF;return uint32_t(1);
    case 0x0254273C:o.manager={};return uint32_t(0);
    case 0x025443B4:o.manager={e.identity.image,e.identity.image+0x40,e.identity.image+0xF0,e.identity.image+0x230,e.identity.image+0x410,0,e.identity.image+0x510,0};if(fault_after_set){scope=false;throw std::runtime_error("synthetic bridge rollback failure");}if(throw_after_set)throw std::runtime_error("synthetic partial native publication");return uint32_t(0);
    case 0x0259F7D4:{const unsigned n=(a[2]-e.actors[0].address)/0x1000;assert(n<2&&a[0]==a[2]+0x3E0);put(o.actors[n].cut,0,a[1]+(alias?0x50:0));put(o.actors[n].cut,8,a[2]);put(o.actors[n].cut,0x10,0xFFFFFFFFu);return uint32_t(0);}
    case 0x02544534:assert(a==std::vector<uint32_t>({e.identity.manager,0}));put(image,0xE4,1);return uint32_t(1);
    case 0x0253EFA8:assert(control[0xC2]==3);control[0xC2]=0;return uint32_t(1);
    default:assert(false);return uint32_t(0);
   }
  },
  [this](uint32_t a,std::span<uint8_t>b){if(a!=e.identity.manager-0xF4||b.size()!=control.size())return false;std::copy(control.begin(),control.end(),b.begin());return true;},
  [this](uint32_t a,std::span<const uint8_t>b){++writes;
   if(a==e.identity.image&&b.size()==0x520){image.assign(b.begin(),b.end());return true;}
   if(a==e.identity.manager-0xF4&&b.size()==control.size()){std::copy(b.begin(),b.end(),control.begin());return true;}
   if(a>=e.identity.manager&&a<e.identity.manager+0x20&&b.size()==4){o.manager[(a-e.identity.manager)/4]=word(b);return true;}
   if(a==e.identity.manager+0x20&&b.size()==4){o.camera_play=word(b);return true;}
   if(a==e.identity.manager+0x53C&&b.size()==4){o.current_event=word(b);return true;}
   if(a==e.identity.manager+0x3C&&b.size()==o.flags.size()){std::copy(b.begin(),b.end(),o.flags.begin());return true;}
   assert(false);return false;
  },
  [this](Cpu*,Captured&out){o.image=image;out=o;return true;},
  [this](const Expected&){return scope;},
  [this](Cpu*,const Expected&){if(!detach_ok)return false;if(detach_lies)return true;o.actors=e.actors;o.camera=e.camera;return true;}
 };}
 void running(bool finished=false){put(image,0xE4,finished?4:2);o.camera_play=1;o.current_event=e.identity.image+0x40;
  for(unsigned n=0;n<4;++n){put(image,0xF0+n*0x50+0x38,n);image[0xF0+n*0x50+0x46]=2;}
  for(unsigned n=0;n<2;++n){put(o.actors[n].cut,4,n);put(o.actors[n].cut,0x10,0);}
  put(o.camera.event_data,4,2);put(o.camera.event_data,0x0C,e.camera.current_type);o.camera.current_type=o.camera.next_type=o.camera.event_type;o.camera.elapsed11C=20;put(o.camera.engine100,0x1C,20);o.camera.timer=20;put(o.camera.work37C,8,20);if(finished)o.flags[3]=0x3F;
 }
 Result released(){o.image=image;o.control_owned=false;o.control_released=true;
  const bool terminal=word(std::span<const uint8_t>(image).subspan(0xE4,4))==0&&word(std::span<const uint8_t>(image).subspan(0x128,4))==0;
  return validate(terminal?Operation::ReleaseTerminal:Operation::Release,e,o);}
};
static Status finish_native(Fake&f,Install&i,Cpu&cpu,bool camera_returns=true,bool reenter=false){
 namespace M=story_native_manager_terminal;namespace C=story_native_camera_return;
 M::Identity identity;identity.lease=f.e.identity;identity.event=f.e.identity.image+0x40;identity.index=0;
 std::array<uint32_t,2>status={0x8000,0x8000};
 auto actor=[&](unsigned n){const auto&a=f.e.actors[n];M::Actor out;
  out.address=a.address;out.pid=a.pid;out.token=a.token;out.profile=a.profile;out.subtype=a.subtype;out.fingerprint=a.fingerprint;return out;};
 for(unsigned n=0;n<2;++n)identity.actors[n]=actor(n);
 M::Terminal* active=nullptr;
 M::Terminal terminal(identity,[&](M::Observation&out){
  out.lease=f.e.identity;out.event=identity.event;out.index=0;out.event_state=word(std::span<const uint8_t>(f.image).subspan(0xE4,4));
  out.camera_play=f.o.camera_play;out.retained_allocation=out.entry_fence=out.idle_depths=out.manager_table_owned=true;
  for(unsigned n=0;n<2;++n){out.actors[n]=actor(n);out.actors[n].status=status[n];}return true;
 },[&](uint32_t address,M::Actor&out){for(unsigned n=0;n<2;++n)if(f.e.actors[n].address==address){out=actor(n);out.status=status[n];return true;}return false;},
 [&](uint32_t manager,int32_t index,uint32_t force){assert(manager==f.e.identity.manager&&index==0&&!force);
  f.calls.push_back(0x0254465C);for(unsigned n=0;n<2;++n)active->route_all_off(f.e.actors[n].address,[&]{status[n]&=~M::kAllOffMask;return 0;});
  put(f.image,0xE4,0);f.o.camera_play=0;
 });active=&terminal;
 // Native mainProc clears current_event before the terminal idle boundary.
 f.o.current_event=0;assert(terminal.close()==M::Result::Completed);
 C::Barrier camera(f.e.identity,f.e.camera,{[&](const Cpu*,C::Capture&out){out.identity=f.e.identity;out.camera=f.o.camera;
  out.event_state=0;out.camera_play=f.o.camera_play;out.terminal_serial=terminal.completion().serial();out.retained=out.exclusive=true;return true;},
  [](const Camera&,const story_native_event_lease::Identity&){return true;},[](const Cpu*){return true;},[]{return true;}});
 assert(camera.arm(terminal.completion()));Cpu camera_cpu{};camera_cpu.r[3]=f.e.camera.address;
 assert(camera.run(&camera_cpu,0x024FE3E8,[&]{if(camera_returns){f.o.camera.current_type=f.o.camera.next_type=f.e.camera.current_type;put(f.o.camera.event_data,12,~0u);}})==C::Decision::Observed);
 if(reenter){f.o.camera.current_type=f.o.camera.next_type=f.o.camera.event_type;put(f.o.camera.event_data,12,f.e.camera.current_type);}
 return i.stop_terminal(&cpu,terminal,camera);
}
int main(){Cpu c{};c.r[10]=100;c.lr=101;c.f[7].ps0=1;Cpu before=c;
 // A real SDK Install transition replaces passive retail partner caches only
 // through native Order/CheckStart. EndControl leaves its last null caches;
 // neither successful entry nor natural release writes an old cache snapshot.
 {Fake idle;for(unsigned n=0;n<4;++n)put(idle.control,0xC4+n*4,n?0x16C:0x110);idle.control[0xD8]=idle.control[0xD9]=0xFF;
  Install own(idle.backend(),idle.e);assert(own.start(&c)==Status::Started&&idle.calls.size()==7&&idle.writes==0);
  for(unsigned n=0;n<4;++n)assert(word(std::span<const uint8_t>(idle.control).subspan(0xC4+n*4,4))==~0u);
  idle.running(true);assert(finish_native(idle,own,c)==Status::Released&&own.can_free(idle.released()));
  for(unsigned n=0;n<4;++n)assert(word(std::span<const uint8_t>(idle.control).subspan(0xC4+n*4,4))==~0u);
  assert(!std::memcmp(&c,&before,sizeof c));
 }
 Fake f;Install installed(f.backend(),f.e);assert(installed.start(&c)==Status::Started&&!memcmp(&c,&before,sizeof c));
 assert(f.writes==0); // start did not initialize the image before native calls
 assert(f.calls==std::vector<uint32_t>({0x0253EC0C,0x0253FD1C,0x0254273C,0x025443B4,0x0259F7D4,0x0259F7D4,0x02544534}));
 assert(!installed.can_free(Result{}));f.running();assert(installed.observe(&c)==Status::Running&&!memcmp(&c,&before,sizeof c));f.running(true);assert(finish_native(f,installed,c)==Status::Released&&!memcmp(&c,&before,sizeof c));assert(installed.can_free(f.released()));
 assert(f.calls.back()==0x0254273C&&std::find(f.calls.begin(),f.calls.end(),0x0254465C)!=f.calls.end());
 Fake missing;auto b=missing.backend();b.detach={};Install no_callbacks(b,missing.e);assert(no_callbacks.start(&c)==Status::Refused&&missing.calls.empty()&&!missing.writes);
 Fake denied;denied.scope=false;Install no_scope(denied.backend(),denied.e);assert(no_scope.start(&c)==Status::Refused&&denied.calls.empty()&&!denied.writes);
 Fake no_image;no_image.image[0]^=0xFF;Install missing_preparation(no_image.backend(),no_image.e);assert(missing_preparation.start(&c)==Status::Refused&&no_image.calls.empty()&&!no_image.writes);
 Fake takeover;takeover.o.identity.room++;Install wrong_room(takeover.backend(),takeover.e);assert(wrong_room.start(&c)==Status::Refused&&takeover.calls.empty());
 for(uint32_t failed:{0x0253EC0Cu,0x0253FD1Cu,0x02544534u}){Fake x;x.fail=failed;Install i(x.backend(),x.e);assert(i.start(&c)==Status::RolledBack);assert(x.o.manager==x.e.retail_manager&&x.control[0xC2]==0&&!i.can_free(x.released()));assert(!memcmp(&c,&before,sizeof c));}
 Fake partial;partial.throw_after_set=1;Install partial_install(partial.backend(),partial.e);assert(partial_install.start(&c)==Status::RolledBack&&partial.o.manager==partial.e.retail_manager&&!memcmp(&c,&before,sizeof c));
 Fake bad_actor;bad_actor.o.actors[0].profile=0x123;Install wrong_actor(bad_actor.backend(),bad_actor.e);assert(wrong_actor.start(&c)==Status::Refused&&bad_actor.calls.empty());
 Fake aliases;aliases.alias=true;Install aliased(aliases.backend(),aliases.e);assert(aliased.start(&c)==Status::RolledBack&&aliases.o.actors[0].cut==aliases.e.actors[0].cut);
 Fake foreign;foreign.fail=0x0253FD1C;foreign.foreign_on_failure=true;Install stolen(foreign.backend(),foreign.e);assert(stolen.start(&c)==Status::Quarantined&&foreign.control[0xC2]==1);
 Fake busy;Install active(busy.backend(),busy.e);assert(active.start(&c)==Status::Started);busy.running(true);busy.o.camera_depth=1;const auto count=busy.calls.size();assert(active.stop(&c)==Status::Quarantined&&busy.calls.size()==count&&!active.can_free(Result{}));
 Fake detached;Install bad_detach(detached.backend(),detached.e);assert(bad_detach.start(&c)==Status::Started);detached.running(true);detached.detach_ok=false;assert(finish_native(detached,bad_detach,c)==Status::Quarantined&&detached.control[0xC2]==3);
 Fake lying;Install no_detach(lying.backend(),lying.e);assert(no_detach.start(&c)==Status::Started);lying.running(true);lying.detach_lies=true;assert(finish_native(lying,no_detach,c)==Status::Quarantined&&lying.control[0xC2]==3);
 Fake end_failure;Install no_end(end_failure.backend(),end_failure.e);assert(no_end.start(&c)==Status::Started);end_failure.running(true);end_failure.fail=0x0253EFA8;assert(finish_native(end_failure,no_end,c)==Status::Quarantined&&!no_end.can_free(Result{}));

 // Rebuild from actual serialized Expected+phase plus fresh opaque live Restore
 // certificate. No native call, memory write or script replay during rehydrate.
 Fake snapshot;Install original(snapshot.backend(),snapshot.e);assert(original.start(&c)==Status::Started);snapshot.running();snapshot.o.image=snapshot.image;snapshot.o.control_owned=true;
 std::vector<uint8_t> record;using story_native_event_lease::Phase;assert(story_native_event_record::encode(snapshot.e,Phase::Running,record));Expected decoded;Phase phase;assert(story_native_event_record::decode(record,decoded,phase));
 auto certificate=validate(Operation::Restore,decoded,snapshot.o);assert(certificate.restorable()&&certificate.matches(decoded));
 const auto calls_before=snapshot.calls.size();const auto writes_before=snapshot.writes;
 Install restored(snapshot.backend(),decoded);assert(restored.rehydrate(certificate,phase)==Status::Running&&snapshot.calls.size()==calls_before&&snapshot.writes==writes_before);assert(!memcmp(&c,&before,sizeof c));
 snapshot.running(true);assert(finish_native(snapshot,restored,c)==Status::Released&&restored.can_free(snapshot.released()));
 Fake staged;Install pending(staged.backend(),staged.e);assert(pending.start(&c)==Status::Started);staged.o.image=staged.image;staged.o.control_owned=true;auto published=validate(Operation::Restore,staged.e,staged.o);Install restored_pending(staged.backend(),staged.e);assert(restored_pending.rehydrate(published,Phase::Published)==Status::Started);
 for(unsigned n=0;n<5;++n){Install invalid(staged.backend(),staged.e);auto proof=n==0?Result{}:n==1?validate(Operation::Publish,staged.e,staged.o):published;const auto bad_phase=n==2?Phase::Running:n==3?Phase::Prepared:n==4?Phase::Retired:Phase::Published;const auto old_calls=staged.calls.size();assert(invalid.rehydrate(proof,bad_phase)==Status::Quarantined&&invalid.start(&c)==Status::Quarantined&&staged.calls.size()==old_calls);}
 auto changed=staged.e;changed.camera.work37C[0]=1;Install wrong_backup(staged.backend(),changed);assert(wrong_backup.rehydrate(published,Phase::Published)==Status::Quarantined);
 changed=staged.e;changed.identity.generation++;Install stale_generation(staged.backend(),changed);assert(stale_generation.rehydrate(published,Phase::Published)==Status::Quarantined);
 Fake draining;Install draining_original(draining.backend(),draining.e);assert(draining_original.start(&c)==Status::Started);draining.running(true);draining.o.image=draining.image;draining.o.control_owned=true;auto drain_certificate=validate(Operation::Restore,draining.e,draining.o);Install restored_drain(draining.backend(),draining.e);assert(restored_drain.rehydrate(drain_certificate,Phase::Draining)==Status::Draining&&finish_native(draining,restored_drain,c)==Status::Released);
 Fake stack_failure;stack_failure.fault_after_set=true;Install poisoned_bridge(stack_failure.backend(),stack_failure.e);assert(poisoned_bridge.start(&c)==Status::Quarantined&&!poisoned_bridge.can_free(Result{}));
 Fake incomplete;Install cancel(incomplete.backend(),incomplete.e);assert(cancel.start(&c)==Status::Started);assert(cancel.stop(&c,false)==Status::Released);assert(cancel.can_free(incomplete.released()));
 Fake native;Install native_install(native.backend(),native.e);assert(native_install.start(&c)==Status::Started);native.running(true);
 assert(finish_native(native,native_install,c)==Status::Released&&!memcmp(&c,&before,sizeof c));
 native.o.image=native.image;native.o.control_owned=false;native.o.control_released=true;
 const auto native_release=validate(Operation::ReleaseTerminal,native.e,native.o);
 assert(native_install.can_free(native_release));
 Fake waiting;Install waiting_install(waiting.backend(),waiting.e);assert(waiting_install.start(&c)==Status::Started);waiting.running(true);
 assert(finish_native(waiting,waiting_install,c,false)==Status::Quarantined&&waiting.control[0xC2]==3&&!waiting_install.can_free(Result{}));
 Fake terminal_lies;Install terminal_detach(terminal_lies.backend(),terminal_lies.e);assert(terminal_detach.start(&c)==Status::Started);terminal_lies.running(true);terminal_lies.detach_lies=true;
 assert(finish_native(terminal_lies,terminal_detach,c)==Status::Quarantined&&terminal_lies.control[0xC2]==3);
 Fake reentered;Install reentered_install(reentered.backend(),reentered.e);assert(reentered_install.start(&c)==Status::Started);reentered.running(true);
 assert(finish_native(reentered,reentered_install,c,true,true)==Status::Quarantined&&reentered.control[0xC2]==3);
 // Typed E60 restore binds the complete opaque definition. No native call,
 // write, prepare-by-extent or publication replay is needed for rehydration.
 Fake turn;story_native_turn_camera_image::Program definition;definition.camera.start_eye={0,10,20};definition.camera.end_eye={10,10,20};
 turn.e.program=story_native_event_program::Program::turn(definition,301);turn.e.identity.bytes=0xE60;turn.e.frames=definition.wait_timer;
 turn.image.assign(turn.e.program->pristine().begin(),turn.e.program->pristine().end());put(turn.image,0xE4,1);
 turn.o.identity=turn.e.identity;turn.o.image=turn.image;turn.o.control_owned=true;turn.o.control_released=false;
 const auto pools=turn.e.program->pool_offsets();for(unsigned n=0;n<8;++n)turn.o.manager[n]=turn.e.identity.image+pools[n];
 for(unsigned n=0;n<2;++n){put(turn.o.actors[n].cut,0,turn.e.identity.image+0xF0+n*0x50);put(turn.o.actors[n].cut,8,turn.e.actors[n].address);}
 const auto turn_restore=validate(Operation::Restore,turn.e,turn.o);assert(turn_restore.restorable());
 story_native_event_lease::Lease raw_turn;assert(!raw_turn.prepare(turn.e.identity));
 const auto turn_calls=turn.calls.size();const auto turn_writes=turn.writes;
 Install restored_turn(turn.backend(),turn.e);assert(restored_turn.rehydrate(turn_restore,Phase::Published)==Status::Started&&turn.calls.size()==turn_calls&&turn.writes==turn_writes);
 auto changed_turn=turn.e;changed_turn.program=story_native_event_program::Program::turn(definition,302);
 Install wrong_turn_definition(turn.backend(),changed_turn);assert(wrong_turn_definition.rehydrate(turn_restore,Phase::Published)==Status::Quarantined);
 changed_turn=turn.e;changed_turn.camera.work37C[4]^=1;Install wrong_turn_baseline(turn.backend(),changed_turn);assert(wrong_turn_baseline.rehydrate(turn_restore,Phase::Published)==Status::Quarantined);
 turn.scope=false;Install missing_turn_scope(turn.backend(),turn.e);assert(missing_turn_scope.rehydrate(turn_restore,Phase::Published)==Status::Quarantined&&turn.calls.size()==turn_calls&&turn.writes==turn_writes);
 auto throwing_backend=turn.backend();throwing_backend.scope=[](const Expected&)->bool{throw std::runtime_error("restore source");};Install throwing_restore(std::move(throwing_backend),turn.e);assert(throwing_restore.rehydrate(turn_restore,Phase::Published)==Status::Quarantined);

}
