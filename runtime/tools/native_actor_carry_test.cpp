// SPDX-License-Identifier: MPL-2.0
#include "mods/native_actor_carry.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <map>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {
constexpr uint32_t Play=0x20000000,Link=0x20010000,Prop=0x20020000,Enemy=0x20021000,UnownedActor=0x20022000;
std::map<uint32_t,void(*)(Cpu*)> hooks;unsigned originals=0,traces=0;
void original(Cpu*c,uint32_t f){assert(f==0x023ED0F0);++originals;c->r[3]=7;}
void wrong_original(Cpu*,uint32_t){}
uint32_t invoke(Cpu*c,uint32_t f,std::initializer_list<uint32_t>){assert(f==0x025200D4);c->r[4]=0xBAD;c->pc=0xBAD;c->f[2].ps0=999;return Play;}
bool prop(uint32_t a){return a==Prop&&ld32(a+4)==10&&ld16(a+0xE)==0x7FFD;}
bool enemy(uint32_t a){return a==Enemy&&ld32(a+4)==11&&ld16(a+0xE)==0x7FFA;}
bool late(uint32_t){return false;}
void trace(uint32_t a){assert(a==Prop||a==Enemy);++traces;}
void run(uint32_t caller,uint32_t target,uint32_t expected){st32(Link+0x65B8,target);Cpu c{};c.r[3]=caller;c.r[4]=0x12345678;c.pc=0x87654321;c.f[2].ps0=4.5;Cpu before=c;before.r[3]=expected;hooks.at(0x023ED0F0)(&c);assert(std::memcmp(&before,&c,sizeof c)==0);}
}
int main(){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(Play),0x30000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(Play),0x30000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
using namespace native_actor_carry;auto owner=[](uint32_t){return std::string();};auto conflict=[](uint32_t){return std::string("foreign");};auto replace=[](uint32_t a,void(*fn)(Cpu*)){hooks[a]=fn;};
assert(!configure(nullptr,invoke)&&configure(original,invoke)&&configure(original,invoke)&&!configure(wrong_original,invoke));
assert(!install(owner,replace)&&hooks.empty());assert(!register_guard("",prop)&&!register_guard("props",nullptr));
assert(register_guard("props",prop,trace)&&register_guard("props",prop,trace));assert(!register_guard("props",enemy,trace)&&!register_guard("alias",prop,trace));assert(register_guard("enemies",enemy,trace));
assert(!available(conflict)&&!install(conflict,replace)&&hooks.empty());assert(install(owner,replace)&&hooks.size()==1);assert(install(conflict,replace)&&hooks.size()==1);assert(register_guard("props",prop,trace)&&!register_guard("late",late));
st32(Play+0x5B34,Link);st32(Prop+4,10);st16(Prop+0xE,0x7FFD);st32(Enemy+4,11);st16(Enemy+0xE,0x7FFA);st16(UnownedActor+0xE,0x7FFA);
run(Link,Prop,0);run(Link,Enemy,0);assert(traces==2&&originals==0);
run(Link,UnownedActor,7); // A reserved profile alone confers no ownership in the shared helper.
st32(Prop+4,99);run(Link,Prop,7);st32(Enemy+4,99);run(Link,Enemy,7); // pointer/PID reuse
run(Link+4,Prop,7); // Caller is not the actual current Link.
run(0,Prop,7); // Out-of-range caller is delegated without a guest query/read.
assert(originals==5&&traces==2);
}
