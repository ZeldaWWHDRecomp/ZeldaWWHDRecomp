// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_end_guard.h"
#include <cassert>
using namespace story_native_event_end_guard;
int main(){
 Identity id{0x48000000,0x60000000,0x520,7,91,0,12,13};
 Observation o{id,id.image+0x40,id.image+0x40,3,~0u,~0u,~0u,~0u,-1,true,true,false};
 Guard g(id);auto foreign=o;foreign.retained_claim=false;foreign.mode=2;
 assert(g.end_proc(foreign,true,true)==Decision::Delegate&&!g.intent().pending);
 assert(g.before_control(o)==Decision::Queue&&!g.intent().pending);
 assert(g.end_proc(o,true,true)==Decision::Queue);assert(g.intent().force_seen&&!g.intent().natural_terminal);
 assert(g.end_proc(o,false,true)==Decision::Queue&&g.intent().natural_terminal);
 Intent result;assert(!g.take(id,false,true,result));auto old=id;--old.generation;
 assert(!g.take(old,true,true,result));assert(!g.take(id,true,false,result));
 assert(g.take(id,true,true,result)&&result.force_seen&&result.natural_terminal&&!g.intent().pending);
 for(int n=0;n<11;++n){Guard bad(id);auto b=o;
 switch(n){case 0:b.mode=2;break;case 1:b.partner1=42;break;case 2:b.control_event_id=0;break;
 case 3:++b.identity.generation;break;case 4:++b.table;break;case 5:++b.event;break;
 case 6:b.identity_valid=false;break;case 7:b.foreign_takeover=true;break;case 8:b.item_partner=0;break;case 9:++b.identity.stage_pid;break;case 10:++b.identity.room_pid;break;}
 assert(bad.before_control(b)==Decision::Quarantine&&bad.quarantined());
 assert(bad.end_proc(o,false,true)==Decision::Quarantine&&!bad.take(id,true,true,result));
 assert(bad.end_proc(foreign,true,false)==Decision::Delegate); // never block foreign cleanup
 }
 Guard fresh(old);assert(!fresh.take(id,true,true,result));
}
