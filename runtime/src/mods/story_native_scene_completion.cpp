// SPDX-License-Identifier: MPL-2.0
#include "story_native_scene_completion.h"
#include <mutex>
#include <stdexcept>
namespace story_native_scene_completion {
struct Completion::State {
 story_native_event_observation::Expected expected;
 std::shared_ptr<const story_native_scene_playback::Activation>activation;
 std::shared_ptr<const void>run_identity;std::mutex mutex;bool consumed=false;
 State(story_native_event_observation::Expected e,std::shared_ptr<const story_native_scene_playback::Activation>a,std::shared_ptr<const void>r):expected(std::move(e)),activation(std::move(a)),run_identity(std::move(r)){}
};
Completion::Completion(story_native_event_observation::Expected e,std::shared_ptr<const story_native_scene_playback::Activation>a,std::shared_ptr<const void>r):state_(std::make_unique<State>(std::move(e),std::move(a),std::move(r))){}
Completion::Completion(Completion&&)noexcept=default;
Completion&Completion::operator=(Completion&&)noexcept=default;
Completion::~Completion()=default;
bool Completion::consume(const story_native_scene_playback::Activation&a){
 if(!state_)return false;std::lock_guard lock(state_->mutex);if(state_->consumed)return false;state_->consumed=true;
 return state_->run_identity&&state_->activation&&state_->activation.get()==&a;
}
const story_native_event_observation::Expected&Completion::expected()const{if(!state_)throw std::logic_error("moved completion");return state_->expected;}
}
