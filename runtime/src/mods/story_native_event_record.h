// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include <span>
#include <vector>
namespace story_native_event_record {
// Portable guest-memory record for the WAIT fixture's baseline and lease phase.
// The checksum detects damage; it is not proof of ownership. A restored record
// must pass the live collector, actor/allocation bindings and lease checks before
// any native callback resumes or an image can be retired.
inline constexpr size_t kMaximumBytes=4096;
bool encode(const story_native_event_observation::Expected&,
            story_native_event_lease::Phase,std::vector<uint8_t>&);
bool decode(std::span<const uint8_t>,story_native_event_observation::Expected&,
            story_native_event_lease::Phase&);
}
