// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_camera_restore.h"
#include "story_native_people_pose_plan.h"
namespace story_native_wait_detach_plan {
namespace P=story_native_people_pose_plan;
namespace C=story_native_camera_restore;
// All three consumers are preflighted before the caller may attempt its first
// write. No fence, writer, journal, native unwind or release authority here.
class Plan {
 bool valid_=false;
 std::array<P::Snapshot,2> baseline_{};
 std::array<P::Plan,2> people_{};
 C::Plan camera_{};
 bool cancel_=false;
 story_native_event_observation::Expected expected_{};
 std::array<P::Snapshot,2> before_people_{};
 story_native_event_observation::Camera before_camera_{};
 C::Boundary boundary_{};
 friend Plan prepare(const story_native_event_observation::Expected&,
  const std::array<P::Snapshot,2>&,const std::array<P::Snapshot,2>&,
  const story_native_event_observation::Camera&,const C::Boundary&,
  C::VerifyCamera,bool,bool,bool,bool);
public:
 bool valid()const{return valid_;}
 bool normal()const{return valid_&&!cancel_;}
 bool verify_pre(const story_native_event_observation::Expected&,
  const std::array<P::Snapshot,2>& baseline,const std::array<P::Snapshot,2>& fresh,
  const story_native_event_observation::Camera&,const C::Boundary&,C::VerifyCamera)const;
 const std::array<P::Plan,2>& people()const{return people_;}
 const C::Plan& camera()const{return camera_;}
 // Fresh source-backed captures under the same fence are required. This only
 // checks fields represented in the plans, not status-bit merge or J3D work.
 bool verify_post(const std::array<P::Snapshot,2>&,
  const story_native_event_observation::Camera&,const C::Boundary&,
  C::VerifyCamera)const;
};
Plan prepare(const story_native_event_observation::Expected&,
 const std::array<P::Snapshot,2>&baseline,const std::array<P::Snapshot,2>&live_people,
 const story_native_event_observation::Camera&live,const C::Boundary&,
 C::VerifyCamera,bool exact_uo2,bool idle,bool detached,bool cancel);
}
