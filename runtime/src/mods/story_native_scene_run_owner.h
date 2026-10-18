// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_scene_playback.h"
#include <thread>
namespace story_native_wait_runtime_controller {class Context;}
namespace story_native_scene_playback {
// Host callback indirection only. Neither an endpoint nor this cell authorizes
// a native entry. Production replacement remains dormant until Context can
// atomically admit its restored front under the genuine final resume proof.
class RunOwnerCell final {
public:
 struct Endpoint {
  std::function<Progress(Cpu*)>poll;
  std::function<void(story_scenes::StopReason)>queue_stop;
 };
private:
 struct State;
 std::shared_ptr<State>state_;
 RunOwnerCell(std::shared_ptr<const Activation>,std::shared_ptr<const void> original_run_life,Endpoint);
 friend class story_native_wait_runtime::NativeScenePlaybackIssuer;
 friend class story_native_wait_runtime_controller::Context;
public:
 class ResumeCertificate final {
  std::shared_ptr<State>owner_;std::shared_ptr<const Activation>activation_;
  std::shared_ptr<const void>original_run_life_;
  uint64_t version_=0;Endpoint endpoint_;bool consumed_=false;
  ResumeCertificate(RunOwnerCell&,std::shared_ptr<const Activation>,std::shared_ptr<const void> original_run_life,Endpoint);
  friend class story_native_wait_runtime_controller::Context;
  friend class RunOwnerCell;
 public:
  ResumeCertificate(const ResumeCertificate&)=delete;
  ResumeCertificate&operator=(const ResumeCertificate&)=delete;
  ResumeCertificate(ResumeCertificate&&)noexcept=default;
  ResumeCertificate&operator=(ResumeCertificate&&)noexcept=default;
 };
public:
 class PreparedResume final {
  std::shared_ptr<State>owner_;Endpoint endpoint_;std::thread::id thread_;
  bool committed_=false;
  PreparedResume(std::shared_ptr<State>,Endpoint);
  void release()noexcept;
  // No validation/callbacks after the final Tracker lock is acquired.
  void commit_unchecked()noexcept;
  friend class RunOwnerCell;friend class story_native_wait_runtime_controller::Context;
 public:
  PreparedResume(const PreparedResume&)=delete;PreparedResume&operator=(const PreparedResume&)=delete;
  PreparedResume(PreparedResume&&)noexcept;PreparedResume&operator=(PreparedResume&&)noexcept;
  ~PreparedResume();
 };
private:
 friend class Player;friend class story_scenes::NativeCheckpointIssuer;
 std::optional<uint64_t>frontend_binding(const Activation&)noexcept;
 std::shared_ptr<const void>checkpoint_life(const Activation&,uint64_t generation)noexcept;
 bool bind_frontend(const Activation&,uint64_t generation)noexcept;
 void record_load(const Activation&,uint64_t prior_generation,uint64_t new_generation)noexcept;
 bool consume_resume(const Activation&,uint64_t prior_generation,uint64_t new_generation)noexcept;
 std::optional<PreparedResume>prepare_resume(ResumeCertificate&&)noexcept;
 // Actual Context alone may join this host swap to its physical-Q protected
 // final Context/depth/model/Tracker admission. RestoredDormant and copied
 // Completion/Origin data must never issue a certificate or call this method.
 bool commit(ResumeCertificate&&)noexcept;
public:
 ~RunOwnerCell();
 RunOwnerCell(const RunOwnerCell&)=delete;RunOwnerCell&operator=(const RunOwnerCell&)=delete;
 const std::shared_ptr<const Activation>&activation()const noexcept;
 Progress poll(Cpu*);
 // Host-only/thread-safe; a busy callback retains the stop for the next poll.
 void queue_stop(story_scenes::StopReason);
};
}
