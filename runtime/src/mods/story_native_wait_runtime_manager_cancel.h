// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_runtime_cancel_camera.h"
#include <memory>
#include <string>
namespace story_native_wait_runtime_manager_cancel {
namespace M=story_native_manager_cancel;
struct Callbacks {
 std::function<bool(const M::O::Expected&)> retained;
 M::I::Write write;
 M::I::Call call; // actual guest_call; accepted only by the dedicated exact TLS capability
 std::function<void()> quarantine;
};
struct Probe {bool quiescent=false,dispatch=false,current=false;std::string reason;};
class Execution {
 struct Impl;std::unique_ptr<Impl>impl_;explicit Execution(std::unique_ptr<Impl>);
public:
 ~Execution();Execution(const Execution&)=delete;Execution&operator=(const Execution&)=delete;
 static std::unique_ptr<Execution>prepare(story_native_wait_session::Session&,
  M::D::Tracker&,M::O::Expected,story_native_event_arena::Layout,
  story_native_stage_owner_observer::Snapshot,story_native_event_game_thread::Ticket,
  Cpu*,story_native_wait_runtime_backend::Bundle,Callbacks);
 // Actual parking/dispatch/census proof only; no helper/native invocation.
 Probe probe(Cpu*);
 M::Result run(Cpu*);
 M::Completion completion()const;
 bool native_end_proc_scope(const Cpu*,uint32_t,uint32_t,int32_t,uint32_t)const noexcept;
 std::optional<uint32_t>route_all_off(Cpu*,const std::function<void()>&);
};
}
