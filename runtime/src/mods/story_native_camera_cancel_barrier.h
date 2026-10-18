// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_manager_cancel.h"
#include <optional>
namespace story_native_camera_cancel_barrier {
namespace M=story_native_manager_cancel;
namespace O=story_native_event_observation;
// outer_serial is the actual immutable TLS CameraExecute frame serial, NOT
// Tracker::snapshot().serial (which advances on legitimate inner callbacks).
struct Frame {M::Boundary boundary{};uint64_t outer_serial=0;};
class Barrier;
class Ready {
 const Barrier* owner_=nullptr;Frame frame_{};M::State state_{};O::Camera camera_{};
 friend class Barrier;
public:
 bool valid()const{return owner_!=nullptr;}
};
class Completion {
 O::Expected expected_{};Frame before_{},after_{};O::Camera camera_{};
 M::Completion cancel_{};bool valid_=false;
 friend class Barrier;
public:
 bool valid()const{return valid_;}
 bool matches(const O::Expected&e)const{return valid_&&expected_==e;}
 const Frame& before()const{return before_;}
 const Frame& after()const{return after_;}
 const O::Camera& camera()const{return camera_;}
 const M::Completion& cancel()const{return cancel_;}
};
struct Sources {
 // Mandatory production factory must derive this from actual retained claim,
 // Session/Stage fences, designated thread and current Tracker TLS frame.
 // The required immutable TLS serial getter/factory is not implemented here.
 std::function<bool(Cpu*,Frame&)> frame;
 // Authenticate the designated actual thread before touching mutable state.
 std::function<bool(Cpu*)> owned_thread;
 M::I::Read read;
 std::function<bool(const O::Expected&,unsigned)> resident;
 std::function<bool(O::Camera&)> camera;
 // Read actual canonical native camera tables, selected Event11/Follow1 cache.
 std::function<bool(const O::Expected&,const O::Camera&)> verify_camera;
 // Must attest the actual Run ORIGINAL wrapper and current owner's delegation,
 // not merely compare the supplied function address or a caller's flag.
 std::function<bool(Cpu*,uint32_t)> original_route;
 std::function<void()> quarantine;
};
enum class Result {Refused,Returned,Quarantined};
// Dormant entered-Event-only same-call barrier. No native Run invocation is
// manufactured: production must supply the genuine current route original.
// NeverEntered/NotRun refuse at ready(), BEFORE force1; no copied state is
// projected into normal completion. OuterReturn/idle cleanup remain separate.
class Barrier {
 O::Expected expected_;Sources sources_;std::atomic<bool>busy_{false},fault_{false};
 bool issued_=false,attempted_=false;Ready ready_{};Completion completion_{};
 bool frame(Cpu*,Frame&);bool capture(M::State&,O::Camera&);bool partial(const M::State&)const;
 bool camera(const M::State&,const O::Camera&,bool returned)const;
 void fail()noexcept;
public:
 Barrier(O::Expected,Sources);
 std::optional<Ready> ready(Cpu*,uint32_t function);
 Result run(Cpu*,uint32_t function,const Ready&,const M::Completion&,const std::function<void()>& genuine_original);
 Completion completion()const{return completion_;}
};
}
