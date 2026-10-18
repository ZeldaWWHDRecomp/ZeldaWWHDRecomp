// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_depth.h"
#include "story_native_people_pose_capture.h"
#include <atomic>
struct Cpu;
namespace story_native_wait_actor_cast {
using Expected=story_native_event_observation::Expected;
namespace D=story_native_event_depth;
using Body=std::function<void()>;
struct Sources {
 story_native_people_pose_capture::Read read;
 std::function<bool(const Expected&)> retained;
 // Actual designated Cpu/thread and its independently attested native stack.
 // Caller holds its real shared session/resource fence across every entry.
 std::function<bool(const Cpu*)> owned_thread;
 story_native_people_pose_capture::VerifyActor resident;
 std::function<void()> queue_cancel; // coalesced, no native calls
};
enum class Decision {Delegate,Handled};
// Exact optional WAIT/TURN definition is copied into the host completion receipt.
// No hooks/publication/status writes. Two genuine composed native routes must
// call this before their prior owners. Original startProc sets FORCEMOVE itself.
class Cast {
 Expected expected_;D::Tracker&depth_;Sources sources_;
 std::atomic<bool>fault_{false},cancelled_{false},initialized_{false};
 bool configured_=false;std::array<bool,2>seen_{};std::array<uint32_t,2>status_{};
 Cpu*cpu_=nullptr;uint32_t param_=0;
 static thread_local Cast*current_;
 bool retained()noexcept;bool thread(const Cpu*)const noexcept;
 void fail()noexcept;
 bool actor(unsigned)const;
 bool image(uint32_t state,bool pristine=false)const;
 bool manager()const;
 bool parent()const;
 bool cleared_flags()const;
public:
 Cast(Expected,D::Tracker&,Sources);
 static constexpr bool publication_enabled(){return false;}
 bool configured()const{return configured_;}bool fault()const{return fault_.load();}
 // Host receipt minted only after genuine native startProc completes all checks.
 bool initialized(const Expected&e)const noexcept{return initialized_.load()&&!fault_.load()&&e==expected_;}
 // 02543C6C: r3 manager, r4 exact event pointer (not event index).
 // Handled means original executed once, or was denied; Delegate never calls it.
 Decision start(Cpu*,const Body&);
 // 02542A38: r3 actor candidate, r4 actual startProc's stack search param.
 // Handled writes only r3=exact actor/zero; never calls the prior/native body.
 Decision candidate(Cpu*);
};
}
