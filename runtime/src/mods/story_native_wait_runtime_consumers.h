// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_consumer_write.h"
#include "story_native_people_pose_capture.h"
#include "story_native_camera_capture.h"
namespace story_native_wait_runtime_consumers {
namespace W=story_native_wait_consumer_write;
using Expected=story_native_event_observation::Expected;
using Poses=std::array<story_native_people_pose_plan::Snapshot,2>;
struct Sources {
 story_native_people_pose_capture::Read read;
 std::function<bool(uint32_t,std::span<const uint8_t>)> write;
 std::function<bool(const Expected&)> retained;
 story_native_people_pose_capture::VerifyActor resident;
 // Actual current entry fence/serial and live manager/control observation.
 std::function<bool(story_native_camera_restore::Boundary&)> boundary;
 std::function<void()> quarantine;
};
// Persistent source adapter, not a publication or snapshot authority. The
// caller holds real entry/resource fences throughout Writer::apply. Returned
// closures borrow this object; it must outlive the writer and native consumers.
class Adapter {
 Expected expected_;Poses baseline_;Sources sources_;
 bool configured_=false;
 bool retained()const;
 bool person(const story_native_people_pose_plan::Snapshot&,
             const story_native_event_observation::Actor&)const;
 bool write(uint32_t,std::span<const uint8_t>)const;
public:
 Adapter(Expected,Poses,Sources);
 bool configured()const{return configured_;}
 static constexpr bool publication_enabled(){return false;}
 bool capture(W::State&)const;
 W::Source source();
};
}
