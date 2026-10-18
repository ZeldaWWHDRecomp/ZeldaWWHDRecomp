// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_native_event_observation.h"
#include <atomic>
#include <mutex>
#include <memory>
#include <optional>
#include <thread>
#include <functional>
struct Cpu;
namespace story_native_outer_return {class OuterReturn;}
namespace story_native_restore_resume {class Boundary;class SavedRunOrigin;}
namespace story_native_wait_runtime_model_graph {class Context;}
namespace guestmods {class NativeRestoreTrackerIssuer;}
namespace story_native_wait_runtime_controller {class Context;}
namespace threads {class NativeQuiescence;}
namespace story_native_event_depth {
using namespace story_native_event_observation;
enum class Entry:uint32_t {PeopleExecute,CameraExecute,ManagerRun,PeopleEventMove,CommonCut,CameraEvent,ManagerMain,Timekeeper,StaffLookup,CutEnd,TurnStart,TurnProcess,WaitStart,WaitProcess,MoveStart,MoveProcess};
uint32_t entry_address(Entry); // canonical native function mapped exactly once
struct HookObserver {uint64_t current_owner=0,delegated_owner=0;bool before_and_after=false;};
struct Coverage {HookObserver people,camera,manager;bool inner_callbacks=false;};
bool complete(Coverage);
enum class Authority {Owned,Foreign,InvalidOwned};
struct Call {
 Entry entry=Entry::PeopleExecute;uint32_t function=0,receiver=0,subject=0,pid=0,token=0;
 story_native_event_lease::Identity identity{};Authority authority=Authority::InvalidOwned;
};
enum class State:uint32_t {Active=1,Draining=2};
struct Snapshot {
 uint32_t npc_depth=0,camera_depth=0,manager_depth=0,callback_depth=0,foreign_depth=0;
 bool fault=false,coverage=false;State state=State::Active;uint64_t serial=0;
 bool idle()const{return !npc_depth&&!camera_depth&&!manager_depth&&!callback_depth&&!foreign_depth&&!fault;}
};
using Record=std::array<uint8_t,96>;
class Tracker;
namespace detail {struct FrameLife;struct OwnerLife;}
// Opaque observation of an actual Tracker TLS frame lifetime. Copying its
// serials is data only; current()/returned() must freshly verify the owner.
class FrameWitness {
 std::shared_ptr<detail::FrameLife> current_,root_;
 FrameWitness(std::shared_ptr<detail::FrameLife>,std::shared_ptr<detail::FrameLife>);
 bool returned_now(bool allow_draining)const;
 bool claim_outer_return(const Tracker&)const;
 friend class Tracker;friend class story_native_outer_return::OuterReturn;
public:
 FrameWitness()=default;
 // Data identity only; current()/returned() still supply actual lifetime checks.
 bool same_frame(const FrameWitness&r)const{return current_&&root_&&current_==r.current_&&root_==r.root_;}
 bool valid()const;
 bool current(const Tracker&)const;
 bool returned(const Tracker&)const;
 uint64_t current_serial()const;
 uint64_t root_serial()const;
 const Call& root_call()const;
 const Expected& expected()const;
};
class Scope {
 Tracker*tracker_=nullptr;uint64_t serial_=0;int exceptions_=0;std::thread::id thread_;
 friend class Tracker;
 Scope(Tracker*,const Call&);
public:
 Scope(const Scope&)=delete;Scope&operator=(const Scope&)=delete;
 Scope(Scope&&)=delete;Scope&operator=(Scope&&)=delete;
 ~Scope();
 bool allowed()const{return tracker_!=nullptr;}
};
// Use one persistent Tracker for the exclusive manager lease, shared by ALL
// observed hooks and guest threads; never construct one per hook or per thread.
// Its atomic counters aggregate those threads; its TLS chain grants authority.
// No hooks or native calls. All ownership observations and current hook owners
// must be independently verified by the adapter. Counters include foreign outer
// calls; authority is an exact TLS chain, never a positive aggregate count.
class Tracker {
 Expected expected_;Coverage coverage_;bool ready_=false;mutable std::mutex mutex_;
 std::atomic<uint32_t>npc_{0},camera_{0},manager_{0},callbacks_{0},foreign_{0};
 bool fault_=false;State state_=State::Active;uint64_t serial_=0;
 bool restore_pending_=false;uint64_t restore_epoch_=0,restore_load_epoch_=0;
 const Cpu*restore_cpu_=nullptr;std::thread::id restore_thread_;
 std::shared_ptr<const story_native_restore_resume::SavedRunOrigin>restore_origin_;
 std::shared_ptr<story_native_wait_runtime_model_graph::Context>restore_model_;
 std::function<bool()>restore_coverage_current_;
 static std::unique_ptr<Tracker>restore_provisional(const story_native_restore_resume::Boundary&,
  const Cpu*,Coverage,std::function<bool()>);
 friend class guestmods::NativeRestoreTrackerIssuer;

