// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include "story_native_people_pose_plan.h"
#include <vector>
namespace story_native_people_pose_record {
using Definition=std::array<uint8_t,32>;
using Poses=std::array<story_native_people_pose_plan::Snapshot,2>;
inline constexpr uint32_t kVersion=1,kBytes=604,kMaximumBytes=1024;
// Portable data-only baseline side record. Definition is the trusted immutable
// readiness configuration hash. A separate SHA-256 binds every byte of the
// canonical Expected record, including actor fingerprints and camera epoch.
// Checksums/hashes detect mismatch; they grant no ownership or resume/release
// authority. Decode requires independently authenticated Expected/Definition.
bool encode(const story_native_event_observation::Expected&,const Definition&,
            const Poses&,std::vector<uint8_t>&);
bool decode(std::span<const uint8_t>,const story_native_event_observation::Expected&,
            const Definition&,Poses&);
}
