// SPDX-License-Identifier: MPL-2.0
#include "story_native_scene_retire.h"
#include "story_native_scene_playback.h"
#include <utility>
namespace story_native_scene_checkpoint {
PreparedRetire::PreparedRetire(story_native_scene_playback::Player&target,std::unique_ptr<story_native_scene_playback::Player>replacement,std::atomic<bool>&input,std::atomic<bool>&reserved)
 :target_(&target),replacement_(std::move(replacement)),input_(&input),reserved_(&reserved),thread_(std::this_thread::get_id()){
 commit_=[](PreparedRetire&p)noexcept{p.commit_impl_unchecked();};
}
void PreparedRetire::release()noexcept{if(!reserved_)return;if(thread_!=std::this_thread::get_id())std::terminate();reserved_->store(false,std::memory_order_release);reserved_=nullptr;}
PreparedRetire::~PreparedRetire(){release();}
PreparedRetire::PreparedRetire(PreparedRetire&&other)noexcept{
 if(other.reserved_&&other.thread_!=std::this_thread::get_id())std::terminate();
 target_=other.target_;replacement_=std::move(other.replacement_);input_=other.input_;reserved_=std::exchange(other.reserved_,nullptr);thread_=other.thread_;committed_=other.committed_;commit_=std::exchange(other.commit_,nullptr);
}
PreparedRetire&PreparedRetire::operator=(PreparedRetire&&other)noexcept{
 if(this==&other)return *this;if(other.reserved_&&other.thread_!=std::this_thread::get_id())std::terminate();release();
 target_=other.target_;replacement_=std::move(other.replacement_);input_=other.input_;reserved_=std::exchange(other.reserved_,nullptr);thread_=other.thread_;committed_=other.committed_;commit_=std::exchange(other.commit_,nullptr);return *this;
}
void PreparedRetire::commit_impl_unchecked()noexcept{
 if(!current())std::terminate();target_->swap_host(*replacement_);input_->store(false,std::memory_order_release);committed_=true;
}
}
