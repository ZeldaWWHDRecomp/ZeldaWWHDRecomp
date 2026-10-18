// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_people_admission.h"
#include "story_native_event_depth.h"
#include "story_native_event_invoke.h"
#include <atomic>
namespace story_native_people_bridge {
namespace A=story_native_people_admission;
namespace D=story_native_event_depth;
using Body=std::function<void()>;
struct Sources {
 A::Sources admission; // invoke replaced by exact CommonCut bridge below
 story_native_event_invoke::Config stack;
 story_native_event_invoke::Read stack_read;
 story_native_event_invoke::Write stack_write;
 story_native_event_invoke::Call guest_call;
 std::function<bool(const Cpu*,const story_native_event_invoke::Config&)>verify_stack;
 // Actual hooks/owners for CommonCut/staff/WAITstart/WAITprocess/cutEnd must
 // compose without replacing foreign hooks; absent proof leaves bridge closed.
 std::function<bool()>verify_routes;
 // Pure, thread-safe proof of actual designated guest game-thread identity.
 std::function<bool(const Cpu*)>verify_owned_thread;
 // Thread-safe snapshot-owned coalesced cancellation only; never native calls,
 // EndControl/global reset. Also invalidate controller checkpoint admission.
 std::function<bool(const story_native_event_lease::Identity&)>queue_cancel;
};
enum class Decision {DelegateOwner,Handled,RejectOwned};
// NOT a generally installable hook: a wrong-thread address match cannot safely
// distinguish retired retail reuse without independent retained-claim routing.
// Caller must prove an active installed lease before invoking this helper on
// denied-thread owned candidates. Until that dispatcher proof exists this stays
// publication-disabled; foreign-thread ambiguity is an explicit integration gate.
class Bridge {
 story_native_event_observation::Expected expected_;std::array<A::Reference,2>references_;Sources sources_;D::Tracker&tracker_;
 std::unique_ptr<A::Adapter>adapter_;bool configured_=false;std::atomic<bool>fault_{false},cancel_queued_{false},cancel_attempted_{false};
 int active_=-1;
 D::Call call(D::Entry,unsigned)const;
 bool identity(unsigned)const;
 bool own(unsigned)const;
 bool thread(const Cpu*)const noexcept;
 bool retained()const noexcept;
 void fail()noexcept;
 uint32_t common(Cpu*,uint32_t,std::initializer_list<uint32_t>);
 Decision action_entry(Cpu*,bool start,bool turn,const Body&);
public:
 Bridge(story_native_event_observation::Expected,std::array<A::Reference,2>,Sources,D::Tracker&);
 static constexpr bool publication_enabled(){return false;}
 bool configured()const{return configured_;}bool fault()const{return fault_.load();}bool cancel_queued()const{return cancel_queued_.load();}
 // To be called INSIDE existing owned PeopleEventMove owner; foreign/unbound
 // addresses return DelegateOwner. Never invoke original outer/privateCut here.
 Decision event_move(Cpu*);
 bool staff_id(Cpu*); // exact r3-only ABI through Router + StaffLookup Scope
 // DelegateOwner NEVER executes the supplied body: caller delegates once.
 // Handled executes a supplied EXISTING native owner body exactly once
 // only after scoped admission. They install no hooks or delegation shortcuts.
 Decision wait(Cpu*,bool start,const Body& original);
 Decision turn(Cpu*,bool start,const Body& original);
 Decision cut_end(Cpu*,const Body& original);
};
}
