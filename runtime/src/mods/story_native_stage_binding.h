// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_stage_owner_observer.h"
#include "native_quiescence.h"
#include <atomic>
#include <memory>
#include <functional>
namespace story_native_wait_runtime_controller {class Context;}
namespace story_native_stage_binding {
// Dormant host-only epoch publication. No native call, guest write, event
// ownership, restored-run admission or permission to bypass a failed source.
class Cell final : public std::enable_shared_from_this<Cell> {
 struct Version;
 using Snapshot=story_native_stage_owner_observer::Snapshot;
 using Scan=story_native_stage_owner::Tracker::Scan;
 using Ticket=story_native_event_game_thread::Ticket;
 std::shared_ptr<const Version>version_;
 story_native_stage_owner::Tracker*tracker_{};
 const Cpu*cpu_{};Ticket ticket_{};std::thread::id thread_;
 // Opaque original host run identity, retained independently of guest bytes.
 std::shared_ptr<const void>lineage_;
 Cell(const Scan&,Snapshot,const Cpu*,Ticket,std::shared_ptr<const void>);
 bool owned()const noexcept;
 static bool parked(const std::shared_ptr<const threads::NativeQuiescence>&,const Cpu*)noexcept;
 friend class story_native_wait_runtime_controller::Context;
public:
 class Validation {
  friend class Cell;
  friend class story_native_wait_runtime_controller::Context;
  std::shared_ptr<Cell>cell_;std::shared_ptr<const Version>before_;
  Snapshot next_;std::shared_ptr<const threads::NativeQuiescence>park_;
  std::function<bool()>actual_source_;bool consumed_=false;
  Validation(std::shared_ptr<Cell>,Snapshot,std::shared_ptr<const threads::NativeQuiescence>,std::function<bool()>);
 public:
  Validation(const Validation&)=delete;Validation&operator=(const Validation&)=delete;
  Validation(Validation&&)=default;Validation&operator=(Validation&&)=default;
 };
 class Certificate {
  friend class Cell;
  std::shared_ptr<Cell>cell_;std::shared_ptr<const Version>before_,after_;
  std::shared_ptr<const threads::NativeQuiescence>park_;std::function<bool()>actual_source_;
  bool consumed_=false;
  Certificate(Validation&&,std::shared_ptr<const Version>);
 public:
  Certificate(const Certificate&)=delete;Certificate&operator=(const Certificate&)=delete;
  Certificate(Certificate&&)=default;Certificate&operator=(Certificate&&)=default;
 };
 // Data-only lookup. A receipt copied from another owner/epoch cannot match.
 std::optional<Snapshot>current(const Scan&)const noexcept;
 // All issuance remains private to the actual runtime Context. Its fresh
 // source callback must bind full Expected, actual retained claim, Session
 // fence/journal, idle Tracker/boundary and native consumer/model proofs.
private:
 static std::shared_ptr<Cell>create(const Scan&,Snapshot,const Cpu*,Ticket,
  std::shared_ptr<const void>,std::shared_ptr<const threads::NativeQuiescence>);
 static std::optional<Certificate>stage(Validation&&,const Scan&);
 bool commit(Certificate&&,const Scan&)noexcept;
};
}
