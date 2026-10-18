// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_save_gate.h"
#include "story_native_wait_runtime_controller.h"
#include "story_native_wait_idle_snapshot.h"
#include "story_native_wait_pristine_snapshot.h"
#include "story_native_restore_front.h"
#include <functional>
#include <thread>
#include <vector>
namespace story_native_wait_active_save_gate {
// Stable aggregate child for one actual Session. Host checkpoints retain their
// original issuing Context; snapshot bytes cannot manufacture that provenance.
class Controller final:public ss::native_gate::Controller {
 using Context=story_native_wait_runtime_controller::Context;
 using Receipt=story_native_wait_runtime_controller::SavedRunReceipt;
 using Checkpoint=story_native_wait_runtime_controller::SavedRunCheckpoint;
 using Permit=story_native_wait_runtime_controller::RunRestorePermit;
 struct Entry {Context*issuer;Checkpoint checkpoint;};
 enum class Mode {Idle,BootstrapSave,BootstrapLoad,ActiveSave,ActiveLoad,PristineSave,PristineLoad,IdleSave,IdleLoad,Progress};
 story_native_wait_session::Session&session_;
 story_native_wait_save_gate::Controller bootstrap_;
 std::function<Context*()>current_;
 std::function<story_native_restore_front::Front()>load_front_;
 std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>load_group_;
 std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>load_owner_;
 std::optional<story_native_stage_restore_group::StageRestoreGroup::Participant>initial_stage_;
 std::optional<story_native_stage_owner_observer::Snapshot>initial_snapshot_;
 std::vector<Entry>checkpoints_;
 std::optional<story_native_wait_pristine_snapshot::Capture>pristine_capture_;
 std::vector<story_native_wait_pristine_snapshot::Checkpoint>pristine_checkpoints_;
 std::optional<story_native_wait_pristine_snapshot::Restore>pristine_restore_;
 const story_native_wait_pristine_snapshot::Checkpoint*selected_pristine_=nullptr;
 std::vector<story_native_wait_idle_snapshot::Checkpoint>idle_checkpoints_;
 std::optional<story_native_wait_idle_snapshot::Capture>idle_capture_;
 std::optional<story_native_wait_idle_snapshot::Restore>idle_restore_;
 const story_native_wait_idle_snapshot::Checkpoint*selected_idle_=nullptr;
 Mode mode_=Mode::Idle;std::thread::id thread_;
 Context*issuer_=nullptr;
 std::optional<Receipt>capture_;
 std::optional<Permit>permit_;
 bool finished_=false;
 const Checkpoint*selected_=nullptr;
 struct Guard;Guard*held_guard_=nullptr;
 struct Guard final:ss::native_gate::Hold {
  Controller&owner;std::unique_ptr<ss::native_gate::Hold>bootstrap;
  std::unique_ptr<story_native_wait_session::Session::Fence>fence;
  explicit Guard(Controller&o):owner(o){}
  ~Guard()override;
 };
 bool held(Mode)const noexcept;
 void release()noexcept;
public:
 Controller(story_native_wait_session::Session&s,std::function<Context*()>current,
  std::function<story_native_restore_front::Front()>load_front={})
  :session_(s),bootstrap_(s),current_(std::move(current)),load_front_(std::move(load_front)){}
 std::unique_ptr<ss::native_gate::Hold>begin_save()override;
 bool finish_save()override;
 std::unique_ptr<ss::native_gate::Hold>begin_load(const ss::native_gate::SavedView&)override;
 std::unique_ptr<ss::native_gate::Hold>begin_group_load(const ss::native_gate::SavedView&,
  std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>,
  std::shared_ptr<story_native_restore_resume::CurrentLoadOwner> = {})override;
 bool finish_load(const ss::native_gate::SavedView&)override;
 std::unique_ptr<ss::native_gate::Hold>begin_progress_load()override;
};
}
