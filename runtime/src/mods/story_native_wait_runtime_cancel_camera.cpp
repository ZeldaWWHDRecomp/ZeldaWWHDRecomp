// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_runtime_cancel_camera.h"
#include "story_native_camera_capture.h"
#include "story_native_setinfo_observation.h"
#include "guest_mods.h"
#include "guest_addr.h"
#include "runtime.h"
#include <memory>
namespace story_native_wait_runtime_cancel_camera {namespace {
namespace D=story_native_event_depth;namespace M=story_native_event_memory;namespace B=story_native_camera_cancel_barrier;
const story_native_camera_capture::Layout CameraLayout{
 GD(0x1046F0B0),GD(0x101D55F0),GD(0x101D5634),GD(0x101D5538),
 {GC(0x025021A4),GC(0x02502288),GC(0x024FFA3C),GC(0x0250235C),GC(0x024FFC40)},
 {GD(0x10049348),GD(0x10049350),GD(0x10044878),GD(0x1004487C)}};
struct Context {
 story_native_wait_session::Session*session;D::Tracker*depth;B::O::Expected expected;
 story_native_event_arena::Layout arena;story_native_stage_owner_observer::Snapshot stage;
 story_native_event_game_thread::Ticket ticket;Cpu*cpu;story_native_wait_runtime_backend::Bundle backend;Callbacks callbacks;
 D::FrameWitness witness;std::thread::id thread;
 bool owned_thread(const Cpu*c)const{return c&&c==cpu&&thread==std::this_thread::get_id()&&session->fence_held()&&threads::current()==c&&story_native_event_game_thread::verify(c,ticket)&&witness.valid()&&witness.current(*depth);}
 bool read(uint32_t at,std::span<uint8_t>out)const{return owned_thread(cpu)&&out.size()&&uint64_t(at)+out.size()<=0x100000000ull&&backend.sources.read(at,out);}
 bool frame(Cpu*c,B::Frame&out)const{
  out={};try{
   auto pin=story_native_stage_owner_observer::tracker().try_scan();
   if(!pin.current()||!owned_thread(c)||!callbacks.retained(expected)||!guestmods::native_camera_run_original_current(c,*depth)||!story_native_event_arena::owns(arena,mem::runtime_alloc_log(),mem::runtime_top())||!backend.sources.allocation(expected.identity,backend.config.memory))return false;
   const auto baseline=session->baseline();const auto journal=session->journal();if(!baseline||*baseline!=expected||!journal||(journal->phase!=story_native_wait_journal::Phase::Published&&journal->phase!=story_native_wait_journal::Phase::Running))return false;
   const auto receipt=story_native_stage_owner_observer::snapshot(pin);story_native_setinfo_candidate::Candidate candidate;
   const auto wanted=backend.stage_binding?backend.stage_binding->current(pin):std::optional(stage);
   if((backend.stage_binding&&!backend.stage_binding->matches_original(stage))||!wanted||!receipt||receipt->control!=wanted->control||receipt->receipt!=wanted->receipt||pin.receipt()!=wanted->receipt||!story_native_setinfo_observation::inspect_live(wanted->control,wanted->receipt.owner,candidate)||candidate!=wanted->receipt.owner)return false;
   M::Scope scope;story_native_event_collect::Clock clock;
   if(!backend.sources.session(scope)||!backend.active_clock(clock)||scope.identity!=expected.identity||!scope.exclusive||!scope.bindings_verified||scope.fenced||scope.foreign||scope.fault||scope.serial!=clock.serial||scope.logic_step!=clock.logic_step||clock.identity!=expected.identity)return false;
   auto&b=out.boundary;b.expected=expected;b.parent=witness.root_call();b.depth=depth->snapshot();b.logic_step=clock.logic_step;b.elapsed=clock.elapsed_ticks;b.retained=true;b.fenced=session->fence_held();b.stage=pin.current();b.exclusive=scope.exclusive;
   if(b.depth.serial!=scope.serial||b.depth.camera_depth!=1||b.depth.npc_depth||b.depth.manager_depth||b.depth.callback_depth||b.depth.foreign_depth||b.depth.fault||!b.depth.coverage||b.depth.state!=D::State::Active||!witness.current(*depth))return false;
   out.outer_serial=witness.root_serial();return out.outer_serial&&out.outer_serial==witness.current_serial();
  }catch(...){out={};return false;}
 }
};
}
std::optional<B::Sources>make(story_native_wait_session::Session&session,D::Tracker&depth,B::O::Expected expected,story_native_event_arena::Layout arena,story_native_stage_owner_observer::Snapshot stage,story_native_event_game_thread::Ticket ticket,Cpu*cpu,story_native_wait_runtime_backend::Bundle backend,Callbacks callbacks){try{
 if(!cpu||!session.fence_held()||!expected.program||expected.program->kind()!=story_native_event_program::Kind::TurnCamera||expected.identity.bytes!=0xE60||!ticket.epoch||!stage.receipt.generation||!backend.active_clock||!backend.sources.read||!backend.sources.actor||!backend.sources.session||!backend.sources.allocation||!callbacks.retained||!callbacks.quarantine||!story_native_event_arena::valid(arena)||expected.identity.image!=arena.turn.image.address||backend.config.memory.stamp!=arena.turn.stamp.address)return {};
 auto witness=depth.current_owned_frame();if(!witness||!witness->current(depth)||witness->expected()!=expected||!guestmods::native_camera_run_original_current(cpu,depth))return {};
 auto s=std::make_shared<Context>(Context{&session,&depth,std::move(expected),arena,stage,ticket,cpu,std::move(backend),std::move(callbacks),*witness,std::this_thread::get_id()});B::Frame first;if(!s->frame(cpu,first))return {};
 B::Sources out;out.frame=[s](Cpu*c,auto&f){return s->frame(c,f);};out.owned_thread=[s](const Cpu*c){return s->owned_thread(c);};out.read=[s](auto at,auto b){return s->read(at,b);};out.resident=[s](const auto&e,unsigned n){return n<2&&e==s->expected&&s->owned_thread(s->cpu)&&s->callbacks.retained(e)&&s->backend.sources.actor(e.actors[n],e.identity);};
 out.camera=[s](auto&c){return story_native_camera_capture::capture([s](auto at,auto b){return s->read(at,b);},CameraLayout,s->expected,c);};out.verify_camera=[s](const auto&e,const auto&c){return e==s->expected&&story_native_camera_policy::verify(CameraLayout.tables,[s](auto at,auto b){return s->read(at,b);},c,e.identity).valid;};out.original_route=[s](Cpu*c,uint32_t fn){return fn==GC(0x024FE3E8)&&s->owned_thread(c)&&guestmods::native_camera_run_original_current(c,*s->depth);};out.quarantine=s->callbacks.quarantine;return out;
 }catch(...){return {};}}
}
