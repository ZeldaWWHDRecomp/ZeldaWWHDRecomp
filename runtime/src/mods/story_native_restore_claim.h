// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_restore_resume.h"
namespace story_native_wait_runtime_model_graph {class Binding;}
namespace story_native_wait_session {class Session;}
namespace story_native_wait_controller {class Controller;}
namespace story_native_restore_claim {
// One synchronous adoption attempt, never a general claim/invoke capability.
class Transfer {
 struct State;std::unique_ptr<State>state_;
 Transfer(const story_native_restore_resume::Boundary&,const story_native_wait_runtime_model_graph::Binding&,story_native_wait_session::Session&);
 bool consume(Cpu*,story_native_event_depth::Tracker&,story_native_wait_session::Session&,const story_native_event_observation::Expected&,const story_native_event_memory::Read&);
 friend class story_native_wait_runtime_controller::Context;
 friend class story_native_wait_controller::Controller;
public:
 Transfer(const Transfer&)=delete;Transfer&operator=(const Transfer&)=delete;
 Transfer(Transfer&&)noexcept;Transfer&operator=(Transfer&&)noexcept;~Transfer();
};
}
