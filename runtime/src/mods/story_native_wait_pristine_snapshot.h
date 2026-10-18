// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <thread>
#include "story_native_wait_session.h"
#include "story_native_stage_restore_group.h"
namespace story_native_wait_pristine_snapshot {
namespace S=story_native_wait_session;namespace O=story_native_stage_owner_observer;
using Group=story_native_stage_restore_group::StageRestoreGroup;
class Checkpoint final {
 S::PristineCheckpoint session_;std::optional<O::Snapshot>stage_;
 Checkpoint(S::PristineCheckpoint,std::optional<O::Snapshot>);friend class Capture;friend class Restore;
 friend std::optional<Checkpoint>commit(class Capture&&,Cpu*,const threads::QuiescenceWitness&);
 friend std::optional<class Restore>prepare(S::Session&,const Checkpoint&,const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&,uint64_t,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>,std::shared_ptr<Group>);
public:
 bool matches_saved(const ss::native_gate::SavedView&)const noexcept;
 bool has_stage()const noexcept{return bool(stage_);}
 const story_native_wait_journal::Record&journal()const noexcept{return session_.journal();}
};
class Capture final {
 std::thread::id thread_=std::this_thread::get_id();
 S::Session*owner_;S::PristineCapture session_;std::optional<O::Snapshot>stage_;
 Capture(S::Session&,S::PristineCapture,std::optional<O::Snapshot>);
 friend std::optional<Capture>begin(S::Session&,Cpu*,const threads::QuiescenceWitness&);
 friend std::optional<Checkpoint>commit(Capture&&,Cpu*,const threads::QuiescenceWitness&);
public:Capture(Capture&&)=default;Capture&operator=(Capture&&)=default;
};
class Restore final {
 std::thread::id thread_=std::this_thread::get_id();
 S::Session*owner_;S::PristineRestore session_;std::shared_ptr<Group>group_;
 std::optional<O::RestorePermit>stage_;
 std::optional<Group::Participant>participant_;std::optional<O::Snapshot>saved_stage_;
 Restore(S::Session&,S::PristineRestore,std::shared_ptr<Group>,std::optional<O::RestorePermit>,std::optional<Group::Participant>,std::optional<O::Snapshot>);
 friend std::optional<Restore>prepare(S::Session&,const Checkpoint&,const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&,uint64_t,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>,std::shared_ptr<Group>);
 friend bool finish(Restore&&,const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&,std::unique_ptr<S::Session::Fence>*);
public:Restore(Restore&&)=default;Restore&operator=(Restore&&)=default;
};
// Caller retains the actual bootstrap/pristine SaveHold fence through commit.
std::optional<Capture>begin(S::Session&,Cpu*,const threads::QuiescenceWitness&);
std::optional<Checkpoint>commit(Capture&&,Cpu*,const threads::QuiescenceWitness&);
std::optional<Restore>prepare(S::Session&,const Checkpoint&,const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&,uint64_t,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>owner={},std::shared_ptr<Group>group={});
bool finish(Restore&&,const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&,std::unique_ptr<S::Session::Fence>*retain=nullptr);
}
