// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_memory.h"
#include "story_native_event_arena.h"
#include "story_native_people_pose_record.h"
#include "story_native_camera_policy.h"
#include "savestate_native_gate.h"
#include <vector>
namespace story_native_wait_run_footprint {
struct Bytes {uint32_t address{};std::vector<uint8_t> bytes;bool operator==(const Bytes&)const=default;};
using Footprint=std::vector<Bytes>;
// Data only. Neither a footprint nor matching saved bytes establishes a lease,
// capture provenance, native receipt, thread, restore epoch or entry authority.
// The persistent Context alone wraps these bytes in an opaque host receipt.
bool capture(const story_native_event_memory::Read&,const story_native_event_arena::Layout&,
 const story_native_event_observation::Expected&,const story_native_people_pose_record::Poses&,
 const story_native_camera_policy::Tables&,Footprint&);
bool matches_live(const story_native_event_memory::Read&,const Footprint&);
bool matches_saved(const story_native_event_arena::Layout&,
 const ss::native_gate::SavedView&,const Footprint&);
}
