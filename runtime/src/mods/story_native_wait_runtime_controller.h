// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_controller.h"
#include "story_native_wait_runtime_backend.h"
#include "story_native_wait_owned_routes.h"
#include "story_native_scene_completion.h"
namespace story_native_wait_runtime_model_graph {class Context;}
namespace story_native_restore_resume {class Boundary;class CurrentLoadOwner;}
namespace story_native_restore_front {class Front;}
namespace story_native_stage_restore_group {class StageRestoreGroup;}
namespace story_native_scene_playback {class RunOwnerCell;}
namespace story_native_scene_checkpoint {class Checkpoint;class CurrentRunRevocation;class PreparedRestore;class PreparedRetire;}
namespace ss::native_gate {class FinishedLoad;}
namespace story_native_wait_runtime {class NativeScenePlaybackIssuer;}
namespace story_native_wait_runtime_controller {
using Expected=story_native_event_observation::Expected;
using Layout=story_native_event_arena::Layout;
using Stage=story_native_stage_owner_observer::Snapshot;
using Ticket=story_native_event_game_thread::Ticket;
struct Sources {
 std::shared_ptr<const story_native_scene_playback::Activation>activation;
 // Actual preclaim native graph baseline; never prepared after TURN begins.
 std::shared_ptr<story_native_wait_runtime_model_graph::Context> model_graph;
 // Pure bounded guest reads while globally parked after the old ticket is
 // revoked. Used only under an opaque restore permit, never entry authority.
 story_native_event_memory::Read restore_read;
 story_native_event_memory::Write write;
 story_native_event_invoke::Call call; // actual guest_call; fixed capabilities wrap it
 std::function<bool(Cpu*,story_native_event_invoke::Config&)> stack;
 std::function<bool()> routes; // exact current original wrappers/outer owners
 // Consumed synchronously at the genuine post-service boundary; never retain its reference.
 std::function<void(Cpu*,const story_native_restore_resume::Boundary&)>restored_boundary;
 // Actual StoryScenes issuer returns an opaque preallocated Player swap;
 // returning true or a new Activation cannot replace this proof.
 std::function<std::shared_ptr<story_native_scene_checkpoint::PreparedRestore>(
  const story_native_scene_checkpoint::Checkpoint&,
  story_native_scene_checkpoint::CurrentRunRevocation&&)>prepare_frontend_restore;
};
enum class State {Ready,Started,Running,AwaitCamera,Released,Refused,Quarantined,RestoredDormant};
class Context;class SavedRunCheckpoint;
// Host-only capture lineage. Pending captures retain a real Session fence;
// all moves, completion and destruction must stay on the issuing host thread.
class SavedRunReceipt {
 struct Capture;std::shared_ptr<Capture>capture_;
 explicit SavedRunReceipt(std::shared_ptr<Capture>);
 friend class Context;friend class SavedRunCheckpoint;
public:
 SavedRunReceipt(const SavedRunReceipt&)=delete;SavedRunReceipt&operator=(const SavedRunReceipt&)=delete;
 SavedRunReceipt(SavedRunReceipt&&)noexcept;SavedRunReceipt&operator=(SavedRunReceipt&&)noexcept;
 ~SavedRunReceipt();
};
// Immutable committed host capture. Comparing/forking this object never grants
// a current native claim; every load attempt still needs authentic admission.
class SavedRunCheckpoint {
 std::shared_ptr<const SavedRunReceipt::Capture>capture_;
 explicit SavedRunCheckpoint(std::shared_ptr<const SavedRunReceipt::Capture>);
 friend class Context;
public:
 bool matches_saved(const ss::native_gate::SavedView&)const;
 std::optional<SavedRunReceipt>fork_attempt()const;
};
// Stage + exact saved owned-consumer continuation only. This token cannot
// attach a Controller, authorize native entries, publish, or permit thaw.
class RunRestorePermit {
 struct Pending;std::unique_ptr<Pending>pending_;
 explicit RunRestorePermit(std::unique_ptr<Pending>);
 friend class Context;
public:
 RunRestorePermit(const RunRestorePermit&)=delete;RunRestorePermit&operator=(const RunRestorePermit&)=delete;
 RunRestorePermit(RunRestorePermit&&)noexcept;RunRestorePermit&operator=(RunRestorePermit&&)noexcept;
 ~RunRestorePermit();
};
// This context must outlive every retained native claim and hook callback.
// prepare constructs helpers only; tick's default never starts publication.
class Context {
 struct Impl;std::unique_ptr<Impl>impl_;
 bool clean_cancel_routes_retired(const Cpu*)const;
 explicit Context(std::unique_ptr<Impl>);
 friend class story_native_wait_runtime::NativeScenePlaybackIssuer;
 std::shared_ptr<const void>run_identity()const;
 bool attach_run_owner(std::shared_ptr<story_native_scene_playback::RunOwnerCell>);
public:
 ~Context();Context(const Context&)=delete;Context&operator=(const Context&)=delete;
 static std::unique_ptr<Context> prepare(story_native_wait_session::Session&,
  story_native_event_depth::Tracker&,Expected,Layout,Stage,Ticket,Cpu*,
  story_native_wait_runtime_backend::Bundle,
  std::array<story_native_people_admission::Reference,2>,Sources);
 // Data-only restored adoption. The returned context retains a dormant
 // claim; it cannot dispatch, write, invoke, or activate the restored tracker.
 static std::unique_ptr<Context> prepare_restored(const story_native_restore_resume::Boundary&,
  story_native_wait_session::Session&,story_native_event_depth::Tracker&,Stage,Ticket,Cpu*,
  story_native_wait_runtime_backend::Bundle,
  std::array<story_native_people_admission::Reference,2>,Sources);
 using RestoredBackendFactory=std::function<std::optional<story_native_wait_runtime_backend::Bundle>(const Expected&,const Layout&)>;
 // Production ordering: invoke the actual pending-source factory only AFTER
 // authentic historical Session caches have been adopted under physical Q.
 static std::unique_ptr<Context> prepare_restored(const story_native_restore_resume::Boundary&,
  story_native_wait_session::Session&,story_native_event_depth::Tracker&,Stage,Ticket,Cpu*,
  RestoredBackendFactory,std::array<story_native_people_admission::Reference,2>,Sources);
 static constexpr bool publication_enabled(){return false;}
 // Final host-only transaction; actual front slots and every private prepared
 // participant must validate under a fresh normal physical quiescence window.
 bool resume_restored(const story_native_restore_resume::Boundary&,story_native_restore_front::Front&&);
 // Fresh idle source/model validation followed by one private Stage-cell CAS.
 bool stage_renewal_needed()const;
 bool renew_stage(Cpu*);
 bool retained()const noexcept;
 State state()const noexcept;
 story_native_wait_owned_routes::Decision route(Cpu*,uint32_t,const std::function<void()>&);
 bool outer(const Cpu*,uint32_t,story_native_event_depth::Call&)noexcept;
 void outer_return(Cpu*,const story_native_outer_return::OuterReturn&) noexcept;
 State tick(Cpu*,bool allow_start=false);
 // Consumes authentic normal release once; callers create a NEW Context/Tracker.
 bool rearm(Cpu*);
 std::optional<story_native_scene_completion::Completion>take_completion();
 // Dormant save/load prerequisites; the production Gate remains denied.
 // Actual TLS Cpu/ticket are sampled internally, never taken from SavedView.
 std::optional<SavedRunReceipt> begin_saved_run();
 bool commit_saved_run(SavedRunReceipt&,std::unique_ptr<story_native_wait_session::Session::Fence>*retain_fence=nullptr);
 std::optional<SavedRunCheckpoint>checkpoint_saved_run(SavedRunReceipt&);
 // Coordinator issues this BEFORE any aggregate child prepares/restores.
 static std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>prepare_current_load(
  story_native_restore_front::Front&&,const ss::native_gate::SavedView&,uint64_t host_restore_epoch);
 using RetireFrontend=std::function<std::shared_ptr<story_native_scene_checkpoint::PreparedRetire>(story_native_scene_checkpoint::CurrentRunRevocation&&)>;
 static bool finish_empty_load(const ss::native_gate::FinishedLoad&,
  story_native_restore_front::Front&&,RetireFrontend)noexcept;
 std::optional<RunRestorePermit> prepare_run_restore(SavedRunReceipt&&,
  const ss::native_gate::SavedView&,uint64_t host_restore_epoch);
 // current_owner must come from the actual runtime front, independently of
 // the registry entry issuing the saved checkpoint. Same Session only.
 std::optional<RunRestorePermit> prepare_run_restore(SavedRunReceipt&&,
  const ss::native_gate::SavedView&,uint64_t host_restore_epoch,Context&current_owner,
  std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>group={});
 std::optional<RunRestorePermit> prepare_run_restore(SavedRunReceipt&&,
  const ss::native_gate::SavedView&,uint64_t host_restore_epoch,story_native_restore_front::Front&&,
  std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>group={},
  std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>current_owner={});
 // Must be called before the first restored guest byte. Revokes this old
 // Context host-only; its owned outer/route front remains denied thereafter.
 bool suspend_run_restore(RunRestorePermit&);
 bool finish_run_restore(RunRestorePermit&&,std::unique_ptr<story_native_wait_session::Session::Fence>*retain_fence=nullptr);
 bool suspended_for_restore(const story_native_restore_resume::Boundary&)const noexcept;
 static bool restore_front_current(const story_native_restore_resume::Boundary&,const Context*observed_owner)noexcept;
 void request_cancel()noexcept; // host queue only; no guest/native effects
 void invalidate()noexcept;
};
}
