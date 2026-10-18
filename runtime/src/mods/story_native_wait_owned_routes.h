// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_people_bridge.h"
#include "story_native_manager_terminal.h"
#include "story_native_camera_return.h"
#include "story_native_event_end_guard.h"
namespace story_native_wait_owned_routes {
using Expected=story_native_event_observation::Expected;
using Actor=story_native_event_observation::Actor;
namespace D=story_native_event_depth;
struct Sources {
 // Persistent retained claim, independent of Install's transient status.
 std::function<bool(const Expected&)> retained;
 std::function<bool(const Cpu*)> owned_thread;
 std::function<bool(const Actor&)> actor_identity;
 std::function<bool()> camera_identity;
 // Independently prove this candidate is ordinary retail and borrows no owned
 // image. A failed identity comparison alone never supplies this proof.
 std::function<bool(const Cpu*,uint32_t)> retail_safe;
 std::function<bool(Cpu*,story_native_event_end_guard::Observation&)> end_observation;
 std::function<void()> queue_cancel; // coalesced, no native calls
 std::function<bool()> terminal_invoking;
 std::function<bool()> control_release;
};
enum class Decision {Delegate,Handled};
// Stable helper references and Sources must outlive every observed native body.
// This installs no hooks, publishes no data and issues no retained claim.
class Routes {
 Expected expected_;D::Tracker&depth_;story_native_people_bridge::Bridge&people_;
 story_native_manager_terminal::Terminal&terminal_;story_native_camera_return::Barrier&camera_;
 story_native_event_end_guard::Guard&end_;Sources sources_;
 std::atomic<bool> fault_{false},cancelled_{false};
 bool claimed()noexcept;bool thread(const Cpu*)noexcept;bool safe(const Cpu*,uint32_t)noexcept;
 void fail()noexcept;
 int actor(uint32_t,bool cut=false)const;
 bool live_actor(unsigned)noexcept;bool live_camera()noexcept;
 bool end_observation(Cpu*,story_native_event_end_guard::Observation&);
 Decision foreign(Cpu*,uint32_t,const std::function<void()>&);
public:
 Routes(Expected,D::Tracker&,story_native_people_bridge::Bridge&,
        story_native_manager_terminal::Terminal&,story_native_camera_return::Barrier&,
        story_native_event_end_guard::Guard&,Sources);
 static constexpr bool publication_enabled(){return false;}
 bool fault()const{return fault_.load();}
 // Resolver classifies only the three genuine outer native entrypoints.
 bool outer(const Cpu*,uint32_t,D::Call&)noexcept;
 Decision route(Cpu*,uint32_t,const std::function<void()>&);
};
}
