// SPDX-License-Identifier: MPL-2.0
// Genuine saved ModelContext/Completion/Boundary/Backend/Graph rebind. Only the
// scheduler and sealed-route coverage issuer are trusted primitive fixtures.
#include "mods/story_native_wait_runtime_model_graph.h"
#include "mods/story_native_restore_resume.h"
#include "mods/story_native_event_game_thread.h"
#include <cassert>
#include <string>
#include <cstdint>
#include <cstdio>
namespace guestmods {class NativeRestoreTrackerIssuer {public:
 static auto acquire(const story_native_restore_resume::Boundary&b,Cpu*c){using namespace story_native_event_depth;return Tracker::restore_provisional(b,c,{{1,1,true},{2,2,true},{3,3,true},true},[]{return true;});}
};}
template<class Session,class OldDepth,class Expected,class Arena,class Config,class Callbacks>
void saved_rebind_case(Cpu*c,const story_native_restore_resume::Boundary&b,Session&session,OldDepth&,const Expected&e,const Arena&arena,const Config&config,const Callbacks&callbacks,uint64_t&step){
 namespace M=story_native_wait_runtime_model_graph;namespace GT=story_native_event_game_thread;
 auto tracker=guestmods::NativeRestoreTrackerIssuer::acquire(b,c);assert(tracker&&tracker->restoring(b)&&!tracker->cancel_drain());
 auto stage=story_native_stage_owner_observer::snapshot();assert(stage&&b.completion().stage()&&stage->control==b.completion().stage()->control&&stage->receipt==b.completion().stage()->receipt);auto hold=session.try_fence();assert(hold);
 auto backend=story_native_wait_runtime_backend::make(e,arena,config,*stage,GT::ticket(c),c,*tracker,callbacks);assert(backend);
 const auto original=b.completion().original_model();assert(original);auto baseline=original->baseline(0);assert(baseline);
 std::string why;
 // Latest receipt data cannot substitute for the exact genuine finish receipt.
 auto wrong=*stage;++wrong.receipt.generation;assert(!original->rebind_saved(b,session,*tracker,wrong,GT::ticket(c),c,*backend,why));
 wrong=*stage;++wrong.control;assert(!original->rebind_saved(b,session,*tracker,wrong,GT::ticket(c),c,*backend,why));
 wrong=*stage;++wrong.receipt.owner.info;assert(!original->rebind_saved(b,session,*tracker,wrong,GT::ticket(c),c,*backend,why));
 auto rebound=original->rebind_saved(b,session,*tracker,*stage,GT::ticket(c),c,*backend,why);
 if(!rebound)std::fprintf(stderr,"rebind failure=%s\n",why.c_str());assert(rebound&&rebound->prepared());
 const auto copied=rebound->baseline(0);assert(copied&&copied->model==baseline->model&&copied->transform==baseline->transform);
 {auto bound=rebound->bind(why);assert(bound);story_native_people_model_graph::Capture live;assert(rebound->capture(*bound,0,live));assert(!rebound->verify_model(*bound,e,0,baseline->model,baseline->transform));}
 assert(rebound->restore_clock(b,why));assert(!rebound->restore_clock(b,why));
 story_native_event_collect::Clock now;assert(backend->config.collector.clock(now));const auto saved=*b.completion().origin().clock();assert(now.elapsed_ticks==saved.elapsed_ticks&&now.serial==saved.serial);
 step+=3;assert(backend->config.collector.clock(now)&&now.elapsed_ticks==saved.elapsed_ticks+3);
 assert(tracker->restoring(b)&&!tracker->ready()&&!tracker->cancel_drain());
}
#define STORY_NATIVE_RUN_REAL_MODEL
#define STORY_NATIVE_RUN_REAL_MODEL_TURN
#define STORY_NATIVE_REBIND_OBSERVER(...) saved_rebind_case(__VA_ARGS__)
#include "story_native_wait_run_restore_test.cpp"
#include <string>
#include <cstdint>
#include <cstdio>
