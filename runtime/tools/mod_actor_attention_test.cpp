// SPDX-License-Identifier: MPL-2.0
#include "mods/mod_actor_attention.h"
#include "guest_addr.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
// Strong synthetic regional code map catches unmapped and double-mapped hooks.
extern "C" {
extern const GuestStep g_guest_code_steps[]={{0,0},{0x02000000,0x100000},{0x03000000,0}};
extern const unsigned g_guest_code_step_count=3;
extern const uint32_t g_guest_code_lo=0x02000000,g_guest_code_hi=0x03000000;
}
void log_msg(const char*,...){}
namespace {
constexpr uint32_t Arena=0x20000000,Actor=Arena+0x1000,Out=Arena+0x100,RuntimeOut=mem::kRuntimeStart+0x10000;
std::map<uint32_t,void(*)(Cpu*)> hooks;unsigned delegated=0,verified=0;uint32_t type=2;double native_weight=8;bool throw_verify=false;
mod_actor::attention::Priority priority=mod_actor::attention::Priority::Normal;
void original(Cpu*c,uint32_t f){assert(f==GC(0x024EB2D4));++delegated;uint32_t out=c->r[8];if((out>=Arena&&out<Arena+0x1000)||(out>=RuntimeOut&&out<RuntimeOut+0x1000))st32(out,type);c->r[3]=0xABCDEF;c->r[4]=777;c->r[5]=888;c->r[8]=999;c->pc=0x456;c->lr=0x789;c->f[1].ps0=native_weight;c->f[1].ps1=123;c->f[3].ps0=44;c->cr[1]=true;}
void other(Cpu*,uint32_t){}
mod_actor::attention::Priority check(uint32_t a){++verified;assert(a==Actor);if(throw_verify)throw std::runtime_error("pure verifier fault");return priority;}
mod_actor::attention::Priority other_check(uint32_t){return mod_actor::attention::Priority::None;}
void run(double expected,uint32_t list='L',uint32_t actor=Actor,uint32_t out=Out){Cpu c{};c.r[3]=Arena;c.r[4]=list;c.r[5]=actor;c.r[8]=out;c.f[2].ps0=42;Cpu expected_cpu=c;original(&expected_cpu,GC(0x024EB2D4));expected_cpu.f[1].ps0=expected;const unsigned n=delegated;hooks.at(GC(0x024EB2D4))(&c);assert(delegated==n+1);assert(std::memcmp(&c,&expected_cpu,sizeof c)==0);}
}
int main(){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(Arena),0x3000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
assert(VirtualAlloc(mem::ptr(RuntimeOut),0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(Arena),0x3000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
assert(mmap(mem::ptr(RuntimeOut),0x1000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
using namespace mod_actor::attention;auto free=[](uint32_t f){assert(f==GC(0x024EB2D4));return std::string();};auto conflict=[](uint32_t){return std::string("other");};auto replace=[](uint32_t f,void(*fn)(Cpu*)){assert(f==GC(0x024EB2D4));hooks[f]=fn;};
assert(!install(free,replace));assert(!configure(nullptr,check)&&!configure(original,nullptr));assert(configure(original,check)&&configure(original,check));assert(!configure(other,check)&&!configure(original,other_check));assert(!available(conflict)&&!install(conflict,replace)&&hooks.empty());assert(install(free,replace)&&hooks.size()==1&&hooks.count(0x025EB2D4));assert(install(conflict,replace)&&hooks.size()==1);
priority=Priority::Normal;run(8);priority=Priority::Low;run(16);run(16,'L',Actor,RuntimeOut);priority=Priority::High;run(4);run(4,'L',Actor,RuntimeOut);priority=Priority::None;run(8);priority=Priority(99);run(8);
priority=Priority::High;unsigned n=verified;run(8,'A');assert(verified==n);type=1;run(8);assert(verified==n);type=2;run(8,'L',Actor,0xFFFFFFFC);assert(verified==n);run(8,'L',Actor+1);assert(verified==n);type=0xFFFFFFFF;run(8);assert(verified==n);type=2;
for(double invalid:{0.0,-1.0,std::numeric_limits<double>::infinity()}){native_weight=invalid;run(invalid);}native_weight=std::numeric_limits<double>::quiet_NaN();run(native_weight);
native_weight=8;throw_verify=true;run(8);throw_verify=false;priority=Priority::Low;native_weight=std::numeric_limits<float>::max();run(native_weight);priority=Priority::High;native_weight=std::numeric_limits<double>::denorm_min();run(native_weight);
}
