// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_runtime_manager_cancel.h"
#include "guest_mods.h"
#include "native_quiescence.h"
#include "runtime.h"
#include "guest_addr.h"
#include <algorithm>
#include <stdexcept>
namespace story_native_wait_runtime_manager_cancel {namespace {
namespace G=story_native_manager_cancel_dispatch;
bool range(M::I::Range r){return r.address&&r.bytes&&uint64_t(r.address)+r.bytes<=0x100000000ull;}
bool overlap(M::I::Range a,M::I::Range b){return uint64_t(a.address)<uint64_t(b.address)+b.bytes&&uint64_t(b.address)<uint64_t(a.address)+a.bytes;}
uint32_t word(std::span<const uint8_t>b){return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];}
}
struct Execution::Impl {
 story_native_wait_session::Session*session;M::D::Tracker*depth;M::O::Expected expected;story_native_event_arena::Layout arena;
 story_native_event_game_thread::Ticket ticket;Cpu*cpu;story_native_wait_runtime_backend::Bundle backend;Callbacks callbacks;
 story_native_camera_cancel_barrier::Sources run_source;M::D::FrameWitness witness;story_native_people_pose_record::Poses poses;
 std::thread::id thread;std::atomic<bool>busy{false};std::unique_ptr<M::Cancel>helper;std::optional<G::Guard>guard;
 static thread_local Impl*invoking;
 bool owned(const Cpu*c)const{return thread==std::this_thread::get_id()&&c==cpu&&session->fence_held()&&run_source.owned_thread(cpu)&&witness.valid()&&witness.current(*depth);}
 bool frame(Cpu*c,M::Boundary&out)const{
  out={};if(!owned(c)||!guard||!guard->current()||!callbacks.retained(expected))return false;
  story_native_event_memory::Scope s;story_native_event_collect::Clock clock;
  if(!backend.sources.session(s)||!backend.active_clock(clock)||s.identity!=expected.identity||!s.exclusive||!s.bindings_verified||s.fenced||s.foreign||s.fault||s.serial!=clock.serial||s.logic_step!=clock.logic_step||clock.identity!=expected.identity)return false;
  out={expected,witness.root_call(),depth->snapshot(),clock.logic_step,clock.elapsed_ticks,true,session->fence_held(),true,s.exclusive};
  return out.depth.serial==s.serial&&out.depth.coverage&&!out.depth.fault&&out.depth.state==M::D::State::Active&&out.depth.camera_depth==1&&!out.depth.npc_depth&&!out.depth.manager_depth&&!out.depth.callback_depth&&!out.depth.foreign_depth&&witness.current(*depth)&&guard->current();
 }
 bool stack(Cpu*c,M::I::Config&out)const{
  out={};uint32_t bottom=0,top=0;if(!owned(c)||!threads::current_stack_bounds(c,bottom,top)||bottom>=top||(c->r[1]&7)||uint64_t(bottom)+0x40>c->r[1]||c->r[1]>top)return false;
  std::array<uint8_t,4>b{},t{};if(!backend.sources.read(ticket.guest+0x394,t)||!backend.sources.read(ticket.guest+0x398,b)||word(b)!=bottom||word(t)!=top)return false;
  out.stack={bottom,top-bottom};out.protected_ranges={{arena.allocation.address,arena.allocation.bytes},{expected.identity.manager-0xF4,0x634},{expected.camera.address-0x248,0xB28}};
  for(unsigned n=0;n<2;++n){out.protected_ranges.push_back({expected.actors[n].address,0x920});out.protected_ranges.push_back({poses[n].identity.morf,0xC8});out.protected_ranges.push_back({poses[n].identity.model,0x170});}
  const M::I::Range frame{c->r[1]-0x40,0x40};for(const auto&r:out.protected_ranges)if(!range(r)||overlap(frame,r))return false;return true;
 }
 bool scope(const Cpu*c,uint32_t fn,uint32_t manager,int32_t index,uint32_t force)const noexcept{
  try{return invoking==this&&owned(c)&&guard&&guard->current()&&helper&&helper->native_cancel_scope()&&fn==GC(0x0254465C)&&manager==expected.identity.manager&&!index&&force==1;}catch(...){return false;}
 }
 uint32_t call(Cpu*c,uint32_t fn,std::initializer_list<uint32_t>args){
  if(args.size()!=3||fn!=GC(0x0254465C)||*args.begin()!=expected.identity.manager||args.begin()[1]||args.begin()[2]!=1||invoking||!helper||!helper->native_cancel_scope())throw std::runtime_error("native cancel ABI");
  M::Boundary before,after;M::I::Config config;if(!frame(c,before)||!stack(c,config))throw std::runtime_error("native cancel boundary");
  const Cpu saved=*c;const uint32_t at=c->r[1]-0x40;std::array<uint8_t,0x40>bytes{},check{};
  if(!backend.sources.read(at,bytes))throw std::runtime_error("native cancel outgoing frame");
  bool called=false,restored=false;uint32_t result=0;invoking=this;
  try{result=callbacks.call(c,fn,{expected.identity.manager,0,1});called=true;}catch(...){}
  invoking=nullptr;
  // Restore the caller registers before callbacks validate and restore its
  // outgoing frame, including when the native entry threw.
  *c=saved;
  try{restored=callbacks.write(at,bytes)&&backend.sources.read(at,check)&&check==bytes;}catch(...){restored=false;}
  const bool live=guard&&guard->current();
  if(!called||!restored||!live||!helper->native_cancel_scope()||!frame(c,after)||after.logic_step!=before.logic_step||after.elapsed!=before.elapsed)throw std::runtime_error("native cancel restore");
  return result;
 }
 std::optional<G::Guard>acquire(Cpu*c,std::shared_ptr<const threads::NativeQuiescence>&park,std::string&why){
  if(!owned(c)||!guestmods::native_camera_run_original_pending(c,*depth)){why="not pending native Run";return {};}
  auto q=threads::try_native_quiescence(c,ticket,250,why);if(!q)return {};
  park=std::make_shared<threads::NativeQuiescence>(std::move(*q));auto result=guestmods::acquire_native_manager_cancel_dispatch(*depth,c,park);if(!result)why="forced-close dispatch/census";return result;
 }
};
thread_local Execution::Impl*Execution::Impl::invoking=nullptr;
Execution::Execution(std::unique_ptr<Impl>i):impl_(std::move(i)){}
Execution::~Execution()=default;
std::unique_ptr<Execution>Execution::prepare(story_native_wait_session::Session&session,M::D::Tracker&depth,M::O::Expected expected,story_native_event_arena::Layout arena,story_native_stage_owner_observer::Snapshot stage,story_native_event_game_thread::Ticket ticket,Cpu*cpu,story_native_wait_runtime_backend::Bundle backend,Callbacks callbacks){try{
 if(!callbacks.retained||!callbacks.write||!callbacks.call||!callbacks.quarantine||!guestmods::native_camera_run_original_pending(cpu,depth))return {};
 auto source=story_native_wait_runtime_cancel_camera::make(session,depth,expected,arena,stage,ticket,cpu,backend,{callbacks.retained,callbacks.quarantine});const auto poses=session.baseline_poses();auto witness=depth.current_owned_frame();if(!source||!poses||!witness||!witness->current(depth)||witness->expected()!=expected)return {};
 auto p=std::make_unique<Impl>();p->session=&session;p->depth=&depth;p->expected=expected;p->arena=arena;p->ticket=ticket;p->cpu=cpu;p->backend=std::move(backend);p->callbacks=std::move(callbacks);p->run_source=std::move(*source);p->witness=*witness;p->poses=*poses;p->thread=std::this_thread::get_id();auto*s=p.get();
 M::Sources sources;sources.boundary=[s](Cpu*c,auto&b){return s->frame(c,b);};sources.resident=[s](const auto&e,unsigned n){return e==s->expected&&n<2&&s->owned(s->cpu)&&s->backend.sources.actor(e.actors[n],e.identity);};sources.routes=[s]{return s->guard&&s->guard->current();};sources.arena=[s](const auto&e,auto&r){if(e!=s->expected||!s->owned(s->cpu)||!s->callbacks.retained(e))return false;r={s->arena.allocation.address,s->arena.allocation.bytes};return true;};sources.stack=[s](Cpu*c,auto&out){return s->stack(c,out);};sources.read=s->run_source.read;sources.write=s->callbacks.write;sources.call=[s](Cpu*c,auto fn,auto args){return s->call(c,fn,args);};sources.quarantine=s->callbacks.quarantine;
 p->helper=std::make_unique<M::Cancel>(expected,*poses,std::move(sources));return std::unique_ptr<Execution>(new Execution(std::move(p)));
 }catch(...){return {};}}
