// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_pristine_snapshot.h"
namespace story_native_wait_pristine_snapshot {
namespace {
std::optional<std::optional<O::Snapshot>>stage(){auto pin=O::tracker().try_scan();if(!pin.holds_lifetime_of(O::tracker()))return {};if(!pin.current())return std::optional<std::optional<O::Snapshot>>{std::in_place,std::nullopt};auto snapshot=O::snapshot(pin);if(!snapshot)return {};return snapshot;}
bool same(const std::optional<O::Snapshot>&a,const std::optional<O::Snapshot>&b){return bool(a)==bool(b)&&(!a||(a->control==b->control&&a->receipt==b->receipt));}
}
Checkpoint::Checkpoint(S::PristineCheckpoint s,std::optional<O::Snapshot>stage):session_(std::move(s)),stage_(stage){}
Capture::Capture(S::Session&s,S::PristineCapture p,std::optional<O::Snapshot>stage):owner_(&s),session_(std::move(p)),stage_(stage){}
Restore::Restore(S::Session&s,S::PristineRestore p,std::shared_ptr<Group>group,std::optional<O::RestorePermit>stage,std::optional<Group::Participant>participant,std::optional<O::Snapshot>saved):owner_(&s),session_(std::move(p)),group_(std::move(group)),stage_(std::move(stage)),participant_(std::move(participant)),saved_stage_(saved){}
bool Checkpoint::matches_saved(const ss::native_gate::SavedView&v)const noexcept{try{if(!session_.matches_saved(v))return false;if(!stage_)return true;return O::matches_saved(*stage_,v);}catch(...){return false;}}
std::optional<Capture>begin(S::Session&s,Cpu*c,const threads::QuiescenceWitness&w){try{
 if(!w.current(c))return {};auto capture=s.begin_pristine_capture(c,w);if(!capture)return {};auto observed=stage();if(!observed||!w.current(c))return {};return Capture(s,std::move(*capture),*observed);
 }catch(...){return {};}}
std::optional<Checkpoint>commit(Capture&&input,Cpu*c,const threads::QuiescenceWitness&w){try{if(input.thread_!=std::this_thread::get_id())return {};auto p=std::move(input);if(!p.owner_||!w.current(c))return {};auto observed=stage();if(!observed||!same(*observed,p.stage_)||!w.current(c))return {};auto saved=p.owner_->commit_pristine_capture(std::move(p.session_),c,w);if(!saved)return {};return Checkpoint(std::move(*saved),p.stage_);}catch(...){return {};}}
std::optional<Restore>prepare(S::Session&s,const Checkpoint&saved,const ss::native_gate::SavedView&v,Cpu*c,const threads::QuiescenceWitness&w,uint64_t epoch,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>owner,std::shared_ptr<Group>group){try{
 if(!w.current(c)||!saved.matches_saved(v))return {};auto session=s.prepare_pristine_restore(saved.session_,v,c,w,epoch,std::move(owner));if(!session)return {};
 std::optional<O::RestorePermit>permit;std::optional<Group::Participant>participant;
 if(saved.stage_){if(group){participant=group->join(*saved.stage_,v,c,w);if(!participant)return {};}else{permit=O::prepare_restore_same_resource(*saved.stage_,v);if(!permit)return {};}}
 if(!w.current(c))return {};return Restore(s,std::move(*session),std::move(group),std::move(permit),std::move(participant),saved.stage_);
 }catch(...){return {};}}
bool finish(Restore&&input,const ss::native_gate::SavedView&v,Cpu*c,const threads::QuiescenceWitness&w,std::unique_ptr<S::Session::Fence>*retain){try{
 if(input.thread_!=std::this_thread::get_id())return false;auto p=std::move(input);if(!p.owner_||!w.current(c))return false;
 if(p.saved_stage_){auto restored=p.participant_?p.participant_->finish(v,c,w):(p.stage_?O::finish_restore(std::move(*p.stage_)):std::nullopt);
 if(!restored||restored->control!=p.saved_stage_->control||restored->receipt.owner!=p.saved_stage_->receipt.owner||!w.current(c)){if(p.participant_)p.participant_->invalidate();return false;}}
 const bool ok=p.owner_->adopt_pristine(std::move(p.session_),v,c,w,retain);if(!ok&&p.participant_)p.participant_->invalidate();return ok;
 }catch(...){return false;}}
}
