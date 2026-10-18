// SPDX-License-Identifier: MPL-2.0
// Real Context/Controller/Install revocation and Session pristine adoption.
// Scheduler parking and actual-front issuance remain isolated primitives, as
// in the parent Context fixture; no production capability factory is mocked.
#include "mods/story_native_wait_pristine_snapshot.h"
#include "mods/story_native_restore_current_owner.h"
#include "mods/story_native_restore_front.h"
#include "mods/story_native_wait_runtime_controller.h"
#include <cassert>
#include <algorithm>
#include <utility>
#include <memory>
namespace PS=story_native_wait_pristine_snapshot;
namespace story_native_wait_runtime {class NativeResumeFrontIssuer {public:
 static auto capture(std::atomic<story_native_wait_runtime_controller::Context*>&c,std::atomic<story_native_event_depth::Tracker*>&d){return story_native_restore_front::Front(c,d);}
};}
#define STORY_NATIVE_RUN_REAL_MODEL
#define STORY_NATIVE_RUN_REAL_MODEL_TURN
#define STORY_NATIVE_RESTORE_CASE_COUNT 4
#define STORY_NATIVE_CONTEXT_PRISTINE_BEFORE_NATIVE \
 (void)covered;SO::state_loaded();current_cpu=&cpu;fixture_frozen=true;++freeze_epoch; \
 auto pristine_park=threads::current_quiescence(&cpu);assert(pristine_park); \
 auto pristine_hold=session.try_fence();assert(pristine_hold); \
 auto pristine_capture=PS::begin(session,&cpu,*pristine_park);assert(pristine_capture); \
 SavedMemory pristine_memory(f,mapped,session.arena());const auto pristine_objects=f.memory; \
 auto pristine_checkpoint=PS::commit(std::move(*pristine_capture),&cpu,*pristine_park);assert(pristine_checkpoint&&!pristine_checkpoint->has_stage()&&pristine_checkpoint->matches_saved(pristine_memory.view())); \
 pristine_hold.reset();fixture_frozen=false;++freeze_epoch;current_cpu=nullptr
#define STORY_NATIVE_CONTEXT_PRISTINE_AFTER_NATIVE \
 fixture_frozen=true;++freeze_epoch; \
 auto parked=threads::current_quiescence(&cpu);assert(parked); \
 std::atomic<RC::Context*>front_context{context.get()};std::atomic<D::Tracker*>front_depth{&depth}; \
 if(fault==1){assert(!PS::prepare(session,*pristine_checkpoint,pristine_memory.view(),&cpu,*parked,1));assert(!session.fence_held()&&context->state()==RC::State::Started);} \
 auto owner=RC::Context::prepare_current_load(story_native_wait_runtime::NativeResumeFrontIssuer::capture(front_context,front_depth),pristine_memory.view(),2); \
 assert(owner&&owner->holds_session(session)&&owner->current(&cpu,*parked,pristine_memory.view(),2)&&session.fence_held()&&context->state()==RC::State::Quarantined&&native_calls==7); \
 if(fault==2){ss::MemoryChunks alien=pristine_memory.chunks;const auto wrong=ss::native_gate::SavedView{ss::Reader(pristine_memory.allocation.b.data(),pristine_memory.allocation.b.size()),pristine_memory.regions,alien};assert(!PS::prepare(session,*pristine_checkpoint,wrong,&cpu,*parked,2,owner));assert(owner->current(&cpu,*parked,pristine_memory.view(),2));} \
 auto restore=PS::prepare(session,*pristine_checkpoint,pristine_memory.view(),&cpu,*parked,2,owner);assert(restore&&session.fence_held()); \
 if(fault==3){restore.reset();assert(session.quarantined()&&!session.expected()&&native_calls==7);owner->close();assert(!session.fence_held());} \
 else{ \
  for(const auto&[at,b]:pristine_objects){auto*p=f.pointer(at,b.size());assert(p);std::copy(b.begin(),b.end(),p);} \
  pristine_memory.restore(write,mapped,arena);SO::state_loaded();GT::state_loaded(); \
  assert(owner->current(&cpu,*parked,pristine_memory.view(),2)); \
  std::unique_ptr<S::Session::Fence>copied_fence;assert(PS::finish(std::move(*restore),pristine_memory.view(),&cpu,*parked,&copied_fence)); \
  assert(!copied_fence&&owner->holds_session(session)&&session.fence_held()&&!session.unexposed()&&!session.expected()&&!session.quarantined()&&native_calls==7); \
  auto descendant=PS::begin(session,&cpu,*parked);assert(descendant);auto checkpoint2=PS::commit(std::move(*descendant),&cpu,*parked);assert(checkpoint2&&checkpoint2->matches_saved(pristine_memory.view())&&!checkpoint2->has_stage()); \
  owner->close();assert(!session.fence_held()); \
 }
#include "story_native_wait_run_restore_test.cpp"
#include <algorithm>
#include <utility>
#include <memory>
