// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <thread>
#include "story_native_wait_session.h"
#include "story_native_stage_owner_observer.h"
#include "story_native_stage_restore_group.h"
#include "story_native_restore_current_owner.h"
namespace story_native_wait_idle_snapshot {
namespace S=story_native_wait_session;
namespace O=story_native_stage_owner_observer;
// Session and Stage proofs remain independent; this wrapper composes their
// genuine retained permits and grants no thread ticket or native admission.
class Checkpoint final {
 S::IdleCheckpoint session_;O::Snapshot stage_;
 Checkpoint(S::IdleCheckpoint,O::Snapshot);
 friend class Capture;friend class Restore;
 friend std::optional<Checkpoint>commit(class Capture&&,Cpu*,const threads::QuiescenceWitness&,std::unique_ptr<S::Session::Fence>*);
 friend std::optional<class Restore>prepare(S::Session&,const Checkpoint&,const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&,uint64_t,std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>);
public:
 bool matches_saved(const ss::native_gate::SavedView&)const noexcept;
 const story_native_wait_journal::Record&journal()const noexcept{return session_.journal();}
};
class Capture final {
 std::thread::id thread_=std::this_thread::get_id();
 S::Session*owner_;S::IdleCapture session_;O::Snapshot stage_;
 Capture(S::Session&,S::IdleCapture,O::Snapshot);
 friend std::optional<Capture>begin(S::Session&,Cpu*,const threads::QuiescenceWitness&);
 friend std::optional<Checkpoint>commit(Capture&&,Cpu*,const threads::QuiescenceWitness&,std::unique_ptr<S::Session::Fence>*);
public:Capture(Capture&&)=default;Capture&operator=(Capture&&)=default;
};
class Restore final {
 std::thread::id thread_=std::this_thread::get_id();
 S::Session*owner_;S::IdleRestore session_;
 std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>group_;
 std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>current_owner_;uint64_t epoch_=0;
 std::optional<O::RestorePermit>stage_;
 std::optional<story_native_stage_restore_group::StageRestoreGroup::Participant>participant_;O::Snapshot saved_stage_;
 Restore(S::Session&,S::IdleRestore,std::optional<O::RestorePermit>,O::Snapshot,std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>,std::optional<story_native_stage_restore_group::StageRestoreGroup::Participant>,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>,uint64_t);
 friend std::optional<Restore>prepare(S::Session&,const Checkpoint&,const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&,uint64_t,std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>);
 friend bool finish(Restore&&,const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&,std::unique_ptr<S::Session::Fence>*);
public:Restore(Restore&&)=default;Restore&operator=(Restore&&)=default;
};
std::optional<Capture>begin(S::Session&,Cpu*,const threads::QuiescenceWitness&);
std::optional<Checkpoint>commit(Capture&&,Cpu*,const threads::QuiescenceWitness&,std::unique_ptr<S::Session::Fence>*retain=nullptr);
std::optional<Restore>prepare(S::Session&,const Checkpoint&,const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&,uint64_t host_epoch,std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>group={},std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>current_owner={});
bool finish(Restore&&,const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&,std::unique_ptr<S::Session::Fence>*retain=nullptr);
}
