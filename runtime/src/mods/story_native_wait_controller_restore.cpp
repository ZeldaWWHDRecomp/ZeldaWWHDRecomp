// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_controller.h"
#include "story_native_restore_claim.h"
#include <stdexcept>
namespace story_native_wait_controller {
void Controller::revoke_for_restore()noexcept{restore_revoked_=true;state_=State::Quarantined;}
bool Controller::prepare_restore_open(Cpu*c,const story_native_restore_resume::Boundary&b,const threads::NativeQuiescence&q){
 if(busy_||!c||c!=started_cpu_||state_!=State::RestoredDormant||!restore_dormant_||restore_revoked_||!retained_||q.read_only()||!q.current(c)||!session_.adopted_restored(b))return false;
 busy_=true;active_cpu_=c;struct Exit{Controller&o;~Exit(){o.active_cpu_=nullptr;o.busy_=false;}}exit{*this};
 try{M::Scope before,after;story_native_event_observation::Captured captured;
  if(!query(before)||!capture(c,captured))return false;
  auto result=story_native_event_observation::validate(story_native_event_observation::Operation::Restore,expected_,captured);
  if(!result.restorable()||!restored_result_||!result.same_definition(*restored_result_)||!memory_->prepare_restore_open(result,b,c)||!query(after)||before.logic_step!=after.logic_step||before.serial!=after.serial||!depth_.restoring(b)||!q.current(c))return false;
  const auto status=install_->status();if((result.event_state()==1&&status!=story_native_event_install::Status::Started)||(result.event_state()!=1&&status!=story_native_event_install::Status::Running))return false;
  restored_result_=std::move(result);return true;
 }catch(...){return false;}
}
void Controller::open_restored_unchecked()noexcept{
 memory_->open_restored_unchecked();restore_dormant_=false;state_=restored_result_->event_state()==1?State::Started:State::Running;
}
State Controller::rehydrate(Cpu*c,const story_native_restore_resume::Boundary&b,story_native_restore_claim::Transfer&&transfer){
 if(busy_||!c||state_!=State::Empty)return state_;
 restore_dormant_=true;busy_=true;active_cpu_=c;
 struct Exit{Controller&owner;~Exit(){owner.active_cpu_=nullptr;owner.busy_=false;}}exit{*this};
 try{
  // A historical host claim is transferred only through the authentic saved
  // run and current post-service boundary. No native call or guest write.
  if(!session_.adopted_restored(b)||!transfer.consume(c,depth_,session_,expected_,sources_.validation.read))throw std::runtime_error("restored claim transfer");
  retained_=true;started_cpu_=c;
  M::Scope before,after;story_native_event_observation::Captured captured;
  if(!query(before)||!before.fenced||!depth_.restoring(b)||!capture(c,captured))throw std::runtime_error("restored native capture");
  const auto proof=story_native_event_observation::validate(story_native_event_observation::Operation::Restore,expected_,captured);
  const auto journal=session_.journal();using Phase=story_native_event_lease::Phase;
  if(!proof.restorable()||!journal||!memory_->adopt_restored(proof,b,c))throw std::runtime_error("restored full observation");
  const auto phase=journal->phase==story_native_wait_journal::Phase::Published?Phase::Published:Phase::Running;
  const auto status=install_->rehydrate(proof,phase);
  if((status!=story_native_event_install::Status::Started&&status!=story_native_event_install::Status::Running)||!query(after)||!after.fenced||
   before.logic_step!=after.logic_step||before.serial!=after.serial||!depth_.restoring(b)||!b.current(c))throw std::runtime_error("restored native lease");
  restored_result_=proof;return state_=State::RestoredDormant;
 }catch(...){quarantine();return state_;}
}
}
