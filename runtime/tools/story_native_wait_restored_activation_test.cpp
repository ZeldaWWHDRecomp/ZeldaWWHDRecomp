// SPDX-License-Identifier: MPL-2.0
// Actual saved Context→Stage/Model/Controller proof→private final host commit.
// Atomic front and physical scheduler/route issuance are trusted primitives;
// this test never invokes restored native code or claims real scheduler proof.
#include "mods/story_native_restore_front.h"
#include "mods/story_native_wait_runtime_controller.h"
#include "mods/story_native_restore_resume.h"
#include <cassert>
#include <string>
#include <optional>
#include <utility>
#include <memory>
namespace story_native_wait_runtime {class NativeResumeFrontIssuer {public:
 static auto capture(std::atomic<story_native_wait_runtime_controller::Context*>&context,std::atomic<story_native_event_depth::Tracker*>&depth){return story_native_restore_front::Front(context,depth);}
};}
template<class Session,class Depth,class Expected,class Original,class OldDepth>
void final_adoption_case(story_native_wait_runtime_controller::Context&restored,Cpu*c,const story_native_restore_resume::Boundary&b,Session&session,Depth&depth,const Expected&e,Original*original,OldDepth&old_depth,unsigned&writes,unsigned&calls,unsigned calls_before,unsigned fault){
 namespace RC=story_native_wait_runtime_controller;namespace D=story_native_event_depth;
 std::atomic<RC::Context*>context_front{original};std::atomic<D::Tracker*>depth_front{&old_depth};
 assert(original&&original->suspended_for_restore(b));
 std::unique_ptr<D::Tracker>unrelated;
 if(fault==16)context_front.store(nullptr);
 if(fault==17){unrelated=std::make_unique<D::Tracker>(e,D::Coverage{{1,1,true},{2,2,true},{3,3,true},true});depth_front.store(unrelated.get());}
 auto front=story_native_wait_runtime::NativeResumeFrontIssuer::capture(context_front,depth_front);
 if(fault==18)context_front.store(nullptr); // capability became stale
 std::optional<threads::NativeQuiescence>existing_q;
 if(fault==19){std::string why;existing_q=threads::try_native_quiescence(c,story_native_event_game_thread::ticket(c),100,why);assert(existing_q);}
 if(fault==20)mem::ptr(e.identity.image)[0]^=1;
 if(fault>=16){
  const auto old_context=context_front.load();const auto old_tracker=depth_front.load();assert(!restored.resume_restored(b,std::move(front)));
  existing_q.reset();if(fault==20)mem::ptr(e.identity.image)[0]^=1;
  assert(context_front.load()==old_context&&depth_front.load()==old_tracker&&!depth.ready()&&depth.snapshot().state==D::State::Draining&&restored.state()==RC::State::Quarantined&&restored.retained()&&writes==0&&calls==calls_before);return;
 }
 assert(restored.resume_restored(b,std::move(front)));
 assert(context_front.load()==&restored&&depth_front.load()==&depth&&depth.ready()&&!depth.restoring(b));
 assert(restored.state()==RC::State::Started&&restored.retained()&&writes==0&&calls==calls_before&&!session.fence_held());
 D::Call call;c->r[3]=e.identity.manager;assert(restored.outer(c,D::entry_address(D::Entry::ManagerRun),call)&&call.authority==D::Authority::Owned);
 auto replay=story_native_wait_runtime::NativeResumeFrontIssuer::capture(context_front,depth_front);assert(!restored.resume_restored(b,std::move(replay)));
 assert(restored.state()==RC::State::Started&&depth.ready()&&context_front.load()==&restored&&writes==0&&calls==calls_before);
}
#define STORY_NATIVE_RESTORE_CASE_COUNT 21
#define STORY_NATIVE_FINAL_ADOPT_OBSERVER(...) final_adoption_case(__VA_ARGS__)
#include "story_native_wait_restored_adoption_test.cpp"
#include <string>
#include <optional>
#include <utility>
#include <memory>
