// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_dispatch.h"
#include "ppc.h"
#include <cassert>
#include <stdexcept>
#include <vector>
#include <cstdint>
namespace D=story_native_event_depth;namespace X=story_native_event_dispatch;
int main(){
 D::Expected e;e.identity={0x20000000,0x60000000,0x520,1,2,3,4};e.room_number=11;for(unsigned n=0;n<2;++n)e.actors[n]={0x20001000+n*0x1000,10+n,1+n,0x175,1,{}};e.camera.address=0x20003248;e.camera.pid=12;D::Coverage coverage{{11,11,true},{0,0,true},{0,0,true},true};D::Call call{D::Entry::PeopleExecute,D::entry_address(D::Entry::PeopleExecute),e.actors[0].address,e.actors[0].address,10,1,e.identity,D::Authority::Owned};
 D::Tracker t(e,coverage);unsigned native=0;std::vector<unsigned> order;Cpu cpu{};cpu.r[3]=5;cpu.f[1].ps0=1;
 auto native_body=[&]{assert(t.snapshot().npc_depth==1);++native;order.push_back(3);cpu.r[3]=77;cpu.f[1].ps0=3.5;return 77u;};
 auto dispatched=X::dispatcher(t,call,[&]{order.push_back(1);assert(t.snapshot().npc_depth==1);order.push_back(2);auto result=X::original(t,call,native_body);assert(result.executed&&result.observed&&result.delegated&&result.value==77);assert(t.snapshot().npc_depth==1);order.push_back(4);assert(!t.begin_drain());order.push_back(5);return result.value;});assert(dispatched.executed&&dispatched.observed&&dispatched.value==77&&native==1&&t.snapshot().idle());assert((order==std::vector<unsigned>{1,2,3,4,5}));assert(cpu.r[3]==77&&cpu.f[1].ps0==3.5);
 // Direct call_original outside dispatcher has its own outer observation.
 auto direct=X::original(t,call,native_body);assert(direct.executed&&direct.observed&&!direct.delegated&&native==2&&t.snapshot().idle());
 auto skip=X::dispatcher(t,call,[&]{assert(t.snapshot().npc_depth==1);return 123u;});assert(skip.value==123&&native==2&&t.snapshot().idle());
 for(unsigned where=0;where<3;++where){D::Tracker exception(e,coverage);try{X::dispatcher(exception,call,[&]()->uint32_t{assert(exception.snapshot().npc_depth==1);if(where==0)throw std::runtime_error("entry");X::original(exception,call,[&]()->uint32_t{if(where==1)throw std::runtime_error("native");return 1;});if(where==2)throw std::runtime_error("return/tail");return 1;});assert(false);}catch(const std::runtime_error&){}assert(exception.snapshot().idle()==false&&exception.snapshot().npc_depth==0&&exception.snapshot().fault);}
 D::Tracker twice(e,coverage);unsigned runs=0;X::dispatcher(twice,call,[&]{assert(X::original(twice,call,[&]{++runs;return 1;}).executed);assert(!X::original(twice,call,[&]{++runs;return 1;}).executed);return 1;});assert(runs==1&&twice.snapshot().fault);
 D::Tracker mismatch(e,coverage);X::dispatcher(mismatch,call,[&]{auto wrong=call;wrong.pid++;assert(!X::original(mismatch,wrong,[&]{assert(false);return 1;}).executed);return 1;});assert(mismatch.snapshot().fault);
 D::Tracker foreign(e,coverage);auto retail=call;retail.authority=D::Authority::Foreign;retail.subject=retail.receiver=0x20009000;retail.pid=99;retail.token=0;unsigned retail_calls=0;assert(X::original(foreign,retail,[&]{++retail_calls;assert(foreign.snapshot().foreign_depth==1);return 8;}).value==8);assert(foreign.begin_drain());auto arrived=X::dispatcher(foreign,retail,[&]{++retail_calls;return 9;});assert(arrived.executed&&!arrived.observed&&arrived.value==9&&retail_calls==2&&foreign.snapshot().fault);
}
