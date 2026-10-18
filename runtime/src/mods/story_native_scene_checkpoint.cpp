// SPDX-License-Identifier: MPL-2.0
#include "story_native_scene_checkpoint.h"
#include "story_native_scene_playback.h"
#include <utility>
namespace story_native_scene_checkpoint {
PreparedRestore::PreparedRestore(story_native_scene_playback::Player&target,std::unique_ptr<story_native_scene_playback::Player>replacement,size_t&current,size_t index,std::atomic<bool>&input,std::atomic<bool>&reserved)
 :target_(&target),replacement_(std::move(replacement)),current_(&current),index_(index),input_(&input),reserved_(&reserved),thread_(std::this_thread::get_id()){commit_=[](PreparedRestore&p)noexcept{p.commit_impl_unchecked();};}
void PreparedRestore::release()noexcept{if(!reserved_)return;if(thread_!=std::this_thread::get_id())std::terminate();reserved_->store(false,std::memory_order_release);reserved_=nullptr;}
PreparedRestore::~PreparedRestore(){release();}
PreparedRestore::PreparedRestore(PreparedRestore&&other)noexcept{
 if(other.reserved_&&other.thread_!=std::this_thread::get_id())std::terminate();
 target_=other.target_;replacement_=std::move(other.replacement_);current_=other.current_;index_=other.index_;input_=other.input_;reserved_=std::exchange(other.reserved_,nullptr);thread_=other.thread_;committed_=other.committed_;commit_=std::exchange(other.commit_,nullptr);
}
PreparedRestore&PreparedRestore::operator=(PreparedRestore&&other)noexcept{
 if(this==&other)return *this;if(other.reserved_&&other.thread_!=std::this_thread::get_id())std::terminate();release();
 target_=other.target_;replacement_=std::move(other.replacement_);current_=other.current_;index_=other.index_;input_=other.input_;reserved_=std::exchange(other.reserved_,nullptr);thread_=other.thread_;committed_=other.committed_;commit_=std::exchange(other.commit_,nullptr);return *this;
}
void PreparedRestore::commit_impl_unchecked()noexcept{
 if(!reserved_||!target_||!current_||!input_||committed_||thread_!=std::this_thread::get_id())std::terminate();
 if(replacement_)target_->swap_host(*replacement_);*current_=index_;input_->store(true,std::memory_order_release);committed_=true;
}

}
