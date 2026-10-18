// SPDX-License-Identifier: MPL-2.0
#include "story_native_wait_owned_routes.h"
#include "guest_addr.h"
#include "runtime.h"
namespace story_native_wait_owned_routes {namespace {
namespace P=story_native_people_bridge;
bool entry(uint32_t fn,D::Entry&e){for(auto x:{D::Entry::PeopleExecute,D::Entry::CameraExecute,D::Entry::ManagerRun,D::Entry::PeopleEventMove,D::Entry::CommonCut,D::Entry::CameraEvent,D::Entry::ManagerMain,D::Entry::Timekeeper,D::Entry::StaffLookup,D::Entry::CutEnd,D::Entry::WaitStart,D::Entry::WaitProcess,D::Entry::TurnStart,D::Entry::TurnProcess})if(fn==D::entry_address(x)){e=x;return true;}return false;}
}
Routes::Routes(Expected e,D::Tracker&d,P::Bridge&p,story_native_manager_terminal::Terminal&t,story_native_camera_return::Barrier&c,story_native_event_end_guard::Guard&g,Sources s):expected_(std::move(e)),depth_(d),people_(p),terminal_(t),camera_(c),end_(g),sources_(std::move(s)){}
void Routes::fail()noexcept{fault_=true;if(cancelled_.exchange(true))return;try{if(sources_.queue_cancel)sources_.queue_cancel();}catch(...){} }
bool Routes::claimed()noexcept{try{if(sources_.retained)return sources_.retained(expected_);}catch(...){fail();return true;}fail();return true;}
bool Routes::thread(const Cpu*c)noexcept{try{return sources_.owned_thread&&sources_.owned_thread(c);}catch(...){return false;}}
bool Routes::safe(const Cpu*c,uint32_t f)noexcept{try{return sources_.retail_safe&&sources_.retail_safe(c,f);}catch(...){return false;}}
bool Routes::live_actor(unsigned n)noexcept{try{return n<2&&sources_.actor_identity&&sources_.actor_identity(expected_.actors[n]);}catch(...){return false;}}
bool Routes::live_camera()noexcept{try{return sources_.camera_identity&&sources_.camera_identity();}catch(...){return false;}}
int Routes::actor(uint32_t a,bool cut)const{for(unsigned n=0;n<2;++n)if(a==expected_.actors[n].address+(cut?0x3E0:0))return int(n);return -1;}
bool Routes::outer(const Cpu*c,uint32_t fn,D::Call&out)noexcept{
 D::Entry e;if(!c||!entry(fn,e)||(e!=D::Entry::PeopleExecute&&e!=D::Entry::CameraExecute&&e!=D::Entry::ManagerRun))return false;
 out={e,fn,c->r[3],c->r[3],0,0,{},D::Authority::Foreign};
 if(!claimed())return true;
 const int n=e==D::Entry::PeopleExecute?actor(c->r[3]):-1;
 bool candidate=n>=0||(e==D::Entry::CameraExecute&&c->r[3]==expected_.camera.address-0x248)||(e==D::Entry::ManagerRun&&c->r[3]==expected_.identity.manager);
 if(!candidate){if(!safe(c,fn)){out.authority=D::Authority::InvalidOwned;fail();}return true;}
 bool live=n>=0?live_actor(unsigned(n)):(e==D::Entry::CameraExecute?live_camera():true);
 if(!live&&safe(c,fn))return true;
 out.identity=expected_.identity;
 if(n>=0){out.pid=expected_.actors[n].pid;out.token=expected_.actors[n].token;}
 if(e==D::Entry::CameraExecute)out.pid=expected_.camera.pid;
 out.authority=live&&thread(c)&&!fault_?D::Authority::Owned:D::Authority::InvalidOwned;
 if(out.authority==D::Authority::InvalidOwned)fail();return true;
}
bool Routes::end_observation(Cpu*c,story_native_event_end_guard::Observation&o){
 try{return sources_.end_observation&&sources_.end_observation(c,o)&&o.retained_claim&&o.identity_valid;}catch(...){return false;}
}
Decision Routes::foreign(Cpu*c,uint32_t fn,const std::function<void()>&next){
 D::Entry e;if(entry(fn,e)&&e!=D::Entry::PeopleExecute&&e!=D::Entry::CameraExecute&&e!=D::Entry::ManagerRun){
  D::Call call{e,fn,c->r[3],c->r[3],0,0,{},D::Authority::Foreign};auto scope=depth_.enter(call);next();return Decision::Handled;
 }return Decision::Delegate;
}
Decision Routes::route(Cpu*c,uint32_t fn,const std::function<void()>&next){
 if(!c||!next){fail();if(c&&fn==D::entry_address(D::Entry::StaffLookup))c->r[3]=~0u;return Decision::Handled;}
 if(!claimed())return foreign(c,fn,next);
 auto reject=[&]{fail();if(fn==D::entry_address(D::Entry::StaffLookup))c->r[3]=~0u;return Decision::Handled;};
 D::Entry e;const bool known=entry(fn,e);
 const auto parent=depth_.current_owned_call();
 // Outer scopes are supplied by the surrounding genuine native observer.
 if(known&&(e==D::Entry::PeopleExecute||e==D::Entry::CameraExecute||e==D::Entry::ManagerRun)){
  D::Call call;if(!outer(c,fn,call))return reject();
  if(call.authority==D::Authority::Foreign)return Decision::Delegate;
  if(call.authority!=D::Authority::Owned||!parent||parent->entry!=e)return reject();
  return Decision::Delegate;
 }
 // Terminal's native ALL iteration deliberately visits foreign actors. Route
 // those too, while its exact synchronous TLS scope is independently attested.
 bool terminal_scope=false;
 try{terminal_scope=sources_.terminal_invoking&&sources_.terminal_invoking();}catch(...){return reject();}
 if(fn==GC(0x02542C9C)&&terminal_scope){
  if(!thread(c)||fault_)return reject();
  c->r[3]=terminal_.route_all_off(c->r[3],[&]{next();return c->r[3];});return Decision::Handled;
 }
 const bool candidate=(actor(c->r[3])>=0)||(actor(c->r[3],true)>=0)||c->r[3]==expected_.identity.manager||c->r[3]==expected_.identity.manager-0xF4||c->r[3]==expected_.camera.address||c->r[3]==expected_.identity.image+0x1E0;
 if(!candidate&&!parent){if(safe(c,fn))return foreign(c,fn,next);return reject();}
 if(!thread(c)||fault_)return reject();
 try{
  if(fn==GC(0x0254465C)){
   if(c->r[3]!=expected_.identity.manager||c->r[4]!=0)return reject();
   if(terminal_scope){next();return Decision::Handled;}
   story_native_event_end_guard::Observation o;if(!end_observation(c,o))return reject();
   const auto d=end_.end_proc(o,c->r[5]!=0,false);
   if(d!=story_native_event_end_guard::Decision::Queue)return reject();
   if(sources_.queue_cancel)sources_.queue_cancel();return Decision::Handled;
  }
  if(fn==GC(0x02542C9C)){
   c->r[3]=terminal_.route_all_off(c->r[3],[&]{next();return c->r[3];});return Decision::Handled;
  }
  if(fn==GC(0x024FE3E8)){
   if(c->r[3]!=expected_.camera.address||!live_camera())return reject();
   const auto d=camera_.run(c,fn,next);
   if(d==story_native_camera_return::Decision::Delegate)return Decision::Delegate;
   if(d==story_native_camera_return::Decision::Quarantined)fail();return Decision::Handled;
  }
  if(fn==GC(0x0253EFA8)||fn==GC(0x0253FF34)){
   if(c->r[3]!=expected_.identity.manager-0xF4)return reject();
   if(fn==GC(0x0253EFA8)&&sources_.control_release&&sources_.control_release()){next();return Decision::Handled;}
   story_native_event_end_guard::Observation o;if(!end_observation(c,o)||end_.before_control(o)!=story_native_event_end_guard::Decision::Queue)return reject();
   next();return Decision::Handled;
  }
  if(!known)return Decision::Delegate;
  if(e==D::Entry::PeopleEventMove){const int n=actor(c->r[3]);if(n<0||!live_actor(unsigned(n))){if(!parent&&safe(c,fn))return foreign(c,fn,next);return reject();}const auto d=people_.event_move(c);if(d==P::Decision::Handled)return Decision::Handled;return reject();}
  if(e==D::Entry::CommonCut){
   const int n=actor(c->r[3],true);
   if(n<0||!parent||parent->entry!=e||parent->receiver!=c->r[3]||!depth_.current_actor_scope(expected_.actors[unsigned(n)],expected_.identity))return reject();
   next();return Decision::Handled;
  }
  if(e==D::Entry::TurnStart||e==D::Entry::TurnProcess){const auto d=people_.turn(c,e==D::Entry::TurnStart,next);if(d==P::Decision::Handled)return Decision::Handled;if(d==P::Decision::RejectOwned)return reject();if(safe(c,fn))return foreign(c,fn,next);return reject();}
  if(e==D::Entry::WaitStart||e==D::Entry::WaitProcess){const auto d=people_.wait(c,e==D::Entry::WaitStart,next);if(d==P::Decision::Handled)return Decision::Handled;if(d==P::Decision::RejectOwned)return reject();if(safe(c,fn))return foreign(c,fn,next);return reject();}
  if(e==D::Entry::StaffLookup&&people_.staff_id(c))return Decision::Handled;
  if(e==D::Entry::CutEnd){const auto d=people_.cut_end(c,next);if(d==P::Decision::Handled)return Decision::Handled;if(d==P::Decision::RejectOwned)return reject();}
  // Exact camera/manager parent identity is inherited, never reconstructed from
  // a positive global counter or a staff index supplied by a caller.
  if(!parent){if(safe(c,fn))return foreign(c,fn,next);return reject();}
  D::Call child=*parent;child.entry=e;child.function=fn;child.receiver=c->r[3];
  bool allowed=false;
  if(e==D::Entry::CameraEvent)allowed=parent->entry==D::Entry::CameraExecute&&live_camera();
  if(e==D::Entry::ManagerMain)allowed=parent->entry==D::Entry::ManagerRun;
  if(e==D::Entry::Timekeeper)allowed=parent->entry==D::Entry::ManagerMain;
  if(e==D::Entry::StaffLookup||e==D::Entry::CutEnd)allowed=parent->entry==D::Entry::CameraEvent||parent->entry==D::Entry::Timekeeper;
  if(e==D::Entry::StaffLookup)allowed=allowed&&c->r[5]==0&&c->r[6]==0&&
   c->r[4]==(parent->entry==D::Entry::CameraEvent?GD(0x1004A8B8):GD(0x1004DAB4));
  if(e==D::Entry::CutEnd)allowed=allowed&&
   c->r[4]==(parent->entry==D::Entry::CameraEvent?2u:3u);
  if(!allowed)return reject();auto scope=depth_.enter(child);if(!scope.allowed())return reject();next();return Decision::Handled;
 }catch(...){fail();throw;}
}
}
