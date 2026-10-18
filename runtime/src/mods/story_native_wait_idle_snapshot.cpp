// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_idle_snapshot.h"
#include "native_quiescence.h"
namespace story_native_wait_idle_snapshot {
Checkpoint::Checkpoint(S::IdleCheckpoint s,O::Snapshot stage):session_(std::move(s)),stage_(stage){}
Capture::Capture(S::Session&s,S::IdleCapture p,O::Snapshot stage):owner_(&s),session_(std::move(p)),stage_(stage){}
Restore::Restore(S::Session&s,S::IdleRestore p,std::optional<O::RestorePermit>stage,O::Snapshot saved,std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>group,std::optional<story_native_stage_restore_group::StageRestoreGroup::Participant>participant,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>current_owner,uint64_t epoch):owner_(&s),session_(std::move(p)),group_(std::move(group)),current_owner_(std::move(current_owner)),epoch_(epoch),stage_(std::move(stage)),participant_(std::move(participant)),saved_stage_(saved){}
bool Checkpoint::matches_saved(const ss::native_gate::SavedView&v)const noexcept{return session_.matches_saved(v);}
std::optional<Capture>begin(S::Session&s,Cpu*c,const threads::QuiescenceWitness&w){try{
 if(!w.current(c))return {};auto session=s.begin_idle_capture(c,w);if(!session)return {};
 auto pin=O::tracker().try_scan();if(!pin.current())return {};auto stage=O::snapshot(pin);
 if(!stage||!w.current(c))return {};return Capture(s,std::move(*session),*stage);
 }catch(...){return {};}}
std::optional<Checkpoint>commit(Capture&&input,Cpu*c,const threads::QuiescenceWitness&w,std::unique_ptr<S::Session::Fence>*retain){try{
 if(input.thread_!=std::this_thread::get_id())return {};auto p=std::move(input);
 if(!p.owner_||!w.current(c))return {};
 {auto pin=O::tracker().try_scan();if(!pin.current())return {};auto fresh=O::snapshot(pin);
  if(!fresh||fresh->control!=p.stage_.control||fresh->receipt!=p.stage_.receipt||!w.current(c))return {};}
 auto session=p.owner_->commit_idle_capture(std::move(p.session_),c,w,retain);if(!session)return {};
 return Checkpoint(std::move(*session),p.stage_);
 }catch(...){return {};}}
std::optional<Restore>prepare(S::Session&s,const Checkpoint&saved,const ss::native_gate::SavedView&v,Cpu*c,const threads::QuiescenceWitness&w,uint64_t epoch,std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>group,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>current_owner){try{
 if(!w.current(c)||!saved.matches_saved(v)||(current_owner&&!current_owner->current(c,w,v,epoch)))return {};
 // Acquire the actual Session fence before Stage's retained permit. Reverse
 // member destruction releases Stage before Session on every refusal path.
 auto session=s.prepare_idle_restore(saved.session_,v,c,w,epoch,current_owner);if(!session)return {};
 if(group){auto participant=group->join(saved.stage_,v,c,w);if(!participant||!w.current(c))return {};
  return Restore(s,std::move(*session),{},saved.stage_,std::move(group),std::move(participant),std::move(current_owner),epoch);}
 auto pin=O::tracker().try_scan();if(!pin.current())return {};
 auto stage=O::prepare_restore_same_resource(saved.stage_,v);if(!stage||!w.current(c))return {};
 return Restore(s,std::move(*session),std::move(stage),saved.stage_,{},{},std::move(current_owner),epoch);
 }catch(...){return {};}}
bool finish(Restore&&input,const ss::native_gate::SavedView&v,Cpu*c,const threads::QuiescenceWitness&w,std::unique_ptr<S::Session::Fence>*retain){try{
 if(input.thread_!=std::this_thread::get_id())return false;auto p=std::move(input);
 if(!p.owner_||!w.current(c)||(p.current_owner_&&!p.current_owner_->current(c,w,v,p.epoch_))){if(p.participant_)p.participant_->invalidate();return false;}
 const auto restored=p.participant_?p.participant_->finish(v,c,w):(p.stage_?O::finish_restore(std::move(*p.stage_)):std::nullopt);
 if(!restored||restored->control!=p.saved_stage_.control||restored->receipt.owner!=p.saved_stage_.receipt.owner||!w.current(c)){if(p.participant_)p.participant_->invalidate();return false;}
 const bool ok=p.owner_->adopt_idle(std::move(p.session_),v,c,w,retain);
 if(!ok&&p.participant_)p.participant_->invalidate();return ok;
 }catch(...){return false;}}
}
