// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_backend.h"
#include "story_native_event_game_thread.h"
#include "story_native_stage_owner_observer.h"
#include "story_native_wait_readiness.h"
#include <optional>
#include "story_native_stage_binding.h"
namespace story_native_wait_session {class Session;}
namespace story_native_wait_runtime_model_graph {class Context;}
namespace story_native_wait_runtime_backend {
using Expected=story_native_event_observation::Expected;
using Config=story_native_event_backend::Config;
using Sources=story_native_event_backend::Sources;
// Synchronous host-only seed. Only the model rebind implementation constructs
// this after authentic Boundary, original-lineage and restored-byte checks.
// The backend consumes its data immediately and never retains its callback.
class RestoreClockSeed {
 Expected expected_;story_native_event_collect::Clock saved_;
 std::function<bool()>current_;
 RestoreClockSeed(Expected e,story_native_event_collect::Clock c,std::function<bool()>f)
  :expected_(std::move(e)),saved_(std::move(c)),current_(std::move(f)){}
 friend class story_native_wait_runtime_model_graph::Context;
public:
 RestoreClockSeed(const RestoreClockSeed&)=delete;
 RestoreClockSeed&operator=(const RestoreClockSeed&)=delete;
 const Expected&expected()const{return expected_;}
 const auto&saved()const{return saved_;}
 bool current()const{try{return current_&&current_();}catch(...){return false;}}
};
struct Callbacks {
 story_native_wait_session::Session* session=nullptr; // actual long-lived Session
 story_native_event_memory::Read read; // actual bounded raw reader, no Policy recursion
 story_native_event_collect::VerifyActor resident; // actual pure resident binding
 std::function<bool()> fence; // actual retained Session entry fence
 std::function<bool(const Expected&)> retained; // mandatory TURN; actual controller claim, independent of bytes
 std::function<bool()> routes; // exact controller/observer route ownership
 // Actual Session readback: staged_expected before seal, baseline after seal.
 std::function<bool(const Expected&)> baseline;
 std::function<bool(const Cpu*,story_native_wait_readiness::Boundary&)> boundary;
 story_native_event_memory::VerifyAllocation allocation; // extra exact loaded binding
};
struct Bundle;
class StageBindingFactory;
// Actual production factory handle. Copies of Bundle share one epoch cell;
// only the real runtime Context may attach/prepare a candidate. No mutable
// receipt setter or caller-provided generation/validator is exposed.
class StageBinding final {
 struct State;std::shared_ptr<State>state_;
 explicit StageBinding(std::shared_ptr<State>);
 friend class StageBindingFactory;
 friend class story_native_wait_runtime_controller::Context;
 friend class story_native_wait_runtime_model_graph::Context;
 bool attach(std::shared_ptr<story_native_stage_binding::Cell>,
  const threads::NativeQuiescence&,const story_native_stage_owner::Tracker::Scan&);
public:
 class Candidate {
  struct State;std::unique_ptr<State>state_;
  explicit Candidate(std::unique_ptr<State>);
  friend class StageBinding;friend class StageBindingFactory;
  friend class story_native_wait_runtime_controller::Context;
  friend class story_native_wait_runtime_model_graph::Context;
  const Bundle&bundle()const;
  const void*origin_binding()const;
  const story_native_stage_owner_observer::Snapshot&stage()const;
  std::shared_ptr<const threads::NativeQuiescence>quiescence()const;
 public:
  Candidate(const Candidate&)=delete;Candidate&operator=(const Candidate&)=delete;
  Candidate(Candidate&&)noexcept;Candidate&operator=(Candidate&&)noexcept;
  ~Candidate();
  bool current()const noexcept; // actual retained Q/Scan only, no admission
 };
 // Copied immutable factory provenance only, never current-epoch authority.
 bool matches_original(const story_native_stage_owner_observer::Snapshot&)const noexcept;
 std::optional<story_native_stage_owner_observer::Snapshot>current(
  const story_native_stage_owner::Tracker::Scan&)const noexcept;
private:
 std::optional<Candidate>candidate(std::shared_ptr<const threads::NativeQuiescence>,
  const story_native_stage_owner::Tracker::Scan&,
  story_native_stage_owner_observer::Snapshot)const;
};
struct Bundle {
 Config config;Sources sources;
 std::shared_ptr<StageBinding>stage_binding; // data source, no minting authority
 // TURN only. Invoke after genuine native ORDER and durable Published journal.
 // This arms a host elapsed clock, never publication/native-call authority.
 std::function<bool(const story_native_event_observation::Result&)> arm_clock;
 // Exact real owned CameraExecute frame only; private armed publication origin.
 std::function<bool(story_native_event_collect::Clock&)> active_clock;
 // TURN only, dormant rebind data. Does not publish or admit native entries.
 std::function<bool(const RestoreClockSeed&)> restore_clock={};
};
// Copies Expected/layout/config/Stage receipt/ticket and callbacks into shared
// persistent storage; returned callbacks never capture factory stack references.
// Session, Cpu and Tracker are the actual long-lived objects and MUST outlive
// every returned callback. The factory independently checks production thread,
// stack, allocator, native route owner and Stage observer/inspect_live facts.
// Supports Active and idle Draining Tracker phases for private controller use.
// No writer/native Invoke/publication or retained claim is supplied here. The
// consuming controller must separately retain its real claim and shared fences.
std::optional<Bundle> make(Expected,story_native_event_arena::Layout,Config,
 story_native_stage_owner_observer::Snapshot,story_native_event_game_thread::Ticket,
 Cpu*,story_native_event_depth::Tracker&,Callbacks);
}
