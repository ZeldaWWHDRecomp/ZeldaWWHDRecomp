// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_camera_cancel_barrier.h"
#include "story_native_wait_runtime_backend.h"
#include "story_native_wait_session.h"
namespace story_native_wait_runtime_cancel_camera {
struct Callbacks {
 std::function<bool(const story_native_event_observation::Expected&)> retained;
 std::function<void()> quarantine;
};
// Acquire only inside the genuine current CameraRun wrapper. Returned closures
// bind the issuing current native frame and take fresh short Stage Scans
// around observations; no Stage mutex is retained across native Run or Q acquisition. They do not
// freeze guest threads or manufacture Run/native calls. Dedicated force1 owns
// its separate tiny all-other-thread quiescence scope.
std::optional<story_native_camera_cancel_barrier::Sources> make(
 story_native_wait_session::Session&,story_native_event_depth::Tracker&,
 story_native_event_observation::Expected,story_native_event_arena::Layout,
 story_native_stage_owner_observer::Snapshot,story_native_event_game_thread::Ticket,
 Cpu*,story_native_wait_runtime_backend::Bundle,Callbacks);
}
