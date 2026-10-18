// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_wait_detach_plan.h"
namespace story_native_wait_consumer_write {
namespace D=story_native_wait_detach_plan;
namespace P=story_native_people_pose_plan;
namespace C=story_native_camera_restore;
using story_native_event_observation::Expected;
using story_native_event_observation::Camera;
struct State {std::array<P::Snapshot,2> people{};Camera camera{};C::Boundary boundary{};};
// Production owns the entry fence throughout apply(), including failure. These
// callbacks must read actual sources; this helper creates no lease or authority.
struct Source {
 std::function<bool(State&)> capture;
 std::function<bool(const Expected&)> retained;
 std::function<bool(const P::Snapshot&,const story_native_event_observation::Actor&)> person;
 std::function<bool(const Camera&,const Expected&)> camera_identity;
 C::VerifyCamera camera;
 std::function<bool(uint32_t,std::span<const uint8_t>)> write;
 // Must retain the allocation and deny owned entries. No native calls/throw.
 std::function<void()> quarantine;
};
enum class Result {Refused,Complete,Quarantined};
class Writer {
 bool attempted_=false,quarantined_=false,complete_=false;
 void quarantine(const Source&)noexcept;
public:
 Writer()=default;
 Writer(const Writer&)=delete;Writer&operator=(const Writer&)=delete;
 bool attempted()const{return attempted_;}
 bool quarantined()const{return quarantined_;}
 // Normal completion only. Cancellation requires native J3D pose refresh and
 // deliberately cannot be performed through this capability.
 Result apply(const D::Plan&,const Expected&,const std::array<P::Snapshot,2>& baseline,const Source&);
};
}
