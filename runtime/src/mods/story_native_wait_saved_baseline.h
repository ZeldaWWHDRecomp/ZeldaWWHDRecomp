// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_run_footprint.h"
#include <memory>
namespace story_native_wait_session {
class Session;
// Original host Session caches and exact saved footprint. This cannot be made
// from a checkpoint record or SavedView. It carries data, not an active lease.
class SavedBaseline {
 struct Data;std::shared_ptr<const Data>data_;
 explicit SavedBaseline(std::shared_ptr<const Data>);
 friend class Session;
public:
 const story_native_event_observation::Expected&expected()const;
 const story_native_people_pose_record::Poses&poses()const;
 bool matches_live(const story_native_event_memory::Read&)const;
};
}
