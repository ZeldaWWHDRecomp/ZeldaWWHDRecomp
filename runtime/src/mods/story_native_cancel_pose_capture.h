// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_cancel_observation.h"
#include "story_native_people_pose_capture.h"
namespace story_native_cancel_pose_capture {
// A read-only observation during the claimed, synchronous cancellation cleanup.
// Each whole restore segment must be either its exact native cancellation
// poststate or its exact original baseline. Partial/arbitrary segment bytes,
// foreign cuts, changed position/scale/attention/eye and unrelated actor writes
// refuse. The actual resource/Session fences remain the caller's obligation.
// This neither authorizes a write nor grants native/refresh/release authority.
bool capture(const story_native_people_pose_capture::Read&,
 const story_native_people_pose_capture::VerifyActor&,
 const story_native_people_pose_capture::Layout&,
 const story_native_event_observation::Expected&,unsigned,
 const story_native_cancel_observation::Result&,Cpu*,
 story_native_event_depth::Tracker&,
 const story_native_outer_return::OuterReturn&,
 const threads::NativeQuiescence&,
 story_native_people_pose_plan::Snapshot&);
// Initial Closed observation bootstrap: authentic same-call return receipt,
// whole actor exactly native poststate; no restoration mixtures permitted.
bool capture_closed(const story_native_people_pose_capture::Read&,
 const story_native_people_pose_capture::VerifyActor&,
 const story_native_people_pose_capture::Layout&,
 const story_native_event_observation::Expected&,unsigned,
 const story_native_camera_cancel_barrier::Completion&,Cpu*,
 story_native_event_depth::Tracker&,
 const story_native_outer_return::OuterReturn&,
 const threads::NativeQuiescence&,
 story_native_people_pose_plan::Snapshot&);
}
