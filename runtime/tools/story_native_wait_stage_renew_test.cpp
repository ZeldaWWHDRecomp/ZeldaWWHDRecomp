// SPDX-License-Identifier: MPL-2.0
// Real Session, Backend, observer, model graph and Context renewal. Only the
// physical scheduler parking and sealed-dispatch primitives are synthetic.
#include <future>
#define STORY_NATIVE_RUN_REAL_MODEL_TURN
#define STORY_NATIVE_RESTORE_CASE_COUNT 1
#define STORY_NATIVE_STAGE_RENEW_FLOW do { \
 assert(!context->stage_renewal_needed());assert(context->renew_stage(&cpu)&&native_calls==7); \
 const auto churn=observer->before_setinfo(&cpu,args);assert(churn); \
 observer->after_setinfo(&cpu,args,1,churn); \
 {std::promise<void> locked,release;auto done=release.get_future();std::thread holder([&]{std::lock_guard guard(*observer->lifetime_fence);locked.set_value();done.wait();});locked.get_future().wait();assert(!context->stage_renewal_needed());release.set_value();holder.join();} \
 assert(context->stage_renewal_needed()); \
 const auto newer=SO::snapshot();assert(newer&&newer->control==stage->control&&newer->receipt.owner==stage->receipt.owner&&newer->receipt.generation!=stage->receipt.generation); \
 {auto fence=session.try_fence();assert(fence);story_native_event_memory::Scope denied;assert(!backend->sources.session(denied));} \
 Cpu foreign=cpu;assert(!context->renew_stage(&foreign)&&native_calls==7); \
 assert(context->renew_stage(&cpu)&&native_calls==7);assert(!context->stage_renewal_needed()); \
 {auto fence=session.try_fence();assert(fence);story_native_event_memory::Scope admitted;assert(backend->sources.session(admitted));assert(admitted.identity==expected.identity);} \
 assert(context->renew_stage(&cpu)&&native_calls==7); \
 const auto retry_churn=observer->before_setinfo(&cpu,args);assert(retry_churn);observer->after_setinfo(&cpu,args,1,retry_churn); \
 w(expected.actors[1].address+4,expected.actors[1].pid+1);assert(!context->renew_stage(&cpu)&&native_calls==7);w(expected.actors[1].address+4,expected.actors[1].pid); \
 f.byte(play+0x51D0+0xC2,1);assert(!context->renew_stage(&cpu)&&native_calls==7);f.byte(play+0x51D0+0xC2,3); \
 {auto fence=session.try_fence();assert(fence);story_native_event_memory::Scope still_denied;assert(!backend->sources.session(still_denied));} \
 assert(context->renew_stage(&cpu)&&native_calls==7);assert(!threads::current_thread_holds_native_quiescence()); \
 fixture_frozen=true;++freeze_epoch;auto saved=context->begin_saved_run();assert(saved&&native_calls==7);assert(context->commit_saved_run(*saved)&&native_calls==7); \
 fixture_frozen=false;++freeze_epoch; \
 } while(false)
#include "story_native_wait_saved_model_test.cpp"
