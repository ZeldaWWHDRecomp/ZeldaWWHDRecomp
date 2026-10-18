// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_model_saved_graph.h"
#include "native_quiescence.h"
#include "story_native_event_depth.h"
#include "story_native_event_collect.h"
#include <functional>
#include <atomic>
#include <memory>
namespace story_native_stage_owner_observer {struct Snapshot;}
namespace ss {class NativeRestorePipelineIssuer;}
namespace interp {class NativeResumeBoundaryIssuer;}
namespace story_native_wait_runtime_controller {class Context;}
namespace story_native_wait_session {class SavedBaseline;}
namespace story_native_scene_checkpoint {class Checkpoint;}
namespace story_native_wait_runtime_model_graph {class Context;}
namespace story_native_restore_resume {
namespace detail {class Access;}
class Boundary;
// Immutable host checkpoint provenance. Scope/record/clock are DATA; the
// original Context alone constructs it from real saved-boundary observations.
// WAIT explicitly has no TURN clock. An absent clock never becomes elapsed=0.
class SavedRunOrigin {
 story_native_event_observation::Expected expected_;
 story_native_event_depth::Record depth_;
 story_native_event_memory::Scope scope_;
 std::optional<story_native_event_collect::Clock>clock_;
 std::shared_ptr<const void>life_;
 std::shared_ptr<const story_native_wait_session::SavedBaseline>session_;
 std::shared_ptr<const story_native_scene_checkpoint::Checkpoint>frontend_;
 SavedRunOrigin(story_native_event_observation::Expected e,story_native_event_depth::Record d,
  story_native_event_memory::Scope s,std::optional<story_native_event_collect::Clock>c,
  std::shared_ptr<const void>life,std::shared_ptr<const story_native_wait_session::SavedBaseline>session={},std::shared_ptr<const story_native_scene_checkpoint::Checkpoint>frontend={}):expected_(std::move(e)),depth_(std::move(d)),scope_(std::move(s)),clock_(std::move(c)),life_(std::move(life)),session_(std::move(session)),frontend_(std::move(frontend)){}
 friend class story_native_wait_runtime_controller::Context;friend class Completion;
public:
 SavedRunOrigin(const SavedRunOrigin&)=default;SavedRunOrigin&operator=(const SavedRunOrigin&)=default;
 const auto&expected()const noexcept{return expected_;}
 const auto&session_baseline()const noexcept{return session_;}
 const auto&frontend_checkpoint()const noexcept{return frontend_;}
 const auto&depth_record()const noexcept{return depth_;}
 const auto&scope()const noexcept{return scope_;}
 const auto&clock()const noexcept{return clock_;}
 bool same_run(const SavedRunOrigin&o)const{return life_&&life_==o.life_&&expected_==o.expected_;}
};
// Independent CURRENT-owner revocation. SavedRunOrigin continues to identify
// the immutable SAVED run. Only the actual Context load transaction can mint
// this host proof, after revoking its independently authenticated live owner.
class CurrentOwnerRevocation final {
 story_native_wait_runtime_controller::Context*owner_;
 std::shared_ptr<const void>life_;
 story_native_event_observation::Expected expected_;
 uint64_t epoch_;
 std::atomic<story_native_wait_runtime_controller::Context*>*context_slot_=nullptr;
 std::atomic<story_native_event_depth::Tracker*>*depth_slot_=nullptr;
 story_native_event_depth::Tracker*prior_depth_=nullptr;
 CurrentOwnerRevocation(story_native_wait_runtime_controller::Context*owner,
  std::shared_ptr<const void>life,story_native_event_observation::Expected expected,uint64_t epoch,
  std::atomic<story_native_wait_runtime_controller::Context*>*context_slot=nullptr,
  std::atomic<story_native_event_depth::Tracker*>*depth_slot=nullptr,
  story_native_event_depth::Tracker*prior_depth=nullptr)
  :owner_(owner),life_(std::move(life)),expected_(std::move(expected)),epoch_(epoch),context_slot_(context_slot),depth_slot_(depth_slot),prior_depth_(prior_depth){}
 friend class story_native_wait_runtime_controller::Context;
};
// Restored bytes and original host provenance only. This is NOT fresh model,
// tracker, lease, clock, route, or native invocation/resume authority.
class Completion {
 struct State;std::shared_ptr<State>state_;
 explicit Completion(std::shared_ptr<State>);
 static std::optional<Completion>make(story_native_event_observation::Expected,
  std::shared_ptr<story_native_wait_runtime_model_graph::Context>,story_native_model_saved_graph::Receipt&&,SavedRunOrigin,
  uint64_t restore_epoch,Cpu*,const threads::QuiescenceWitness&,
  std::function<void(Cpu*,const Boundary&)> observer={},
  const story_native_stage_owner_observer::Snapshot*finished_stage=nullptr,
  std::shared_ptr<const CurrentOwnerRevocation>current_owner={});
 bool publish();
 friend class story_native_wait_runtime_controller::Context;friend class detail::Access;
public:
 Completion(const Completion&)=delete;Completion&operator=(const Completion&)=delete;
 Completion(Completion&&)noexcept=default;Completion&operator=(Completion&&)noexcept=default;
 const story_native_event_observation::Expected&expected()const;
 const story_native_model_saved_graph::Receipt&saved_graph()const;
 std::shared_ptr<story_native_wait_runtime_model_graph::Context>original_model()const;
 uint64_t restore_epoch()const noexcept;
 const SavedRunOrigin&origin()const;
 const std::shared_ptr<const CurrentOwnerRevocation>&current_owner()const;
 // Exact receipt returned by genuine finish_restore; data only until fresh
 // physical parking + actual Stage scan authenticate it again.
 const story_native_stage_owner_observer::Snapshot*stage()const noexcept;
};
class Boundary {
 const Completion*completion_;Cpu*cpu_;uint32_t guest_;uint64_t epoch_;
 std::function<bool(const Cpu*)>current_;
 Boundary(const Completion&,Cpu*,uint32_t,uint64_t);
 friend class detail::Access;
public:
 Boundary(const Boundary&)=delete;Boundary&operator=(const Boundary&)=delete;
 bool current(const Cpu*c)const noexcept{try{return current_&&current_(c);}catch(...){return false;}}
 uint64_t load_epoch()const noexcept{return epoch_;}
 const Completion&completion()const{return *completion_;}
};
// Only actual savestate.cpp can open/advance the load pipeline; only the real
// interp frame hook can issue its post-service boundary. Ordinary gate booleans
// or a copied Completion cannot manufacture either boundary.
class LoadWindow {
 struct State;std::shared_ptr<State>state_;
 explicit LoadWindow(Cpu*);
 void threads_loaded();void after_thaw();
 static void service_return(Cpu*)noexcept;
 friend class ss::NativeRestorePipelineIssuer;
 friend class interp::NativeResumeBoundaryIssuer;
 friend class detail::Access;
public:
 LoadWindow(const LoadWindow&)=delete;LoadWindow&operator=(const LoadWindow&)=delete;
 ~LoadWindow();
};
}
