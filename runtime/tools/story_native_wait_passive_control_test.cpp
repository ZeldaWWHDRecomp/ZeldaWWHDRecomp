// SPDX-License-Identifier: MPL-2.0
// Real Readiness -> Session staging -> Backend -> Model -> Controller/Install.
// Only native function bodies/scheduler primitives use the shared fixture seam.
#define STORY_NATIVE_RUN_REAL_MODEL
#define STORY_NATIVE_RUN_REAL_MODEL_TURN
#define STORY_NATIVE_RESTORE_CASE_COUNT 1
#define STORY_NATIVE_PASSIVE_CONTROL_BEFORE_INSPECT \
 for(unsigned n=0;n<4;++n)f.put(play+0x51D0+0xC4+n*4,n?0x16C:0x110)
#define STORY_NATIVE_STAGE_RENEW_FLOW \
 do { \
  (void)covered;assert(native_calls==7&&context->state()==RC::State::Started&&context->retained()); \
  for(unsigned n=0;n<4;++n){auto*p=f.pointer(play+0x51D0+0xC4+n*4,4);assert(p&&p[0]==0xFF&&p[1]==0xFF&&p[2]==0xFF&&p[3]==0xFF);} \
  assert(f.pointer(play+0x51D0+0xC2,1)[0]==3); \
  fixture_frozen=true;++freeze_epoch;auto capture=context->begin_saved_run();assert(capture&&context->commit_saved_run(*capture)&&native_calls==7); \
 } while(false)
#include "story_native_wait_run_restore_test.cpp"
