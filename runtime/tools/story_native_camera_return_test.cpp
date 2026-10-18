// SPDX-License-Identifier: MPL-2.0
#include "../src/mods/story_native_camera_return.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cassert>
#include <stdexcept>
#include <cstdint>
using namespace story_native_camera_return;
namespace {
void put(Camera&c,uint32_t v){for(unsigned n=0;n<4;++n)c.event_data[12+n]=uint8_t(v>>(24-8*n));}
story_native_manager_terminal::Completion complete(Identity lease){
 namespace M=story_native_manager_terminal;
 M::Identity id;id.lease=lease;id.event=lease.image+0x40;id.index=0;
 for(unsigned n=0;n<2;++n){auto&a=id.actors[n];a.address=0x48020000+n*0x1000;a.pid=4+n;a.token=6+n;a.profile=0x175;a.subtype=1;a.fingerprint[0]=uint8_t(n+1);}
 M::Observation live;live.lease=lease;live.event=id.event;live.index=0;live.event_state=4;live.camera_play=1;live.actors=id.actors;
 live.retained_allocation=live.entry_fence=live.idle_depths=live.manager_table_owned=true;
 M::Terminal*active=nullptr;M::Terminal terminal(id,[&](M::Observation&out){out=live;return true;},
  [&](uint32_t address,M::Actor&out){for(const auto&a:live.actors)if(a.address==address){out=a;return true;}return false;},
  [&](uint32_t,int32_t,uint32_t){for(const auto&a:live.actors)active->route_all_off(a.address,[]{return 0u;});live.event_state=live.camera_play=0;});
 active=&terminal;assert(terminal.close()==M::Result::Completed);return terminal.completion();
}
struct Fixture {
 Identity id{0x48000000,0x60000000,0x520,12,13,7,91};
 story_native_manager_terminal::Completion proof=complete(id);
 Camera baseline{};Capture current{};bool thread=true,route=true,throw_read=false;unsigned calls=0;
 Fixture(){baseline.address=0x48010000;baseline.pid=3;baseline.current_type=baseline.next_type=2;
  baseline.event_type=6;put(baseline,~0u);current.identity=id;current.camera=baseline;
  current.camera.current_type=current.camera.next_type=6;put(current.camera,2);
  current.retained=current.exclusive=true;current.event_state=current.camera_play=0;current.terminal_serial=proof.serial();}
 Sources sources(){return {[&](const Cpu*,Capture&out){if(throw_read)throw std::runtime_error("read");out=current;return true;},
  [](const Camera&,const Identity&){return true;},[&](const Cpu*){return thread;},[&]{return route;}};}
 void returned(){current.camera.current_type=current.camera.next_type=2;put(current.camera,~0u);}
};
}
int main(){
 const auto run=GC_OPTIONAL(0x024FE3E8);assert(run);
 Fixture f;Barrier b(f.id,f.baseline,f.sources());assert(!b.arm({})&&b.arm(f.proof)&&!b.arm(f.proof));
 Cpu c{};c.r[3]=f.baseline.address;
 assert(b.run(&c,run+4,[&]{++f.calls;})==Decision::Delegate&&f.calls==0);
 assert(b.run(&c,run,[&]{++f.calls;c.r[3]=123;})==Decision::Observed&&!b.returned()&&c.r[3]==123);
 c.r[3]=f.baseline.address;
 assert(b.run(&c,run,[&]{++f.calls;f.returned();c.r[3]=456;})==Decision::Observed&&b.returned()&&c.r[3]==456);
 c.r[3]=f.baseline.address;
 assert(b.run(&c,run,[&]{++f.calls;})==Decision::Delegate&&f.calls==2);
 assert(b.verify_fresh(&c,f.id,f.current.camera));
 f.current.retained=false;assert(!b.verify_fresh(&c,f.id,f.current.camera));f.current.retained=true;
 f.current.exclusive=false;assert(!b.verify_fresh(&c,f.id,f.current.camera));f.current.exclusive=true;
 ++f.current.terminal_serial;assert(!b.verify_fresh(&c,f.id,f.current.camera));--f.current.terminal_serial;
 f.current.camera.current_type=f.current.camera.next_type=f.baseline.event_type;put(f.current.camera,2);
 assert(!b.verify_fresh(&c,f.id,f.current.camera)); // sticky return cannot authorize re-entry cleanup
 // A failed onTypeChange after nextType consumed +408 must retain/quarantine.
 Fixture hazard;Barrier h(hazard.id,hazard.baseline,hazard.sources());assert(h.arm(hazard.proof));
 assert(h.run(&c,run,[&]{put(hazard.current.camera,~0u);})==Decision::Quarantined&&h.quarantined());
 Fixture pending;Barrier inconsistent(pending.id,pending.baseline,pending.sources());assert(inconsistent.arm(pending.proof));
 assert(inconsistent.run(&c,run,[&]{pending.current.camera.next_type=2;})==Decision::Quarantined);
 for(unsigned n=0;n<7;++n){Fixture bad;Barrier barrier(bad.id,bad.baseline,bad.sources());assert(barrier.arm(bad.proof));
  switch(n){case 0:bad.current.retained=false;break;case 1:bad.current.foreign=true;break;
   case 2:++bad.current.terminal_serial;break;case 3:bad.current.camera_play=1;break;
   case 4:++bad.current.identity.generation;break;case 5:bad.thread=false;break;case 6:bad.throw_read=true;break;}
  unsigned called=0;assert(barrier.run(&c,run,[&]{++called;})==Decision::Quarantined&&called==0);
 }
 Fixture changed;Barrier wrong(changed.id,changed.baseline,changed.sources());assert(wrong.arm(changed.proof));
 assert(wrong.run(&c,run,[&]{changed.returned();changed.current.camera.current_type=changed.current.camera.next_type=4;})==Decision::Quarantined);
 Fixture thrown;Barrier exception(thrown.id,thrown.baseline,thrown.sources());assert(exception.arm(thrown.proof));
 bool caught=false;try{exception.run(&c,run,[]{throw std::runtime_error("native");});}catch(const std::runtime_error&){caught=true;}
 assert(caught&&exception.quarantined());
 Fixture nested;Barrier recursive(nested.id,nested.baseline,nested.sources());assert(recursive.arm(nested.proof));
 unsigned executions=0;
 assert(recursive.run(&c,run,[&]{++executions;
  assert(recursive.run(&c,run,[&]{++executions;})==Decision::Quarantined);
 })==Decision::Quarantined&&executions==1);
}
