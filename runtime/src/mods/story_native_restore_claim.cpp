// SPDX-License-Identifier: MPL-2.0
#include "story_native_restore_claim.h"
#include "story_native_wait_runtime_model_graph.h"
namespace story_native_restore_claim {
struct Transfer::State {
 const story_native_restore_resume::Boundary*boundary;
 const story_native_wait_runtime_model_graph::Binding*binding;
 story_native_wait_session::Session*session;
 story_native_restore_resume::SavedRunOrigin origin;
};
Transfer::Transfer(const story_native_restore_resume::Boundary&b,const story_native_wait_runtime_model_graph::Binding&m,story_native_wait_session::Session&s):state_(std::make_unique<State>(State{&b,&m,&s,b.completion().origin()})){}
Transfer::Transfer(Transfer&&)noexcept=default;Transfer&Transfer::operator=(Transfer&&)noexcept=default;Transfer::~Transfer()=default;
bool Transfer::consume(Cpu*c,story_native_event_depth::Tracker&d,story_native_wait_session::Session&s,const story_native_event_observation::Expected&e,const story_native_event_memory::Read&read){
 auto proof=std::move(state_);if(!proof)return false;
 try{const auto&b=*proof->boundary;const auto baseline=proof->origin.session_baseline();const auto q=proof->binding->quiescence();
  return c&&proof->session==&s&&s.fence_held()&&b.current(c)&&d.restoring(b)&&proof->origin.same_run(b.completion().origin())&&
   proof->origin.depth_record()==b.completion().origin().depth_record()&&proof->origin.clock()==b.completion().origin().clock()&&
   e==b.completion().expected()&&baseline&&baseline==b.completion().origin().session_baseline()&&baseline->expected()==e&&
   proof->binding->current()&&q&&q->current(c)&&baseline->matches_live(read)&&b.current(c)&&proof->binding->current();
 }catch(...){return false;}
}
}
