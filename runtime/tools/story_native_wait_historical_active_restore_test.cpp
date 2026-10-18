// SPDX-License-Identifier: MPL-2.0
// Same-file repeated load against a DIFFERENT genuinely restored live Context.
// Physical scheduler/front/tracker issuers are isolated test primitives; all
// Session/graph/checkpoint/claim/Controller/Install/Stage proofs are real.
#include "mods/story_native_restore_front.h"
#include "mods/story_native_wait_runtime_controller.h"
#include "mods/story_native_restore_resume.h"
#include "mods/story_native_restore_current_owner.h"
#include "mods/story_native_stage_restore_group.h"
#include "mods/story_native_wait_runtime_model_graph.h"
#include <cassert>
#include <functional>
#include <array>
#include <vector>
#include <cstdint>
#include <utility>
#include <memory>
namespace guestmods {class NativeRestoreTrackerIssuer {public:
 static auto acquire(const story_native_restore_resume::Boundary&b,Cpu*c){using namespace story_native_event_depth;return Tracker::restore_provisional(b,c,{{1,1,true},{2,2,true},{3,3,true},true},[]{return true;});}
};}
namespace story_native_wait_runtime {class NativeResumeFrontIssuer {public:
 static auto capture(std::atomic<story_native_wait_runtime_controller::Context*>&c,std::atomic<story_native_event_depth::Tracker*>&d){return story_native_restore_front::Front(c,d);}
};}
static std::function<void(Cpu*,const story_native_restore_resume::Boundary&)>historical_observer;
#define STORY_NATIVE_RUN_REAL_MODEL
#define STORY_NATIVE_RUN_REAL_MODEL_TURN
#define STORY_NATIVE_RESTORE_CASE_COUNT 3
#define STORY_NATIVE_ADOPT_OBSERVER(c,boundary,...) historical_observer(c,boundary)
#define STORY_NATIVE_HISTORICAL_RESTORE_FLOW \
 { \
 std::vector<std::unique_ptr<D::Tracker>>restored_depths; \
 std::vector<std::unique_ptr<RC::Context>>restored_contexts; \
 std::atomic<RC::Context*>front_context{context.get()};std::atomic<D::Tracker*>front_depth{&depth}; \
 historical_observer=[&](Cpu*c,const story_native_restore_resume::Boundary&b){ \
  auto*old=front_context.load();assert(old&&old->suspended_for_restore(b)); \
  auto fresh=guestmods::NativeRestoreTrackerIssuer::acquire(b,c);assert(fresh); \
  const auto stage_now=SO::snapshot();assert(stage_now); \
  auto*new_depth=fresh.get();restored_depths.push_back(std::move(fresh)); \
  RC::Context::RestoredBackendFactory factory=[&](const auto&e,const auto&a){return RB::make(e,a,config,*stage_now,GT::ticket(c),c,*new_depth,restore_callbacks);}; \
  auto next=RC::Context::prepare_restored(b,session,*new_depth,*stage_now,GT::ticket(c),c,std::move(factory),{{{"test","first"},{"test","second"}}},restored_sources); \
  assert(next&&next->state()==RC::State::RestoredDormant&&next->retained()); \
  auto*next_ptr=next.get();restored_contexts.push_back(std::move(next)); \
  if(fault==1){std::atomic<RC::Context*>foreign_context{old};std::atomic<D::Tracker*>foreign_depth{front_depth.load()}; \
   assert(!next_ptr->resume_restored(b,story_native_wait_runtime::NativeResumeFrontIssuer::capture(foreign_context,foreign_depth))); \
   assert(front_context.load()==old&&!new_depth->ready()&&next_ptr->retained()&&next_ptr->state()==RC::State::Quarantined&&native_calls==7);return;} \
  assert(next_ptr->resume_restored(b,story_native_wait_runtime::NativeResumeFrontIssuer::capture(front_context,front_depth))); \
  actual_context=next_ptr;assert(new_depth->ready()&&front_context.load()==next_ptr&&native_calls==7); \
 }; \
 {std::atomic<RC::Context*>none_context{nullptr};std::atomic<D::Tracker*>none_depth{nullptr};auto absent=checkpoint->fork_attempt(); \
  assert(absent&&!context->prepare_run_restore(std::move(*absent),saved.view(),1,story_native_wait_runtime::NativeResumeFrontIssuer::capture(none_context,none_depth))); \
  assert(!session.fence_held()&&native_calls==7);} \
 for(uint64_t epoch=1;epoch<=3;++epoch){ \
  auto*live=front_context.load();assert(live&&live->retained()); \
  if(epoch>1){auto stale=checkpoint->fork_attempt();assert(stale&&!context->prepare_run_restore(std::move(*stale),saved.view(),epoch));} \
  auto group=story_native_stage_restore_group::StageRestoreGroup::create();assert(group); \
  const auto parked=threads::current_quiescence(&cpu);assert(parked); \
  std::array<uint8_t,0x100>pre_owner{};assert(read(selected.header.address,pre_owner)); \
  auto owner=RC::Context::prepare_current_load(story_native_wait_runtime::NativeResumeFrontIssuer::capture(front_context,front_depth),saved.view(),epoch); \
  assert(owner&&owner->current(&cpu,*parked,saved.view(),epoch)&&owner->holds_session(session)&&session.fence_held()&&live->state()==RC::State::Quarantined&&live->retained()); \
  S::Session unrelated(req,f.source());assert(!owner->holds_session(unrelated)); \
  auto different_regions=saved.regions;auto changed_view=saved.view();changed_view.regions=different_regions; \
  assert(!owner->current(&cpu,*parked,changed_view,epoch)&&!owner->current(&cpu,*parked,saved.view(),epoch+100)); \
  front_context.store(nullptr);assert(!owner->current(&cpu,*parked,saved.view(),epoch));front_context.store(live);assert(owner->current(&cpu,*parked,saved.view(),epoch)); \
  if(fault==2){owner->close();assert(!owner->current(&cpu,*parked,saved.view(),epoch)&&!session.fence_held());auto abandoned=checkpoint->fork_attempt(); \
   assert(abandoned&&!context->prepare_run_restore(std::move(*abandoned),saved.view(),epoch,story_native_wait_runtime::NativeResumeFrontIssuer::capture(front_context,front_depth),group,owner)); \
   std::array<uint8_t,0x100>after_owner{};assert(read(selected.header.address,after_owner)&&after_owner==pre_owner&&native_calls==7);group->close();break;} \
  auto sibling=group->join(*stage,saved.view(),&cpu,*parked);assert(sibling); \
  auto attempt=checkpoint->fork_attempt();assert(attempt); \
  auto permit=context->prepare_run_restore(std::move(*attempt),saved.view(),epoch,story_native_wait_runtime::NativeResumeFrontIssuer::capture(front_context,front_depth),group,owner);assert(permit&&session.fence_held()); \
  assert(context->suspend_run_restore(*permit)); \
  saved.restore(write,mapped,arena);live->invalidate();SO::state_loaded();GT::state_loaded(); \
  auto window=ss::NativeRestorePipelineIssuer::open(&cpu);assert(window&&context->finish_run_restore(std::move(*permit))); \
  const auto shared_stage=sibling->finish(saved.view(),&cpu,*parked);assert(shared_stage); \
  assert(SO::snapshot()->receipt==shared_stage->receipt);group->close();assert(!story_native_stage_owner::current_thread_holds_lifetime_pin()); \
  ss::NativeRestorePipelineIssuer::threads_loaded(*window);owner->close();assert(!session.fence_held());fixture_frozen=false;++freeze_epoch; \
  ss::NativeRestorePipelineIssuer::thawed(*window);window.reset();interp::NativeResumeBoundaryIssuer::returned(&cpu); \
  assert(restored_contexts.size()==epoch&&resume_boundaries==epoch&&native_calls==7);if(fault==1){assert(front_context.load()==live);break;} \
  assert(front_context.load()!=live); \
  assert(checkpoint->matches_saved(saved.view()));fixture_frozen=true;++freeze_epoch; \
 } \
 historical_observer={};actual_context=context.get();restored_contexts.clear();restored_depths.clear(); \
 }
#include "story_native_wait_run_restore_test.cpp"
#include <array>
#include <vector>
#include <cstdint>
#include <utility>
#include <memory>
