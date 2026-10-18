// SPDX-License-Identifier: MPL-2.0
// Exercise the actual threads.cpp issuer, registry, freeze gates and scheduler.
// Only unused HLE registration and external logging/profiling are omitted.
#include "runtime.h"
#include <string>
#include <cstdint>
#include <utility>
#include <cstring>
#undef HLE
#define HLE(lib,name) static void hle_impl_##lib##_##name(Cpu*c)
#include "../src/threads.cpp"
#include <cassert>
#include <future>
#include <string>
#include <cstdint>
#include <utility>
#include <cstring>
void log_msg(const char*,...){}
namespace mem {std::string read_cstr(uint32_t){return "test";}}
namespace rprof {uint64_t thread_cpu_ns(){return 0;}}
static void setup(HostThread&t,uint32_t guest,uint32_t core,bool service=false){
 t.guest=guest;t.initialized=true;t.started=true;t.service=service;t.core=t.cpu.core=core;t.cpu.thread=&t;t.stack_bottom=0x20020000;t.stack_top=0x20030000;t.stack_bounds_valid=true;t.cpu.r[1]=0x2002FF00;t.acquired=std::chrono::steady_clock::now();
 std::lock_guard lock(g_threads_mutex);g_threads[t.guest]=&t;
}
static void remove(HostThread&t){std::lock_guard lock(g_threads_mutex);g_threads.erase(t.guest);}
static void restored(const HostThread&t,const Cpu&before){assert(!g_frozen&&g_entry_parks==0&&t.wst==kRunning&&threads::owns_core(const_cast<HostThread*>(&t),1)&&!std::memcmp(&before,&t.cpu,sizeof(Cpu)));}
int main(){
 HostThread owner;setup(owner,0x20010000,1);owner.held_core=1;owner.holds_core=true;t_self=&owner;t_cpu=&owner.cpu;g_sched[1].owner=&owner;assert(story_native_event_game_thread::observe_logic(&owner.cpu));auto ticket=story_native_event_game_thread::ticket(&owner.cpu);std::string why;const Cpu before=owner.cpu;Cpu wrong{};
 {story_native_stage_owner::Tracker stage;auto pin=stage.scan();assert(story_native_stage_owner::current_thread_holds_lifetime_pin());assert(!threads::try_native_quiescence(&owner.cpu,ticket,10,why)&&why=="Stage lifetime pin held before native freeze");restored(owner,before);}assert(!story_native_stage_owner::current_thread_holds_lifetime_pin());
 assert(!threads::current_thread_holds_native_quiescence());assert(!threads::current_quiescence(&owner.cpu));assert(!threads::borrow_native_read_quiescence(&owner.cpu,ticket,threads::QuiescenceWitness{},why));
 assert(!threads::try_native_quiescence(&wrong,ticket,10,why));assert(!threads::try_native_quiescence(&owner.cpu,{},10,why));restored(owner,before);
 {auto cap=threads::try_native_quiescence(&owner.cpu,ticket,20,why);assert(cap&&cap->current(&owner.cpu)&&!cap->current(&wrong)&&threads::current_thread_holds_native_quiescence());assert(!std::memcmp(&owner.cpu,&before,sizeof(Cpu)));assert(!threads::current_quiescence(&owner.cpu));
  std::thread foreign([&]{assert(!cap->current(&owner.cpu)&&!threads::current_thread_holds_native_quiescence());});foreign.join();
  assert(!threads::try_native_quiescence(&owner.cpu,ticket,1,why));assert(!threads::quiesce(1,why));threads::thaw();assert(cap->current(&owner.cpu));
  {story_native_stage_owner::Tracker stage;auto pin=stage.scan();assert(story_native_stage_owner::current_thread_holds_lifetime_pin()&&cap->current(&owner.cpu));}
  auto moved=std::move(*cap);assert(!cap->current(&owner.cpu)&&moved.current(&owner.cpu));cap.reset();assert(moved.current(&owner.cpu));
 }restored(owner,before);assert(!threads::current_thread_holds_native_quiescence());
 // A registered service still being constructed is never excluded as merely
 // unstarted; publishing initialized is necessary even when wst is Parked.
 HostThread service;setup(service,0x20040000,0,true);service.started=false;service.initialized=false;service.wst=kParked;
 assert(!threads::try_native_quiescence(&owner.cpu,ticket,4,why));restored(owner,before);service.started=true;service.initialized.store(true,std::memory_order_release);
 // Resetting entry parking after success must not open the actual freeze gate.
 {auto cap=threads::try_native_quiescence(&owner.cpu,ticket,20,why);assert(cap&&cap->current(&owner.cpu)&&g_entry_parks==0);std::promise<void>entered;auto ready=entered.get_future();std::atomic<bool>ran=false;
  std::thread worker([&]{t_self=&service;t_cpu=&service.cpu;entered.set_value();threads::service_begin();ran=true;threads::service_end();});ready.wait();std::this_thread::sleep_for(std::chrono::milliseconds(3));assert(!ran&&cap->current(&owner.cpu));cap.reset();worker.join();assert(ran&&service.wst==kParked);
 }restored(owner,before);
 // Physical parking can retain a Stage mutex acquired before a native entry
 // safepoint. The owner must refuse try_scan and thaw instead of blocking.
 {story_native_stage_owner::Tracker stage;std::promise<void>locked,unlock;auto ready=locked.get_future();auto release=unlock.get_future();
  std::thread holder([&]{t_self=&service;t_cpu=&service.cpu;auto pin=stage.scan();locked.set_value();release.wait();});ready.wait();
  auto cap=threads::try_native_quiescence(&owner.cpu,ticket,20,why);assert(cap&&cap->current(&owner.cpu));{auto denied=stage.try_scan();assert(!denied.valid()&&!story_native_stage_owner::current_thread_holds_lifetime_pin());}assert(cap->current(&owner.cpu));cap.reset();unlock.set_value();holder.join();
 }restored(owner,before);
 // A real HLE waiter wakes its condition while frozen, but park_gate prevents
 // its guest continuation and keeps its core released until the guard thaws.
 HostThread waiter;setup(waiter,0x20050000,0);std::mutex wait_mutex;std::condition_variable wait_cv;std::atomic<bool>wake=false,ran=false;
 std::thread waiting([&]{t_self=&waiter;t_cpu=&waiter.cpu;core_acquire(&waiter);std::unique_lock lock(wait_mutex);assert(park_wait(lock,wait_cv,[&]{return wake.load();},W_EVENT,0));ran=true;lock.unlock();core_release(&waiter);waiter.wst=kParked;});
 while(waiter.wst!=kParked)std::this_thread::yield();
 {auto cap=threads::try_native_quiescence(&owner.cpu,ticket,20,why);assert(cap);wake=true;wait_cv.notify_all();std::this_thread::sleep_for(std::chrono::milliseconds(3));assert(!ran&&cap->current(&owner.cpu)&&g_entry_parks==0);cap.reset();}waiting.join();assert(ran);remove(waiter);restored(owner,before);
 // A new registrar cannot insert a thread or acquire a guest core while the
 // actual registry pin is retained. Native thread_main also gates first entry.
 HostThread newcomer;newcomer.guest=0x20060000;newcomer.initialized=true;std::atomic<bool>registered=false;std::thread registrar;
 {auto cap=threads::try_native_quiescence(&owner.cpu,ticket,20,why);assert(cap);registrar=std::thread([&]{assert(!t_self);std::lock_guard lock(g_threads_mutex);g_threads[newcomer.guest]=&newcomer;registered=true;});std::this_thread::sleep_for(std::chrono::milliseconds(3));assert(!registered&&cap->current(&owner.cpu));cap.reset();}registrar.join();assert(registered);remove(newcomer);restored(owner,before);
 // Timeout on an actually running entry restores every Cpu byte, requester
 // state and original emulated core ownership without borrowing another freeze.
 service.wst=kRunning;assert(!threads::try_native_quiescence(&owner.cpu,ticket,4,why));restored(owner,before);service.wst=kParked;
 assert(threads::quiesce(20,why));auto witness=threads::current_quiescence(&owner.cpu);assert(witness&&witness->current(&owner.cpu)&&!witness->current(&wrong));
 assert(!threads::quiesce(1,why));assert(!threads::current_quiescence(&owner.cpu)&&!witness->current(&owner.cpu));assert(!threads::borrow_native_read_quiescence(&owner.cpu,ticket,*witness,why));threads::thaw();assert(g_frozen&&witness->current(&owner.cpu));
 std::thread foreign([&]{threads::thaw();assert(!witness->current(&owner.cpu));});foreign.join();assert(witness->current(&owner.cpu));

 // Borrowing the genuine legacy owner pins the registry without freezing,
 // reacquiring a core or thawing the outer save operation.
 {story_native_stage_owner::Tracker stage;auto pin=stage.scan();assert(!threads::borrow_native_read_quiescence(&owner.cpu,ticket,*witness,why));}
 assert(!threads::borrow_native_read_quiescence(&wrong,ticket,*witness,why));assert(!threads::borrow_native_read_quiescence(&owner.cpu,{},*witness,why));
 {const auto core_held=owner.holds_core;const Cpu legacy_cpu=owner.cpu;
  auto cap=threads::borrow_native_read_quiescence(&owner.cpu,ticket,*witness,why);assert(cap&&cap->read_only()&&cap->current(&owner.cpu));
  assert(threads::current_thread_holds_native_quiescence()&&threads::current_thread_holds_read_only_native_quiescence());
  assert(witness->current(&owner.cpu)&&threads::current_quiescence(&owner.cpu));
  assert(!threads::borrow_native_read_quiescence(&owner.cpu,ticket,*witness,why));
  {story_native_stage_owner::Tracker stage;auto pin=stage.try_scan();assert(pin.holds_lifetime_of(stage)&&cap->current(&owner.cpu)&&witness->current(&owner.cpu));}
  std::atomic<bool>registry_acquired=false;std::promise<void>serialize_started;auto serialization_ready=serialize_started.get_future();
  std::thread waiting([&]{assert(!cap->current(&owner.cpu)&&!threads::current_thread_holds_native_quiescence());serialize_started.set_value();
   // Exercise the actual serializer's registry acquisition, not a substitute
   // mutex. It must wait until the capture-only borrowed window is dropped.
   ss::Writer snapshot;std::string refused;assert(threads_ss_save(snapshot,refused));ss::Reader r(snapshot.b.data(),snapshot.b.size());
   assert(r.u32()==1);const auto saved=r.pod<ThreadRec>();assert(r.ok&&saved.guest==owner.guest&&saved.wst==kRequester&&!saved.cpu.thread);registry_acquired=true;
  });
  serialization_ready.wait();std::this_thread::sleep_for(std::chrono::milliseconds(2));assert(!registry_acquired);
  auto moved=std::move(*cap);assert(!cap->current(&owner.cpu)&&moved.current(&owner.cpu)&&moved.read_only());cap.reset();
  assert(owner.holds_core==core_held&&!std::memcmp(&legacy_cpu,&owner.cpu,sizeof(Cpu)));
  {auto final=std::move(moved);assert(final.current(&owner.cpu));}
  waiting.join();assert(registry_acquired&&g_frozen&&witness->current(&owner.cpu));
  assert(!threads::current_thread_holds_native_quiescence()&&!threads::current_thread_holds_read_only_native_quiescence());
 }
 // Full capture serializes the actual thread registry after the short model
 // observation window. Keep the legacy park, but release its borrowed registry
 // pin first: retaining that pin here self-deadlocked the real game in070.
 {ss::Writer serialized;assert(!threads::current_thread_holds_native_quiescence());
  assert(threads_ss_save(serialized,why)&&g_frozen&&witness->current(&owner.cpu));
  ss::Reader snapshot(serialized.b.data(),serialized.b.size());assert(snapshot.u32()==1&&snapshot.ok);
 }
 {auto cap=threads::borrow_native_read_quiescence(&owner.cpu,ticket,*witness,why);assert(cap);story_native_event_game_thread::state_loaded();assert(!cap->current(&owner.cpu)&&witness->current(&owner.cpu));}
 story_native_event_game_thread::state_loaded();assert(witness->current(&owner.cpu));threads::thaw();assert(!witness->current(&owner.cpu));restored(owner,before);
 assert(threads::quiesce(20,why));auto newer=threads::current_quiescence(&owner.cpu);assert(newer&&newer->current(&owner.cpu)&&!witness->current(&owner.cpu));threads::thaw();assert(!newer->current(&owner.cpu));restored(owner,before);
 assert(story_native_event_game_thread::observe_logic(&owner.cpu));ticket=story_native_event_game_thread::ticket(&owner.cpu);
 assert(threads::quiesce(20,why));auto fresh=threads::current_quiescence(&owner.cpu);assert(fresh);
 {auto cap=threads::borrow_native_read_quiescence(&owner.cpu,ticket,*fresh,why);assert(cap);threads::thaw();assert(!cap->current(&owner.cpu)&&!fresh->current(&owner.cpu));}
 restored(owner,before);
 // Freeze epochs never wrap or let a rejected claimant release another owner.
 threads::detail::FreezeOwnership epochs(UINT64_MAX-1);threads::detail::FreezeClaim a,b;assert(epochs.claim(threads::detail::FreezeKind::Native,&owner,a)&&a.epoch==UINT64_MAX);assert(!epochs.claim(threads::detail::FreezeKind::Legacy,&service,b)&&!epochs.release(b));assert(epochs.release(a)&&!epochs.claim(threads::detail::FreezeKind::Native,&owner,b));
 remove(service);remove(owner);
 // The real scheduler starts a detached safety tick thread. End this isolated
 // process after all assertions rather than destruct its live global CV.
 std::_Exit(0);
}
