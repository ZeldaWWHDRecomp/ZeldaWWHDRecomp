// SPDX-License-Identifier: MPL-2.0
#include "mods/story_native_event_table_scan.h"
#include <cassert>
#include <map>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
#include <span>
using namespace story_native_event_table_scan;
constexpr uint32_t Play=0x1046F0B0,Manager=Play+0x52C4,Base=0x20000000;
struct Fixture {
 std::map<uint32_t,std::vector<uint8_t>>memory;Context context;unsigned queries=0,reads=0;bool changed=false,throw_read=false,mutate_state=false;
 Fixture(){memory[Play].resize(0x6000);memory[0x1047E6C4].resize(4);memory[0x1047E8F0].resize(64*0x22C);memory[0x104753A8].resize(4);memory[Base].resize(0x1000);
 context={77,88,11,{'s','e','a'},{Base,0x1000,9,0x1004DF68,0x1004DF70,true},100,12,true};
 put(0x1047E6C4,77);put(0x1047E8F0+11*0x22C,88);put(0x104753A8,1);std::copy(context.stage_name.begin(),context.stage_name.end(),ptr(Play+0x5134,8));
 put(Manager,Base);put(Manager+4,Base+0x40);put(Base,0x40);put(Base+4,2);
 }
 uint8_t*ptr(uint32_t a,size_t n){for(auto&[base,b]:memory)if(a>=base&&uint64_t(a)+n<=uint64_t(base)+b.size())return b.data()+a-base;return nullptr;}
 void put(uint32_t a,uint32_t n){auto*p=ptr(a,4);assert(p);for(unsigned k=0;k<4;++k)p[k]=uint8_t(n>>(24-k*8));}
 Result scan(){return inspect([&](uint32_t a,std::span<uint8_t>b){++reads;if(throw_read)throw std::runtime_error("read");auto*p=ptr(a,b.size());if(!p)return false;std::copy_n(p,b.size(),b.begin());if(mutate_state&&a==Base+0x40+176)put(Base+0x40+0xA4,1);return true;},[&](Context&out){out=context;if(changed&&queries++)++out.resource.generation;return true;});}
};
int main(){Fixture f;auto good=f.scan();assert(good.idle&&good.events==2&&good.reason==Reason::None);
 // A retail census is not limited to the authored fixture's pool counts.
 Fixture retail;retail.memory[Base].resize(0x10000);retail.context.resource.bytes=0x10000;
 retail.put(Base+4,252);auto census=retail.scan();assert(census.idle&&census.events==252);
 retail.put(Base+0x40+251*176+0xA4,2);auto last=retail.scan();
 assert(!last.idle&&last.blocking_index==251&&last.reason==Reason::Busy);
 for(uint32_t state:{1u,2u,4u,3u,~0u}){Fixture x;x.put(Base+0x40+176+0xA4,state);auto bad=x.scan();assert(!bad.idle&&bad.blocking_index==1&&bad.blocking_state==state&&bad.reason==((state==1||state==2||state==4)?Reason::Busy:Reason::State));}
 for(unsigned fault=0;fault<12;++fault){Fixture x;if(fault==0)x.context.resource.loaded_retail=false;if(fault==1)x.context.resource.lookup_name++;if(fault==2)x.context.resource.generation=0;if(fault==3)x.context.exclusive_idle=false;if(fault==4)x.put(0x1047E6C4,78);if(fault==5)x.put(Manager+0x53C,Base+0x40);if(fault==6)x.put(Manager+0x20,1);if(fault==7)x.put(Manager+4,Base+0x44);if(fault==8)x.put(Base+4,129);if(fault==9)x.put(Base,0xFFFFFFF0);if(fault==10)x.put(0x104753A8,0);if(fault==11)x.put(Manager,0x60000000);assert(!x.scan().idle);}
 Fixture overlap;overlap.put(Base+8,0x40);overlap.put(Base+12,1);overlap.put(Manager+8,Base+0x40);assert(!overlap.scan().idle);
 Fixture stale_pool;stale_pool.put(Manager+8,Base+0x400);assert(!stale_pool.scan().idle);
 Fixture changed;changed.changed=true;assert(changed.scan().reason==Reason::Changed);
 Fixture mixed;mixed.mutate_state=true;assert(mixed.scan().reason==Reason::Changed);
 Fixture thrown;thrown.throw_read=true;assert(!thrown.scan().idle);
 assert(!inspect({},{}).idle); // no provenance/native Expected fallback
}
