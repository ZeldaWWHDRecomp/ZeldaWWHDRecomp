// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_scene_checkpoint.h"
namespace story_native_scene_checkpoint {
// Preallocated host-only removal of the OLD native frontend after an exact
// aggregate load with no saved active SDK scene. This cannot issue completion,
// rewards, a new native entry, or restored model/Tracker authority.
class PreparedRetire final {
 story_native_scene_playback::Player*target_=nullptr;
 std::unique_ptr<story_native_scene_playback::Player>replacement_;
 std::atomic<bool>*input_=nullptr,*reserved_=nullptr;
 std::thread::id thread_;bool committed_=false;
 void(*commit_)(PreparedRetire&)noexcept=nullptr;
 PreparedRetire(story_native_scene_playback::Player&,std::unique_ptr<story_native_scene_playback::Player>,std::atomic<bool>&,std::atomic<bool>&);
 void release()noexcept;void commit_impl_unchecked()noexcept;
 bool current()const noexcept{return thread_==std::this_thread::get_id()&&target_&&replacement_&&input_&&reserved_&&!committed_&&reserved_->load(std::memory_order_acquire);}
 void commit_unchecked()noexcept{if(!commit_)std::terminate();commit_(*this);}
 friend class story_scenes::NativeCheckpointIssuer;
 friend class story_native_wait_runtime_controller::Context;
public:
 PreparedRetire(const PreparedRetire&)=delete;PreparedRetire&operator=(const PreparedRetire&)=delete;
 PreparedRetire(PreparedRetire&&)noexcept;PreparedRetire&operator=(PreparedRetire&&)noexcept;
 ~PreparedRetire();
};
}
namespace story_scenes {
std::optional<story_native_scene_checkpoint::PreparedRetire>prepare_native_retire(
 story_native_scene_checkpoint::CurrentRunRevocation&&)noexcept;
}
