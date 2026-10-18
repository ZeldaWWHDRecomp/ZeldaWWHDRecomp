// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_runtime_model_graph.h"
#include "guest_mods.h"
#include "story_native_refresh_stack.h"
#include "story_native_restore_resume.h"
#include "runtime.h"
#include <exception>
namespace story_native_wait_runtime_model_graph {
namespace D=story_native_event_depth;
namespace J=story_native_wait_journal;
struct Binding::State {
 std::shared_ptr<const threads::NativeQuiescence>park;
 std::unique_ptr<story_native_wait_session::Session::Fence>fence;
 std::optional<story_native_stage_owner::Tracker::Scan>stage;
 std::shared_ptr<const story_native_refresh_dispatch::Guard>dispatch;
 std::function<bool()>verify;
 std::optional<D::FrameWitness>pending_camera;
 std::thread::id thread=std::this_thread::get_id();
 const void*owner=nullptr;
 ~State(){if(thread!=std::this_thread::get_id())std::terminate();dispatch.reset();stage.reset();fence.reset();park.reset();}
 bool current()const{return thread==std::this_thread::get_id()&&verify&&verify();}
};
struct Context::Impl {
 story_native_wait_session::Session*session;
 D::Tracker*depth;G::Expected expected;
 story_native_stage_owner_observer::Snapshot stage;
 story_native_event_game_thread::Ticket ticket;Cpu*cpu;
 story_native_wait_runtime_backend::Bundle backend;
 std::weak_ptr<Binding::State>active;
 std::unique_ptr<G::Adapter>graph;
 bool prepared=false,dormant_restore=false,restore_open_pending=false;std::array<G::Capture,2>baseline;
 story_native_model_saved_graph::Lineage lineage;
 std::vector<G::Range>protected_allocations;
 std::thread::id thread=std::this_thread::get_id();
 bool owned()const{return thread==std::this_thread::get_id()&&threads::current()==cpu&&
  story_native_event_game_thread::verify(cpu,ticket);}
 bool stack_disjoint(uint32_t required)const{
  uint32_t low=0,high=0;
  if(!owned()||!threads::current_stack_bounds(cpu,low,high)||low<0x50000000||
   !story_native_refresh_stack::fits(low,high,cpu->r[1],required)||protected_allocations.empty())return false;
  for(const auto&r:protected_allocations)
   if(!r.address||!r.bytes||uint64_t(r.address)+r.bytes>0x100000000ull||
    (uint64_t(low)<uint64_t(r.address)+r.bytes&&uint64_t(r.address)<high))return false;
  return true;
 }
 bool pending(const D::FrameWitness&w)const{
  if(!owned()||!prepared||!stack_disjoint(0x40)||!session->fence_held()||!w.valid()||w.expected()!=expected||!w.current(*depth)||
   !guestmods::native_camera_run_original_pending(cpu,*depth))return false;
  const auto baseline=session->baseline();const auto journal=session->journal();
  if(!baseline||*baseline!=expected||!journal||
   (journal->phase!=J::Phase::Published&&journal->phase!=J::Phase::Running))return false;
  const auto frame=depth->current_owned_frame();const auto now=depth->snapshot();
  return frame&&frame->same_frame(w)&&now.coverage&&!now.fault&&now.state==D::State::Active&&
   now.camera_depth==1&&!now.npc_depth&&!now.manager_depth&&!now.callback_depth&&!now.foreign_depth;
 }
 bool scope(G::Scope&out)const{
  out={};auto pin=active.lock();
  if(!pin||pin->owner!=this||!owned()||!session->fence_held()||!pin->park||
   !pin->park->current(cpu)||!pin->stage||!pin->stage->current()||
   !pin->dispatch||!pin->dispatch->current())return false;
  const auto owner=story_native_stage_owner_observer::snapshot(*pin->stage);
  const auto journal=session->journal();
  const auto wanted=backend.stage_binding?backend.stage_binding->current(*pin->stage):std::optional(stage);
  if((backend.stage_binding&&!backend.stage_binding->matches_original(stage))||!wanted||!owner||owner->control!=wanted->control||owner->receipt!=wanted->receipt||
   pin->stage->receipt()!=wanted->receipt||!journal||
   journal->phase==J::Phase::Unpublished||journal->phase==J::Phase::Quarantined)return false;
  auto baseline=session->baseline();if(!baseline)baseline=session->staged_expected();
  if(!baseline||*baseline!=expected)return false;
  story_native_event_memory::Scope live;
  if(!backend.sources.session||!backend.sources.session(live)||live.identity!=expected.identity||
   !live.exclusive||!live.bindings_verified||live.foreign||live.fault||live.npc_depth||
   (!pin->pending_camera&&live.camera_depth)||live.manager_depth||live.callback_depth||live.foreign_depth)return false;
  const auto now=depth->snapshot();
  if(!now.coverage||
   (pin->pending_camera?(!pending(*pin->pending_camera)||live.camera_depth!=1):!now.idle())||now.serial!=live.serial||
   !pin->park->current(cpu)||!pin->stage->current())return false;
  out.expected=expected;out.fenced=true;out.stage_live=true;out.resources_pinned=true;
  out.preclaim=journal->phase==J::Phase::Preparing;
  out.native_dispatch=pin->dispatch;out.native_quiescence=pin->park;out.native_cpu=cpu;
  return true;
 }
};
Binding::Binding(std::shared_ptr<State>s):state_(std::move(s)){}
Binding::~Binding()=default;
bool Binding::current()const{try{return state_&&state_->current();}catch(...){return false;}}
std::shared_ptr<const threads::NativeQuiescence>Binding::quiescence()const{return current()?state_->park:nullptr;}
Context::Context(std::shared_ptr<Impl>i):impl_(std::move(i)){}
std::optional<Binding>Context::bind(std::string&why){
 why.clear();try{
  if(!impl_||!impl_->owned()||!impl_->active.expired()){why="model owner or nested binding";return {};}
  auto q=threads::try_native_quiescence(impl_->cpu,impl_->ticket,250,why);if(!q)return {};
  return bind(std::make_shared<threads::NativeQuiescence>(std::move(*q)),why);
 }catch(...){why="model parking exception";return {};}
}
std::optional<Binding>Context::bind(std::shared_ptr<const threads::NativeQuiescence>park,std::string&why){
 return bind_impl(std::move(park),{},why);
}
std::optional<Binding>Context::bind_impl(std::shared_ptr<const threads::NativeQuiescence>park,
 std::optional<D::FrameWitness>pending,std::string&why){
 why.clear();auto p=impl_;
 try{
  if(!p||!p->owned()||!p->active.expired()||!park||!park->current(p->cpu)){why="model owner or park";return {};}
  auto pin=std::make_shared<Binding::State>();pin->owner=p.get();pin->pending_camera=std::move(pending);
  pin->park=std::move(park);
  // Session::try_fence is nonblocking. An already-held real entry fence may
  // be borrowed, but current() continues to require it for every operation.
  if(!p->session->fence_held())pin->fence=p->session->try_fence();
  if(!p->session->fence_held()){why="model Session busy";return {};}
  auto scan=story_native_stage_owner_observer::tracker().try_scan();
  if(!scan.current()){why="model Stage busy";return {};}
  pin->stage.emplace(std::move(scan));
  auto dispatch=guestmods::acquire_native_people_refresh_dispatch();
  if(!dispatch){why="model native dispatch";return {};}
  pin->dispatch=std::make_shared<story_native_refresh_dispatch::Guard>(std::move(*dispatch));
  p->active=pin;
  std::weak_ptr<Impl>owner=p;
  pin->verify=[owner]{auto live=owner.lock();G::Scope scope;return live&&live->scope(scope);};
  if(!pin->current()){why="model live binding";return {};}
  return Binding(std::move(pin));
 }catch(...){why="model binding exception";return {};}
}
std::shared_ptr<Context>Context::prepare(story_native_wait_session::Session&s,D::Tracker&d,
 G::Expected expected,story_native_stage_owner_observer::Snapshot stage,
 story_native_event_game_thread::Ticket ticket,Cpu*cpu,
 story_native_wait_runtime_backend::Bundle backend,std::string&why){
 try{
  auto p=std::make_shared<Impl>();p->session=&s;p->depth=&d;p->expected=std::move(expected);
  p->stage=stage;p->ticket=ticket;p->cpu=cpu;p->backend=std::move(backend);
  if(!cpu||!p->backend.sources.read||!p->backend.sources.actor){why="model sources";return {};}
  auto result=std::shared_ptr<Context>(new Context(p));
  G::Sources sources;sources.read=p->backend.sources.read;sources.resident=p->backend.sources.actor;
  std::weak_ptr<Impl>owner=p;
  sources.scope=[owner](G::Scope&out){auto live=owner.lock();return live&&live->scope(out);};
  p->graph=std::make_unique<G::Adapter>(p->expected,std::move(sources));
  auto binding=result->bind(why);if(!binding)return {};
  G::Scope scope;if(!p->scope(scope)||!scope.preclaim||!p->graph->prepare()){
   why="model preclaim graph";return {};
  }
  G::Capture a,b;if(!result->capture(*binding,0,a)||!result->capture(*binding,1,b)){
   why="model baseline recapture";return {};
  }
  // Retain allocation bounds as baseline data for the later stack-footprint
  // proof. A fresh graph capture must still authenticate these resources.
  p->protected_allocations={{p->expected.identity.manager-0xF4,0x634},
   {p->expected.camera.address-0x248,0xB28}};
  const auto arena=s.arena();
  p->protected_allocations.push_back({arena.allocation.address,arena.allocation.bytes});
  for(unsigned n=0;n<2;++n){
   const auto diagnostic=p->graph->diagnose(n);
   if(!diagnostic.parsed||!binding->current()){why="model baseline allocation capture";return {};}
   p->protected_allocations.push_back({p->expected.actors[n].address,0x920});
   for(const auto range:{diagnostic.actor_heap,diagnostic.metadata,diagnostic.payload_heap}){
    if(!range.address||!range.bytes||uint64_t(range.address)+range.bytes>0x100000000ull){
     why="model baseline allocation range";return {};
    }
    p->protected_allocations.push_back(range);
   }
  }
  p->baseline={a,b};p->lineage=story_native_model_saved_graph::Lineage::issue();
  p->prepared=true;return result;
 }catch(...){why="model preparation exception";return {};}
}
std::shared_ptr<Context>Context::rebind_saved(const story_native_restore_resume::Boundary&boundary,
 story_native_wait_session::Session&s,D::Tracker&depth,
 story_native_stage_owner_observer::Snapshot stage,story_native_event_game_thread::Ticket ticket,
 Cpu*cpu,story_native_wait_runtime_backend::Bundle backend,std::string&why)const{
 why.clear();try{
  auto original=impl_;const auto&completion=boundary.completion();const auto&origin=completion.origin();const auto*finished_stage=completion.stage();
  if(!cpu||!boundary.current(cpu)||!finished_stage||
   stage.control!=finished_stage->control||stage.receipt!=finished_stage->receipt||!original||!original->prepared||original->dormant_restore||
   completion.original_model().get()!=this||completion.expected()!=original->expected||
   stage.control!=original->stage.control||stage.receipt.owner!=original->stage.receipt.owner||
   origin.expected()!=original->expected||original->cpu!=cpu||original->session!=&s||
   original->thread!=std::this_thread::get_id()||!original->active.expired()||
   !original->lineage.valid()||!original->graph||!completion.saved_graph().matches(original->expected,original->lineage)||
   !D::Tracker::valid_idle_record(origin.depth_record(),original->expected)||
   ticket==original->ticket||!story_native_event_game_thread::verify(cpu,ticket)||
   !depth.restoring(boundary)||!backend.sources.read||!backend.sources.actor){
   why="model restored host lineage or boundary";return {};
  }
  auto p=std::make_shared<Impl>();p->session=&s;p->depth=&depth;p->expected=original->expected;
  p->stage=stage;p->ticket=ticket;p->cpu=cpu;p->backend=std::move(backend);
  p->baseline=original->baseline;p->lineage=original->lineage;
  p->protected_allocations=original->protected_allocations;p->dormant_restore=true;
  G::Sources sources;sources.read=p->backend.sources.read;sources.resident=p->backend.sources.actor;
  std::weak_ptr<Impl>owner=p;
  sources.scope=[owner](G::Scope&out){auto live=owner.lock();return live&&live->scope(out);};
  p->graph=original->graph->clone_sources(std::move(sources));
  if(!p->graph){why="model original baselines unavailable";return {};}
  auto result=std::shared_ptr<Context>(new Context(p));
  auto binding=result->bind(why);if(!binding)return {};
  const auto snapshot=depth.snapshot();
  if(!boundary.current(cpu)||!depth.restoring(boundary)||!snapshot.idle()||
   snapshot.serial!=origin.scope().serial||!p->stack_disjoint(story_native_refresh_stack::kDownwardBytes)){
   why="model restored tracker or stack";return {};
  }
  p->prepared=true;
  if(!result->matches_live(*binding,completion.saved_graph())||!boundary.current(cpu)||
   !depth.restoring(boundary)||!binding->current()){
   why="model restored graph or exact saved bytes";return {};
  }
  return result;
 }catch(...){why="model restored binding exception";return {};}
}
bool Context::validate_stage_candidate(const story_native_wait_runtime_backend::StageBinding::Candidate&candidate,std::string&why)const{
 why.clear();try{auto original=impl_;if(!original||!original->prepared||original->dormant_restore||!original->active.expired()||
  !candidate.current()||!original->owned()||!original->session->fence_held()||!original->backend.stage_binding||
  candidate.origin_binding()!=original->backend.stage_binding->state_.get()||
  candidate.stage().control!=original->stage.control||candidate.stage().receipt.owner!=original->stage.receipt.owner){why="model Stage candidate lineage";return false;}
  const auto depth=original->depth->snapshot();const auto journal=original->session->journal();
  if(!depth.idle()||depth.state!=D::State::Active||!depth.coverage||depth.fault||!journal||
   (journal->phase!=J::Phase::Published&&journal->phase!=J::Phase::Running)){why="model Stage active boundary";return false;}
  auto copy=std::make_shared<Impl>();copy->session=original->session;copy->depth=original->depth;copy->expected=original->expected;
  copy->stage=candidate.stage();copy->ticket=original->ticket;copy->cpu=original->cpu;copy->backend=candidate.bundle();
  copy->baseline=original->baseline;copy->lineage=original->lineage;copy->protected_allocations=original->protected_allocations;
  copy->prepared=true;copy->dormant_restore=true; // capture-only even internally
  G::Sources source;source.read=copy->backend.sources.read;source.resident=copy->backend.sources.actor;
  std::weak_ptr<Impl>weak=copy;source.scope=[weak](G::Scope&out){auto p=weak.lock();return p&&p->scope(out);};
  copy->graph=original->graph->clone_sources(std::move(source));if(!copy->graph){why="model Stage original baseline clone";return false;}
  Context temporary(copy);auto binding=temporary.bind(candidate.quiescence(),why);if(!binding)return false;
  G::Capture a,b;return temporary.capture(*binding,0,a)&&temporary.capture(*binding,1,b)&&
   binding->current()&&candidate.current();
 }catch(...){why="model Stage candidate exception";return false;}
}
Context::PreparedRestoreOpen::PreparedRestoreOpen(std::shared_ptr<Impl>p,std::shared_ptr<Binding::State>b)
 :impl_(std::move(p)),binding_(std::move(b)){}
Context::PreparedRestoreOpen::~PreparedRestoreOpen(){
 if(impl_&&impl_->thread!=std::this_thread::get_id())std::terminate();
 if(impl_&&!consumed_)impl_->restore_open_pending=false;
}
void Context::PreparedRestoreOpen::open_unchecked()noexcept{
 if(!impl_||!binding_||consumed_||!impl_->restore_open_pending||!impl_->dormant_restore||impl_->thread!=std::this_thread::get_id())std::terminate();
 consumed_=true;impl_->dormant_restore=false;impl_->restore_open_pending=false;
}
std::optional<Context::PreparedRestoreOpen>Context::prepare_restore_open(
 const story_native_restore_resume::Boundary&b,const Binding&binding,
 const story_native_event_observation::Result&r,std::string&why){
 why.clear();try{auto p=impl_;const auto&completion=b.completion();
  if(!p||!p->prepared||!p->dormant_restore||p->restore_open_pending||!r.restorable()||!r.matches(p->expected)||
   !b.current(p->cpu)||completion.expected()!=p->expected||!completion.origin().clock()||
   !p->depth->restoring(b)||!p->session->adopted_restored(b)||!binding.state_||
   binding.state_->owner!=p.get()||!binding.current()||!binding.state_->park||
   binding.state_->park->read_only()||!binding.state_->park->current(p->cpu)||
   !matches_live(binding,completion.saved_graph())||!p->backend.config.collector.clock){why="model restore open binding";return {};}
  story_native_event_collect::Clock actual;
  if(!p->backend.config.collector.clock(actual)||actual.identity!=p->expected.identity||
   actual.serial!=completion.origin().scope().serial||!actual.logic_step||
   actual.elapsed_ticks!=completion.origin().clock()->elapsed_ticks||
   !binding.current()||!b.current(p->cpu)||!p->depth->restoring(b)){why="model restore open clock";return {};}
  p->restore_open_pending=true;return PreparedRestoreOpen(p,binding.state_);
 }catch(...){why="model restore open exception";return {};}
}
bool Context::restore_clock(const story_native_restore_resume::Boundary&boundary,std::string&why){
 why.clear();try{
  auto p=impl_;const auto&completion=boundary.completion();const auto&origin=completion.origin();
  if(!p||!p->prepared||!p->dormant_restore||!boundary.current(p->cpu)||
   completion.expected()!=p->expected||origin.expected()!=p->expected||!origin.clock()||
   !p->depth->restoring(boundary)||!p->backend.restore_clock||
   !completion.saved_graph().matches(p->expected,p->lineage)){
   why="model restored clock origin";return false;
  }
  auto binding=bind(why);if(!binding)return false;
  if(!matches_live(*binding,completion.saved_graph())){why="model restored clock bytes";return false;}
  story_native_wait_runtime_backend::RestoreClockSeed seed(p->expected,*origin.clock(),[&]{
   return boundary.current(p->cpu)&&p->depth->restoring(boundary)&&binding->current();
  });
  if(!p->backend.restore_clock(seed)){why="model restored backend clock";return false;}
  return true;
 }catch(...){why="model restored clock exception";return false;}
}
bool Context::capture(const Binding&b,unsigned n,G::Capture&out)const{
 out={};try{return impl_&&b.state_&&b.state_->owner==impl_.get()&&b.current()&&
  impl_->active.lock()==b.state_&&impl_->graph&&impl_->graph->capture(n,out)&&b.current();}catch(...){return false;}
}
bool Context::verify_model(const Binding&b,const G::Expected&e,unsigned n,
 const G::R::ModelIdentity&m,const G::R::Transform&t)const{
 try{return impl_&&impl_->prepared&&!impl_->dormant_restore&&impl_->stack_disjoint(story_native_refresh_stack::kDownwardBytes)&&b.state_&&b.state_->park&&!b.state_->park->read_only()&&!b.state_->pending_camera&&b.state_->owner==impl_.get()&&b.current()&&
  impl_->active.lock()==b.state_&&impl_->graph&&impl_->graph->verify_model(e,n,m,t)&&b.current();}catch(...){return false;}
}
bool PendingCameraInspection::current()const noexcept{try{return current_&&current_();}catch(...){return false;}}
bool PendingCameraInspection::refresh_ready()const noexcept{try{return !threads::current_thread_holds_read_only_native_quiescence()&&current()&&ready_&&ready_();}catch(...){return false;}}
std::optional<PendingCameraInspection>Context::inspect_pending_camera(const D::FrameWitness&w,std::string&why){
 why.clear();auto p=impl_;
 try{
  if(!p||!p->pending(w)||!p->active.expired()){why="model pending Camera frame";return {};}
  auto park=threads::try_native_quiescence(p->cpu,p->ticket,250,why);if(!park)return {};
  auto binding=bind_impl(std::make_shared<threads::NativeQuiescence>(std::move(*park)),w,why);
  if(!binding)return {};
  G::Capture a,b;
  if(!capture(*binding,0,a)||!capture(*binding,1,b)||!p->pending(w)){
   why="model pending graph recapture";return {};
  }
  std::weak_ptr<Impl>owner=p;
  return PendingCameraInspection(
   [owner,w]{auto live=owner.lock();return live&&live->pending(w);},
   [owner,w]{auto live=owner.lock();return G::Adapter::refresh_admission_enabled()&&
    live&&live->pending(w)&&live->stack_disjoint(story_native_refresh_stack::kDownwardBytes);});
 }catch(...){why="model pending inspection exception";return {};}
}
std::optional<story_native_model_saved_graph::Receipt>Context::capture_saved(const Binding&b)const{
 namespace S=story_native_model_saved_graph;auto p=impl_;
 try{
  if(!p||!p->prepared||!p->lineage.valid()||!b.state_||b.state_->pending_camera||
   b.state_->owner!=p.get()||!b.current()||p->active.lock()!=b.state_)return {};
  const auto journal=p->session->journal();const auto depth=p->depth->snapshot();
  if(!journal||(journal->phase!=J::Phase::Published&&journal->phase!=J::Phase::Running)||
   depth.state!=D::State::Active||!depth.coverage||!depth.idle()||depth.fault)return {};
  S::Trace complete;
  for(unsigned n=0;n<2;++n){
   S::Trace trace;if(!p->graph->trace(n,trace)||!b.current())return {};
   complete.reads.insert(complete.reads.end(),trace.reads.begin(),trace.reads.end());
   complete.writes.insert(complete.writes.end(),trace.writes.begin(),trace.writes.end());
  }
  const auto read=[p,&b](uint32_t at,std::span<uint8_t>out){
   return b.current()&&p->backend.sources.read(at,out)&&b.current();
  };
  auto result=S::Receipt::capture(p->expected,p->lineage,complete,read);
  if(!result||!b.current()||p->depth->snapshot().serial!=depth.serial)return {};
  return result;
 }catch(...){return {};}
}
bool Context::matches_saved(const story_native_model_saved_graph::Receipt&r,const ss::native_gate::SavedView&v)const{
 try{return impl_&&impl_->prepared&&r.matches(impl_->expected,impl_->lineage)&&r.matches_saved(v);}catch(...){return false;}
}
bool Context::matches_live(const Binding&b,const story_native_model_saved_graph::Receipt&r)const{
 auto p=impl_;try{
  if(!p||!p->prepared||!r.matches(p->expected,p->lineage)||!b.state_||b.state_->pending_camera||
   b.state_->owner!=p.get()||!b.current()||p->active.lock()!=b.state_)return false;
  for(unsigned n=0;n<2;++n){G::Capture capture;if(!this->capture(b,n,capture))return false;}
  return r.matches_live([p,&b](auto at,auto out){return b.current()&&p->backend.sources.read(at,out)&&b.current();})&&b.current();
 }catch(...){return false;}
}
std::optional<G::Capture>Context::baseline(unsigned n)const{if(!impl_||!impl_->prepared||n>=2)return {};return impl_->baseline[n];}
bool Context::prepared()const{return impl_&&impl_->prepared;}
}
