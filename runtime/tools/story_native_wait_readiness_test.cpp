// SPDX-License-Identifier: MPL-2.0
// Reuse the bounded native class byte fixture, not its test entrypoint.
#define main collector_fixture_contract_unused
#include "story_native_event_collect_test.cpp"
#include <algorithm>
#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
#include <utility>
#include <cstring>
#undef main
void log_msg(const char*,...) __attribute__((format(printf,1,2)));
void log_msg(const char*,...) {} // private discovery diagnostics are not test authority
#include "mods/story_native_event_backend.h"
#include <memory>
namespace B=story_native_event_backend;
namespace D=story_native_event_depth;
struct RuntimeFixture:Fake {
 bool session_valid=true,actor_valid=true;uint64_t logic=100;D::Tracker*tracker=nullptr;
 RuntimeFixture(){
  memory[0x10044878].resize(4+145*0x84);memory[0x10049348].resize(8+63*0x40);put(0x10044878,145);put(0x10049348,63);e.camera.style_count=145;e.camera.type_count=63;
  const char name[]="Event";copy(0x10049350+2*0x40,std::span(reinterpret_cast<const uint8_t*>(name),sizeof(name)));put(0x1004487C+4,11);
  // Synthetic trusted placement rows, with the REAL bounded ActorTag/People
  // byte layout used by snapshot roots (never a native invocation seam).
  add(0x101F3328,12);add(0x60070000,0x2000);
  put(0x101F3328,e.actors[0].address+0xC8);put(0x101F332C,e.actors[1].address+0xC8);put(0x101F3330,2);
  for(unsigned n=0;n<2;++n){const auto&a=e.actors[n];put(a.address+0xB4,0x100209C4);
   put(a.address+0xC8,n?e.actors[0].address+0xC8:0);put(a.address+0xCC,0x101F3328);put(a.address+0xD0,n?0:e.actors[1].address+0xC8);put(a.address+0xD4,a.address);byte(a.address+0xD8,1);
   put(0x60070080+n*4,a.pid);put(0x60071600+n*40,0x52455631);put(0x60071604+n*40,a.token);copy(0x60071608+n*40,a.fingerprint);
  }
  copy(0x60070300,e.stage_name);put(0x60070318,e.identity.stage);put(0x60070400,e.identity.room);
  for(unsigned offset:{0xC4u,0xC8u,0xCCu,0xD0u})put(play+0x51D0+offset,0xFFFFFFFFu);half(play+0x51D0+0xD8,0xFFFF);
 }
 B::Scaffold scaffold(D::Tracker&d){tracker=&d;B::Config cfg;cfg.collector=config;cfg.memory={stamp,link_actor,55,{0x60002000,4096}};B::Sources sources;
  sources.read=[&](uint32_t a,std::span<uint8_t>b){++reads;auto*p=pointer(a,b.size());if(!p)return false;std::copy_n(p,b.size(),b.begin());return true;};
  sources.session=[&](story_native_event_memory::Scope&s){s.identity=e.identity;s.link=link_actor;s.link_pid=55;s.exclusive=session_valid;s.bindings_verified=owned;s.logic_step=logic;s.serial=tracker->snapshot().serial;return true;};
  sources.allocation=[&](const auto&,const auto&){return allocated;};sources.actor=[&](const Actor&a,const auto&){const auto n=(a.address-e.actors[0].address)/0x1000;return actor_valid&&n<2&&a.pid==e.actors[n].pid&&a.fingerprint==actual_fingerprints[n];};return B::Scaffold(e,cfg,sources,d);
 }
};
[[maybe_unused]] static D::Coverage covered(){return {{11,11,true},{0,0,true},{22,22,true},true};}
#include "mods/story_native_wait_readiness.h"
#include "mods/story_scenes.h"
#include <algorithm>
#include <string>
#include <array>
#include <vector>
#include <cstdint>
#include <span>
#include <utility>
#include <cstring>
namespace W=story_native_wait_readiness;
static RuntimeFixture*current_fixture=nullptr;
static std::vector<mem::AllocRec>allocations;
static unsigned allocation_calls=0,scene_lookups=0,queries=0;static bool definitions=true,rows=true,pending_native=false,query_throw=false,active_scene=false;
static story_notebook::ManifestScene fixture_scene;
namespace mem {
uint32_t runtime_alloc(uint32_t size,uint32_t align){assert(size==story_native_event_arena::kBytes&&align==16);++allocation_calls;const uint32_t base=
#ifdef STORY_NATIVE_CROSS_SESSION_MULTI_ARENA
 0x60010000+(allocation_calls-1)*0x40000;
#else
 0x60010000;
#endif
 current_fixture->memory[base].assign(size,0);allocations.push_back({base,size,123});return base;}
uint32_t runtime_top(){uint32_t top=0x60000000;for(const auto&a:allocations){const uint64_t end=uint64_t(a.addr)+a.size;if(end>UINT32_MAX)return 0;top=std::max(top,uint32_t(end));}return top;}
std::vector<AllocRec>runtime_alloc_log(){return allocations;}
}
namespace story_scenes {
bool declared_scene(const std::string&mod,const std::string&id,story_notebook::ManifestScene&out){++scene_lookups;out={};if(!definitions||mod!="test"||id!="private-wait")return false;out=fixture_scene;return true;}
bool active(){return active_scene;}
}
namespace story_actors {
bool resident_event_declaration(const std::string&mod,const std::string&id,ResidentEventDeclaration&out){out={};if(!definitions||mod!="test")return false;unsigned n=id=="first"?0:id=="second"?1:2;if(n==2)return false;const auto&a=current_fixture->e.actors[n];out={a.token,0,1,"sea",a.fingerprint};return true;}
uint32_t query_resident_event_address(Cpu*c,const std::string&mod,const std::string&id){
#ifdef STORY_NATIVE_READINESS_QUERY_PRECONDITION
 STORY_NATIVE_READINESS_QUERY_PRECONDITION(c);
#endif
 ++queries;c->r[10]=0xBAD;c->lr=0xBAD;c->f[2].ps0=99;std::fill_n(current_fixture->memory[0x60030000].begin()+0x80,64,0xCC);if(query_throw)throw std::runtime_error("synthetic native search");if(pending_native||mod!="test")return 0;return id=="first"?current_fixture->e.actors[0].address:id=="second"?current_fixture->e.actors[1].address:0;}
bool resident_event_identity(const std::string&mod,const std::string&id,uint32_t actor,uint32_t pid,ResidentEventIdentity&out){out={};if(!rows||mod!="test")return false;unsigned n=id=="first"?0:id=="second"?1:2;if(n==2)return false;const auto&a=current_fixture->e.actors[n];if(actor!=a.address||pid!=a.pid)return false;out={a.token,a.address,a.pid,current_fixture->e.identity.stage,current_fixture->e.identity.room,0,1,a.fingerprint};return true;}
bool resident_event_identity_quiet(const std::string&mod,const std::string&id,uint32_t actor,uint32_t pid,ResidentEventIdentity&out){return resident_event_identity(mod,id,actor,pid,out);}
bool resident_event_process(const std::string&mod,const std::string&id,ResidentEventProcess&out){
 out={};ResidentEventDeclaration declaration;if(!rows||!resident_event_declaration(mod,id,declaration))return false;
 const unsigned n=id=="second"?1:0;const auto&a=current_fixture->e.actors[n];ResidentEventProcess p;
 p.presence=ResidentEventProcess::Presence::Present;p.declaration=declaration;p.pid=a.pid;p.pid_slot=0x60070080+n*4;p.identity_row=0x60071600+n*40;p.scratch_window=0x60070300;
 auto copy=[&](uint32_t at,auto&bytes){auto*source=current_fixture->pointer(at,bytes.size());if(!source)return false;std::copy_n(source,bytes.size(),bytes.begin());return true;};
 if(!copy(p.pid_slot,p.pid_bytes)||!copy(p.identity_row,p.identity_bytes)||!copy(p.scratch_window,p.scratch_bytes))return false;
 const auto get=[](const auto&bytes,unsigned at){return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];};
 if(get(p.pid_bytes,0)!=p.pid||get(p.identity_bytes,0)!=0x52455631||get(p.identity_bytes,4)!=p.declaration.token||!std::equal(p.declaration.fingerprint.begin(),p.declaration.fingerprint.end(),p.identity_bytes.begin()+8))return false;
 out=std::move(p);return true;
}

}
struct Readiness:RuntimeFixture {
 bool committed=true,idle=true,frame_ok=true,change_boundary=false;unsigned boundaries=0;
 Readiness(){current_fixture=this;allocation_calls=scene_lookups=queries=0;allocations.clear();definitions=rows=true;pending_native=query_throw=active_scene=false;fixture_scene={};fixture_scene.id="private-wait";story_notebook::SceneStep wait,end;wait.kind=story_notebook::SceneStep::Kind::Wait;wait.frames=60;end.kind=story_notebook::SceneStep::Kind::End;fixture_scene.steps={wait,end};memory[0x60030000].assign(0x1000,0x55);}
 W::Sources source(){W::Sources s;s.startup_complete=[&](){return committed;};s.boundary=[&](const Cpu*,W::Boundary&out){++boundaries;out={0x60040000,0x60030000,0x60031000,100,idle};if(change_boundary&&boundaries>1)out.logic_step++;return true;};s.read=[&](uint32_t a,std::span<uint8_t>b){auto*p=pointer(a,b.size());if(!p)return false;std::copy_n(p,b.size(),b.begin());return true;};s.restore_frame=[&](uint32_t a,std::span<const uint8_t>b){if(!frame_ok)return false;auto*p=pointer(a,b.size());if(!p)return false;std::copy(b.begin(),b.end(),p);return true;};return s;}
};
static W::Request request(){return {true,"test","private-wait","sdk-private-wait-v1",{"first","second"},30,{}};}
#ifndef STORY_NATIVE_WAIT_READINESS_FIXTURE_ONLY
int main(){
 Readiness off;auto disabled_request=request();disabled_request.private_opt_in=false;W::Controller disabled(disabled_request,off.source());assert(disabled.startup()==W::Status::Disabled&&allocation_calls==0&&scene_lookups==0);Cpu c{};c.r[1]=0x600300C0;c.r[3]=3;c.lr=4;c.f[2].ps0=1;const Cpu before=c;assert(disabled.inspect(&c)==W::Status::Disabled&&queries==0);
 Readiness f;W::Controller controller(request(),f.source());assert(controller.startup()==W::Status::Allocated&&allocation_calls==1);assert(controller.startup()==W::Status::Allocated&&allocation_calls==1);assert(controller.inspect(&c)==W::Status::Ready&&!memcmp(&before,&c,sizeof c));const auto*candidate=controller.candidate();assert(candidate&&candidate->actors[0].fingerprint==f.e.actors[0].fingerprint&&candidate->identity.image==0x60010100&&candidate->camera==f.e.camera&&candidate->frames==30&&!controller.publication_enabled());assert(controller.original_image().size()==0x520);for(auto b:f.memory[0x60010000])assert(b==0);for(unsigned n=0x80;n<0xC0;++n)assert(f.memory[0x60030000][n]==0x55);const auto definition_hash=controller.definition_hash();f.idle=false;assert(controller.inspect(&c)==W::Status::NotReady&&!controller.candidate()&&controller.original_image().empty()&&allocation_calls==1);f.idle=true;assert(controller.inspect(&c)==W::Status::Ready&&controller.candidate()&&allocation_calls==1&&!memcmp(&before,&c,sizeof c));for(auto b:f.memory[0x60010000])assert(b==0);
 for(unsigned fault=0;fault<5;++fault){Readiness x;auto r=request();if(fault==0)definitions=false;if(fault==1)x.committed=false;if(fault==2)r.residents[1]=r.residents[0];if(fault==3)fixture_scene.steps[1].then.push_back({story_notebook::StoryAction::Kind::Flag,"unsafe",1});if(fault==4)r.fixture_program="unbound";W::Controller bad(r,x.source());assert(bad.startup()==W::Status::Definitions&&allocation_calls==0);}
 for(unsigned fault=0;fault<9;++fault){Readiness x;W::Controller bad(request(),x.source());assert(bad.startup()==W::Status::Allocated);if(fault==0)x.idle=false;if(fault==1)pending_native=true;if(fault==2)rows=false;if(fault==3)x.put(cam_process+4,0);if(fault==4)x.put(play+0x52C4+0x53C,0x20007000);if(fault==5)x.change_boundary=true;if(fault==6)x.put(0x1004487C+4,19);if(fault==7)x.frame_ok=false;if(fault==8)query_throw=true;c=before;auto state=bad.inspect(&c);assert((state==W::Status::NotReady||state==W::Status::Quarantined)&&!bad.candidate()&&!memcmp(&before,&c,sizeof c));}
 // Actual retail post-talk tuple from090: cached PIDs are inert in mode0.
 // No control byte is rewritten by readiness, and active control still denies.
 for(unsigned fault=0;fault<6;++fault){Readiness x;for(unsigned n=0;n<4;++n)x.put(play+0x51D0+0xC4+n*4,n?0x16C:0x110);
  if(fault==1)x.byte(play+0x51D0+0xC0,1);if(fault>=2&&fault<=4)x.byte(play+0x51D0+0xC2,fault-1);if(fault==5)x.half(play+0x51D0+0xD8,0);
  std::array<uint8_t,0xF4>control{};std::copy_n(x.pointer(play+0x51D0,control.size()),control.size(),control.begin());
  W::Controller idle(request(),x.source());assert(idle.startup()==W::Status::Allocated);c=before;const auto result=idle.inspect(&c);
  assert((result==W::Status::Ready)==(fault==0)&&!std::memcmp(&before,&c,sizeof c));assert(!std::memcmp(x.pointer(play+0x51D0,control.size()),control.data(),control.size()));
  if(fault)assert(!idle.candidate()&&idle.check()==W::Check::Control);
 }
 // Typed TURN readiness selects the separate arena instance without
 // publishing any native bytes; the complete immutable program binds the hash.
 std::array<uint8_t,32> turn_hash{};
 for(unsigned variant=0;variant<2;++variant){Readiness x;x.put(0x1004487C+4,1);x.half(0x10049350+2*0x40+0x18,1);x.put(0x1004487C+0x84+4,11);auto r=request();story_native_turn_camera_image::Program def;def.camera.start_eye={0,10,20};def.camera.end_eye={10,10,20};r.program=story_native_event_program::Program::turn(def,301+variant);r.fixture_program="sdk-private-turn-v1";W::Controller turn(r,x.source());assert(turn.startup()==W::Status::Allocated);assert(turn.inspect(&c)==W::Status::Ready);const auto*e=turn.candidate();assert(e&&e->program==r.program&&e->identity.image==turn.arena().turn.image.address&&e->identity.bytes==0xE60&&turn.original_image().size()==0xE60);for(auto byte:x.memory[0x60010000])assert(byte==0);if(!variant)turn_hash=turn.definition_hash();else assert(turn.definition_hash()!=turn_hash);}
 for(unsigned fault=0;fault<3;++fault){Readiness x;auto r=request();story_native_turn_camera_image::Program def;def.camera.start_eye={0,10,20};def.camera.end_eye={10,10,20};r.program=story_native_event_program::Program::turn(def,301);r.fixture_program="sdk-private-turn-v1";if(fault==0)r.fixture_program="sdk-private-wait-v1";if(fault==1)r.native_wait_timer=31;if(fault==2)r.program.reset();W::Controller bad(r,x.source());assert(bad.startup()==W::Status::Definitions&&allocation_calls==0);}
 Readiness different;auto timer=request();timer.native_wait_timer=31;W::Controller changed(timer,different.source());assert(changed.startup()==W::Status::Allocated&&changed.definition_hash()!=definition_hash);
 return 0;
}
#endif
