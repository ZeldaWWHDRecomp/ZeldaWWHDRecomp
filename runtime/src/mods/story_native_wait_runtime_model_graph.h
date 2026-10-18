// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_people_model_graph.h"
#include "story_native_wait_runtime_backend.h"
#include "story_native_wait_session.h"
namespace story_native_restore_resume {class Boundary;}
namespace story_native_wait_runtime_model_graph {
namespace G=story_native_people_model_graph;
class Context;
// Read-only original-model inspection at one genuine pending CameraRun.
// Contains no parking/pin or refresh/native-call authority.
class PendingCameraInspection {
 std::function<bool()>current_,ready_;
 explicit PendingCameraInspection(std::function<bool()>f,std::function<bool()>r):current_(std::move(f)),ready_(std::move(r)){}
 friend class Context;
public:
 PendingCameraInspection(const PendingCameraInspection&)=delete;
 PendingCameraInspection&operator=(const PendingCameraInspection&)=delete;
 PendingCameraInspection(PendingCameraInspection&&)noexcept=default;
 PendingCameraInspection&operator=(PendingCameraInspection&&)noexcept=default;
 bool current()const noexcept;
 bool refresh_ready()const noexcept;
};
class Binding {
 struct State;
 std::shared_ptr<State>state_;
 explicit Binding(std::shared_ptr<State>);
 friend class Context;
public:
 Binding(const Binding&)=delete;Binding&operator=(const Binding&)=delete;
 Binding(Binding&&)noexcept=default;Binding&operator=(Binding&&)noexcept=default;
 ~Binding();
 bool current()const;
 std::shared_ptr<const threads::NativeQuiescence>quiescence()const;
};
// Retains only immutable native graph baselines between calls. Every actual
// observation requires a new physical park, Session fence and nonblocking Stage
// pin. These objects must not outlive their actual Session/Cpu/Tracker.
class Context {
 struct Impl;std::shared_ptr<Impl>impl_;
 explicit Context(std::shared_ptr<Impl>);
 std::optional<Binding>bind_impl(std::shared_ptr<const threads::NativeQuiescence>,
  std::optional<story_native_event_depth::FrameWitness>,std::string&);
public:
 class PreparedRestoreOpen {
  std::shared_ptr<Impl>impl_;std::shared_ptr<Binding::State>binding_;bool consumed_=false;
  PreparedRestoreOpen(std::shared_ptr<Impl>,std::shared_ptr<Binding::State>);
  friend class Context;friend class story_native_wait_runtime_controller::Context;
  void open_unchecked()noexcept;
 public:
  PreparedRestoreOpen(const PreparedRestoreOpen&)=delete;PreparedRestoreOpen&operator=(const PreparedRestoreOpen&)=delete;
  PreparedRestoreOpen(PreparedRestoreOpen&&)noexcept=default;
  PreparedRestoreOpen&operator=(PreparedRestoreOpen&&)=delete;
  ~PreparedRestoreOpen();
 };
private:
 friend class story_native_wait_runtime_controller::Context;
 // Capture-only candidate inspection. Copies ORIGINAL graph baselines and
 // lineage into temporary Q/Scan-bound sources; never returns an invoker or
 // a ModelContext capable of native refresh, and never mutates stable sources.
 bool validate_stage_candidate(const story_native_wait_runtime_backend::StageBinding::Candidate&,
  std::string&why)const;
 std::optional<PreparedRestoreOpen>prepare_restore_open(const story_native_restore_resume::Boundary&,
  const Binding&,const story_native_event_observation::Result&,std::string&why);
public:
 static std::shared_ptr<Context>prepare(story_native_wait_session::Session&,
  story_native_event_depth::Tracker&,G::Expected,
  story_native_stage_owner_observer::Snapshot,story_native_event_game_thread::Ticket,
  Cpu*,story_native_wait_runtime_backend::Bundle,std::string&why);
 // Restored data inspection only. Copies the ORIGINAL preclaim baseline and
 // lineage into fresh live sources under an authentic post-service boundary.
 // The returned context remains unable to authorize native model refresh.
 std::shared_ptr<Context>rebind_saved(const story_native_restore_resume::Boundary&,
  story_native_wait_session::Session&,story_native_event_depth::Tracker&,
  story_native_stage_owner_observer::Snapshot,story_native_event_game_thread::Ticket,
  Cpu*,story_native_wait_runtime_backend::Bundle,std::string&why)const;
 // Restore the host elapsed offset from the same authentic saved origin.
 // Requires the dormant rebound context; native refresh stays denied.
 bool restore_clock(const story_native_restore_resume::Boundary&,std::string&why);
 std::optional<Binding>bind(std::string&why);
 std::optional<Binding>bind(std::shared_ptr<const threads::NativeQuiescence>,std::string&why);
 std::optional<PendingCameraInspection>inspect_pending_camera(
  const story_native_event_depth::FrameWitness&,std::string&why);
 // Data-only saved graph/scratch comparison. A receipt carries the original
 // host preclaim lineage; SavedView never supplies a baseline or entry proof.
 std::optional<story_native_model_saved_graph::Receipt>capture_saved(const Binding&)const;
 bool matches_saved(const story_native_model_saved_graph::Receipt&,const ss::native_gate::SavedView&)const;
 bool matches_live(const Binding&,const story_native_model_saved_graph::Receipt&)const;
 std::optional<G::Capture>baseline(unsigned)const;
 bool capture(const Binding&,unsigned,G::Capture&)const;
 bool verify_model(const Binding&,const G::Expected&,unsigned,
  const G::R::ModelIdentity&,const G::R::Transform&)const;
 // This is data about a completed preclaim observation, not call authority.
 bool prepared()const;
};
}
