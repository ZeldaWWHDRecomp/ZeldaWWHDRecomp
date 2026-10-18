// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_readiness.h"
#include "story_scenes.h"
#include "story_native_camera_policy.h"
#include "story_native_event_image.h"
#include "mod_hash.h"
#include "guest_addr.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
namespace story_native_wait_readiness {namespace {
using namespace story_native_event_observation;
const uint32_t Play=GD(0x1046F0B0),Stage=GD(0x1047E6C4),Rooms=GD(0x1047E8F0),Initialized=GD(0x104753A8);
const uint32_t Profile=GD(0x101C4CD0),Camera0=GD(0x101D55F0),Camera1=GD(0x101D5634),Methods=GD(0x101D5538);
const story_native_camera_policy::Tables Tables{GD(0x10049348),GD(0x10049350),GD(0x10044878),GD(0x1004487C)};
uint32_t word(std::span<const uint8_t>b,unsigned p=0){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
uint16_t half(std::span<const uint8_t>b,unsigned p){return uint16_t(b[p])<<8|b[p+1];}
bool span(uint32_t a,size_t bytes){return !(a&3)&&bytes&&((a>=mem::kMem2Start&&uint64_t(a)+bytes<=mem::kMem2End)||(a>=mem::kRuntimeStart&&uint64_t(a)+bytes<=mem::kHostStart));}
bool overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m){return uint64_t(a)<uint64_t(b)+m&&uint64_t(b)<uint64_t(a)+n;}
std::array<uint8_t,32>hash(const Request&r,const story_notebook::ManifestScene&s,const std::array<story_actors::ResidentEventDeclaration,2>&defs){
 std::string data;auto u=[&](uint32_t v){for(int shift=24;shift>=0;shift-=8)data.push_back(char(v>>shift));};auto text=[&](const std::string&t){u(uint32_t(t.size()));data+=t;};
 text("sdk-private-wait-readiness-v1");text(r.fixture_program);text(r.mod);text(r.scene);u(r.native_wait_timer);u(story_native_event_arena::kVersion);
 if(r.program){u(uint32_t(r.program->kind()));u(r.program->watchdog());u(r.program->bytes());const auto bytes=r.program->pristine();data.append(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
 // This slice admits only an exact WAIT→END scene with no trigger/actions.
 text(s.id);u(s.steps[0].frames);for(const auto&step:s.steps){text(step.id);text(step.next);u(uint32_t(step.kind));u(uint32_t(step.successors.size()));for(auto next:step.successors)u(uint32_t(next));}
 for(unsigned n=0;n<2;++n){text(r.residents[n]);u(defs[n].token);text(defs[n].stage);u(defs[n].room);u(defs[n].subtype);data.append(reinterpret_cast<const char*>(defs[n].fingerprint.data()),defs[n].fingerprint.size());}
 const auto hex=mods::hash::sha256_text(data);std::array<uint8_t,32>out{};auto digit=[](char c){return uint8_t(c<='9'?c-'0':c-'a'+10);};for(unsigned n=0;n<32;++n)out[n]=uint8_t(digit(hex[n*2])*16+digit(hex[n*2+1]));return out;
}
}
Controller::Controller(Request r,Sources s):request_(std::move(r)),sources_(std::move(s)){if(!request_.private_opt_in&&!request_.declared)status_=Status::Disabled;}
bool Controller::read(uint32_t a,std::span<uint8_t>b)const{return b.size()<=19140&&span(a,b.size())&&sources_.read&&sources_.read(a,b);}
Status Controller::startup(){
 if(status_==Status::Disabled||startup_attempted_||status_==Status::Quarantined)return status_;
 startup_attempted_=true;
 try{
  const bool declared=bool(request_.declared);
  if(declared){
   if(!request_.declared->startup_current()||!request_.declared->current())return status_=Status::Definitions;
   const auto&d=*request_.declared;
   // Only immutable committed data selects the native program and actors.
   request_.mod=d.mod();request_.scene=d.scene();request_.residents=d.compiled().actor_ids();
   request_.program=d.compiled().program();request_.native_wait_timer=request_.program->frames();
   request_.fixture_program="sdk-declared-native-turn-v1";
  }
  const bool turn=request_.program&&request_.program->kind()==story_native_event_program::Kind::TurnCamera;
  if(!sources_.startup_complete||!sources_.startup_complete()||!sources_.boundary||!sources_.read||!sources_.restore_frame||request_.fixture_program!=(declared?"sdk-declared-native-turn-v1":turn?"sdk-private-turn-v1":"sdk-private-wait-v1")||request_.mod.empty()||request_.scene.empty()||request_.residents[0].empty()||request_.residents[0]==request_.residents[1]||!request_.native_wait_timer||request_.native_wait_timer>32767||(request_.program&&request_.program->frames()!=request_.native_wait_timer))return status_=Status::Definitions;
  story_notebook::ManifestScene scene;if(!story_scenes::declared_scene(request_.mod,request_.scene,scene))return status_=Status::Definitions;
  if(!declared){
   if(scene.steps.size()!=2||!scene.trigger.empty()||!scene.stage.empty()||!scene.actor.empty()||!scene.quest.empty()||!scene.chart.empty())return status_=Status::Definitions;
   const auto&wait=scene.steps[0];const auto&end=scene.steps[1];if(wait.kind!=story_notebook::SceneStep::Kind::Wait||!wait.frames||wait.frames>18000||!wait.then.empty()||end.kind!=story_notebook::SceneStep::Kind::End||!end.then.empty()||!end.successors.empty()||(!wait.successors.empty()&&wait.successors!=std::vector<size_t>{1}))return status_=Status::Definitions;
  }else if(scene!=request_.declared->definition())return status_=Status::Definitions;
  for(unsigned n=0;n<2;++n)if(!story_actors::resident_event_declaration(request_.mod,request_.residents[n],definitions_[n])||definitions_[n].subtype!=1)return status_=Status::Definitions;
  if(definitions_[0].token==definitions_[1].token||definitions_[0].stage!=definitions_[1].stage||definitions_[0].room!=definitions_[1].room)return status_=Status::Definitions;
  if(declared){if(!request_.declared->startup_current()||!request_.declared->current())return status_=Status::Definitions;definition_hash_=request_.declared->definition_hash();}
  else definition_hash_=hash(request_,scene,definitions_);
  const uint32_t base=mem::runtime_alloc(story_native_event_arena::kBytes,16);arena_=story_native_event_arena::make(base);initialized_=true;
  if(!story_native_event_arena::owns(arena_,mem::runtime_alloc_log(),mem::runtime_top()))return status_=Status::Quarantined;
  return status_=Status::Allocated;
 }catch(...){return status_=Status::Quarantined;}
}
Status Controller::inspect(Cpu*c){
 if(status_==Status::Disabled||status_==Status::Quarantined||!initialized_||!c||busy_)return status_;
 status_=Status::NotReady;check_=Check::Allocation;expected_={};original_image_.clear();busy_=true;struct Clear{bool&b;~Clear(){b=false;}}clear{busy_};
 try{
  if(!story_native_event_arena::owns(arena_,mem::runtime_alloc_log(),mem::runtime_top()))return status_;
  const auto scene_idle=[&](const Boundary&b){
   if(!story_scenes::active())return true;
   return request_.declared&&b.owned_scene&&b.owned_scene->declaration()==request_.declared&&
    b.owned_scene->definition_hash()==request_.declared->definition_hash()&&request_.declared->current();
  };
  check_=Check::Boundary;
  Boundary boundary;if(!sources_.boundary(c,boundary)||!scene_idle(boundary)||!boundary.exclusive_idle||!boundary.guest_thread||!span(boundary.stack_begin,boundary.stack_end-boundary.stack_begin)||boundary.stack_end<=boundary.stack_begin)return status_;
  if(request_.declared&&!request_.declared->current())return status_;
  const uint32_t sp=c->r[1];if((sp&15)||sp<0x40||sp-0x40<boundary.stack_begin||sp>boundary.stack_end||overlap(sp-0x40,0x40,arena_.allocation.address,arena_.allocation.bytes))return status_;
  std::array<uint8_t,0x40>frame{};if(!read(sp-0x40,frame))return status_;
  check_=Check::Search;
  const Cpu saved=*c;bool frame_restored=false;std::array<uint32_t,2>addresses{};
  auto restore=[&](){*c=saved;try{frame_restored=sources_.restore_frame(sp-0x40,frame);}catch(...){frame_restored=false;}};
  try{for(unsigned n=0;n<2;++n)addresses[n]=story_actors::query_resident_event_address(c,request_.mod,request_.residents[n]);}catch(...){restore();return status_=Status::Quarantined;}restore();if(!frame_restored)return status_=Status::Quarantined;
  if(const char*probe=std::getenv("WWHD_TEST_NATIVE_WAIT");probe&&std::strcmp(probe,"1")==0){
   static thread_local std::array<uint32_t,2>last{~0u,~0u};static thread_local unsigned reports=0;
   if(addresses!=last&&reports<16){LOG("[native_wait] resident query addresses=%08X/%08X",addresses[0],addresses[1]);last=addresses;++reports;}
  }
  Boundary after_query;if(!sources_.boundary(c,after_query)||after_query!=boundary)return status_;
  check_=Check::Actors;
  std::array<story_actors::ResidentEventIdentity,2>actors;
  for(unsigned n=0;n<2;++n){
   std::array<uint8_t,4>pid{};if((addresses[n]&3)||addresses[n]<mem::kMem2Start||uint64_t(addresses[n])+0x920>mem::kMem2End||!read(addresses[n]+4,pid)||!story_actors::resident_event_identity(request_.mod,request_.residents[n],addresses[n],word(pid),actors[n]))return status_;
  }
  const bool turn=request_.program&&request_.program->kind()==story_native_event_program::Kind::TurnCamera;
  const auto&instance=turn?arena_.turn:arena_.wait;
  Expected e;e.program=request_.program;e.identity.manager=Play+0x52C4;e.identity.image=instance.image.address;e.identity.bytes=instance.image.bytes;e.identity.generation=1; // provisional readiness ONLY; never publish/restore this identity
  e.identity.program=uint64_t(word(definition_hash_))<<32|word(definition_hash_,4);if(!e.identity.program)return status_;
  e.identity.stage=actors[0].stage_pid;e.identity.room=actors[0].room_pid;e.room_number=definitions_[0].room;e.frames=request_.native_wait_timer;
  for(unsigned n=0;n<2;++n){if(actors[n].token!=definitions_[n].token||actors[n].fingerprint!=definitions_[n].fingerprint||actors[n].subtype!=1||actors[n].stage_pid!=e.identity.stage||actors[n].room_pid!=e.identity.room||actors[n].room!=e.room_number)return status_;
   auto&a=e.actors[n];a.address=actors[n].actor;a.pid=actors[n].pid;a.token=actors[n].token;a.profile=0x175;a.subtype=1;a.fingerprint=actors[n].fingerprint;if(!read(a.address+0x3E0,a.cut))return status_;
   if(overlap(boundary.stack_begin,boundary.stack_end-boundary.stack_begin,a.address,0x920))return status_;
  }
  if(e.actors[0].address==e.actors[1].address||e.actors[0].pid==e.actors[1].pid)return status_;
  check_=Check::Stage;
  std::array<uint8_t,4>scalar{};if(!read(Initialized,scalar)||!word(scalar)||!read(Stage,scalar)||word(scalar)!=e.identity.stage||!read(Rooms+e.room_number*0x22C,scalar)||word(scalar)!=e.identity.room||!read(Play+0x5134,e.stage_name))return status_;
  std::array<uint8_t,8>stage{};std::copy(definitions_[0].stage.begin(),definitions_[0].stage.end(),stage.begin());if(e.stage_name!=stage)return status_;
  check_=Check::Link;
  if(!read(Play+0x5B34,scalar))return status_;const uint32_t link=word(scalar);std::array<uint8_t,0x328>link_bytes{};if(link<mem::kMem2Start||uint64_t(link)+link_bytes.size()>mem::kMem2End||!read(link,link_bytes)||half(link_bytes,14)!=0xA8||!word(link_bytes,4)||word(link_bytes,4)==0xFFFFFFFFu||link_bytes[0x326]!=e.room_number)return status_;
  check_=Check::Control;
  // Retail talkEnd/endProc leave the last partner PIDs cached. Mode0 ignores
  // them; our genuine compulsory order will replace them with null partners.
  // These bytes confer no actor ownership or native invocation authority.
  std::array<uint8_t,0xF4>control{};if(!read(Play+0x51D0,control)||control[0xC0]||control[0xC2]||half(control,0xD8)!=0xFFFF)return status_;
  check_=Check::Manager;
  std::array<uint8_t,0x540>manager{};if(!read(e.identity.manager,manager))return status_;for(unsigned n=0;n<8;++n)e.retail_manager[n]=word(manager,n*4);e.retail_camera_play=word(manager,0x20);e.retail_current_event=word(manager,0x53C);std::copy_n(manager.begin()+0x3C,e.retail_flags.size(),e.retail_flags.begin());if(e.retail_camera_play||e.retail_current_event)return status_;
  for(auto p:e.retail_manager)if(p>=arena_.allocation.address&&uint64_t(p)<uint64_t(arena_.allocation.address)+arena_.allocation.bytes)return status_;
  if(e.retail_current_event>=arena_.allocation.address&&uint64_t(e.retail_current_event)<uint64_t(arena_.allocation.address)+arena_.allocation.bytes)return status_;
  check_=Check::Camera;
  if(!read(Play+0x5AF8,scalar))return status_;const uint32_t process=word(scalar);std::array<uint8_t,0xB28>raw{};if(process<mem::kMem2Start||uint64_t(process)+raw.size()>mem::kMem2End||!read(process,raw))return status_;
  const uint32_t profile=word(raw,0x10),name=half(raw,14);if((name!=0x1DC&&name!=0x1DD)||profile!=(name==0x1DC?Camera0:Camera1)||word(raw,0x228)!=Methods||word(raw,0x248)!=process||!word(raw,4)||word(raw,4)==0xFFFFFFFFu)return status_;
  std::array<uint8_t,0x40>meta{};std::array<uint8_t,20>methods{};if(!read(profile,meta)||half(meta,8)!=name||word(meta,0x10)!=0xB28||word(meta,0x3C)!=Methods||!read(Methods,methods))return status_;
  const std::array<uint32_t,5>functions={GC(0x025021A4),GC(0x02502288),GC(0x024FFA3C),GC(0x0250235C),GC(0x024FFC40)};for(unsigned n=0;n<5;++n)if(word(methods,n*4)!=functions[n])return status_;
  auto&cam=e.camera;cam.address=process+0x248;cam.pid=word(raw,4);auto w=[&](unsigned off){return word(raw,0x248+off);};auto copy=[&](unsigned off,auto&array){std::copy_n(raw.begin()+0x248+off,array.size(),array.begin());};
  cam.flags=w(0x510);cam.timer=w(0x384);cam.current_type=w(0x51C);cam.next_type=w(0x520);cam.event_type=w(0x8B4);cam.style=w(0x514);cam.bg=w(0x68);cam.trim=w(0x600);cam.counter108=w(0x108);cam.counter118=w(0x118);cam.elapsed11C=w(0x11C);cam.type_table=Tables.types;cam.style_table=Tables.styles;cam.param_pointer=w(0x8A8);cam.param_index=w(0x8AC);
  if(!read(Tables.type_count,scalar))return status_;cam.type_count=word(scalar);if(!read(Tables.style_count,scalar))return status_;cam.style_count=word(scalar);
  copy(0x518,cam.type_gate518);copy(0x100,cam.engine100);copy(0x13C,cam.mode_phase13C);copy(0x10,cam.initial_view010);copy(0x100,cam.done100);copy(0x37C,cam.work37C);copy(0xA4,cam.positions0A4);copy(0x3C,cam.view03C);copy(0x3FC,cam.event_data);
  auto reader=[&](uint32_t a,std::span<uint8_t>b){return read(a,b);};if(!valid_camera_transition(cam,cam)||(cam.flags&0x20000000u)||!story_native_camera_policy::verify(Tables,reader,cam,e.identity).valid)return status_;
  check_=Check::Stable;
  Boundary final;if(!sources_.boundary(c,final)||final!=boundary||!scene_idle(final)||!story_native_event_arena::owns(arena_,mem::runtime_alloc_log(),mem::runtime_top()))return status_;
  for(unsigned n=0;n<2;++n){story_actors::ResidentEventIdentity fresh;if(!story_actors::resident_event_identity(request_.mod,request_.residents[n],addresses[n],actors[n].pid,fresh)||fresh.token!=actors[n].token||fresh.fingerprint!=actors[n].fingerprint||fresh.stage_pid!=e.identity.stage||fresh.room_pid!=e.identity.room)return status_;}
  for(unsigned n=0;n<2;++n){std::array<uint8_t,0x6C>fresh_cut{};if(!read(e.actors[n].address+0x3E0,fresh_cut)||fresh_cut!=e.actors[n].cut)return status_;}
  std::array<uint8_t,0xB28>fresh_camera{};std::array<uint8_t,0x540>fresh_manager{};std::array<uint8_t,0xF4>fresh_control{};
  if(!read(process,fresh_camera)||fresh_camera!=raw||!read(e.identity.manager,fresh_manager)||fresh_manager!=manager||!read(Play+0x51D0,fresh_control)||fresh_control!=control)return status_;
  Boundary last;if(!sources_.boundary(c,last)||last!=boundary)return status_;
  if(request_.declared&&!request_.declared->current())return status_;
  expected_=e;if(e.program){const auto bytes=e.program->pristine();original_image_.assign(bytes.begin(),bytes.end());}else original_image_=story_native_event_image::make_wait_fixture(e.frames);check_=Check::Ready;return status_=Status::Ready;
 }catch(...){return status_=Status::Quarantined;}
}
}
