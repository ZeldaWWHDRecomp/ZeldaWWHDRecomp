// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_depth.h"
struct Cpu;
namespace guestmods {class NativeOuterReturnIssuer;}
namespace story_native_outer_return {
// Production issuer is defined only in guest_mods.cpp. It must independently
// prove actual observed_native CameraExecute dispatcher/chain completion and
// exactly one completed genuine original before constructing this token.
// A returned Tracker witness alone is NOT proof of a genuine native pipeline.
// No public constructor/factory, copied serial mint, or Outcome-based mint.
class OuterReturn final {
 story_native_event_depth::FrameWitness witness_;
 const Cpu*cpu_=nullptr;const story_native_event_depth::Tracker*tracker_=nullptr;std::thread::id thread_;
 mutable std::atomic<bool>claimed_{false};bool activated_=false;
 static thread_local const OuterReturn*active_;
 OuterReturn(Cpu*,story_native_event_depth::Tracker&,story_native_event_depth::FrameWitness);
 friend class guestmods::NativeOuterReturnIssuer;
public:
 class Binding {
  story_native_event_depth::FrameWitness witness_;
  explicit Binding(story_native_event_depth::FrameWitness w):witness_(std::move(w)){}
  friend class OuterReturn;
 public:
  Binding()=default;
  bool current(const Cpu*c,const story_native_event_observation::Expected&e,const story_native_event_depth::Tracker&t,const OuterReturn&o)const{return witness_.same_frame(o.witness_)&&o.claimed_current(c,e,t);}
  bool same_binding(const Binding&b)const{return witness_.same_frame(b.witness_);}
 };
 Binding binding()const{return activated_&&active_==this&&claimed_?Binding(witness_):Binding{};}
 OuterReturn(const OuterReturn&)=delete;OuterReturn&operator=(const OuterReturn&)=delete;
 OuterReturn(OuterReturn&&)=delete;OuterReturn&operator=(OuterReturn&&)=delete;
 ~OuterReturn();
 bool current(const Cpu*,const story_native_event_observation::Expected&)const;
 // One synchronous cleanup claimant only. Claim is not Stage, allocation,
 // Session fence, model invocation, consumer restore or image-free authority.
 bool claim(const Cpu*,const story_native_event_observation::Expected&)const;
 bool claimed_current(const Cpu*c,const story_native_event_observation::Expected&e)const{return claimed_&&current(c,e);}
 // Exact actual Tracker bound by the private verified issuer constructor.
 // Equal Expected/serial counters on another Tracker cannot substitute.
 bool claimed_current(const Cpu*c,const story_native_event_observation::Expected&e,const story_native_event_depth::Tracker&t)const{return tracker_==&t&&claimed_current(c,e);}
 uint64_t root_serial()const{return witness_.root_serial();} // data only
 const story_native_event_observation::Expected& expected()const{return witness_.expected();}
};
}
