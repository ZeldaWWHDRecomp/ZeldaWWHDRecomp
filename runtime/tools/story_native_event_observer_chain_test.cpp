// SPDX-License-Identifier: MPL-2.0
// Isolated model of ppc_mod_run accounting. It does not install native hooks.
#include "mods/story_native_event_depth.h"
#include <cassert>
#include <functional>
#include <stdexcept>
#include <vector>
using namespace story_native_event_depth;
using Fn=std::function<void()>;
struct Chain {std::vector<Fn>entry,ret;Fn replacement,original;};
Expected expected(){Expected e;e.identity={0x20000000,0x60000000,0x520,1,2,3,4};e.room_number=11;for(unsigned n=0;n<2;++n)e.actors[n]={0x20001000+n*0x1000,10+n,1+n,0x175,1,{}};e.camera.address=0x20003248;e.camera.pid=12;return e;}
Call outer(){const auto e=expected();return {Entry::PeopleExecute,entry_address(Entry::PeopleExecute),e.actors[0].address,e.actors[0].address,e.actors[0].pid,e.actors[0].token,e.identity,Authority::Owned};}
Coverage coverage(){return {{11,11,true},{0,0,true},{0,0,true},true};}
bool dispatch(Tracker&t,Call c,Chain&ch){auto scope=t.enter(c);if(!scope.allowed())return false;for(auto&f:ch.entry)f();if(ch.replacement)ch.replacement();else ch.original();for(auto&f:ch.ret)f();return true;}
int main(){
 for(bool replace:{false,true}){Tracker t(expected(),coverage());std::vector<unsigned>order;unsigned original_calls=0;
  auto busy=[&]{assert(t.snapshot().npc_depth==1&&!t.begin_drain()&&!Tracker::valid_idle_record(t.record(),expected()));};
  Chain ch;ch.entry.push_back([&]{busy();order.push_back(1);});ch.original=[&]{busy();++original_calls;order.push_back(3);};
  if(replace)ch.replacement=[&]{busy();order.push_back(2);ch.original();busy();order.push_back(4);}; // direct call_original/mod_skip: no second dispatcher scope
  ch.ret.push_back([&]{busy();order.push_back(5);});assert(dispatch(t,outer(),ch));assert(original_calls==1&&t.snapshot().idle());assert(order==(replace?std::vector<unsigned>{1,2,3,4,5}:std::vector<unsigned>{1,3,5}));
 }
 // Each callback boundary stays inside the full observer; exceptions cannot leave false-idle.
 for(unsigned where=0;where<3;++where){Tracker t(expected(),coverage());unsigned native=0,returns=0;Chain ch;
  ch.entry.push_back([&]{assert(t.snapshot().npc_depth==1);if(where==0)throw std::runtime_error("entry");});
  ch.original=[&]{assert(t.snapshot().npc_depth==1);++native;};
  ch.replacement=[&]{ch.original();assert(t.snapshot().npc_depth==1);if(where==1)throw std::runtime_error("replacement tail");};
  ch.ret.push_back([&]{assert(t.snapshot().npc_depth==1);++returns;if(where==2)throw std::runtime_error("return hook");});
  try{dispatch(t,outer(),ch);assert(false);}catch(const std::runtime_error&){}assert(!t.snapshot().npc_depth&&t.snapshot().fault&&!t.begin_drain());assert(native==(where==0?0:1)&&returns==(where==2?1:0));
 }
 // Full outer stays counted after a nested common interpreter has already returned.
 Tracker nested(expected(),coverage());Chain ch;ch.original=[&]{auto move=outer();move.entry=Entry::PeopleEventMove;move.function=entry_address(move.entry);auto m=nested.enter(move);assert(m.allowed());auto cut=move;cut.entry=Entry::CommonCut;cut.function=entry_address(cut.entry);cut.receiver+=0x3E0;auto c=nested.enter(cut);assert(c.allowed()&&nested.snapshot().npc_depth==3);};ch.ret.push_back([&]{assert(nested.snapshot().npc_depth==1&&!nested.begin_drain());});assert(dispatch(nested,outer(),ch)&&nested.snapshot().idle());
 // Original foreign chain is still counted; unknown owner delegation refuses owned admission.
 Tracker foreign(expected(),coverage());auto f=outer();f.authority=Authority::Foreign;f.receiver=f.subject=0x20009000;f.pid=99;f.token=0;unsigned called=0;Chain retail;retail.original=[&]{++called;assert(foreign.snapshot().foreign_depth==1);};assert(dispatch(foreign,f,retail)&&called==1&&foreign.snapshot().idle());
 auto conflict=coverage();conflict.people.delegated_owner=22;Tracker blocked(expected(),conflict);assert(!dispatch(blocked,outer(),retail)&&called==1);
}
