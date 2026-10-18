// SPDX-License-Identifier: MPL-2.0
// Two real Sessions/Models/Contexts; only scheduler/dispatch/native bodies are
// isolated primitives. No opaque Result, revocation or restore proof is forged.
#include "mods/story_native_restore_front.h"
#include "mods/story_native_wait_runtime_controller.h"
#include "mods/story_native_restore_resume.h"
#include "mods/story_native_restore_current_owner.h"
#include "mods/story_native_stage_restore_group.h"
#include "mods/story_native_wait_runtime_model_graph.h"
#include "runtime.h"
#include <cassert>
#include <functional>
#include <optional>
#include <cstdint>
#include <utility>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
namespace guestmods {class NativeRestoreTrackerIssuer {public:
 static auto acquire(const story_native_restore_resume::Boundary&b,Cpu*c){using namespace story_native_event_depth;return Tracker::restore_provisional(b,c,{{1,1,true},{2,2,true},{3,3,true},true},[]{return true;});}
};}
namespace story_native_wait_runtime {class NativeResumeFrontIssuer {public:
 static auto capture(std::atomic<story_native_wait_runtime_controller::Context*>&c,std::atomic<story_native_event_depth::Tracker*>&d){return story_native_restore_front::Front(c,d);}
};}
static uint8_t*cross_map(){
#ifdef _WIN32
 auto*p=VirtualAlloc(PPC_MEM_BASE+0x60050000,0x7000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
 auto*p=mmap(PPC_MEM_BASE+0x60050000,0x7000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0);
#endif
 assert(p==PPC_MEM_BASE+0x60050000);return static_cast<uint8_t*>(p);
}
static void cross_unmap(uint8_t*p){
#ifdef _WIN32
 assert(VirtualFree(p,0,MEM_RELEASE));
#else
 assert(!munmap(p,0x7000));
#endif
}
static std::function<void(Cpu*,const story_native_restore_resume::Boundary&)>cross_observer;
#define STORY_NATIVE_RUN_REAL_MODEL
#define STORY_NATIVE_RUN_REAL_MODEL_TURN
#define STORY_NATIVE_RESTORE_CASE_COUNT 1
#define STORY_NATIVE_CROSS_SESSION_MULTI_ARENA
#define STORY_NATIVE_ADOPT_OBSERVER(c,boundary,...) cross_observer(c,boundary)
#define STORY_NATIVE_CROSS_SESSION_BEFORE_START const auto cross_retail_memory=f.memory
#define STORY_NATIVE_CROSS_SESSION_SETUP \
 auto b_mapping=std::unique_ptr<uint8_t,void(*)(uint8_t*)>(cross_map(),cross_unmap); \
 auto b_request=req;b_request.program=story_native_event_program::Program::turn(turn,302); \
 S::Session b_session(b_request,f.source());assert(b_session.startup());story_native_event_arena::Layout b_arena; \
 std::optional<S::PristineCheckpoint>b_pristine; \
 {auto h=b_session.try_fence();auto parked=threads::current_quiescence(&cpu);assert(h&&parked);b_arena=b_session.arena();assert(b_arena.allocation.address==0x60050000&&b_arena.allocation.address!=arena.allocation.address);auto capture=b_session.begin_pristine_capture(&cpu,*parked);assert(capture);b_pristine=b_session.commit_pristine_capture(std::move(*capture),&cpu,*parked);assert(b_pristine);}
#define STORY_NATIVE_CROSS_SESSION_FLOW_INCLUDE "story_native_wait_cross_session_restore_flow.inc"
#include "story_native_wait_run_restore_test.cpp"
#include <optional>
#include <cstdint>
#include <utility>
#include <memory>
