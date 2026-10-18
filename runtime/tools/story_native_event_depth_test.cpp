// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_depth.h"
#include <cassert>
#include <future>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace story_native_event_depth;
Expected expected(){Expected e;e.identity={0x20000000,0x60000000,0x520,1,2,3,4};e.room_number=11;for(unsigned n=0;n<2;++n){e.actors[n].address=0x20001000+n*0x1000;e.actors[n].pid=10+n;e.actors[n].token=1+n;e.actors[n].profile=0x175;e.actors[n].subtype=1;}e.camera.address=0x20003248;e.camera.pid=12;return e;}
Coverage coverage(){return {{11,11,true},{0,0,true},{22,22,true},true};}
Call call(Entry entry,unsigned family=0,Authority authority=Authority::Owned){const auto e=expected();Call c;c.entry=entry;c.function=entry_address(entry);c.identity=e.identity;c.authority=authority;
 if(family==0){c.subject=e.actors[0].address;c.pid=e.actors[0].pid;c.token=e.actors[0].token;}
 if(family==1){c.subject=e.camera.address-0x248;c.pid=e.camera.pid;}
 if(family==2)c.subject=e.identity.manager;
 switch(entry){case Entry::PeopleExecute:case Entry::PeopleEventMove:c.receiver=c.subject;break;case Entry::CameraExecute:c.receiver=e.camera.address-0x248;break;case Entry::CameraEvent:c.receiver=e.camera.address;break;case Entry::ManagerRun:case Entry::ManagerMain:case Entry::StaffLookup:case Entry::CutEnd:c.receiver=e.identity.manager;break;case Entry::Timekeeper:c.receiver=e.identity.image+0x1E0;break;default:c.receiver=c.subject+0x3E0;break;}return c;}
