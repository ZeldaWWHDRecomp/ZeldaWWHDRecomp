// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_stage_owner_observer.h"
#include "native_quiescence.h"
#include <memory>
namespace story_native_stage_restore_group {
// Per-aggregate-load rendezvous only. Creation conveys no Stage authority;
// joining requires the real Observer's saved/live proof and actual park.
class StageRestoreGroup final {
 struct State;std::shared_ptr<State>state_;
 StageRestoreGroup();
 void close_impl()noexcept;void(*close_)(StageRestoreGroup&)noexcept=nullptr;
public:
 class Participant final {
  std::shared_ptr<State>state_;bool consumed_=false;
  explicit Participant(std::shared_ptr<State>);friend class StageRestoreGroup;
 public:
  Participant(const Participant&)=delete;Participant&operator=(const Participant&)=delete;
  Participant(Participant&&)noexcept;Participant&operator=(Participant&&)noexcept;
  ~Participant();
  std::optional<story_native_stage_owner_observer::Snapshot>finish(
   const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&);
  // Overall child failure must revoke the shared proof even if Stage finish
  // succeeded before another child predicate failed.
  void invalidate()noexcept;
 };
 StageRestoreGroup(const StageRestoreGroup&)=delete;StageRestoreGroup&operator=(const StageRestoreGroup&)=delete;
 static std::shared_ptr<StageRestoreGroup>create();
 ~StageRestoreGroup();
 std::optional<Participant>join(const story_native_stage_owner_observer::Snapshot&,
  const ss::native_gate::SavedView&,Cpu*,const threads::QuiescenceWitness&);
 // Coordinator calls this BEFORE releasing child Session fences. It releases
 // all retained Stage resources explicitly, regardless of shared_ptr order.
 void close()noexcept{if(close_)close_(*this);}
};
}