Probe Execution::probe(Cpu*c){Probe out;auto&s=*impl_;if(!s.owned(c)||s.busy.exchange(true)){out.reason="not current exclusive caller";return out;}struct Done{std::atomic<bool>&busy;~Done(){busy=false;}}done{s.busy};try{std::shared_ptr<const threads::NativeQuiescence>park;auto guard=s.acquire(c,park,out.reason);out.quiescent=bool(park)&&park->current(c);out.dispatch=bool(guard);out.current=guard&&guard->current();return out;}catch(...){out.reason="quiescence probe exception";return out;}}
M::Result Execution::run(Cpu*c){auto&s=*impl_;if(!s.owned(c)||s.busy.exchange(true))return M::Result::Refused;struct Done{Impl&s;~Done(){s.guard.reset();s.busy=false;}}done{s};try{std::shared_ptr<const threads::NativeQuiescence>park;std::string why;s.guard=s.acquire(c,park,why);if(!s.guard||!s.guard->current())return M::Result::Refused;const auto result=s.helper->run(c);s.guard.reset();park.reset();return result;}catch(...){return M::Result::Refused;}}
M::Completion Execution::completion()const{const auto&s=*impl_;return s.thread==std::this_thread::get_id()&&!s.busy?s.helper->completion():M::Completion{};}
bool Execution::native_end_proc_scope(const Cpu*c,uint32_t fn,uint32_t manager,int32_t index,uint32_t force)const noexcept{return impl_->scope(c,fn,manager,index,force);}
std::optional<uint32_t>Execution::route_all_off(Cpu*c,const std::function<void()>&next){auto&s=*impl_;if(!s.scope(c,GC(0x0254465C),s.expected.identity.manager,0,1))return {};return s.helper->route_all_off(c->r[3],[&]{next();return c->r[3];});}
}
