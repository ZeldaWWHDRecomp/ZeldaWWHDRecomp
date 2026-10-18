// SPDX-License-Identifier: MPL-2.0
// Genuine Context/Session/opaque saved origin/ModelContext/Controller adoption.
// Only scheduler and sealed-route coverage issuance are synthetic primitives.
#include "mods/story_native_wait_runtime_controller.h"
#include "mods/story_native_restore_resume.h"
#include "mods/story_native_wait_runtime_model_graph.h"
#include <cassert>
#include <optional>
#include <cstdint>
#include <span>
namespace guestmods {class NativeRestoreTrackerIssuer {public:
 static auto acquire(const story_native_restore_resume::Boundary&b,Cpu*c){using namespace story_native_event_depth;return Tracker::restore_provisional(b,c,{{1,1,true},{2,2,true},{3,3,true},true},[]{return true;});}
};}
template<class Session,class Expected,class Arena,class Config,class Callbacks,class Sources,class Original,class OldDepth>
void saved_adoption_case(Cpu*c,const story_native_restore_resume::Boundary&b,Session&session,const Expected&e,const Arena&arena,const Config&config,const Callbacks&callbacks,Sources source,unsigned&native_calls,[[maybe_unused]]Original*original,[[maybe_unused]]OldDepth&old_depth,[[maybe_unused]]unsigned fault){
 namespace RC=story_native_wait_runtime_controller;namespace GT=story_native_event_game_thread;namespace D=story_native_event_depth;
 auto tracker=guestmods::NativeRestoreTrackerIssuer::acquire(b,c);assert(tracker&&tracker->restoring(b));
 auto stage=story_native_stage_owner_observer::snapshot();assert(stage);
 std::optional<story_native_wait_runtime_backend::Bundle>backend;
 {auto fence=session.try_fence();assert(fence);backend=story_native_wait_runtime_backend::make(e,arena,config,*stage,GT::ticket(c),c,*tracker,callbacks);assert(backend);}
 unsigned writes=0;const auto write=source.write;source.write=[&](auto a,auto bytes){++writes;return write(a,bytes);};
 const auto calls_before=native_calls;
 // Caller-provided names cannot replace original host run references, and
 // a failed fresh read must not consume a valid later adoption attempt.
 assert(!RC::Context::prepare_restored(b,session,*tracker,*stage,GT::ticket(c),c,*backend,{{{"test","foreign"},{"test","second"}}},source));
 auto unreadable=source;unreadable.restore_read=[](uint32_t,std::span<uint8_t>){return false;};
 assert(!RC::Context::prepare_restored(b,session,*tracker,*stage,GT::ticket(c),c,*backend,{{{"test","first"},{"test","second"}}},unreadable));
 assert(!threads::current_thread_holds_native_quiescence()&&!story_native_stage_owner::current_thread_holds_lifetime_pin()&&!session.fence_held());

 auto restored=RC::Context::prepare_restored(b,session,*tracker,*stage,GT::ticket(c),c,*backend,{{{"test","first"},{"test","second"}}},source);
 assert(restored&&restored->state()==RC::State::RestoredDormant&&restored->retained());
 assert(writes==0&&native_calls==calls_before&&tracker->restoring(b)&&!tracker->ready()&&!tracker->cancel_drain());
 assert(restored->tick(c,true)==RC::State::RestoredDormant);
 D::Call call;c->r[3]=e.identity.manager;assert(restored->outer(c,D::entry_address(D::Entry::ManagerRun),call)&&call.authority==D::Authority::InvalidOwned);
 unsigned bodies=0;assert(restored->route(c,D::entry_address(D::Entry::ManagerRun),[&]{++bodies;})==story_native_wait_owned_routes::Decision::Handled&&bodies==0);
 assert(!restored->take_completion()&&!restored->rearm(c));
 // Exact same post-service epoch cannot transfer the saved host claim twice.
 assert(!RC::Context::prepare_restored(b,session,*tracker,*stage,GT::ticket(c),c,*backend,{{{"test","first"},{"test","second"}}},source));
 assert(writes==0&&native_calls==calls_before);
#ifdef STORY_NATIVE_FINAL_ADOPT_OBSERVER
 STORY_NATIVE_FINAL_ADOPT_OBSERVER(*restored,c,b,session,*tracker,e,original,old_depth,writes,native_calls,calls_before,fault);
#endif
}
#define STORY_NATIVE_RUN_REAL_MODEL
#define STORY_NATIVE_RUN_REAL_MODEL_TURN
#define STORY_NATIVE_ADOPT_OBSERVER(...) saved_adoption_case(__VA_ARGS__)
#include "story_native_wait_run_restore_test.cpp"
#include <optional>
#include <cstdint>
#include <span>
