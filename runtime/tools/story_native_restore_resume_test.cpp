// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_restore_resume.h"
#include "mods/story_native_event_game_thread.h"
#include "runtime.h"
#include <cassert>
#include <thread>
#include <algorithm>
#include <optional>
#include <cstdint>
#include <span>
#include <utility>
#include <functional>
#include <memory>
// Standalone primitive scheduler and source-issuer fixtures only. This target
// does not link savestate/interp, runtime Context, native routes or admission.
namespace {thread_local Cpu*actual=nullptr;thread_local uint32_t guest=1;bool frozen=false,route_current=true;unsigned origin_fault=0;uint64_t freeze_epoch=0;
story_native_event_depth::Coverage coverage(){story_native_event_depth::HookObserver h{1,1,true};return {h,h,h,true};}}
namespace threads {
Cpu*current(){return actual;}uint32_t current_thread(){return guest;}
class NativeQuiescenceIssuer {public:static QuiescenceWitness witness(){const auto epoch=freeze_epoch;auto*c=actual;auto g=guest;return QuiescenceWitness([=](const Cpu*p){return frozen&&freeze_epoch==epoch&&p==c&&actual==c&&guest==g;});}};
std::optional<QuiescenceWitness>current_quiescence(Cpu*c){if(!frozen||c!=actual)return {};return NativeQuiescenceIssuer::witness();}
}
namespace story_native_stage_owner {bool current_thread_holds_lifetime_pin()noexcept{return false;}}
namespace story_native_wait_runtime_model_graph {class Context {public:
 static auto receipt(const story_native_event_observation::Expected&e){auto lineage=story_native_model_saved_graph::Lineage::issue();story_native_model_saved_graph::Trace t{{{0x30000000,4}},{{0x30000004,4}}};return story_native_model_saved_graph::Receipt::capture(e,lineage,t,[](uint32_t,std::span<uint8_t>b){std::fill(b.begin(),b.end(),0);return true;});}
};}
namespace story_native_wait_runtime_controller {class Context {public:
 static auto completion(story_native_event_observation::Expected e,std::shared_ptr<story_native_wait_runtime_model_graph::Context>m,uint64_t epoch,const threads::QuiescenceWitness&w,std::function<void(Cpu*,const story_native_restore_resume::Boundary&)>o){auto receipt=story_native_wait_runtime_model_graph::Context::receipt(e);assert(receipt);story_native_event_memory::Scope saved_scope;saved_scope.identity=e.identity;saved_scope.logic_step=17;saved_scope.serial=0;saved_scope.exclusive=true;story_native_event_depth::Tracker original(e,coverage());assert(original.ready());auto record=original.record();std::optional<story_native_event_collect::Clock>clock;
 if(origin_fault==1)record[92]^=1;if(origin_fault==2)++saved_scope.serial;if(origin_fault==3)saved_scope.foreign=true;if(origin_fault==4)++saved_scope.identity.generation;if(origin_fault==5)clock=story_native_event_collect::Clock{e.identity,17,0,0};
 if(origin_fault==6||origin_fault==7){if(origin_fault==6)record[67]=2;else{for(unsigned n=68;n<76;++n)record[n]=0xFF;saved_scope.serial=UINT64_MAX;}uint32_t hash=2166136261u;for(unsigned n=0;n<92;++n)hash=(hash^record[n])*16777619u;for(unsigned n=0;n<4;++n)record[92+n]=uint8_t(hash>>(24-8*n));}
 story_native_restore_resume::SavedRunOrigin origin(e,record,saved_scope,clock,m);return story_native_restore_resume::Completion::make(e,m,std::move(*receipt),std::move(origin),epoch,actual,w,std::move(o));}
 static bool publish(story_native_restore_resume::Completion&c){return c.publish();}
};}
namespace guestmods {class NativeRestoreTrackerIssuer {public:
 static auto acquire(const story_native_restore_resume::Boundary&b,const Cpu*c){return story_native_event_depth::Tracker::restore_provisional(b,c,coverage(),[]{return route_current;});}
};}
namespace ss {class NativeRestorePipelineIssuer {public:
 static auto begin(Cpu*c){return std::unique_ptr<story_native_restore_resume::LoadWindow>(new story_native_restore_resume::LoadWindow(c));}
 static void loaded(story_native_restore_resume::LoadWindow&w){w.threads_loaded();}
 static void thawed(story_native_restore_resume::LoadWindow&w){w.after_thaw();}
};}
namespace interp {class NativeResumeBoundaryIssuer {public:static void issue(Cpu*c){story_native_restore_resume::LoadWindow::service_return(c);}};}
int main(){
 namespace G=story_native_event_game_thread;using C=story_native_wait_runtime_controller::Context;using P=ss::NativeRestorePipelineIssuer;using B=interp::NativeResumeBoundaryIssuer;
 Cpu cpu{},wrong{};actual=&cpu;assert(G::observe_logic(&cpu));auto old=G::ticket(&cpu);assert(G::verify(&cpu,old));
 auto model=std::make_shared<story_native_wait_runtime_model_graph::Context>();story_native_event_observation::Expected e;e.identity.manager=0x20010000;e.identity.image=0x30000000;e.identity.bytes=0x520;e.identity.stage=1;e.identity.room=2;e.identity.generation=2;e.identity.program=3;e.camera.address=0x40000248;e.camera.pid=2;for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x41000000+n*0x1000;a.pid=10+n;a.token=1+n;a.profile=0x175;a.subtype=1;}
 unsigned callbacks=0;bool throw_observer=false;std::unique_ptr<story_native_event_depth::Tracker>previous;
 auto observer=[&](Cpu*c,const story_native_restore_resume::Boundary&b){++callbacks;assert(c==&cpu&&b.current(c)&&!b.current(&wrong)&&b.completion().original_model()==model&&b.completion().expected()==e&&b.completion().restore_epoch()==9&&G::verify(c,G::ticket(c))&&!G::verify(c,old));
  if(previous)assert(!previous->restoring(b));
  auto seed=guestmods::NativeRestoreTrackerIssuer::acquire(b,c);if(origin_fault){assert(!seed);return;}assert(seed&&seed->restoring(b));
  assert(seed->snapshot().idle()&&seed->snapshot().state==story_native_event_depth::State::Draining&&!seed->ready()&&!seed->cancel_drain()&&!seed->begin_drain());
  story_native_event_depth::Call entry;entry.authority=story_native_event_depth::Authority::Owned;{auto scope=seed->enter(entry);assert(!scope.allowed());}assert(seed->restoring(b));
  route_current=false;assert(!seed->restoring(b)&&!guestmods::NativeRestoreTrackerIssuer::acquire(b,c));route_current=true;assert(seed->restoring(b));previous=std::move(seed);
  if(throw_observer)throw 1;};
 for(unsigned mode=0;mode<7;++mode){
  G::state_loaded();assert(!G::ticket(&cpu).epoch);frozen=true;++freeze_epoch;auto witness=threads::current_quiescence(&cpu);assert(witness);
  auto completion=C::completion(e,model,9,*witness,observer);assert(completion);
  assert(!C::publish(*completion)); // copied bytes cannot create a pipeline
  auto window=P::begin(&cpu);assert(C::publish(*completion));assert(!C::publish(*completion));
  B::issue(&cpu);assert(!G::ticket(&cpu).epoch); // still inside legacy freeze
  if(mode==5)guest=2;
  if(mode!=1)P::loaded(*window);guest=1;throw_observer=mode==6;
  frozen=false;P::thawed(*window);window.reset();
  if(mode==2){B::issue(&wrong);B::issue(&cpu);}
  else if(mode==3){G::state_loaded();B::issue(&cpu);}
  else if(mode==4){std::thread other([&]{actual=&cpu;B::issue(&cpu);assert(!G::ticket(&cpu).epoch);});other.join();B::issue(&cpu);}
  else B::issue(&cpu);
  const bool success=mode!=1&&mode!=2&&mode!=3&&mode!=5;
  assert(bool(G::ticket(&cpu).epoch)==success);auto before=callbacks;B::issue(&cpu);assert(callbacks==before);
 }
 assert(callbacks==3);
 G::state_loaded();frozen=true;++freeze_epoch;{auto window=P::begin(&cpu);P::loaded(*window);frozen=false;P::thawed(*window);}B::issue(&cpu);assert(!G::ticket(&cpu).epoch);
 for(origin_fault=1;origin_fault<=7;++origin_fault){G::state_loaded();frozen=true;++freeze_epoch;auto witness=threads::current_quiescence(&cpu);assert(witness);auto completion=C::completion(e,model,9,*witness,observer);assert(completion);auto window=P::begin(&cpu);assert(C::publish(*completion));P::loaded(*window);frozen=false;P::thawed(*window);window.reset();B::issue(&cpu);assert(G::verify(&cpu,G::ticket(&cpu)));}origin_fault=0;
 // A new freeze cannot satisfy the earlier successful owner's witness.
 G::state_loaded();frozen=true;++freeze_epoch;auto stale=threads::current_quiescence(&cpu);++freeze_epoch;assert(!C::completion(e,model,9,*stale,observer));frozen=false;
}
