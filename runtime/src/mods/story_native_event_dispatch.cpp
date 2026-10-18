// SPDX-License-Identifier: MPL-2.0
#include "story_native_event_dispatch.h"
#include <array>
namespace story_native_event_dispatch {namespace {
namespace D=story_native_event_depth;
struct Frame{D::Tracker* tracker;D::Call call;bool dispatcher,delegated=false;};
thread_local std::array<Frame,32> frames{};thread_local unsigned count=0;
bool exact(const D::Call&a,const D::Call&b){return a.entry==b.entry&&a.function==b.function&&a.receiver==b.receiver&&a.subject==b.subject&&a.pid==b.pid&&a.token==b.token&&a.identity==b.identity&&a.authority==b.authority;}
struct Pop{~Pop(){--count;}};
Outcome observe(D::Tracker&t,const D::Call&call,const Body&body,bool dispatch){
 if(!body)return {};
 if(count==frames.size()){auto invalid=call;invalid.authority=D::Authority::InvalidOwned;auto fault=t.enter(invalid);if(call.authority==D::Authority::Foreign)return {true,false,false,body()};return {};}
 auto scope=t.enter(call);
 if(!scope.allowed()){if(call.authority==D::Authority::Foreign)return {true,false,false,body()};return {};}
 frames[count++]={&t,call,dispatch,false};Pop pop;
 return {true,true,false,body()};
}
}
Outcome dispatcher(D::Tracker&t,const D::Call&call,const Body&body){return observe(t,call,body,true);}
Outcome original(D::Tracker&t,const D::Call&call,const Body&body){
 if(!body)return {};
 if(count){auto&frame=frames[count-1];if(frame.tracker==&t&&frame.dispatcher&&exact(frame.call,call)&&!frame.delegated){
  frame.delegated=true;return {true,true,true,body()};
 }}
 // Different identity/receiver or a second original is a true separate entry,
 // so Tracker rejects owned recursion rather than silently executing twice.
 return observe(t,call,body,false);
}
}
