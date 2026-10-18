// SPDX-License-Identifier: MPL-2.0
// Fresh gate registry, real bootstrap Session and Stage-group restore. Only
// scheduler/front issuance uses the existing isolated trusted primitive seam.
#include "mods/story_native_wait_active_save_gate.h"
#include "mods/story_native_restore_current_owner.h"
#include <cstdint>
#include <span>
namespace story_native_wait_runtime {class NativeResumeFrontIssuer {public:
 static auto capture(std::atomic<story_native_wait_runtime_controller::Context*>&c,std::atomic<story_native_event_depth::Tracker*>&d){return story_native_restore_front::Front(c,d);}
};}
#define STORY_NATIVE_RUN_REAL_MODEL
#define STORY_NATIVE_RUN_REAL_MODEL_TURN
#define STORY_NATIVE_RESTORE_CASE_COUNT 6
#define STORY_NATIVE_INITIAL_IMPORT_FLOW \
 do { \
  (void)covered;current_cpu=&cpu;fixture_frozen=true;++freeze_epoch;unsigned native_calls=0; \
  auto initial_hold=session.try_fence();assert(initial_hold&&session.unexposed());const auto arena=session.arena();initial_hold.reset(); \
  SO::state_loaded();const auto*initial_observer=SO::observer();SO::Args initial_args{{mapped.relocated(resource_fixture::Ctrl),mapped.relocated(resource_fixture::Args),mapped.relocated(resource_fixture::Args+0x20),0}}; \
  const auto initial_cookie=initial_observer->before_setinfo(&cpu,initial_args);assert(initial_cookie);cpu.r[3]=1;initial_observer->after_setinfo(&cpu,initial_args,1,initial_cookie);assert(SO::snapshot()); \
  auto parked=threads::current_quiescence(&cpu);assert(parked); \
  SavedMemory saved(f,mapped,arena); \
  std::atomic<RC::Context*>front_context{nullptr};std::atomic<D::Tracker*>front_depth{nullptr}; \
  auto owner=RC::Context::prepare_current_load(story_native_wait_runtime::NativeResumeFrontIssuer::capture(front_context,front_depth),saved.view(),2);assert(owner); \
  auto group=story_native_stage_restore_group::StageRestoreGroup::create();assert(group); \
  story_native_wait_active_save_gate::Controller gate(session,[]{return static_cast<RC::Context*>(nullptr);}); \
  const auto set_saved=[&](uint32_t at,uint32_t value){auto&b=saved.bytes.at(at&~(ss::kMemoryChunk-1));const auto p=at&(ss::kMemoryChunk-1);for(unsigned n=0;n<4;++n)b[p+n]=uint8_t(value>>(24-8*n));}; \
  if(fault==1)set_saved(arena.allocation.address+story_native_wait_journal::kBytes,1); \
  if(fault==2)set_saved(GD(0x1046F0B0)+0x51D0+0xF4,arena.allocation.address); \
  if(fault==3)SO::state_loaded(); \
  if(fault==4)set_saved(f.e.actors[0].address+0x3E0,arena.allocation.address); \
  if(fault==5)saved.chunks.erase(GD(0x101F3328)&~(ss::kMemoryChunk-1)); \
  auto load=gate.begin_group_load(saved.view(),group,owner); \
  if(fault){assert(!load&&native_calls==0);} \
  else{ \
   assert(load&&session.fence_held()&&native_calls==0); \
   saved.restore([](uint32_t,std::span<const uint8_t>){return false;},mapped,arena);SO::state_loaded();GT::state_loaded(); \
   assert(gate.finish_load(saved.view())&&session.fence_held()&&session.unexposed()&&native_calls==0); \
   assert(!gate.finish_load(saved.view())); \
  } \
  group->close();load.reset();owner->close();assert(!session.fence_held()&&native_calls==0); \
 } while(false)
#include "story_native_wait_run_restore_test.cpp"
#include <cstdint>
#include <span>
