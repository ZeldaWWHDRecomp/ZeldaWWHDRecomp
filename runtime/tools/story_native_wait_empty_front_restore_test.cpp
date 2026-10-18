// SPDX-License-Identifier: MPL-2.0
// Genuine Context/PS/CurrentLoadOwner + Aggregate-issued FinishedLoad. The
// scheduler/front issuer and idle frontend reservation are explicit primitives;
// this does NOT stand in for the separately tested StoryScenes registry.
#include "mods/story_native_wait_pristine_snapshot.h"
#include "mods/story_native_restore_current_owner.h"
#include "mods/story_native_restore_front.h"
#include "mods/story_native_wait_runtime_controller.h"
#include "mods/story_native_scene_retire.h"
#include "mods/story_native_scene_playback.h"
#include "savestate_native_aggregate.h"
#include "savestate_native_finished_load.h"
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <string>
#include <array>
#include <optional>
#include <cstdint>
#include <utility>
#include <functional>
#include <memory>
namespace PS=story_native_wait_pristine_snapshot;
namespace ER=story_native_wait_runtime_controller;
namespace ED=story_native_event_depth;
namespace story_native_wait_runtime {class NativeResumeFrontIssuer {public:
 static auto capture(std::atomic<ER::Context*>&c,std::atomic<ED::Tracker*>&d){return story_native_restore_front::Front(c,d);}
};}
namespace story_scenes {class NativeCheckpointIssuer {public:
 static std::shared_ptr<story_native_scene_checkpoint::PreparedRetire>prepare(
  story_native_scene_checkpoint::CurrentRunRevocation&&proof,ER::Context*expected,
  story_native_scene_playback::Player&target,std::atomic<bool>&input,std::atomic<bool>&reserved){
  // No proof constructor seam: consume only the actual Context-issued value.
  if(proof.thread_!=std::this_thread::get_id()||proof.consumed_||proof.context_!=expected||!proof.life_||proof.load_epoch_!=2||proof.current_cell_||!proof.current_owner_)return {};
  bool no=false;if(!reserved.compare_exchange_strong(no,true))return {};
  auto replacement=std::make_unique<story_native_scene_playback::Player>();
  auto result=std::shared_ptr<story_native_scene_checkpoint::PreparedRetire>(new story_native_scene_checkpoint::PreparedRetire(target,std::move(replacement),input,reserved));proof.consumed_=true;return result;
 }
};}
namespace {
struct EmptyHold:ss::native_gate::Hold{std::function<void()>release;explicit EmptyHold(std::function<void()>f):release(std::move(f)){}~EmptyHold(){if(release)release();}};
struct Child:ss::native_gate::Controller {
 std::function<std::unique_ptr<ss::native_gate::Hold>(const ss::native_gate::SavedView&,std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>)>begin;
 std::function<bool(const ss::native_gate::SavedView&)>finish;
 std::unique_ptr<ss::native_gate::Hold>begin_save()override{return {};}
 std::unique_ptr<ss::native_gate::Hold>begin_load(const ss::native_gate::SavedView&)override{return {};}
 std::unique_ptr<ss::native_gate::Hold>begin_group_load(const ss::native_gate::SavedView&v,std::shared_ptr<story_native_stage_restore_group::StageRestoreGroup>g,std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>o)override{return begin(v,std::move(g),std::move(o));}
 bool finish_load(const ss::native_gate::SavedView&v)override{return finish(v);}
 std::unique_ptr<ss::native_gate::Hold>begin_progress_load()override{return {};}
};
std::function<bool(const ss::native_gate::FinishedLoad&)>final_callback;
bool finish_thunk(const ss::native_gate::FinishedLoad&f)noexcept{try{return final_callback&&final_callback(f);}catch(...){return false;}}
template<class Maker,class Restore,class Open>
void active_marker_flow(story_native_wait_session::Session&session,Cpu&cpu,std::unique_ptr<ER::Context>&context,ED::Tracker&depth,Maker make_memory,Restore restore,Open open,unsigned&native_calls){
 auto parked=threads::current_quiescence(&cpu);assert(parked);auto receipt=context->begin_saved_run();assert(receipt);
 auto saved=make_memory();assert(context->commit_saved_run(*receipt)&&!session.fence_held());
 std::atomic<ER::Context*>front{context.get()};std::atomic<ED::Tracker*>depth_front{&depth};
 std::optional<ER::RunRestorePermit>pending;std::unique_ptr<story_native_wait_session::Session::Fence>child_fence;
 std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>owner;unsigned final_calls=0,retire_calls=0;
 Child child;child.begin=[&](const auto&view,auto group,auto current)->std::unique_ptr<ss::native_gate::Hold>{
  pending=context->prepare_run_restore(std::move(*receipt),view,2,story_native_wait_runtime::NativeResumeFrontIssuer::capture(front,depth_front),std::move(group),std::move(current));
  if(!pending||!context->suspend_run_restore(*pending))return {};return std::make_unique<EmptyHold>([&]{pending.reset();child_fence.reset();});
 };
 child.finish=[&](const auto&){if(!pending)return false;const bool ok=context->finish_run_restore(std::move(*pending),&child_fence);pending.reset();return ok;};
 final_callback=[&](const auto&finished){++final_calls;const bool ok=ER::Context::finish_empty_load(finished,story_native_wait_runtime::NativeResumeFrontIssuer::capture(front,depth_front),[&](auto&&){++retire_calls;return std::shared_ptr<story_native_scene_checkpoint::PreparedRetire>{};});assert(ok&&front.load()==context.get()&&depth_front.load()==&depth&&native_calls==7);return ok;};
 std::array<ss::native_gate::Controller*,1>children{&child};std::string why;
 auto aggregate=ss::native_gate::Aggregate::create(children,why,[&](const auto&view){owner=ER::Context::prepare_current_load(story_native_wait_runtime::NativeResumeFrontIssuer::capture(front,depth_front),view,2);assert(owner);return ss::native_gate::Aggregate::LoadGroup{story_native_stage_restore_group::StageRestoreGroup::create(),[](auto&g)noexcept{g.close();},owner,[](auto&o)noexcept{o.close();},finish_thunk};});assert(aggregate);
 auto hold=aggregate->begin_load(saved.view());assert(hold);auto window=open();assert(window);restore(saved);story_native_stage_owner_observer::state_loaded();story_native_event_game_thread::state_loaded();
 assert(aggregate->finish_load(saved.view())&&final_calls==1&&!retire_calls&&native_calls==7);hold.reset();assert(!session.fence_held());window.reset();final_callback={};
}
template<class Fixture,class Checkpoint,class Objects,class Memory,class Restore>
void empty_flow(unsigned fault,Fixture&f,story_native_wait_session::Session&session,Cpu&cpu,
 std::unique_ptr<ER::Context>&context,ED::Tracker&depth,Checkpoint&checkpoint,
 const Objects&objects,Memory&memory,Restore restore,unsigned&native_calls){
 auto parked=threads::current_quiescence(&cpu);assert(parked&&native_calls==7);
 std::atomic<ER::Context*>front{context.get()};std::atomic<ED::Tracker*>depth_front{&depth};
 story_native_scene_playback::Player player;std::atomic<bool>input{true},reserved{false};unsigned retire_calls=0,final_calls=0,child_finishes=0;
 uint64_t epoch=2;std::unique_ptr<story_native_wait_session::Session::Fence>child_fence;
 std::optional<PS::Restore>pending;std::shared_ptr<story_native_restore_resume::CurrentLoadOwner>owner;
 Child child;child.begin=[&](const auto&view,auto group,auto current)->std::unique_ptr<ss::native_gate::Hold>{
  assert(current==owner);if(front.load())assert(owner->holds_session(session)&&session.fence_held());
  if(fault==6)return {};
  pending=PS::prepare(session,checkpoint,view,&cpu,*parked,epoch,current,std::move(group));if(!pending)return {};return std::make_unique<EmptyHold>([&]{pending.reset();child_fence.reset();});
 };
 child.finish=[&](const auto&view){++child_finishes;if(fault==1)return false;if(fault==5)throw 1;
  if(!pending)return false;const bool ok=PS::finish(std::move(*pending),view,&cpu,*parked,&child_fence);pending.reset();return ok;
 };
 final_callback=[&](const auto&finished){++final_calls;assert(finished.current()&&session.fence_held());
  std::atomic<ER::Context*>foreign_front{front.load()};std::atomic<ED::Tracker*>foreign_depth{depth_front.load()};
  const auto retire=[&](auto&&revocation){++retire_calls;if(fault==2)return std::shared_ptr<story_native_scene_checkpoint::PreparedRetire>{};return story_scenes::NativeCheckpointIssuer::prepare(std::move(revocation),context.get(),player,input,reserved);};
  const bool had_owner=front.load()!=nullptr;const bool ok=ER::Context::finish_empty_load(finished,story_native_wait_runtime::NativeResumeFrontIssuer::capture(fault==3?foreign_front:front,fault==3?foreign_depth:depth_front),fault==4?ER::Context::RetireFrontend{}:retire);
  if(ok){assert(!front.load()&&!depth_front.load()&&!input.load()&&native_calls==7);if(had_owner)assert(!ER::Context::finish_empty_load(finished,story_native_wait_runtime::NativeResumeFrontIssuer::capture(front,depth_front),retire));}
  return ok;
 };
 std::array<ss::native_gate::Controller*,1>children{&child};std::string why;
 auto aggregate=ss::native_gate::Aggregate::create(children,why,[&](const auto&view){owner=ER::Context::prepare_current_load(story_native_wait_runtime::NativeResumeFrontIssuer::capture(front,depth_front),view,epoch);assert(owner);if(front.load())assert(context->state()==ER::State::Quarantined);return ss::native_gate::Aggregate::LoadGroup{story_native_stage_restore_group::StageRestoreGroup::create(),[](auto&g)noexcept{g.close();},owner,[](auto&o)noexcept{o.close();},finish_thunk};});assert(aggregate);
 auto hold=aggregate->begin_load(memory.view());
 if(fault==6){assert(!hold&&!final_calls&&!child_finishes&&front.load()==context.get()&&!session.fence_held());}
 else{
  assert(hold&&owner&&session.fence_held());
  for(const auto&[at,bytes]:objects){auto*p=f.pointer(at,bytes.size());assert(p);std::copy(bytes.begin(),bytes.end(),p);}restore();story_native_stage_owner_observer::state_loaded();story_native_event_game_thread::state_loaded();
  const bool success=aggregate->finish_load(memory.view());if(success!=(fault==0))std::fprintf(stderr,"empty-front fault=%u success=%u final=%u retire=%u\n",fault,unsigned(success),final_calls,retire_calls);assert(success==(fault==0));
  if(fault==1||fault==5)assert(!final_calls&&front.load()==context.get());
  else if(fault)assert(final_calls==1&&front.load()==context.get());
  assert(native_calls==7);hold.reset();pending.reset();assert(!session.fence_held()&&!reserved.load());
  if(!fault){
   // The real empty atomic slots can be claimed by the next load attempt;
   // no quarantined Context/status shortcut is used to manufacture NONE.
   epoch=3;const auto retired=retire_calls;auto second=aggregate->begin_load(memory.view());assert(second&&owner&&!owner->holds_session(session));
   for(const auto&[at,bytes]:objects){auto*p=f.pointer(at,bytes.size());assert(p);std::copy(bytes.begin(),bytes.end(),p);}restore();story_native_stage_owner_observer::state_loaded();story_native_event_game_thread::state_loaded();
   assert(aggregate->finish_load(memory.view())&&!front.load()&&!depth_front.load()&&retire_calls==retired&&native_calls==7);second.reset();assert(!session.fence_held());
  }
 }
 assert(native_calls==7);final_callback={};
}
}
#define STORY_NATIVE_RUN_REAL_MODEL
#define STORY_NATIVE_RUN_REAL_MODEL_TURN
#define STORY_NATIVE_RESTORE_CASE_COUNT 8
#define STORY_NATIVE_CONTEXT_PRISTINE_BEFORE_NATIVE \
 (void)covered;SO::state_loaded();current_cpu=&cpu;fixture_frozen=true;++freeze_epoch; \
 auto pristine_park=threads::current_quiescence(&cpu);assert(pristine_park); \
 auto pristine_hold=session.try_fence();assert(pristine_hold); \
 auto pristine_capture=PS::begin(session,&cpu,*pristine_park);assert(pristine_capture); \
 SavedMemory pristine_memory(f,mapped,session.arena());const auto pristine_objects=f.memory; \
 auto pristine_checkpoint=PS::commit(std::move(*pristine_capture),&cpu,*pristine_park);assert(pristine_checkpoint&&pristine_checkpoint->matches_saved(pristine_memory.view())); \
 pristine_hold.reset();fixture_frozen=false;++freeze_epoch;current_cpu=nullptr
#define STORY_NATIVE_CONTEXT_PRISTINE_AFTER_NATIVE \
 fixture_frozen=true;++freeze_epoch; \
 if(fault==7)active_marker_flow(session,cpu,context,depth,[&]{return SavedMemory(f,mapped,arena);},[&](auto&saved){saved.restore(write,mapped,arena);},[&]{return ss::NativeRestorePipelineIssuer::open(&cpu);},native_calls); \
 else empty_flow(fault,f,session,cpu,context,depth,*pristine_checkpoint,pristine_objects,pristine_memory,[&]{pristine_memory.restore(write,mapped,arena);},native_calls)
#include "story_native_wait_run_restore_test.cpp"
#include <algorithm>
#include <string>
#include <array>
#include <optional>
#include <cstdint>
#include <utility>
#include <functional>
#include <memory>
