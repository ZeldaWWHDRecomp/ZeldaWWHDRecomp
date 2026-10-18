// SPDX-License-Identifier: MPL-2.0
#include "story_native_scene_playback.h"
#include "story_native_scene_run_owner.h"
#include <stdexcept>
namespace story_native_scene_playback {namespace {
Backend::Start backend;
}
Run::Run(std::shared_ptr<const Activation>a,decltype(poll_)poll,decltype(queue_stop_)stop,std::shared_ptr<RunOwnerCell>owner):activation_(std::move(a)),poll_(std::move(poll)),queue_stop_(std::move(stop)),owner_(std::move(owner)){
 if(!activation_||!poll_||!queue_stop_)throw std::invalid_argument("native run requires actual activation and runtime callbacks");
}
Attempt::Attempt(Kind kind,std::unique_ptr<Run>run):kind_(kind),run_(std::move(run)){}
bool Backend::install(Start start){if(!start||backend)return false;backend=std::move(start);return true;}
StartResult Player::start(Cpu*cpu,const std::string&mod,const story_notebook::ManifestScene&scene,uint64_t generation,decltype(finish_)finish){
 if(active()||busy_)return StartResult::Denied;
 if(!cpu||!backend)return StartResult::WholeFallback;
 // Complete every potentially throwing host-data copy before the backend may
 // acquire ownership or write a guest byte. Unsupported scenes stay whole.
 story_native_scene_coverage::Coverage coverage;std::string copied_mod;
 try{coverage=story_native_scene_coverage::inspect(scene);copied_mod=mod;}catch(...){return StartResult::WholeFallback;}
 if(coverage.selection!=story_native_scene_coverage::Selection::NativeAlternative)return StartResult::WholeFallback;
 busy_=true;struct Clear{bool&b;~Clear(){b=false;}}clear{busy_};
 try{
  auto attempt=backend(cpu,mod,scene);
  if(attempt.kind_==Attempt::Kind::WholeFallback&&!attempt.run_)return StartResult::WholeFallback;
  // Only an actual pre-entry backend decision without a Run may retry. It
  // conveys no ownership, callbacks, completion or permission to invoke native.
  if(attempt.kind_==Attempt::Kind::Deferred&&!attempt.run_)return StartResult::Deferred;
  mod_=std::move(copied_mod);coverage_=std::move(coverage);finish_=std::move(finish);generation_=generation;reason_.reset();post_load_stop_.reset();load_generation_.reset();reconciled_generation_.reset();stop_sent_=false;
  run_=std::move(attempt.run_);
  if(attempt.kind_!=Attempt::Kind::Owned||!run_){blocked_=true;return StartResult::Denied;}
  const auto&activation=*run_->activation_;const auto&declaration=*activation.declaration();
  if(declaration.mod()!=mod||declaration.scene()!=scene.id||declaration.definition()!=scene||activation.definition_hash()!=declaration.definition_hash()){
   request_stop(story_scenes::StopReason::Failed);return StartResult::Owned;
  }
  if(run_->owner_&&(run_->owner_->activation()!=run_->activation_||!run_->owner_->bind_frontend(activation,generation))){request_stop(story_scenes::StopReason::Failed);return StartResult::Owned;}
  return StartResult::Owned;
 }catch(...){
  // An exception cannot attest "before mutation". Keep the queue entry as a
  // fail-closed owner rather than entering the legacy fallback afterward.
  blocked_=true;return StartResult::Denied;
 }
}
void Player::swap_host(Player&other)noexcept{
 run_.swap(other.run_);std::swap(blocked_,other.blocked_);std::swap(busy_,other.busy_);std::swap(stop_sent_,other.stop_sent_);
 std::swap(generation_,other.generation_);reason_.swap(other.reason_);post_load_stop_.swap(other.post_load_stop_);load_generation_.swap(other.load_generation_);reconciled_generation_.swap(other.reconciled_generation_);
 mod_.swap(other.mod_);std::swap(coverage_.selection,other.coverage_.selection);std::swap(coverage_.end_step,other.coverage_.end_step);coverage_.rewards.swap(other.coverage_.rewards);coverage_.reason.swap(other.coverage_.reason);finish_.swap(other.finish_);
}
void Player::send_stop()noexcept{
 if(!run_||blocked_||stop_sent_||!reason_)return;stop_sent_=true;
 try{run_->queue_stop_(*reason_);}catch(...){blocked_=true;}
}
void Player::request_stop(story_scenes::StopReason reason)noexcept{
 if(!active()||reason==story_scenes::StopReason::Finished)return;
 auto rank=[](story_scenes::StopReason r){return r==story_scenes::StopReason::StateLoaded?2:r==story_scenes::StopReason::Interrupted?1:0;};
 if(load_generation_&&reason!=story_scenes::StopReason::StateLoaded&&(!post_load_stop_||rank(reason)>rank(*post_load_stop_)))post_load_stop_=reason;
 if(!reason_||rank(reason)>rank(*reason_)){reason_=reason;stop_sent_=false;}
 // This callback can only queue host state. No Cpu/native-call argument is
 // supplied, including when abort/state_loaded happens outside a game tick.
 send_stop();
}
void Player::state_loaded(uint64_t generation)noexcept{
 if(!active())return;
 if(!load_generation_||*load_generation_!=generation){if(run_&&run_->owner_)run_->owner_->record_load(*run_->activation_,generation_,generation);load_generation_=generation;post_load_stop_.reset();reason_=story_scenes::StopReason::StateLoaded;stop_sent_=false;}
 send_stop();
}
bool Player::consume_load_reconciliation(uint64_t generation)noexcept{
 if(!reconciled_generation_||*reconciled_generation_!=generation)return false;
 reconciled_generation_.reset();return true;
}
TickResult Player::tick(Cpu*cpu,uint64_t generation){
 if(!active())return TickResult::Failed;
 if(busy_||blocked_||!run_||!cpu)return TickResult::Active;
 busy_=true;struct Clear{bool&b;~Clear(){b=false;}}clear{busy_};
 if(run_->owner_&&run_->owner_->consume_resume(*run_->activation_,generation_,generation)){
  generation_=generation;reconciled_generation_=generation;reason_=post_load_stop_;post_load_stop_.reset();load_generation_.reset();stop_sent_=false;
 }
 if(generation!=generation_)state_loaded(generation);
 send_stop();if(blocked_)return TickResult::Active;
 try{
  auto progress=run_->poll_(cpu);
  if(progress.reason)request_stop(*progress.reason);
  if(progress.phase!=Progress::Phase::Retired){
   if(progress.natural){blocked_=true;reason_=story_scenes::StopReason::Failed;}
   return TickResult::Active;
  }
  TickResult result=reason_?TickResult::Stopped:TickResult::Failed;
  if(!reason_&&progress.natural&&progress.natural->consume(*run_->activation_)){
   // Consume before actions; exceptions, false returns and reentrant aborts
   // never replay this terminal reward list. Status enums cannot reach here.
   bool success=false;try{success=coverage_.rewards.empty()||(finish_&&finish_(mod_,coverage_.rewards));}catch(...){success=false;}
   result=success?TickResult::Finished:TickResult::Failed;
  }
  run_.reset();blocked_=false;finish_={};return result;
 }catch(...){blocked_=true;reason_=story_scenes::StopReason::Failed;return TickResult::Active;}
}
}
