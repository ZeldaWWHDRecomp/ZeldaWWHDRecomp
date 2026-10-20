// SPDX-License-Identifier: MPL-2.0
#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <thread>
#include <utility>
#include "story_native_wait_active_save_gate.h"
#include "native_quiescence.h"
#include "story_native_restore_current_owner.h"
#include "runtime.h"
namespace story_native_wait_active_save_gate {
namespace {
std::atomic<uint64_t>epochs{0};
uint64_t next_epoch(){
 auto previous=epochs.load(std::memory_order_relaxed);
 while(previous!=std::numeric_limits<uint64_t>::max())
  if(epochs.compare_exchange_weak(previous,previous+1,std::memory_order_relaxed))return previous+1;
 return 0;
}
}
Controller::Guard::~Guard(){
 // Fail before any thread-affine fence member can unlock on a foreign host.
 if(owner.thread_!=std::this_thread::get_id())std::terminate();
 owner.release();
}
bool Controller::held(Mode mode)const noexcept{return mode_==mode&&thread_==std::this_thread::get_id();}
void Controller::release()noexcept{
 // Holds are synchronously owned by savestate on its issuing host thread.
 if(thread_!=std::this_thread::get_id())return;
 initial_stage_.reset();initial_snapshot_.reset();pristine_restore_.reset();selected_pristine_=nullptr;pristine_capture_.reset();idle_restore_.reset();idle_capture_.reset();load_group_.reset();load_owner_.reset();selected_idle_=nullptr;permit_.reset();capture_.reset();issuer_=nullptr;selected_=nullptr;held_guard_=nullptr;finished_=false;mode_=Mode::Idle;thread_={};
}
std::unique_ptr<ss::native_gate::Hold>Controller::begin_save(){
 if(mode_!=Mode::Idle)return {};
 auto guard=std::make_unique<Guard>(*this);thread_=std::this_thread::get_id();held_guard_=guard.get();
 if(auto clean=bootstrap_.begin_save()){
  guard->bootstrap=std::move(clean);
  auto*c=threads::current();auto parked=threads::current_quiescence(c);
  if(!c||!parked)return {};
  pristine_capture_=story_native_wait_pristine_snapshot::begin(session_,c,*parked);if(!pristine_capture_)return {};
  mode_=Mode::BootstrapSave;return guard;
 }
 issuer_=current_?current_():nullptr;
 if(issuer_)capture_=issuer_->begin_saved_run();
 if(capture_){mode_=Mode::ActiveSave;return guard;}
 auto*c=threads::current();auto parked=threads::current_quiescence(c);
 if(!c||!parked)return {};
 guard->fence=session_.try_fence();
 if(guard->fence){
  pristine_capture_=story_native_wait_pristine_snapshot::begin(session_,c,*parked);
  if(pristine_capture_){mode_=Mode::PristineSave;return guard;}
  guard->fence.reset();
 }
 idle_capture_=story_native_wait_idle_snapshot::begin(session_,c,*parked);
 if(!idle_capture_)return {};mode_=Mode::IdleSave;return guard;
}
bool Controller::finish_save(){
 if(held(Mode::IdleSave)&&!finished_&&idle_capture_&&held_guard_){
  finished_=true;auto*c=threads::current();auto parked=threads::current_quiescence(c);
  if(!c||!parked)return false;
  auto saved=story_native_wait_idle_snapshot::commit(std::move(*idle_capture_),c,*parked,&held_guard_->fence);
  idle_capture_.reset();if(!saved)return false;
  idle_checkpoints_.push_back(std::move(*saved));return true;
 }
 if((held(Mode::BootstrapSave)||held(Mode::PristineSave))&&!finished_){
  finished_=true;if(!pristine_capture_||(mode_==Mode::BootstrapSave&&!bootstrap_.finish_save()))return false;
  auto*c=threads::current();auto parked=threads::current_quiescence(c);if(!c||!parked)return false;
  auto saved=story_native_wait_pristine_snapshot::commit(std::move(*pristine_capture_),c,*parked);
  pristine_capture_.reset();if(!saved)return false;
  pristine_checkpoints_.push_back(std::move(*saved));return true;
 }
 if(!held(Mode::ActiveSave)||finished_||!issuer_||!capture_||!held_guard_)return false;
 finished_=true;
 if(!issuer_->commit_saved_run(*capture_,&held_guard_->fence))return false;
 auto checkpoint=issuer_->checkpoint_saved_run(*capture_);if(!checkpoint)return false;
 checkpoints_.push_back({issuer_,std::move(*checkpoint)});return true;
}
std::unique_ptr<ss::native_gate::Hold>Controller::begin_group_load(const ss::native_gate::SavedView&view,
 std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>group,
 std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>owner){
 if(mode_!=Mode::Idle)return {};
 load_group_=std::move(group);load_owner_=std::move(owner);
 return begin_load(view);
}
std::unique_ptr<ss::native_gate::Hold>Controller::begin_load(const ss::native_gate::SavedView&view){
 if(mode_!=Mode::Idle)return {};
 auto guard=std::make_unique<Guard>(*this);thread_=std::this_thread::get_id();held_guard_=guard.get();
 if(auto clean=bootstrap_.begin_load(view)){
  guard->bootstrap=std::move(clean);
  if(load_group_){
   auto*c=threads::current();auto parked=threads::current_quiescence(c);
   if(!c||!parked||!load_owner_||!load_owner_->current(c,*parked,view,load_owner_->epoch())||
      !session_.initial_import_roots(view,c,*parked))return {};
   // This receipt comes from the current genuine Observer, not saved host
   // metadata. Its saved-only inspector admits only the same native resource.
   auto scan=story_native_stage_owner_observer::tracker().try_scan();
   if(!scan.current())return {};
   initial_snapshot_=story_native_stage_owner_observer::snapshot(scan);
   if(!initial_snapshot_)return {};
   initial_stage_=load_group_->join(*initial_snapshot_,view,c,*parked);
   if(!initial_stage_||!load_owner_->current(c,*parked,view,load_owner_->epoch()))return {};
  }
  mode_=Mode::BootstrapLoad;return guard;
 }
 // Newest matching capture first. A mismatched file never consumes a saved
 // checkpoint; each authentic load attempt gets a distinct one-use receipt.
 for(auto i=checkpoints_.rbegin();i!=checkpoints_.rend();++i){
  if(!i->checkpoint.matches_saved(view))continue;
  auto attempt=i->checkpoint.fork_attempt();const auto epoch=load_owner_?load_owner_->epoch():next_epoch();
  if(!attempt||!epoch)return {};
  selected_=&i->checkpoint;issuer_=i->issuer;permit_=load_front_?issuer_->prepare_run_restore(std::move(*attempt),view,epoch,load_front_(),load_group_,load_owner_):
   issuer_->prepare_run_restore(std::move(*attempt),view,epoch);
  if(!permit_||!issuer_->suspend_run_restore(*permit_))return {};
  mode_=Mode::ActiveLoad;return guard;
 }
 for(auto i=pristine_checkpoints_.rbegin();i!=pristine_checkpoints_.rend();++i){
  if(!i->matches_saved(view))continue;
  auto*c=threads::current();auto parked=threads::current_quiescence(c);
  const auto epoch=load_owner_?load_owner_->epoch():next_epoch();
  if(!c||!parked||!epoch)return {};
  selected_pristine_=&*i;
  pristine_restore_=story_native_wait_pristine_snapshot::prepare(session_,*i,view,c,*parked,epoch,load_owner_,load_group_);
  if(!pristine_restore_)return {};mode_=Mode::PristineLoad;return guard;
 }
 for(auto i=idle_checkpoints_.rbegin();i!=idle_checkpoints_.rend();++i){
  if(!i->matches_saved(view))continue;
  auto*c=threads::current();auto parked=threads::current_quiescence(c);const auto epoch=load_owner_?load_owner_->epoch():next_epoch();
  if(!c||!parked||!epoch)return {};
  selected_idle_=&*i;idle_restore_=story_native_wait_idle_snapshot::prepare(session_,*i,view,c,*parked,epoch,load_group_,load_owner_);
  if(!idle_restore_)return {};mode_=Mode::IdleLoad;return guard;
 }
 return {};
}
bool Controller::finish_load(const ss::native_gate::SavedView&view){
 if(held(Mode::PristineLoad)&&!finished_&&pristine_restore_&&selected_pristine_&&held_guard_){
  finished_=true;if(!selected_pristine_->matches_saved(view))return false;
  auto*c=threads::current();auto parked=threads::current_quiescence(c);if(!c||!parked)return false;
  return story_native_wait_pristine_snapshot::finish(std::move(*pristine_restore_),view,c,*parked,&held_guard_->fence);
 }

 if(held(Mode::IdleLoad)&&!finished_&&idle_restore_&&selected_idle_&&held_guard_){
  finished_=true;if(!selected_idle_->matches_saved(view))return false;
  auto*c=threads::current();auto parked=threads::current_quiescence(c);if(!c||!parked)return false;
  return story_native_wait_idle_snapshot::finish(std::move(*idle_restore_),view,c,*parked,&held_guard_->fence);
 }
 if(held(Mode::BootstrapLoad)&&!finished_){
  finished_=true;
  if(!load_group_)return bootstrap_.finish_load(view);
  auto*c=threads::current();auto parked=threads::current_quiescence(c);
  const auto current=[&]{return c&&parked&&load_owner_&&load_owner_->current(c,*parked,view,load_owner_->epoch())&&session_.initial_import_roots(view,c,*parked);};
  if(!initial_stage_||!initial_snapshot_||!current()){if(initial_stage_)initial_stage_->invalidate();return false;}
  auto restored=initial_stage_->finish(view,c,*parked);
  const bool ok=restored&&restored->control==initial_snapshot_->control&&restored->receipt.owner==initial_snapshot_->receipt.owner&&
   current()&&bootstrap_.finish_load(view)&&current();
  if(!ok)initial_stage_->invalidate();return ok;
 }
 if(!held(Mode::ActiveLoad)||finished_||!issuer_||!permit_||!selected_||!held_guard_)return false;
 finished_=true;
 if(!selected_->matches_saved(view)){issuer_->invalidate();return false;}
 return issuer_->finish_run_restore(std::move(*permit_),&held_guard_->fence);
}
std::unique_ptr<ss::native_gate::Hold>Controller::begin_progress_load(){
 if(mode_!=Mode::Idle)return {};
 auto guard=std::make_unique<Guard>(*this);thread_=std::this_thread::get_id();held_guard_=guard.get();
 auto*c=threads::current();auto parked=threads::current_quiescence(c);
 if(c&&parked){
  guard->fence=session_.try_fence();
  if(guard->fence&&(session_.preserve_progress_load(c,*parked)||session_.preserve_pristine_progress_load(c,*parked))){
   mode_=Mode::Progress;return guard;
  }
  guard->fence.reset();
 }
 if(auto clean=bootstrap_.begin_progress_load()){
  guard->bootstrap=std::move(clean);mode_=Mode::Progress;return guard;
 }
 guard->fence=session_.try_fence();if(!guard->fence)return {};
 issuer_=current_?current_():nullptr;if(!issuer_)return {};
 issuer_->invalidate();mode_=Mode::Progress;return guard;
}
}
