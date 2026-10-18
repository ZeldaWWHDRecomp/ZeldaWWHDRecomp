// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_cancel_observation.h"
#include "story_native_wait_consumer_write.h"
namespace story_native_cancel_consumer_write {
namespace X=story_native_cancel_observation;
namespace P=story_native_people_pose_plan;
namespace C=story_native_camera_restore;
using Expected=story_native_event_observation::Expected;
using State=story_native_wait_consumer_write::State;
struct Sources {
 // Actual receipt-bound cancel capture for both actors, typed camera and
 // control boundary, under physical quiescence and nonblocking Stage pin.
 std::function<bool(State&)>capture;
 std::function<bool(const Expected&)>retained;
 C::VerifyCamera camera;
 std::function<bool(uint32_t,std::span<const uint8_t>)>write;
 std::function<void()>quarantine;
};
enum class Result {Refused,Complete,Quarantined};
// This restores consumers only. Completion is NOT a native pose-refresh or
// detach/release receipt; genuine refresh and fresh Closed->Detached validation
// remain mandatory before control/image teardown.
class Writer {
 std::atomic<bool>busy_{false};
 bool attempted_=false,complete_=false,quarantined_=false;
 void quarantine(const Sources&)noexcept;
public:
 Writer()=default;Writer(const Writer&)=delete;Writer&operator=(const Writer&)=delete;
 Result apply(Cpu*,story_native_event_depth::Tracker&,
  const story_native_outer_return::OuterReturn&,const threads::NativeQuiescence&,
  const Expected&,const X::Result&,const Sources&);
};
}
