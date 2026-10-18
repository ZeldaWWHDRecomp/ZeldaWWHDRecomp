// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_depth.h"
#include "story_native_event_invoke.h"
#include "story_native_people_pose_plan.h"
#include <atomic>
namespace story_native_manager_cancel {
namespace O=story_native_event_observation;
namespace D=story_native_event_depth;
namespace I=story_native_event_invoke;
namespace P=story_native_people_pose_plan;
struct Boundary {
 O::Expected expected{}; D::Call parent{}; D::Snapshot depth{};
 uint64_t logic_step=0;uint32_t elapsed=0;
 bool retained=false,fenced=false,stage=false,exclusive=false;
};
struct State {
 std::array<uint8_t,0x540> manager{};
 std::array<uint8_t,0xE60> image{};
 std::array<std::array<uint8_t,0x920>,2> actors{};
};
class Cancel;
// A scoped native force=1 receipt only. It grants no normal terminal, camera
// return, model refresh, detach, journal, or allocation release authority.
class Completion {
 O::Expected expected_{}; Boundary before_{},after_{};State pre_{},post_{};
 std::array<P::Snapshot,2> baseline_{};uint64_t serial_=0;
 friend class Cancel;
public:
 bool valid()const{return serial_!=0;}
 bool matches(const O::Expected&e)const{return valid()&&expected_==e;}
 uint64_t serial()const{return serial_;}
 const Boundary& before()const{return before_;}
 const Boundary& after()const{return after_;}
 const std::array<P::Snapshot,2>& baseline()const{return baseline_;}
 const State& prestate()const{return pre_;}
 const State& poststate()const{return post_;}
};
struct Sources {
 // Production adapter must sample real Session fence/retained allocation,
 // immutable Stage receipt, designated native thread and Tracker TLS ancestry.
 // An arbitrary copied Boundary does not authenticate a production caller.
 std::function<bool(Cpu*,Boundary&)> boundary;
 std::function<bool(const O::Expected&,unsigned)> resident;
 // Exact EndProc/CloseProc/allOff dispatch ownership, independently sealed.
 std::function<bool()> routes;
 // Actual retained allocation, including stamp/journal; never image-only extent.
 std::function<bool(const O::Expected&,I::Range&)> arena;
 // Must include the actual full retained arena (including journal/stamp),
 // not merely the image extent. The helper adds known native object spans.
 std::function<bool(Cpu*,I::Config&)> stack;
 I::Read read;I::Write write;
 // Dedicated bounded native capability, not the generic Invoke whitelist.
 // Must require native_cancel_scope(), exact fn/args and preserved CPU/frame.
 I::Call call;
 std::function<void()> quarantine;
};
enum class Result {Refused,Completed,Quarantined};
class Cancel {
 O::Expected expected_;std::array<P::Snapshot,2> poses_;Sources sources_;
 std::atomic<bool> busy_{false},fault_{false};bool attempted_=false;
 std::array<bool,2> seen_{};State pre_{};Completion completion_{};
 Cpu* active_cpu_=nullptr;Boundary invoking_boundary_{};
 static thread_local Cancel* scope_;
 bool boundary(Cpu*,Boundary&);bool capture(State&);bool initial(const State&)const;
 bool invoke(Cpu*);void fail()noexcept;
public:
 Cancel(O::Expected,std::array<P::Snapshot,2>,Sources);
 Result run(Cpu*);
 bool native_cancel_scope()const noexcept;
 uint32_t route_all_off(uint32_t,const std::function<uint32_t()>&);
 Completion completion()const{return completion_;}
};
}
