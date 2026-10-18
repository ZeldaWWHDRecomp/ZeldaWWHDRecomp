// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include "story_native_people_pose_plan.h"
#include "story_native_turn_camera_image.h"
#include <span>
namespace story_native_turn_record {
inline constexpr uint32_t kVersion=3,kMaximumBytes=16384;
struct Record {
 story_native_event_observation::Expected expected;
 story_native_turn_camera_image::Program program;
 std::array<story_native_people_pose_plan::Snapshot,2> poses;
 story_native_event_lease::Phase phase=story_native_event_lease::Phase::Empty;
 uint32_t elapsed_ticks=0,completion_flags=0,watchdog_ticks=0;
};
// Distinct E60 codec. Checksum detects corruption, not authenticity. Independent
// immutable definition/allocation authorization and actual saved image/flags,
// idle depth and consumer validation remain checkpoint backend obligations.
bool encode(const Record&,std::vector<uint8_t>&);
bool decode(std::span<const uint8_t>,Record&);
}