int main(){auto e=expected();
 assert(entry_address(Entry::PeopleExecute)==0x022C36D0&&entry_address(Entry::CameraExecute)==0x024FFA3C&&entry_address(Entry::ManagerRun)==0x02544374);
 // Read-only ownership queries require this exact current thread's whole chain.
 Tracker query(e,coverage()),other(e,coverage());
 assert(!query.current_owned_call()&&!query.current_actor_scope(e.actors[0],e.identity));
 {
  auto outer=query.enter(call(Entry::PeopleExecute));assert(outer.allowed());
  assert(query.current_owned_call()->entry==Entry::PeopleExecute);
  assert(query.current_actor_scope(e.actors[0],e.identity)&&!other.current_owned_call());
  for(unsigned n=0;n<4;++n){auto actor=e.actors[0];auto id=e.identity;
   if(n==0)++actor.pid;if(n==1)++actor.token;if(n==2)++id.generation;if(n==3)++actor.fingerprint[0];
   assert(!query.current_actor_scope(actor,id));
  }
  std::thread worker([&]{assert(!query.current_owned_call()&&!query.current_actor_scope(e.actors[0],e.identity));});worker.join();
  {auto move=query.enter(call(Entry::PeopleEventMove));assert(move.allowed());
   assert(query.current_owned_call()->entry==Entry::PeopleEventMove&&query.current_actor_scope(e.actors[0],e.identity));
   {auto foreign=query.enter(call(Entry::CommonCut,0,Authority::Foreign));assert(foreign.allowed());
    assert(!query.current_owned_call()&&!query.current_actor_scope(e.actors[0],e.identity));}
   assert(query.current_actor_scope(e.actors[0],e.identity));
   {auto foreign=other.enter(call(Entry::CommonCut,0,Authority::Foreign));assert(foreign.allowed());
    assert(!query.current_owned_call()&&!other.current_owned_call());}
  }
  assert(query.current_owned_call()->entry==Entry::PeopleExecute);
 }
 assert(!query.current_owned_call()&&!query.current_actor_scope(e.actors[0],e.identity));
 assert(query.begin_drain()&&!query.current_owned_call()&&!query.current_actor_scope(e.actors[0],e.identity));
 assert(query.cancel_drain());
 {auto outer=query.enter(call(Entry::PeopleExecute));assert(outer.allowed());
  auto bad=call(Entry::PeopleEventMove);++bad.pid;auto refused=query.enter(bad);assert(!refused.allowed());
  assert(!query.current_owned_call()&&!query.current_actor_scope(e.actors[0],e.identity));}
 // Entire People Execute remains busy AFTER its common-cut callback returned.
 Tracker t(e,coverage());assert(t.ready()&&t.snapshot().idle()&&Tracker::valid_idle_record(t.record(),e));{
  auto outer=t.enter(call(Entry::PeopleExecute));assert(outer.allowed()&&t.snapshot().npc_depth==1&&!t.begin_drain());{
   auto move=t.enter(call(Entry::PeopleEventMove));assert(move.allowed());auto cut=t.enter(call(Entry::CommonCut));assert(cut.allowed());{
    auto process=t.enter(call(Entry::TurnProcess));assert(process.allowed());auto end=t.enter(call(Entry::CutEnd));assert(end.allowed()&&t.snapshot().manager_depth==1&&t.snapshot().callback_depth==4);
   }assert(t.snapshot().npc_depth==3);
  }assert(t.snapshot().npc_depth==1&&!t.snapshot().callback_depth&&!t.begin_drain()&&!Tracker::valid_idle_record(t.record(),e));
 }assert(t.snapshot().idle());
 // Camera event completion does not mean its Execute/view/counter tail detached.
 Tracker cam(e,coverage());{
  auto outer=cam.enter(call(Entry::CameraExecute,1));assert(outer.allowed());{
   auto event=cam.enter(call(Entry::CameraEvent,1));assert(event.allowed());auto staff=cam.enter(call(Entry::StaffLookup,1));assert(staff.allowed()&&cam.snapshot().camera_depth==2);
  }assert(cam.snapshot().camera_depth==1&&!cam.begin_drain());
 }assert(cam.snapshot().idle());
 Tracker manager(e,coverage());{
  auto run=manager.enter(call(Entry::ManagerRun,2));assert(run.allowed());auto main=manager.enter(call(Entry::ManagerMain,2));assert(main.allowed());auto timer=manager.enter(call(Entry::Timekeeper,2));assert(timer.allowed()&&manager.snapshot().manager_depth==3);
 }assert(manager.snapshot().idle());
 // Hook ownership conflicts cannot be papered over with a second replacement.
 for(unsigned n=0;n<4;++n){auto c=coverage();if(n==0)c.people.delegated_owner=99;if(n==1)c.camera.before_and_after=false;if(n==2)c.manager.delegated_owner=0;if(n==3)c.inner_callbacks=false;Tracker missing(e,c);assert(!missing.ready());auto scope=missing.enter(call(Entry::PeopleExecute));assert(!scope.allowed()&&missing.snapshot().fault);}
 // Exact identity, current subject/PID/token/receiver/function, and outer chain.
 for(unsigned f=0;f<10;++f){Tracker bad(e,coverage());auto c=call(Entry::PeopleExecute);switch(f){case 0:++c.pid;break;case 1:++c.token;break;case 2:++c.identity.stage;break;case 3:++c.identity.room;break;case 4:++c.identity.generation;break;case 5:++c.identity.program;break;case 6:++c.receiver;break;case 7:++c.function;break;case 8:c.authority=Authority::InvalidOwned;break;case 9:++c.identity.image;break;}auto s=bad.enter(c);assert(!s.allowed()&&bad.snapshot().fault);}
 Tracker no_outer(e,coverage());{auto inner=no_outer.enter(call(Entry::CommonCut));assert(!inner.allowed());}assert(no_outer.snapshot().fault);
 Tracker reentry(e,coverage());{auto first=reentry.enter(call(Entry::PeopleExecute));assert(first.allowed());auto again=reentry.enter(call(Entry::PeopleExecute));assert(!again.allowed());}assert(reentry.snapshot().fault&&!reentry.ready());
 // Legitimate foreign originals are counted and delegated unchanged in Active.
 Tracker foreign(e,coverage());unsigned original_calls=0;{auto c=call(Entry::PeopleExecute,0,Authority::Foreign);c.pid=999;c.subject=c.receiver=0x20009000;auto s=foreign.enter(c);if(s.allowed())++original_calls;assert(original_calls==1&&foreign.snapshot().foreign_depth==1&&!foreign.begin_drain());}assert(foreign.snapshot().idle());
 // A foreign chain/another thread's aggregate depth can never authorize owned callbacks.
 Tracker adoption(e,coverage());{auto f=adoption.enter(call(Entry::PeopleExecute,0,Authority::Foreign));assert(f.allowed());auto owned=adoption.enter(call(Entry::PeopleEventMove));assert(!owned.allowed());}assert(adoption.snapshot().fault);
 Tracker threads(e,coverage());{auto outer=threads.enter(call(Entry::CameraExecute,1));assert(outer.allowed());std::thread worker([&]{auto inner=threads.enter(call(Entry::CameraEvent,1));assert(!inner.allowed());});worker.join();assert(threads.snapshot().camera_depth==1&&threads.snapshot().fault);}assert(!threads.ready());
 Tracker aggregate(e,coverage());{auto outer=aggregate.enter(call(Entry::PeopleExecute));std::promise<void>entered,release;auto done=release.get_future();std::thread worker([&]{auto f=aggregate.enter(call(Entry::CameraExecute,1,Authority::Foreign));assert(f.allowed());entered.set_value();done.wait();});entered.get_future().wait();assert(aggregate.snapshot().npc_depth==1&&aggregate.snapshot().camera_depth==1&&aggregate.snapshot().foreign_depth==1&&!aggregate.begin_drain());release.set_value();worker.join();}assert(aggregate.snapshot().idle());
 // Exception unwinds every depth but retains a sticky admission fault.
 Tracker exception(e,coverage());try{auto o=exception.enter(call(Entry::PeopleExecute));auto m=exception.enter(call(Entry::PeopleEventMove));assert(o.allowed()&&m.allowed());throw std::runtime_error("native synthetic fault");}catch(const std::runtime_error&){}assert(!exception.snapshot().npc_depth&&exception.snapshot().fault&&!Tracker::valid_idle_record(exception.record(),e));
 // Draining fences all new consumer entry before coherent copyback.
 Tracker drain(e,coverage());assert(drain.begin_drain()&&!drain.ready()&&!Tracker::valid_idle_record(drain.record(),e));{auto owned=drain.enter(call(Entry::PeopleExecute));assert(!owned.allowed()&&!drain.snapshot().fault);}assert(drain.cancel_drain()&&drain.ready());assert(drain.begin_drain());{auto f=drain.enter(call(Entry::CameraExecute,1,Authority::Foreign));assert(!f.allowed()&&drain.snapshot().fault);}assert(!drain.cancel_drain());
 // Busy, corrupt, wrong-generation and reserved records reject host rehydration.
 auto record=t.record();assert(Tracker::valid_idle_record(record,e));record[80]=1;assert(!Tracker::valid_idle_record(record,e));record=t.record();record[95]^=1;assert(!Tracker::valid_idle_record(record,e));auto wrong=e;++wrong.identity.generation;assert(!Tracker::valid_idle_record(t.record(),wrong));
 // Bounded TLS capacity and out-of-order destruction retain denial instead of false-idle.
 Tracker capacity(e,coverage());std::vector<std::unique_ptr<Scope>>scopes;for(unsigned n=0;n<32;++n){scopes.emplace_back(new Scope(capacity.enter(call(Entry::PeopleExecute,0,Authority::Foreign))));assert(scopes.back()->allowed());}{auto overflow=capacity.enter(call(Entry::PeopleExecute,0,Authority::Foreign));assert(!overflow.allowed());}while(!scopes.empty())scopes.pop_back();assert(!capacity.snapshot().npc_depth&&capacity.snapshot().fault);
 Tracker order(e,coverage());auto*one=new Scope(order.enter(call(Entry::PeopleExecute,0,Authority::Foreign)));auto*two=new Scope(order.enter(call(Entry::CameraExecute,1,Authority::Foreign)));delete one;delete two;assert(order.snapshot().npc_depth==1&&order.snapshot().fault&&!order.begin_drain());
}
