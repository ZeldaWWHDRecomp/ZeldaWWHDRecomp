// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_runtime_backend.h"
#include "story_native_manager_terminal.h"
#include "story_native_camera_return.h"
#include "story_native_wait_session.h"
namespace story_native_wait_runtime_terminal {
using Expected=story_native_event_observation::Expected;
struct Callbacks {
 std::function<bool(const Expected&)> retained; // actual persistent exclusive claim
 // Dedicated terminal ABI capability, NOT raw guest_call or the generic start
 // Invoke bridge (which deliberately does not allow endProc). Must independently
 // preserve Cpu and the bounded outgoing 64-byte frame, protect owned spans,
 // verify Terminal TLS/allOff composition, and accept only these exact args.
 std::function<bool(Cpu*,uint32_t,uint32_t,int32_t,uint32_t)> end_proc;
 std::function<story_native_manager_terminal::Completion()> completion;
 std::function<bool()> camera_route; // certifies exact native Run original wrapper
 std::function<void()> quarantine; // queue quarantine; never destroy retained context
};
struct Bundle {
 story_native_manager_terminal::Identity identity;
 story_native_manager_terminal::Probe probe;
 story_native_manager_terminal::ActorProbe actor;
 story_native_manager_terminal::Invoke invoke;
 story_native_camera_return::Sources camera;
};
// Persistent copied context only; no hooks, activation, native Run invocation or
// release authority. Session/Cpu/Tracker must outlive returned callbacks. Camera
// route wrappers MUST hold the actual Session Fence across Barrier::run/next;
// nested routes reuse that held TLS fence. Camera capture admits an idle boundary
// or the exact owned CameraExecute TLS frame, never an aggregate depth alone.
std::optional<Bundle> make(story_native_wait_session::Session&,
 story_native_event_depth::Tracker&,Expected,story_native_event_arena::Layout,
 story_native_stage_owner_observer::Snapshot,story_native_event_game_thread::Ticket,
 Cpu*,story_native_event_backend::Config,story_native_event_backend::Sources,Callbacks,
 std::shared_ptr<story_native_wait_runtime_backend::StageBinding>stage_binding={});
}
