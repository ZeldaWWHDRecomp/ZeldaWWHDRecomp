// SPDX-License-Identifier: MPL-2.0
#include "story_native_restore_resume.h"
#include "story_native_event_game_thread.h"
#include "story_native_stage_owner.h"
#include "story_native_stage_owner_observer.h"
#include "runtime.h"
#include <thread>
namespace story_native_restore_resume {
struct Completion::State {
 story_native_event_observation::Expected expected;
 std::shared_ptr<story_native_wait_runtime_model_graph::Context>model;
 story_native_model_saved_graph::Receipt graph;SavedRunOrigin origin;
 std::shared_ptr<const CurrentOwnerRevocation>current_owner;
 uint64_t restore_epoch,load_epoch;Cpu*cpu;uint32_t guest;
 threads::QuiescenceWitness witness;std::thread::id host=std::this_thread::get_id();
 std::function<void(Cpu*,const Boundary&)>observer;
 std::optional<story_native_stage_owner_observer::Snapshot>stage;
 State(story_native_event_observation::Expected e,std::shared_ptr<story_native_wait_runtime_model_graph::Context>m,story_native_model_saved_graph::Receipt&&g,SavedRunOrigin origin_value,uint64_t r,Cpu*c,uint32_t t,const threads::QuiescenceWitness&w,std::function<void(Cpu*,const Boundary&)>o,const story_native_stage_owner_observer::Snapshot*finished_stage,std::shared_ptr<const CurrentOwnerRevocation>owner):expected(std::move(e)),model(std::move(m)),graph(std::move(g)),origin(std::move(origin_value)),current_owner(std::move(owner)),restore_epoch(r),load_epoch(story_native_event_game_thread::current_load_epoch()),cpu(c),guest(t),witness(w),observer(std::move(o)){if(finished_stage)stage=*finished_stage;}
};
struct LoadWindow::State {Cpu*cpu;uint32_t guest;threads::QuiescenceWitness witness;std::thread::id host=std::this_thread::get_id();std::optional<Completion>completion;bool threads_done=false,finished=false;};
namespace detail {
class Access {
 static thread_local std::weak_ptr<LoadWindow::State>window;
 static thread_local std::optional<Completion>pending;
 static thread_local const Boundary*boundary;
public:
 static bool publish(Completion&c){auto w=window.lock();if(!w||!c.state_||w->completion||w->threads_done||w->finished)return false;auto&s=*c.state_;if(w->host!=std::this_thread::get_id()||s.host!=w->host||s.cpu!=w->cpu||s.guest!=w->guest||!s.witness.current(s.cpu)||!w->witness.current(s.cpu))return false;w->completion.emplace(std::move(c));return true;}
 static void open(const std::shared_ptr<LoadWindow::State>&w){pending.reset();if(!window.expired())return;window=w;}
 static void close(const std::shared_ptr<LoadWindow::State>&w){if(window.lock()==w)window.reset();}
 static bool current(const Boundary&b,const Cpu*c){return boundary==&b&&b.cpu_==c&&c&&threads::current()==c&&threads::current_thread()==b.guest_&&b.epoch_==story_native_event_game_thread::current_load_epoch()&&b.completion_->state_->host==std::this_thread::get_id();}
 static void finish(const std::shared_ptr<LoadWindow::State>&w){if(window.lock()!=w||!w->threads_done||w->finished||!w->completion||w->host!=std::this_thread::get_id()||threads::current()!=w->cpu||threads::current_thread()!=w->guest||w->witness.current(w->cpu)||threads::current_quiescence(w->cpu)||threads::current_thread_holds_native_quiescence()||story_native_stage_owner::current_thread_holds_lifetime_pin())return;w->finished=true;pending.emplace(std::move(*w->completion));w->completion.reset();}
 static void issue(Cpu*c){if(!pending)return;auto completion=std::move(*pending);pending.reset();auto&s=*completion.state_;if(s.host!=std::this_thread::get_id()||s.cpu!=c||s.guest!=threads::current_thread()||s.load_epoch!=story_native_event_game_thread::current_load_epoch()||threads::current_thread_holds_native_quiescence()||story_native_stage_owner::current_thread_holds_lifetime_pin()||threads::current_quiescence(c))return;Boundary b(completion,c,s.guest,s.load_epoch);boundary=&b;struct Clear{~Clear(){Access::boundary=nullptr;}}clear;if(!b.current(c)||!story_native_event_game_thread::reissue_after_restore(c,b))return;if(s.observer)s.observer(c,b);}
};
thread_local std::weak_ptr<LoadWindow::State>Access::window;
thread_local std::optional<Completion>Access::pending;
thread_local const Boundary*Access::boundary=nullptr;
}
Completion::Completion(std::shared_ptr<State>s):state_(std::move(s)){}
std::optional<Completion>Completion::make(story_native_event_observation::Expected e,std::shared_ptr<story_native_wait_runtime_model_graph::Context>m,story_native_model_saved_graph::Receipt&&g,SavedRunOrigin origin,uint64_t epoch,Cpu*c,const threads::QuiescenceWitness&w,std::function<void(Cpu*,const Boundary&)>observer,const story_native_stage_owner_observer::Snapshot*finished_stage,std::shared_ptr<const CurrentOwnerRevocation>current_owner){
 // Called only by the finishing Context after its exact byte/lineage checks.
 if((finished_stage&&(!finished_stage->control||!finished_stage->receipt.generation))||!origin.life_||origin.expected_!=e||!epoch||!m||!g.byte_count()||!c||threads::current()!=c||!threads::current_thread()||!w.current(c))return {};
 return Completion(std::make_shared<State>(std::move(e),std::move(m),std::move(g),std::move(origin),epoch,c,threads::current_thread(),w,std::move(observer),finished_stage,std::move(current_owner)));}
bool Completion::publish(){return detail::Access::publish(*this);}
const story_native_event_observation::Expected&Completion::expected()const{static const story_native_event_observation::Expected empty{};return state_?state_->expected:empty;}
const story_native_model_saved_graph::Receipt&Completion::saved_graph()const{return state_->graph;}
std::shared_ptr<story_native_wait_runtime_model_graph::Context>Completion::original_model()const{return state_?state_->model:nullptr;}
uint64_t Completion::restore_epoch()const noexcept{return state_?state_->restore_epoch:0;}
const SavedRunOrigin&Completion::origin()const{return state_->origin;}
const std::shared_ptr<const CurrentOwnerRevocation>&Completion::current_owner()const{return state_->current_owner;}
const story_native_stage_owner_observer::Snapshot*Completion::stage()const noexcept{return state_&&state_->stage?&*state_->stage:nullptr;}
Boundary::Boundary(const Completion&c,Cpu*p,uint32_t g,uint64_t e):completion_(&c),cpu_(p),guest_(g),epoch_(e),current_([this](const Cpu*cpu){return detail::Access::current(*this,cpu);}){}
LoadWindow::LoadWindow(Cpu*c){auto w=threads::current_quiescence(c);if(!c||threads::current()!=c||!w||!w->current(c))return;state_=std::make_shared<State>();state_->cpu=c;state_->guest=threads::current_thread();state_->witness=*w;detail::Access::open(state_);}
LoadWindow::~LoadWindow(){if(state_)detail::Access::close(state_);}
void LoadWindow::threads_loaded(){if(state_&&state_->host==std::this_thread::get_id()&&threads::current()==state_->cpu&&threads::current_thread()==state_->guest&&state_->witness.current(state_->cpu))state_->threads_done=true;}
void LoadWindow::after_thaw(){if(state_)detail::Access::finish(state_);}
void LoadWindow::service_return(Cpu*c)noexcept{try{detail::Access::issue(c);}catch(...){}}
}