 std::shared_ptr<detail::OwnerLife> lifetime_;
 std::weak_ptr<detail::FrameLife> last_returned_;
 friend class Scope;friend class FrameWitness;
 std::optional<Call> current_owned_call_locked()const;
 bool frame_current_locked(const FrameWitness&)const;
 bool frame_returned_locked(const FrameWitness&,bool allow_draining)const;
 bool enter(const Call&,uint64_t&);
 void leave(uint64_t,bool exception,std::thread::id);
 bool matches(const Call&)const;
 Snapshot snapshot_locked()const;
public:
 class PreparedRestoreOpen {
  Tracker*owner_{};std::unique_lock<std::mutex>lock_;std::thread::id thread_;bool consumed_=false;
  PreparedRestoreOpen(Tracker*,std::unique_lock<std::mutex>&&);
  friend class Tracker;friend class story_native_wait_runtime_controller::Context;
  void open_unchecked()noexcept;
 public:
  PreparedRestoreOpen(const PreparedRestoreOpen&)=delete;PreparedRestoreOpen&operator=(const PreparedRestoreOpen&)=delete;
  PreparedRestoreOpen(PreparedRestoreOpen&&)noexcept=default;
  PreparedRestoreOpen&operator=(PreparedRestoreOpen&&)=delete;
  ~PreparedRestoreOpen();
 };
private:
 friend class story_native_wait_runtime_controller::Context;
 // Acquire LAST: retains the real Tracker mutex until callback-free commit.
 // No Source, Binding.current or Tracker query may follow this acquisition.
 std::optional<PreparedRestoreOpen>prepare_restore_open(const story_native_restore_resume::Boundary&,
  const threads::NativeQuiescence&,const Result&);
public:
 Tracker(Expected,Coverage);
 ~Tracker();
 Scope enter(const Call&c){return Scope(this,c);}
 Snapshot snapshot()const;
 // Shared entry fence: succeeds only at fully quiescent counters. While fenced
 // every new native consumer is refused. A foreign arrival faults admission;
 // never call/skip a retail original on that basis without a reviewed adapter.
 bool begin_drain();
 bool cancel_drain();
 bool ready()const;
 // Genuine current post-load seed, permanently fenced pending a separately
 // reviewed full resume certificate. No public operation opens this tracker.
 bool restoring(const story_native_restore_resume::Boundary&)const;
 // Instantaneous TLS observations, never transferable authority for later calls.
 // Every current frame must belong to this exact owned ancestry; foreign or
 // other-tracker frames hide the underlying owner's authority.
 std::optional<Call> current_owned_call()const;
 std::optional<FrameWitness> current_owned_frame()const;
 bool current_actor_scope(const Actor&,const story_native_event_lease::Identity&)const;
 Record record()const; // caller publishes to snapshot-owned guest storage
 // Read-only validation: busy/faulted/draining records cannot rebuild host scopes.
 // This is NOT image/allocation/actor/camera ownership or a free authorization.
 static bool valid_idle_record(const Record&,const Expected&);
};
}
