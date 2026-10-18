// SPDX-License-Identifier: MPL-2.0
// Genuine helper-generated cancellation receipts with the real Install/Lease
// state machine. Native bodies and thread issuer are explicit standalone seams;
// this test never grants production invocation or game-session authority.
#include "mods/story_native_event_install.h"
#include "mods/story_native_stage_owner.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cassert>
#include <cstring>
#include <memory>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
#include <span>
namespace NI=story_native_event_install;
namespace XO=story_native_cancel_observation;
namespace DO=story_native_event_depth;
static unsigned cancel_case=0,completed_cases=0;
static std::shared_ptr<const threads::NativeQuiescence> test_parking(bool&);
template<class F>struct InstallCancelDriver {
 F&f;std::unique_ptr<NI::Install>install;std::unique_ptr<DO::Tracker>other;
 std::vector<uint32_t>calls;unsigned writes=0,ends=0;bool scope=true,started=false,closed_seen=false,finished=false;
 static void store(auto&b,unsigned p,uint32_t v){for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(v>>(24-8*n));}
 static uint32_t load(const auto&b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
 explicit InstallCancelDriver(F&fixture):f(fixture){}
 NI::Backend backend(){NI::Backend b;
 b.read=[&](uint32_t a,std::span<uint8_t>v){return f.read(a,v);};
 b.write=[&](uint32_t a,std::span<const uint8_t>v){++writes;if(cancel_case==7&&writes==4)return false;if(cancel_case==8&&writes==4)throw std::runtime_error("partial manager restore");return f.write(a,v);};
 b.scope=[&](const auto&e){return scope&&e==f.e;};b.detach=[](Cpu*,const auto&){return true;};
 b.capture=[&](Cpu*,story_native_event_observation::Captured&o){o={};o.identity=f.e.identity;o.room_number=f.e.room_number;o.stage_name=f.e.stage_name;o.image=f.mem[f.e.identity.image];o.resource_generation_matches=true;o.camera=f.current;auto&m=f.mem[f.e.identity.manager];for(unsigned n=0;n<8;++n)o.manager[n]=load(m,n*4);o.camera_play=load(m,0x20);o.current_event=load(m,0x53C);std::copy_n(m.begin()+0x3C,0x500,o.flags.begin());o.actors=f.e.actors;for(unsigned n=0;n<2;++n)std::copy_n(f.mem[f.e.actors[n].address].begin()+0x3E0,0x6C,o.actors[n].cut.begin());return true;};
 b.invoke=[&](Cpu*c,uint32_t fn,std::initializer_list<uint32_t>args,std::initializer_list<uint32_t>stack){calls.push_back(fn);c->r[10]=0xBAD;c->lr=0xBAD;auto&control=f.mem[f.e.identity.manager-0xF4];auto&manager=f.mem[f.e.identity.manager];std::vector<uint32_t>a(args);
  if(fn==GC(0x0253EC0C)){assert(a.size()==8&&a[0]==f.e.identity.manager-0xF4&&a[1]==3&&std::vector<uint32_t>(stack)==std::vector<uint32_t>{0xFF});control[1]=3;control[16]=control[17]=control[20]=control[21]=0xFF;control[19]=1;control[0xC0]=1;return 1u;}
  if(fn==GC(0x0253FD1C)){control[0xC0]=0;control[0xC2]=3;for(unsigned p:{0xC4u,0xC8u,0xCCu,0xD0u})store(control,p,~0u);for(unsigned p:{0xD8u,0xD9u,0xDEu,0xDFu})control[p]=0xFF;return 1u;}
  if(fn==GC(0x0254273C)){for(unsigned n=0;n<8;++n)store(manager,n*4,0);return 0u;}
  if(fn==GC(0x025443B4)){assert(a==std::vector<uint32_t>({f.e.identity.manager,f.e.identity.image}));auto pools=f.e.program->pool_offsets();for(unsigned n=0;n<8;++n)store(manager,n*4,n&&!pools[n]?0:f.e.identity.image+pools[n]);return 0u;}
  if(fn==GC(0x0259F7D4)){assert(a.size()==3);unsigned n=a[2]==f.e.actors[0].address?0:1;assert(a[0]==f.e.actors[n].address+0x3E0&&a[1]==f.e.identity.image+0xF0+n*0x50);auto&actor=f.mem[f.e.actors[n].address];store(actor,0x3E0,a[1]);store(actor,0x3E8,a[2]);store(actor,0x3F0,~0u);return 0u;}
  if(fn==GC(0x02544534)){assert(a==std::vector<uint32_t>({f.e.identity.manager,0}));store(f.mem[f.e.identity.image],0xE4,1);return 1u;}
  if(fn==GC(0x0253EFA8)){assert(!threads::current_thread_holds_native_quiescence()&&!story_native_stage_owner::current_thread_holds_lifetime_pin());++ends;assert(a==std::vector<uint32_t>({f.e.identity.manager-0xF4})&&control[0xC2]==3);control[0xC2]=0;if(cancel_case==5)return 0u;if(cancel_case==6)throw std::runtime_error("native EndControl partial");return 1u;}
  assert(false);return 0u;};return b;
 }
 void start(){
  // Install reads/writes the SAME fixture memory used later by ManagerCancel.
  // The following explicit body simulation supplies native scene progression
  // after the genuine seven-call publication, with no forged O::Result.
  f.e.retail_manager={0x31000000,0x31000040,0x310000F0,0x31000230,0x31000500,0x31000D80,0x31000E00,0x31000E40};f.b.expected=f.e;
  const auto running_image=f.mem[f.e.identity.image],running_manager=f.mem[f.e.identity.manager];const auto entered=f.current;
  f.mem[f.e.identity.image]={f.e.program->pristine().begin(),f.e.program->pristine().end()};auto&manager=f.mem[f.e.identity.manager];manager.assign(0x540,0);for(unsigned n=0;n<8;++n)store(manager,n*4,f.e.retail_manager[n]);f.current=f.e.camera;f.mem[f.e.identity.manager-0xF4].resize(0xF4);
  for(unsigned n=0;n<2;++n){auto&actor=f.mem[f.e.actors[n].address];std::copy(f.e.actors[n].cut.begin(),f.e.actors[n].cut.end(),actor.begin()+0x3E0);store(actor,0x2E0,f.poses[n].status);}
  install=std::make_unique<NI::Install>(backend(),f.e);const Cpu cpu=f.cpu;assert(install->start(&f.cpu)==NI::Status::Started&&!std::memcmp(&cpu,&f.cpu,sizeof cpu)&&!writes);
  assert(calls==std::vector<uint32_t>({GC(0x0253EC0C),GC(0x0253FD1C),GC(0x0254273C),GC(0x025443B4),GC(0x0259F7D4),GC(0x0259F7D4),GC(0x02544534)}));
  f.mem[f.e.identity.image]=running_image;f.mem[f.e.identity.manager]=running_manager;f.current=entered;for(unsigned n=0;n<2;++n){auto&actor=f.mem[f.e.actors[n].address];store(actor,0x2E0,f.poses[n].status|0x8000u);store(actor,0x3E4,n);store(actor,0x3F0,3);store(f.mem[f.e.identity.image],0xF0+n*0x50+0x3C,3);}
  assert(install->observe(&f.cpu)==NI::Status::Running&&calls.size()==7&&!writes);
  if(cancel_case==2){other=std::make_unique<DO::Tracker>(f.e,DO::Coverage{{11,11,true},{22,22,true},{33,33,true},true});{auto frame=other->enter(f.b.parent);assert(frame.allowed());}assert(other->begin_drain());}
  started=true;
 }
 template<class T,class Outer,class Source>bool closed(T&t,const Outer&o,const XO::Result&r,Source&){
  closed_seen=true;const auto native_calls=calls.size();if(cancel_case==1)scope=false;
  auto state=install->begin_cancel(&f.cpu,cancel_case==2?*other:t,o,r);assert(calls.size()==native_calls&&!writes);
  if(cancel_case==1||cancel_case==2){assert(state==NI::Status::Quarantined&&!install->can_free(r));assert(install->begin_cancel(&f.cpu,t,o,r)==NI::Status::Quarantined&&calls.size()==native_calls&&!writes);finished=true;return false;}
  assert(state==NI::Status::Draining);return true;
 }
 template<class T,class Outer,class Source>bool detached(T&t,const Outer&o,const XO::Result&closed_result,const XO::Result&r,Source&s){
  const auto before=calls.size();const Cpu cpu=f.cpu;
  if(cancel_case==3){assert(threads::current_thread_holds_native_quiescence());assert(install->release_cancel_native(&f.cpu,t,o,r)==NI::Status::Quarantined&&calls.size()==before&&!writes&&!ends);finished=true;return false;}
  s.quiescence.reset();assert(!threads::current_thread_holds_native_quiescence());
  NI::Status state;
  if(cancel_case==4){story_native_stage_owner::Tracker stage;auto pin=stage.scan();assert(story_native_stage_owner::current_thread_holds_lifetime_pin());state=install->release_cancel_native(&f.cpu,t,o,r);}else state=install->release_cancel_native(&f.cpu,t,o,cancel_case==10?closed_result:r);
  assert(!std::memcmp(&cpu,&f.cpu,sizeof cpu));
  if(cancel_case==4||cancel_case==10){assert(state==NI::Status::Quarantined&&calls.size()==before&&!writes&&!ends);finished=true;return false;}
  if(cancel_case>=5&&cancel_case<=8){assert(state==NI::Status::Quarantined&&ends==1&&!install->can_free(r));const auto count=calls.size();const auto written=writes;assert(install->release_cancel_native(&f.cpu,t,o,r)==NI::Status::Quarantined&&calls.size()==count&&writes==written);finished=true;return false;}
  assert(state==NI::Status::Draining&&ends==1&&calls.size()==before+2&&writes==11);
  if(cancel_case==9){const auto count=calls.size();const auto written=writes;assert(install->release_cancel_native(&f.cpu,t,o,r)==NI::Status::Quarantined&&calls.size()==count&&writes==written);finished=true;return false;}
  // Fresh opaque parking for the bounded Released readback only; broad native
  // control release above deliberately held neither Q nor Stage pin.
  s.quiescence=test_parking(parked);return true;
 }
 bool parked=true;
 template<class T,class Outer,class Source>void released(T&t,const Outer&o,const XO::Result&closed_result,const XO::Result&detached_result,const XO::Result&r,Source&){
  assert(r.released()&&r.same_cancellation(closed_result)&&r.same_cancellation(detached_result));const auto count=calls.size();const auto written=writes;assert(install->retire_cancel(&f.cpu,t,o,r)==NI::Status::Released&&install->can_free(r)&&!install->can_free(story_native_event_observation::Result{})&&calls.size()==count&&writes==written);finished=true;
 }
 ~InstallCancelDriver(){assert(started&&closed_seen&&finished);++completed_cases;}
};
#define STORY_CANCEL_BEFORE_CAMERA(f) InstallCancelDriver<decltype(f)> install_driver(f);install_driver.start()
#define STORY_CANCEL_AFTER_CLOSED(f,t,o,r,s) if(!install_driver.closed(t,o,r,s))return
#define STORY_CANCEL_AFTER_DETACHED(f,t,o,c,r,s) if(!install_driver.detached(t,o,c,r,s))return
#define STORY_CANCEL_AFTER_RELEASED(f,t,o,c,d,r,s) install_driver.released(t,o,c,d,r,s)
#define main observation_cancel_fixture_main
#include "story_native_cancel_observation_test.cpp"
#include <algorithm>
#include <cstdint>
#include <span>
#undef main
static std::shared_ptr<const threads::NativeQuiescence> test_parking(bool&live){return std::make_shared<threads::NativeQuiescence>(threads::NativeQuiescenceIssuer::test(live));}
int main(){for(cancel_case=0;cancel_case<=10;++cancel_case)assert(observation_cancel_fixture_main()==0);assert(completed_cases==11);}
