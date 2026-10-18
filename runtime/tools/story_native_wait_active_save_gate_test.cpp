// SPDX-License-Identifier: MPL-2.0
// Genuine Context/Session/model capture through the registered gate contract.
// Scheduler/route issuance remains an isolated primitive, as in adoption tests.
#include "mods/story_native_wait_active_save_gate.h"
#define STORY_NATIVE_RESTORE_CASE_COUNT 2
#define STORY_NATIVE_ACTIVE_GATE_FLOW \
 do { \
  story_native_wait_active_save_gate::Controller gate(session,[&]{return context.get();}); \
  auto save_hold=ss::native_gate::try_begin_save(&gate); \
  assert(save_hold&&session.fence_held()&&!ss::native_gate::try_begin_save(&gate)); \
  assert(!threads::current_thread_holds_native_quiescence()); \
  SavedMemory saved(f,mapped,arena); \
  bool foreign_finish=true;std::thread([&]{foreign_finish=ss::native_gate::try_finish_save(&gate);}).join(); \
  assert(!foreign_finish&&session.fence_held()); \
  assert(ss::native_gate::try_finish_save(&gate)&&session.fence_held()); \
  assert(!threads::current_thread_holds_native_quiescence()); \
  assert(!ss::native_gate::try_finish_save(&gate)); \
  save_hold.reset();assert(!session.fence_held()); \
  const auto page=expected.actors[0].address&~(ss::kMemoryChunk-1); \
  const auto offset=expected.actors[0].address&(ss::kMemoryChunk-1); \
  saved.bytes.at(page)[offset]^=1; \
  assert(!ss::native_gate::try_begin_load(&gate,saved.view())&&!session.fence_held()); \
  saved.bytes.at(page)[offset]^=1; \
  auto load_hold=ss::native_gate::try_begin_load(&gate,saved.view()); \
  assert(load_hold&&session.fence_held()&&!ss::native_gate::try_begin_save(&gate)); \
  saved.restore(write,mapped,arena);context->invalidate();SO::state_loaded();GT::state_loaded(); \
  auto load_window=ss::NativeRestorePipelineIssuer::open(&cpu);assert(load_window); \
  if(fault)saved.bytes.at(page)[offset]^=1; \
  const bool finished=ss::native_gate::try_finish_load(&gate,saved.view()); \
  assert(finished==!fault&&session.fence_held()); \
  assert(!ss::native_gate::try_finish_load(&gate,saved.view())); \
  if(finished)ss::NativeRestorePipelineIssuer::threads_loaded(*load_window); \
  load_hold.reset();assert(!session.fence_held()); \
  fixture_frozen=false;++freeze_epoch; \
  if(finished)ss::NativeRestorePipelineIssuer::thawed(*load_window); \
  load_window.reset();interp::NativeResumeBoundaryIssuer::returned(&cpu); \
  assert(resume_boundaries==unsigned(!fault)&&native_calls==7); \
 } while(false)
#include "story_native_wait_restored_adoption_test.cpp"
