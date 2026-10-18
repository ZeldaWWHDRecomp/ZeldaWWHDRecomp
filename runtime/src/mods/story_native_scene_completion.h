// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include <memory>
namespace story_native_wait_runtime_controller {class Context;}
namespace story_native_scene_playback {class Activation;}
namespace story_native_scene_completion {
// A genuine natural scene completion, never native entry/publication authority.
// It remains data after Session rearm; copied bytes or Released booleans cannot
// construct it. Only the completing persistent native Context is its issuer.
class Completion {
 struct State;std::unique_ptr<State>state_;
 Completion(story_native_event_observation::Expected,
  std::shared_ptr<const story_native_scene_playback::Activation>,std::shared_ptr<const void>run_identity);
 friend class story_native_wait_runtime_controller::Context;
public:
 Completion(const Completion&)=delete;Completion&operator=(const Completion&)=delete;
 Completion(Completion&&)noexcept;Completion&operator=(Completion&&)noexcept;
 ~Completion();
 // One attempt, including a wrong activation. A caller-supplied serial or
 // declaration name cannot substitute for the actual activation object.
 bool consume(const story_native_scene_playback::Activation&);
 const story_native_event_observation::Expected&expected()const;
};
}
