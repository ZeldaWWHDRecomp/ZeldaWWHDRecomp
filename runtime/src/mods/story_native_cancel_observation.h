// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_camera_cancel_barrier.h"
#include "story_native_people_cancel_refresh.h"
#include "story_native_outer_return.h"
#include "native_quiescence.h"
namespace story_native_cancel_observation {
namespace O=story_native_event_observation;
namespace D=story_native_event_depth;
namespace M=story_native_manager_cancel;
namespace B=story_native_camera_cancel_barrier;
namespace P=story_native_people_pose_plan;
namespace C=story_native_camera_restore;
namespace R=story_native_people_cancel_refresh;
enum class Phase {Closed,Detached,Released};
// Owned storage for a bounded, fresh production capture. observation.image
// must refer to state.image; no caller-supplied alternate image is accepted.
struct Sample {
 M::State state{};O::Captured observation{};
 std::array<P::Snapshot,2> poses{};C::Boundary boundary{};
};
struct Sources {
 // Acquire actual physical parking BEFORE Stage::Scan/any guest-blocking
 // lifetime mutex. While parked, acquire Stage only through nonblocking
 // try_scan: another parked guest may hold its mutex. Release pins before
 // this capability. Reconstruct
 // per bounded observation; never carry a freeze across Run or EndControl.
 std::shared_ptr<const threads::NativeQuiescence> quiescence;
 // Actual adapter must retain Session fence, Stage scan and persistent claim
 // throughout both samples. Real resident/accessor and camera table checks are
 // mandatory; these callbacks cannot be derived from a saved journal.
 std::function<bool(Cpu*,Sample&)> sample;
 std::function<bool(const O::Expected&)> retained;
 std::function<bool(const O::Expected&,unsigned)> resident;
 std::function<bool(const O::Expected&,const O::Camera&)> verify_camera;
};
class Validator;
class Result {
 bool valid_=false;Phase phase_=Phase::Closed;O::Expected expected_{};
 B::Completion returned_{};story_native_outer_return::OuterReturn::Binding binding_{};std::array<P::Snapshot,2> baseline_{};
 uint64_t step_=0,serial_=0,root_=0;C::Boundary boundary_{};
 friend class Validator;
public:
 bool valid()const{return valid_;}Phase phase()const{return phase_;}
 bool released()const{return valid_&&phase_==Phase::Released;}
 bool matches(const O::Expected&e)const{return valid_&&expected_==e;}
 bool same_definition(const O::Result&r)const{return valid_&&r.matches(expected_);}
 bool same_cancellation(const Result&r)const{return valid_&&r.valid_&&expected_==r.expected_&&root_==r.root_&&step_==r.step_&&serial_==r.serial_&&returned_.cancel().serial()==r.returned_.cancel().serial()&&binding_.same_binding(r.binding_);}
 bool current_binding(Cpu*c,const D::Tracker&t,const story_native_outer_return::OuterReturn&o)const{return valid_&&binding_.current(c,expected_,t,o);}
 bool matches_baseline(const std::array<P::Snapshot,2>&p)const{
  auto same=[](const P::Snapshot&a,const P::Snapshot&b){return a.identity==b.identity&&a.angles==b.angles&&a.shape==b.shape&&a.joint==b.joint&&a.cut==b.cut&&a.target==b.target&&a.velocity==b.velocity&&a.head_only==b.head_only&&a.noticed==b.noticed&&a.talk_ready==b.talk_ready&&a.target_mode==b.target_mode&&a.status==b.status;};
  return valid_&&same(p[0],baseline_[0])&&same(p[1],baseline_[1]);
 }
 const story_native_event_lease::Identity& identity()const{return expected_.identity;}
 uint64_t logic_step()const{return step_;}uint64_t serial()const{return serial_;}
 const C::Boundary& boundary()const{return boundary_;} // data only
 const B::Completion& returned()const{return returned_;} // data, not permission
};
// This validator neither writes nor invokes native code. A cancellation receipt
// never substitutes for a normal Terminal certificate or activates public SDK.
class Validator {
 O::Expected expected_;Sources sources_;
 bool sample(Cpu*,D::Tracker&,const story_native_outer_return::OuterReturn&,Sample&);
 bool stable(const Sample&,const Sample&)const;
 bool restored_actors(const Sample&,const Result&)const;
 bool exact_partial(const Sample&,const Result&,bool detached)const;
 Result finish(Phase,const Sample&,const B::Completion&,const story_native_outer_return::OuterReturn&)const;
public:
 Validator(O::Expected,Sources);
 Result closed(Cpu*,D::Tracker&,const story_native_outer_return::OuterReturn&,const B::Completion&);
 Result detached(Cpu*,D::Tracker&,const story_native_outer_return::OuterReturn&,const Result&,const R::Completion&);
 Result released(Cpu*,D::Tracker&,const story_native_outer_return::OuterReturn&,const Result&);
};
}
