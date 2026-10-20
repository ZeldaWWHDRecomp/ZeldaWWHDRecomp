#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
// SPDX-License-Identifier: MPL-2.0
#include "mods/native_actor_profiles.h"
#include "runtime.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {std::map<uint32_t,void(*)(Cpu*)> hooks;unsigned originals=0,notifications=0;uint32_t retired_actor=0;
void before_delete(uint32_t a,uint32_t pid,uint16_t profile) noexcept {assert(a==0x10000000&&pid==77&&profile==0x1D8);++notifications;retired_actor=a;}
void other_delete(uint32_t,uint32_t,uint16_t) noexcept {}
template<unsigned N> void bounded_listener(uint32_t,uint32_t,uint16_t) noexcept {}
void original(Cpu*c,uint32_t f){assert(f==0x025E10E0||f==0x025DD414||f==0x025D4B4C);if(f==0x025D4B4C&&c->r[3]==0x10000000)assert(retired_actor==c->r[3]);++originals;c->r[3]=0x12345678;}}
int main(){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(0x60000000),0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(0x60000000),0x1000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(0x10000000),0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(0x10000000),0x1000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
using namespace native_actor_profiles;
assert(!add_before_delete_listener("",before_delete));
assert(!add_before_delete_listener("retire",nullptr));
assert(add_before_delete_listener("retire",before_delete));
assert(add_before_delete_listener("retire",before_delete));
assert(!add_before_delete_listener("alias",before_delete));
assert(!add_before_delete_listener("retire",other_delete));
const BeforeDelete extra_listeners[]={bounded_listener<0>,bounded_listener<1>,bounded_listener<2>,bounded_listener<3>,bounded_listener<4>,bounded_listener<5>,bounded_listener<6>};
for(unsigned i=0;i<7;++i)assert(add_before_delete_listener("bounded-"+std::to_string(i),extra_listeners[i]));
assert(!add_before_delete_listener("overflow",bounded_listener<7>));
assert(add_before_delete_listener("bounded-6",extra_listeners[6]));
constexpr uint16_t ids[]={0x7FFD,0x7FFE,0x7FFC,0x7FFB,0x7FFA,0x7FF9,0x7FF8,0x7FF7,0x7FF6,0x7FF5};
for(unsigned i=0;i<10;++i)st16(0x60000008+i*0x100,ids[i]);
st16(0x60000A08,0x7FFA);
assert(valid_storage(mem::kRuntimeStart)&&valid_storage(mem::kHostStart-0x30));
for(uint32_t bad:{mem::kMem2Start,mem::kHostStart,mem::kRuntimeEnd,mem::kRuntimeStart+1,mem::kRuntimeStart-4,mem::kHostStart-0x2C})assert(!valid_storage(bad));
auto empty=[](uint32_t){return std::string();};auto conflict=[](uint32_t){return std::string("foreign");};auto replace=[](uint32_t a,void(*f)(Cpu*)){hooks[a]=f;};
configure(original);assert(!available(conflict));assert(!install(empty,replace)&&hooks.empty());
assert(!add(0x1C5,0x60000000)&&!add(0x7FFD,1)&&!add(0x7FFD,0xFFFFFFFC));
assert(add(ids[0],0x60000000)&&add(ids[0],0x60000000)&&!add(ids[0],0x60000100));
assert(!install(conflict,replace)&&hooks.empty());assert(install(empty,replace)&&hooks.size()==3);
// Every remaining reserved slot fails closed at both boundaries, preserving the rest of Cpu.
for(unsigned i=1;i<10;++i)for(unsigned fn:{0x025E10E0u,0x025DD414u}){
 Cpu c{};c.r[3]=ids[i];c.r[4]=0x87654321;c.f[1].ps0=3.25;Cpu expected=c;expected.r[3]=0;
 hooks.at(fn)(&c);assert(std::memcmp(&expected,&c,sizeof c)==0&&originals==0);
}
for(unsigned i=1;i<10;++i){const uint32_t at=0x60000000+i*0x100;assert(!add(ids[i],0x60000000));assert(add(ids[i],at)&&add(ids[i],at));}
assert(!add(0x7FF4,0x60000A00)&&!add(0x7FFF,0x60000A00));
assert(!add(0x7FFA,0x60000A00)); // Changed address rejected even with matching native profile ID.
assert(install(conflict,replace)&&hooks.size()==3);
for(unsigned i=0;i<10;++i){
 Cpu c{};c.r[3]=ids[i];c.r[4]=0x87654321;c.f[1].ps0=3.25;Cpu expected=c;expected.r[3]=0x60000000+i*0x100;
 hooks.at(0x025E10E0)(&c);assert(std::memcmp(&expected,&c,sizeof c)==0);
 c.r[3]=ids[i];expected=c;expected.r[3]=0x12345678;hooks.at(0x025DD414)(&c);assert(std::memcmp(&expected,&c,sizeof c)==0);
}
assert(originals==10);
for(unsigned id:{0x1C5u,0x7FCFu,0x7FFFu}){Cpu c{};c.r[3]=id;hooks.at(0x025E10E0)(&c);assert(c.r[3]==0x12345678);}
// the IDs kept for later enemy kinds (7FD0..7FF3) fail closed until registered
for(unsigned id:{0x7FD0u,0x7FDFu,0x7FE0u,0x7FE9u,0x7FF0u,0x7FF1u,0x7FF2u,0x7FF3u}){Cpu c{};c.r[3]=id;hooks.at(0x025E10E0)(&c);assert(c.r[3]==0);}
for(unsigned fn:{0x025DD414u,0x025D4B4Cu}){Cpu c{};c.r[3]=0x1C5;c.r[4]=0x87654321;c.f[1].ps0=3.25;Cpu expected=c;expected.r[3]=0x12345678;hooks.at(fn)(&c);assert(std::memcmp(&expected,&c,sizeof c)==0);}
assert(originals==15);
assert(add_before_delete_listener("retire",before_delete));
assert(!add_before_delete_listener("late",other_delete));
st32(0x10000004,77);st16(0x1000000E,0x1D8);
Cpu c{};c.r[3]=0x10000000;c.r[4]=0x87654321;c.f[1].ps0=3.25;
Cpu expected=c;expected.r[3]=0x12345678;
hooks.at(0x025D4B4C)(&c);
assert(notifications==1&&originals==16&&std::memcmp(&expected,&c,sizeof c)==0);
}
