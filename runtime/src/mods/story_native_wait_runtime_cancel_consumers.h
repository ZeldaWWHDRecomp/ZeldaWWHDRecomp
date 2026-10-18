// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_cancel_consumer_write.h"
#include "story_native_cancel_pose_capture.h"
#include "story_native_wait_runtime_model_graph.h"
namespace story_native_wait_runtime_cancel_consumers {
namespace X=story_native_cancel_observation;
namespace G=story_native_wait_runtime_model_graph;
struct Callbacks {
 std::function<bool(const X::O::Expected&)>retained;
 story_native_event_invoke::Write write;
 story_native_event_invoke::Call call;
 std::function<void()>quarantine;
};
// Route-local: the actual Binding, Session, Cpu, Tracker and claimed outer token
// must outlive this adapter and all its returned callbacks. It never acquires a
// second freeze or carries a pin across broad control release/native Run.
class Adapter {
 struct Impl;std::unique_ptr<Impl>impl_;
 explicit Adapter(std::unique_ptr<Impl>);
public:
 ~Adapter();Adapter(const Adapter&)=delete;Adapter&operator=(const Adapter&)=delete;
 static std::unique_ptr<Adapter>make(X::O::Expected,story_native_event_arena::Layout,
  story_native_wait_session::Session&,X::D::Tracker&,Cpu*,
  const story_native_outer_return::OuterReturn&,G::Context&,G::Binding&,
  story_native_wait_runtime_backend::Bundle,const X::B::Completion&,Callbacks);
 X::Sources observation_sources();
 bool use_closed(const X::Result&);
 // Caller must first persist journal Detaching. Both native refreshes require
 // the admitted original model graph, a fresh binding and the full stack budget.
 std::optional<X::R::Completion>restore();
};
}
