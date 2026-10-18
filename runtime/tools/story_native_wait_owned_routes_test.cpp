// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_wait_owned_routes.h"
#include "mods/story_actors.h"
#include "runtime.h"
#include <cassert>
#include <thread>
#include <string>
#include <cstdint>
namespace story_actors {bool resident_event_identity(const std::string&,const std::string&,uint32_t,uint32_t,ResidentEventIdentity&){return false;}}
namespace R=story_native_wait_owned_routes;
namespace D=story_native_event_depth;
namespace E=story_native_event_end_guard;
using Expected=story_native_event_observation::Expected;
Expected expected(){Expected e;e.identity={0x20000000,0x60000000,0x520,1,2,3,4};e.room_number=11;e.frames=30;e.camera.address=0x20005248;e.camera.pid=90;for(unsigned n=0;n<2;++n){auto&a=e.actors[n];a.address=0x20001000+n*0x1000;a.pid=10+n;a.token=1+n;a.profile=0x175;a.subtype=1;a.fingerprint.fill(n+1);}return e;}
struct Fixture {
 Expected e=expected();D::Tracker d{e,{{1,1,true},{2,2,true},{3,3,true},true}};
 story_native_people_bridge::Bridge bridge{e,{}, {},d};
 story_native_manager_terminal::Terminal terminal{{},{},{},{}};
 story_native_camera_return::Barrier camera{e.identity,e.camera,{}};
 E::Identity id{e.identity.manager,e.identity.image,e.identity.bytes,e.identity.generation,e.identity.program,0,e.identity.stage,e.identity.room};
 E::Guard end{id};bool claim=true,live=true,retail=false,owned=true,terminal_call=false,release=false;unsigned cancelled=0;
 R::Sources sources(){R::Sources s;s.retained=[&](const auto&x){return claim&&x==e;};s.owned_thread=[&](auto*){return owned;};s.actor_identity=[&](const auto&){return live;};s.camera_identity=[&]{return live;};s.retail_safe=[&](auto*,auto){return retail;};s.queue_cancel=[&]{++cancelled;};s.terminal_invoking=[&]{return terminal_call;};s.control_release=[&]{return release;};s.end_observation=[&](auto*,E::Observation&o){o.identity=id;o.table=e.identity.image+0x40;o.event=o.table;o.mode=3;o.partner1=o.partner2=o.talk_partner=o.item_partner=~0u;o.control_event_id=-1;o.retained_claim=o.identity_valid=true;return true;};return s;}
};
int main(){
 Cpu c{};unsigned calls=0;
 {Fixture f;R::Routes r(f.e,f.d,f.bridge,f.terminal,f.camera,f.end,f.sources());D::Call out;c.r[3]=f.e.actors[0].address;assert(r.outer(&c,0x022C36D0,out)&&out.authority==D::Authority::Owned);{
 auto scope=f.d.enter(out);assert(scope.allowed());assert(r.route(&c,0x022C36D0,[&]{++calls;})==R::Decision::Delegate);}
 assert(!f.cancelled);
 f.live=false;f.retail=true;assert(r.outer(&c,0x022C36D0,out)&&out.authority==D::Authority::Foreign);assert(!r.fault());
 f.retail=false;assert(r.outer(&c,0x022C36D0,out)&&out.authority==D::Authority::InvalidOwned&&r.fault()&&f.cancelled==1);
 }
 {Fixture f;f.owned=false;R::Routes r(f.e,f.d,f.bridge,f.terminal,f.camera,f.end,f.sources());c.r[3]=f.e.actors[0].address;D::Call out;assert(r.outer(&c,0x022C36D0,out)&&out.authority==D::Authority::InvalidOwned);assert(r.route(&c,0x022C2958,[&]{++calls;})==R::Decision::Handled&&calls==0&&f.cancelled==1);}
 {Fixture f;f.claim=false;R::Routes r(f.e,f.d,f.bridge,f.terminal,f.camera,f.end,f.sources());c.r[3]=f.e.actors[0].address;assert(r.route(&c,0x022C2958,[&]{++calls;})==R::Decision::Handled&&calls==1&&!f.cancelled&&f.d.snapshot().idle());}
 {Fixture f;R::Routes r(f.e,f.d,f.bridge,f.terminal,f.camera,f.end,f.sources());c.r[3]=f.e.identity.manager-0xF4;assert(r.route(&c,0x0253FF34,[&]{++calls;})==R::Decision::Handled&&calls==2);assert(r.route(&c,0x0253EFA8,[&]{++calls;})==R::Decision::Handled&&calls==3);c.r[3]=f.e.identity.manager;c.r[4]=c.r[5]=0;assert(r.route(&c,0x0254465C,[&]{++calls;})==R::Decision::Handled&&calls==3&&f.end.intent().pending);f.terminal_call=true;assert(r.route(&c,0x0254465C,[&]{++calls;})==R::Decision::Handled&&calls==4);}
 {Fixture f;R::Routes r(f.e,f.d,f.bridge,f.terminal,f.camera,f.end,f.sources());c.r[3]=f.e.camera.address;assert(r.route(&c,0x024FE3E8,[&]{++calls;})==R::Decision::Delegate&&calls==4);}
 // Manager scopes and their callback ancestry span the original once.
 {Fixture f;R::Routes r(f.e,f.d,f.bridge,f.terminal,f.camera,f.end,f.sources());c.r[3]=f.e.identity.manager;D::Call out;assert(r.outer(&c,0x02544374,out));auto outer=f.d.enter(out);assert(outer.allowed());assert(r.route(&c,0x02543D80,[&]{++calls;assert(f.d.current_owned_call()->entry==D::Entry::ManagerMain);c.r[3]=f.e.identity.image+0x1E0;assert(r.route(&c,0x02541664,[&]{++calls;assert(f.d.current_owned_call()->entry==D::Entry::Timekeeper);})==R::Decision::Handled);})==R::Decision::Handled&&calls==6&&!r.fault());}
 // Source-audited camera/timekeeper lookup ABI and staff identity are exact.
 for(unsigned bad=0;bad<4;++bad){Fixture f;R::Routes r(f.e,f.d,f.bridge,f.terminal,f.camera,f.end,f.sources());
  c={};c.r[3]=f.e.identity.manager;D::Call call;assert(r.outer(&c,0x02544374,call));auto outer=f.d.enter(call);
  r.route(&c,0x02543D80,[&]{c.r[3]=f.e.identity.image+0x1E0;r.route(&c,0x02541664,[&]{
   c.r[3]=f.e.identity.manager;c.r[4]=0x1004DAB4;c.r[5]=c.r[6]=0;
   if(bad==1)++c.r[4];if(bad==2)c.r[5]=f.e.actors[0].address;if(bad==3)c.r[6]=1;
   unsigned invoked=0;assert(r.route(&c,0x02542D88,[&]{++invoked;assert(f.d.current_owned_call()->entry==D::Entry::StaffLookup);})==R::Decision::Handled);
   assert(invoked==(bad?0u:1u));if(bad)assert(c.r[3]==~0u);
  });});assert(r.fault()==bool(bad));
 }
 // Rejected unscoped/wrong-thread staff lookup follows the native -1 ABI.
 for(bool wrong_thread:{false,true}){Fixture f;f.owned=!wrong_thread;R::Routes r(f.e,f.d,f.bridge,f.terminal,f.camera,f.end,f.sources());Cpu query{};query.r[3]=f.e.identity.manager;query.r[4]=0x10003000;unsigned bodies=0;assert(r.route(&query,0x02542D88,[&]{++bodies;})==R::Decision::Handled&&query.r[3]==~0u&&!bodies&&r.fault());}
 // Actual Terminal and Barrier helpers: scoped ALL suppresses foreign actors,
 // and a completed camera Run is handled once without replaying its body.
 {
  Fixture f;auto baseline=f.e.camera;baseline.current_type=baseline.next_type=1;baseline.event_type=2;
  auto setword=[](auto&b,unsigned at,uint32_t v){for(unsigned n=0;n<4;++n)b[at+n]=uint8_t(v>>(24-n*8));};
  setword(baseline.event_data,12,~0u);
  story_native_manager_terminal::Identity ti;ti.lease=f.e.identity;ti.event=f.e.identity.image+0x40;ti.index=0;
  for(unsigned n=0;n<2;++n){const auto&a=f.e.actors[n];ti.actors[n]={a.address,a.pid,a.token,a.profile,a.subtype,0,a.fingerprint};}
  uint32_t state=4,play=1;R::Routes*routes=nullptr;unsigned originals=0;
  story_native_manager_terminal::Terminal terminal(ti,[&](auto&o){o.lease=ti.lease;o.event=ti.event;o.index=0;o.event_state=state;o.camera_play=play;o.actors=ti.actors;o.retained_allocation=o.entry_fence=o.idle_depths=o.manager_table_owned=true;return true;},[&](uint32_t a,auto&o){for(const auto&x:ti.actors)if(a==x.address){o=x;return true;}return false;},[&](uint32_t manager,int32_t index,uint32_t force){
   f.terminal_call=true;Cpu native{};native.r[3]=manager;native.r[4]=uint32_t(index);native.r[5]=force;
   assert(routes->route(&native,0x0254465C,[&]{for(uint32_t a:{ti.actors[0].address,0x22220000u,ti.actors[1].address}){native.r[3]=a;assert(routes->route(&native,0x02542C9C,[&]{++originals;native.r[3]=0;})==R::Decision::Handled);}state=play=0;})==R::Decision::Handled);f.terminal_call=false;
  });
  auto current=baseline;current.current_type=current.next_type=2;setword(current.event_data,12,1);
  story_native_camera_return::Sources cs;cs.capture=[&](auto*,auto&o){o.identity=f.e.identity;o.camera=current;o.event_state=state;o.camera_play=play;o.terminal_serial=terminal.completion().serial();o.retained=o.exclusive=true;return true;};cs.verify_camera=[](auto&,auto&){return true;};cs.owned_thread=[](auto*){return true;};cs.route=[]{return true;};
  story_native_camera_return::Barrier barrier(f.e.identity,baseline,cs);
  R::Routes r(f.e,f.d,f.bridge,terminal,barrier,f.end,f.sources());routes=&r;
  assert(terminal.close()==story_native_manager_terminal::Result::Completed&&originals==2&&barrier.arm(terminal.completion()));
  Cpu camera{};camera.r[3]=baseline.address;unsigned runs=0;
  assert(r.route(&camera,0x024FE3E8,[&]{++runs;current.current_type=current.next_type=1;setword(current.event_data,12,~0u);})==R::Decision::Handled&&runs==1&&barrier.returned());
  assert(r.route(&camera,0x024FE3E8,[&]{++runs;})==R::Decision::Delegate&&runs==1);
 }

}
