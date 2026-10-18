// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_runtime_terminal.h"
#include "story_native_event_image.h"
#include "story_native_camera_capture.h"
#include "story_native_people_pose_capture.h"
#include "guest_mods.h"
#include "guest_addr.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
namespace story_native_wait_runtime_terminal {namespace {
namespace D=story_native_event_depth;namespace M=story_native_event_memory;
namespace T=story_native_manager_terminal;namespace C=story_native_camera_return;
const uint32_t Play=GD(0x1046F0B0);
const story_native_camera_capture::Layout CameraLayout{
 GD(0x1046F0B0),GD(0x101D55F0),GD(0x101D5634),GD(0x101D5538),
 {GC(0x025021A4),GC(0x02502288),GC(0x024FFA3C),GC(0x0250235C),GC(0x024FFC40)},
 {GD(0x10049348),GD(0x10049350),GD(0x10044878),GD(0x1004487C)}};
uint32_t word(std::span<const uint8_t>b,unsigned p){return uint32_t(b[p])<<24|uint32_t(b[p+1])<<16|uint32_t(b[p+2])<<8|b[p+3];}
uint16_t half(std::span<const uint8_t>b,unsigned p){return uint16_t(b[p])<<8|b[p+1];}
bool span(uint32_t at,size_t bytes){return bytes&&uint64_t(at)+bytes<=0x100000000ULL&&
 ((at>=mem::kMem2Start&&uint64_t(at)+bytes<=mem::kMem2End)||
  (at>=mem::kRuntimeStart&&uint64_t(at)+bytes<=mem::runtime_top()&&uint64_t(at)+bytes<=mem::kHostStart));}
struct Context {
 story_native_wait_session::Session*session;D::Tracker*depth;Expected expected;
 story_native_event_arena::Layout arena;story_native_stage_owner_observer::Snapshot stage;
 story_native_event_game_thread::Ticket ticket;Cpu*cpu;
 story_native_event_backend::Config config;story_native_event_backend::Sources sources;Callbacks callbacks;
 T::Identity identity;story_native_people_pose_record::Poses baseline;
 std::shared_ptr<story_native_wait_runtime_backend::StageBinding>stage_binding{};
 void quarantine()const noexcept{try{callbacks.quarantine();}catch(...) {}}
 bool read(uint32_t at,std::span<uint8_t>b)const{return span(at,b.size())&&sources.read(at,b);}
 bool scalar(uint32_t at,uint32_t&out)const{std::array<uint8_t,4>b{};if((at&3)||!read(at,b))return false;out=word(b,0);return true;}
 bool thread(const Cpu*c)const{
  uint32_t bottom=0,top=0,guest_bottom=0,guest_top=0;
  return c&&c==cpu&&session->fence_held()&&threads::current()==c&&
   story_native_event_game_thread::verify(c,ticket)&&
   threads::current_stack_bounds(c,bottom,top)&&bottom<top&&span(bottom,uint64_t(top)-bottom)&&
   span(ticket.guest,0x39C)&&scalar(ticket.guest+0x394,guest_top)&&scalar(ticket.guest+0x398,guest_bottom)&&
   // Read-only native callbacks may carry an audited 8-byte stack boundary;
   // the dedicated EndProc/native mutation capabilities separately require 16.
   top==guest_top&&bottom==guest_bottom&&!(c->r[1]&7)&&uint64_t(bottom)+0x40<=c->r[1]&&c->r[1]<=top;
 }
 bool camera_scope(const D::Snapshot&d)const{
  if(d.idle())return true;
  if(d.state!=D::State::Active||d.camera_depth!=1||d.npc_depth||d.manager_depth||d.callback_depth||d.foreign_depth||d.fault)return false;
  const auto owned=depth->current_owned_call();
  return owned&&owned->entry==D::Entry::CameraExecute&&owned->identity==expected.identity&&
   owned->function==D::entry_address(D::Entry::CameraExecute)&&
   owned->subject==expected.camera.address-0x248&&owned->receiver==owned->subject&&
   owned->pid==expected.camera.pid&&!owned->token;
 }
 bool guard(bool camera,M::Scope&out)const{
  out={};if(!thread(cpu))return false;
  // A physically parked guest may still own Stage's mutex. Never block
  // under NativeQ: acquire this real pin first or refuse before backend queries.
  auto scan=story_native_stage_owner_observer::tracker().try_scan();
  if(!scan.valid()||!callbacks.retained(expected)||!guestmods::routes_owned())return false;
  const auto stored=session->baseline();const auto poses=session->baseline_poses();
  if(!stored||*stored!=expected||!poses)return false;
  for(unsigned n=0;n<2;++n)if(poses->at(n).identity!=baseline[n].identity||poses->at(n).status!=baseline[n].status)return false;
  if(!story_native_event_arena::owns(arena,mem::runtime_alloc_log(),mem::runtime_top())||
     !sources.allocation(expected.identity,config.memory)||!sources.session(out))return false;
  const auto observed=depth->snapshot();
  if(!observed.coverage||observed.fault||out.identity!=expected.identity||!out.exclusive||!out.bindings_verified||
     out.foreign||out.fault||out.serial!=observed.serial||out.fenced!=(observed.state==D::State::Draining)||
     out.npc_depth!=observed.npc_depth||out.camera_depth!=observed.camera_depth||out.manager_depth!=observed.manager_depth||
     out.callback_depth!=observed.callback_depth||out.foreign_depth!=observed.foreign_depth||
     (camera?!camera_scope(observed):(!observed.idle()||observed.state!=D::State::Draining)))return false;
  const auto current=story_native_stage_owner_observer::snapshot(scan);story_native_setinfo_candidate::Candidate candidate;
  const auto wanted=stage_binding?stage_binding->current(scan):std::optional(stage);
  return (!stage_binding||stage_binding->matches_original(stage))&&wanted&&scan.valid()&&scan.receipt()==wanted->receipt&&current&&current->control==wanted->control&&
   current->receipt==wanted->receipt&&story_native_setinfo_observation::inspect_live(wanted->control,wanted->receipt.owner,candidate)&&
   candidate==wanted->receipt.owner&&scan.current();
 }
 bool actor(unsigned n,T::Actor&out)const{
  out={};if(n>=2)return false;
  story_native_people_pose_plan::Snapshot pose;
  const story_native_people_pose_capture::Layout layout{GD(0x101C4CD0),GD(0x100209C4)};
  const auto raw=[this](uint32_t at,std::span<uint8_t>b){return read(at,b);};
  auto mode=story_native_people_pose_capture::Mode::Owned;
  if(expected.program&&expected.program->kind()==story_native_event_program::Kind::TurnCamera){uint32_t state=0;if(!scalar(expected.identity.image+0xE4,state))return false;if(state==0)mode=story_native_people_pose_capture::Mode::Exhausted;}
  if(!story_native_people_pose_capture::capture(raw,sources.actor,layout,expected,n,pose,mode)||pose.identity!=baseline[n].identity)return false;
  const auto&a=expected.actors[n];out={a.address,a.pid,a.token,a.profile,a.subtype,pose.status,a.fingerprint};return true;
 }
 struct Manager {
  std::array<uint8_t,0x540> bytes{};std::vector<uint8_t>image;
  std::array<uint8_t,0xF4> control{};
  bool operator==(const Manager&)const=default;
 };
 bool manager(Manager&out)const{
  out.image.resize(expected.identity.bytes);
  if(!read(expected.identity.manager,out.bytes)||!read(expected.identity.image,out.image)||!read(Play+0x51D0,out.control))return false;
  const auto base=expected.identity.image;
  std::array<uint32_t,8>table={base,base+0x40,base+0xF0,base+0x230,base+0x410,0,base+0x510,0};
  if(expected.program){const auto offsets=expected.program->pool_offsets();for(unsigned n=0;n<8;++n)table[n]=offsets[n]?base+offsets[n]:n?0:base;}
  for(unsigned n=0;n<table.size();++n)if(word(out.bytes,n*4)!=table[n])return false;
  if(word(out.image,0)!=0x40||word(out.image,4)!=1)return false; // exact event index0
  const auto canonical=expected.program?std::vector<uint8_t>(expected.program->pristine().begin(),expected.program->pristine().end()):story_native_event_image::make_wait_fixture(expected.frames);
  if(out.image.size()!=canonical.size())return false;
  for(unsigned p=0;p<canonical.size();++p){bool mutable_byte=p>=0xE4&&p<0xE8;for(unsigned n=0;n<4;++n)mutable_byte|=p>=0xF0+n*0x50+0x38&&p<0xF0+n*0x50+0x48;if(!mutable_byte&&out.image[p]!=canonical[p])return false;}
  const uint32_t mask=expected.program?expected.program->completion_mask():0x3F;
  for(unsigned p=0;p<0x500;++p)if(out.bytes[0x3C+p]!=(p<4?uint8_t(mask>>(24-p*8)):0))return false;
  const auto state=word(out.image,0xE4),current=word(out.bytes,0x53C),camera_play=word(out.bytes,0x20);
  // mainProc clears CurrentEvent then selects PLAY only. At CLOSE/NONE the
  // retained first event is selected by exact table/index, not CurrentEvent.
  if((state==0||state==4)?current!=0:(current!=0&&current!=base+0x40))return false;
  if(camera_play!=((state==2||state==4)?1u:0u))return false;
  const auto&c=out.control;
  return c[0xC0]==0&&c[0xC2]==3&&word(c,0xC4)==~0u&&word(c,0xC8)==~0u&&word(c,0xCC)==~0u&&word(c,0xD0)==~0u&&
   half(c,0xD6)==0&&half(c,0xD8)==0xFFFF&&c[0xDE]==0xFF&&c[0xDF]==0xFF&&half(c,0xE8)==0;
 }
 bool probe(T::Observation&out)const{
  out={};try{M::Scope before,after;Manager first,last;
   if(!guard(false,before)||!manager(first))return false;
   T::Observation value;value.lease=expected.identity;value.event=expected.identity.image+0x40;value.index=0;
   value.event_state=word(first.image,0xE4);value.camera_play=word(first.bytes,0x20);
   for(unsigned n=0;n<2;++n)if(!actor(n,value.actors[n]))return false;
   if(!manager(last)||first!=last||!guard(false,after)||before.serial!=after.serial||before.logic_step!=after.logic_step)return false;
   value.retained_allocation=value.entry_fence=value.idle_depths=value.manager_table_owned=true;
   out=value;return true;
  }catch(...){return false;}
 }
 bool camera(C::Capture&out)const{
  out={};try{M::Scope before,after;Manager first,last;story_native_event_observation::Camera camera;
   const auto completion=callbacks.completion();
   if(!completion.valid()||completion.lease()!=expected.identity||!guard(true,before)||!callbacks.camera_route()||!manager(first)||
      !story_native_camera_capture::capture([this](uint32_t at,std::span<uint8_t>b){return read(at,b);},CameraLayout,expected,camera)||
      !story_native_camera_policy::verify(CameraLayout.tables,[this](uint32_t at,std::span<uint8_t>b){return read(at,b);},camera,expected.identity).valid||
      !manager(last)||first!=last||!guard(true,after)||before.serial!=after.serial||before.logic_step!=after.logic_step||
      callbacks.completion()!=completion)return false;
   out.identity=expected.identity;out.camera=std::move(camera);out.event_state=word(first.image,0xE4);
   out.camera_play=word(first.bytes,0x20);out.terminal_serial=completion.serial();out.retained=out.exclusive=true;return true;
  }catch(...){return false;}
 }
};
}
std::optional<Bundle> make(story_native_wait_session::Session&session,D::Tracker&depth,Expected expected,
 story_native_event_arena::Layout arena,story_native_stage_owner_observer::Snapshot stage,
 story_native_event_game_thread::Ticket ticket,Cpu*cpu,story_native_event_backend::Config config,
 story_native_event_backend::Sources sources,Callbacks callbacks,
 std::shared_ptr<story_native_wait_runtime_backend::StageBinding>stage_binding){
 try{
  if(!session.fence_held()||!cpu||!ticket.epoch||!stage.receipt.generation||
     !sources.read||!sources.session||!sources.allocation||!sources.actor||!callbacks.retained||
     !callbacks.end_proc||!callbacks.completion||!callbacks.camera_route||!callbacks.quarantine)return {};
  const bool turn=expected.program&&expected.program->kind()==story_native_event_program::Kind::TurnCamera;
  const auto&instance=turn?arena.turn:arena.wait;
  const auto stored=session.baseline();const auto poses=session.baseline_poses();
  if(!stored||*stored!=expected||!poses||!story_native_event_arena::valid(arena)||
     expected.identity.image!=instance.image.address||expected.identity.bytes!=instance.image.bytes||
     config.memory.stamp!=instance.stamp.address||config.collector.stamp!=config.memory.stamp)return {};
  T::Identity identity;identity.lease=expected.identity;identity.event=expected.identity.image+0x40;identity.index=0;
  for(unsigned n=0;n<2;++n){const auto&a=expected.actors[n];const auto&s=poses->at(n);
   if(s.status&T::kAllOffMask)return {};
   identity.actors[n]={a.address,a.pid,a.token,a.profile,a.subtype,s.status,a.fingerprint};
  }
  auto state=std::make_shared<Context>(Context{&session,&depth,std::move(expected),arena,std::move(stage),ticket,cpu,config,std::move(sources),std::move(callbacks),identity,*poses,std::move(stage_binding)});
  M::Scope verified;if(!state->guard(true,verified))return {};
  Bundle result;result.identity=identity;
  result.probe=[state](T::Observation&out){const bool ok=state->probe(out);if(!ok)state->quarantine();return ok;};
  result.actor=[state](uint32_t address,T::Actor&out){
   out={};try{M::Scope before,after;if(!state->guard(false,before)){state->quarantine();return false;}
    for(unsigned n=0;n<2;++n)if(state->expected.actors[n].address==address){
     if(!state->actor(n,out)||!state->guard(false,after)||before.serial!=after.serial||before.logic_step!=after.logic_step){out={};state->quarantine();return false;}return true;
    }
   }catch(...){state->quarantine();}return false;
  };
  result.invoke=[state](uint32_t manager,int32_t index,uint32_t force){
   try{T::Observation before;
    if(manager!=state->expected.identity.manager||index!=0||force!=0||!state->probe(before)||before.event_state!=T::kClose||
       !state->callbacks.end_proc(state->cpu,GC(0x0254465C),manager,index,force))throw std::runtime_error("native WAIT terminal capability refused");
    M::Scope after;if(!state->guard(false,after))throw std::runtime_error("native WAIT terminal boundary changed");
   }catch(...){state->quarantine();throw;}
  };
  result.camera.capture=[state](const Cpu*c,C::Capture&out){out={};const bool ok=c==state->cpu&&state->camera(out);if(!ok)state->quarantine();return ok;};
  result.camera.verify_camera=[state](const auto&camera,const auto&identity){
   try{M::Scope scope;const bool ok=identity==state->expected.identity&&camera.address==state->expected.camera.address&&camera.pid==state->expected.camera.pid&&
    state->guard(true,scope)&&story_native_camera_policy::verify(CameraLayout.tables,[state](uint32_t at,std::span<uint8_t>b){return state->read(at,b);},camera,identity).valid;
    if(!ok)state->quarantine();return ok;
   }catch(...){state->quarantine();return false;}
  };
  result.camera.owned_thread=[state](const Cpu*c){try{const bool ok=state->thread(c);if(!ok)state->quarantine();return ok;}catch(...){state->quarantine();return false;}};
  result.camera.route=[state]{try{const bool ok=state->session->fence_held()&&guestmods::routes_owned()&&state->callbacks.camera_route();if(!ok)state->quarantine();return ok;}catch(...){state->quarantine();return false;}};
  return result;
 }catch(...){return {};}
}
}
