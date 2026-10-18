// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_scene_activation.h"
#include <optional>
#include <atomic>
#include <thread>
namespace story_scenes {class NativeCheckpointIssuer;}
namespace story_native_wait_runtime_controller {class Context;}
namespace story_native_restore_resume {class CurrentOwnerRevocation;}
namespace story_native_scene_playback {class RunOwnerCell;class Player;}
namespace story_native_scene_checkpoint {
// Separate current-owner provenance, never inferred from Quarantined/state.
// Actual Context issues it only after the coordinator supplies authentic
// current-vs-saved suspension/claim transfer under the genuine load epoch.
class CurrentRunRevocation final {
 story_native_wait_runtime_controller::Context*context_=nullptr;
 std::shared_ptr<const void>life_;std::shared_ptr<story_native_scene_playback::RunOwnerCell>current_cell_;
 std::shared_ptr<const story_native_restore_resume::CurrentOwnerRevocation>current_owner_;
 uint64_t load_epoch_=0;bool consumed_=false;std::thread::id thread_=std::this_thread::get_id();
 CurrentRunRevocation(story_native_wait_runtime_controller::Context*c,std::shared_ptr<const void>life,uint64_t epoch,std::shared_ptr<story_native_scene_playback::RunOwnerCell>cell={},std::shared_ptr<const story_native_restore_resume::CurrentOwnerRevocation>proof={}):context_(c),life_(std::move(life)),current_cell_(std::move(cell)),current_owner_(std::move(proof)),load_epoch_(epoch){}
 friend class story_native_wait_runtime_controller::Context;friend class story_scenes::NativeCheckpointIssuer;
public:
 CurrentRunRevocation(const CurrentRunRevocation&)=delete;CurrentRunRevocation&operator=(const CurrentRunRevocation&)=delete;
 CurrentRunRevocation(CurrentRunRevocation&&)noexcept=default;CurrentRunRevocation&operator=(CurrentRunRevocation&&)noexcept=default;
};
// Genuine host frontend provenance only. Retaining a checkpoint does not
// resume a Player, revoke a different owner, or authorize any native entry.
class Checkpoint final {
 struct State;std::shared_ptr<const State>state_;
 Checkpoint(std::shared_ptr<const story_native_scene_playback::Activation>,
  std::shared_ptr<story_native_scene_playback::RunOwnerCell>,std::shared_ptr<const void>,
  std::string,story_notebook::ManifestScene,size_t,uint64_t);
 friend class story_scenes::NativeCheckpointIssuer;
public:
 const std::shared_ptr<const story_native_scene_playback::Activation>&activation()const noexcept;
 const std::shared_ptr<story_native_scene_playback::RunOwnerCell>&cell()const noexcept;
 const std::shared_ptr<const void>&original_run_life()const noexcept;
 const std::string&mod()const noexcept;
 const story_notebook::ManifestScene&definition()const noexcept;
 size_t scene_index()const noexcept;
 uint64_t generation()const noexcept;
 // Fresh immutable registry check only; no live process/Player authority.
 bool definition_current()const noexcept;
};
// Prepared host frontend participant. The actual Context final transaction
// retains this reservation BEFORE cell/Tracker tokens, then calls only its
// private no-fail host swap. This participant grants no native admission.
class PreparedRestore final {
 story_native_scene_playback::Player*target_=nullptr;
 std::unique_ptr<story_native_scene_playback::Player>replacement_;
 size_t*current_=nullptr;size_t index_=0;
 std::atomic<bool>*input_=nullptr,*reserved_=nullptr;
 std::thread::id thread_;bool committed_=false;
 PreparedRestore(story_native_scene_playback::Player&,std::unique_ptr<story_native_scene_playback::Player>,size_t&,size_t,std::atomic<bool>&,std::atomic<bool>&);
 void release()noexcept;void commit_impl_unchecked()noexcept;
 void (*commit_)(PreparedRestore&)noexcept=nullptr;
 // Issuer-installed host-only trampoline keeps native Context primitive links
 // independent of the actual Player protocol implementation.
 void commit_unchecked()noexcept{if(!commit_)std::terminate();commit_(*this);}
 friend class story_scenes::NativeCheckpointIssuer;friend class story_native_wait_runtime_controller::Context;
public:
 PreparedRestore(const PreparedRestore&)=delete;PreparedRestore&operator=(const PreparedRestore&)=delete;
 PreparedRestore(PreparedRestore&&)noexcept;PreparedRestore&operator=(PreparedRestore&&)noexcept;
 ~PreparedRestore();
};

}
namespace story_scenes {
std::optional<story_native_scene_checkpoint::Checkpoint>capture_native_checkpoint(
 const story_native_scene_playback::Activation&,
 const std::shared_ptr<story_native_scene_playback::RunOwnerCell>&)noexcept;
std::optional<story_native_scene_checkpoint::PreparedRestore>prepare_native_restore(
 const story_native_scene_checkpoint::Checkpoint&,story_native_scene_checkpoint::CurrentRunRevocation&&)noexcept;
}
