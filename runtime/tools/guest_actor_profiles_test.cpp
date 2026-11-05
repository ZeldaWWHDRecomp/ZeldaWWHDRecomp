// SPDX-License-Identifier: MPL-2.0
#include "mods/native_actor_profiles.h"
#include "mods/native_actor_profile_startup.h"
#include "mods/guest_profile_state.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <map>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {
std::map<uint32_t,void(*)(Cpu*)> hooks;
unsigned originals=0;
void retire(uint32_t,uint32_t,uint16_t)noexcept{}
void original(Cpu*c,uint32_t){++originals;c->r[3]=0xCAFE;}
void map(uint32_t a,uint32_t n){
#ifdef _WIN32
 assert(VirtualAlloc(mem::ptr(a),n,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
#else
 assert(mmap(mem::ptr(a),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
constexpr uint32_t descriptor=0x7F100000,profile=descriptor+0x40,methods=descriptor+0x80;
void setup(uint16_t id,const char*name){
 st32(descriptor,28);st32(descriptor+4,1);st32(descriptor+8,profile);
 std::memset(mem::ptr(descriptor+12),0,8);std::memcpy(mem::ptr(descriptor+12),name,std::strlen(name));st32(descriptor+20,uint32_t(-1));st32(descriptor+24,0);
 std::memset(mem::ptr(profile),0,0x30);st16(profile+8,id);st32(profile+0x10,0x3B8);st32(profile+0x24,methods);
 for(unsigned i=0;i<5;++i)st32(methods+i*4,descriptor+0x100+i*4);
}
}
int main(){
 using namespace native_actor_profiles;
 // Import order and failures cannot leak discovery from a refused module.
 for(bool importer_first:{false,true}){
  bool imported=false;
  {startup::ImportAttempt failed(imported);imported=true;}assert(!imported);
  for(bool use_api:{importer_first,!importer_first}){
   {startup::ImportAttempt accepted(imported);if(use_api)imported=true;accepted.accept();}
   const bool before=imported;
   {startup::ImportAttempt failed_between(imported);imported=true;}assert(imported==before);
  }
  assert(imported);
 }
 bool allowed=false;
 {startup::EntryScope game_main(allowed,true);assert(allowed);
  {startup::EntryScope other(allowed,false);assert(!allowed);}assert(allowed);
 }assert(!allowed);

 assert(guest_execute_method(0x1E4,0x60000030)==0); // no dereference before installation
 map(0x60000000,0x10000);map(0x101E0000,0x40000);map(0x10000000,0x10000);map(descriptor,0x10000);
 st32(GD(0x101F3EE0)+0x1E3*4,0x10000000);st16(0x10000008,0x1E3);
 st32(GD(0x101F3EE0)+0xB6*4,0x10000100);st32(0x1000010C,0x10123450);st32(0x1000011C,0x10123460);
 std::memcpy(mem::ptr(GD(0x101EBFDC)),"Retail",6);
 auto range=[](uint32_t a,uint32_t n){return a>=descriptor&&uint64_t(a)+n<=uint64_t(descriptor)+0x10000;};
 auto function=[](uint32_t a){return a>=descriptor+0x100&&a<descriptor+0x114&&!(a&3);};
 const std::string fingerprint(64,'a');
 auto reg=[&]{return register_guest("example",fingerprint,descriptor,range,function);};
 auto owner=[](uint32_t){return std::string();};auto replace=[](uint32_t a,void(*f)(Cpu*)){hooks[a]=f;};
 configure(original);
 unsigned allocations=0;
 auto pool=[&](uint32_t at){return [&allocations,at]{++allocations;return at;};};
 assert(!install_guest(pool(0x7F000000),owner,replace));assert(allocations==1);
 // A refused bridge must not take the profile pool from the startup allocator.
 assert(!install_guest(pool(0x60000000),[](uint32_t){return std::string("foreign");},replace));assert(hooks.empty()&&allocations==1);
 // Port services install their listener/profile first, before the bridge.
 assert(add_before_delete_listener("existing-port-listener",retire));
 st16(0x60008008,0x7FFE);assert(add(0x7FFE,0x60008000));
 assert(install(owner,replace)&&hooks.size()==3);
 assert(install_guest(pool(0x60000000),owner,replace)&&hooks.size()==5&&allocations==2);
 assert(add_before_delete_listener("existing-port-listener",retire));
 // GameMain entry invokes registration before the original begins allocating
 // processes. Lookup queries do not close the window; first base create does.
 setup(0x1E4,"SwOp");Cpu c{};c.r[3]=0x1E4;hooks.at(GC(0x025E10E0))(&c);assert(c.r[3]==0);
 st16(profile+0xA,0xFFFF);st16(profile+0x22,0xFFFF);st16(profile+0x2E,0xFFFF);
 assert(reg()==0x1E4);
 assert(guest_execute_method(0x1E4,0x60000030)==descriptor+0x108);
 assert(guest_execute_method(0x1E5,0x60000030)==0); // ID/table identity must agree
 for(uint32_t invalid:{0u,0x6000002Fu,0x60000031u,0x60000034u,0x60000090u,0x6000FFF8u,0x68000030u,0xFFFFFFFFu})
     assert(guest_execute_method(0x1E4,invalid)==0);
 // A source module can no longer mutate its already admitted copy.
 st32(methods+8,0);assert(guest_execute_method(0x1E4,0x60000030)==descriptor+0x108);
 // Every copied callback and profile byte is protected, not just Execute.
 for(unsigned offset:{0u,8u,0x24u,0x30u,0x34u,0x38u,0x3Cu,0x40u,0x5Cu}){
     const uint32_t before=ld32(0x60000000+offset);st32(0x60000000+offset,before^4);
     assert(guest_execute_method(0x1E4,0x60000030)==0);
     assert(ld32(0x60000000+offset)==(before^4)); // query never repairs state
     restore_guest_snapshots();assert(guest_execute_method(0x1E4,0x60000030)==descriptor+0x108);
 }
 assert(!ld16(0x6000000A)&&!ld16(0x60000022)&&!ld16(0x6000002E));const auto identity=guest_identity();assert(!identity.empty());
 assert(reg()==-2);setup(0x1E5,"SwOp");assert(reg()==-2);
 setup(0x1E5,"Retail");assert(reg()==-2);
 setup(0x1E3,"Other");assert(reg()==-1);setup(0x7FD0,"Other");assert(reg()==-1);
 setup(0x1E5,"DngSw");st32(methods,0x02500000);assert(reg()==-1);
 setup(0x1E5,"DngSw");st32(profile+0xC,descriptor);assert(reg()==-1);
 setup(0x1E5,"DngSw");st32(profile+0x10,0x3AB);assert(reg()==-1);
 setup(0x1E5,"DngSw");assert(reg()==0x1E5);
 assert(guest_execute_method(0x1E5,0x60000090)==descriptor+0x108);
 assert(guest_execute_method(0x1E4,0x60000090)==0);
 c.r[3]=0x1E4;hooks.at(GC(0x025E10E0))(&c);assert(c.r[3]==0x60000000);
 assert(ld32(c.r[3]+0xC)==0x10123450&&ld32(c.r[3]+0x1C)==0x10123460&&ld32(c.r[3]+0x24)==0x60000030);
 assert(ld32(0x60000030)==descriptor+0x100);
 // Source edits and stale restored runtime memory cannot mutate admission.
 st16(profile+8,0x1234);st32(0x60000030,0);restore_guest_snapshots();assert(ld32(0x60000030)==descriptor+0x100);
 std::memcpy(mem::ptr(descriptor+0x200),"SwOp",5);c.r[3]=descriptor+0x200;hooks.at(GC(0x025C109C))(&c);assert(c.r[3]==0x60000044);
 c.r[3]=0x1E4;c.r[4]=uint32_t(-1);hooks.at(GC(0x025C10E8))(&c);assert(c.r[3]==0x60000050&&!std::strcmp(reinterpret_cast<const char*>(mem::ptr(c.r[3])),"SwOp"));
 // Fill every bounded slot; the next valid request refuses without mutation.
 for(unsigned i=2;i<kGuestCapacity;++i){char name[9];std::snprintf(name,sizeof name,"X%07u",i);setup(uint16_t(0x1E4+i),name);assert(reg()==int32_t(0x1E4+i));}
 setup(0x300,"Overflow");const auto full=guest_identity();assert(reg()==-5&&guest_identity()==full);
 c.r[3]=0x1E4;hooks.at(GC(0x025DD414))(&c);assert(c.r[3]==0xCAFE);
 setup(0x1E6,"Later");assert(reg()==-3); // new ID still refuses late calls
 c.r[3]=0x7FCF;hooks.at(GC(0x025E10E0))(&c);assert(c.r[3]==0);
 c.r[3]=0x1E3;hooks.at(GC(0x025E10E0))(&c);assert(c.r[3]==0xCAFE);
 // Existing runtime-owned profiles continue to use their reserved table.
 st16(0x60008008,0x7FFE);assert(add(0x7FFE,0x60008000));c.r[3]=0x7FFE;hooks.at(GC(0x025E10E0))(&c);assert(c.r[3]==0x60008000);
 ss::Writer w;w.str(guest_identity());assert(compatible_state(ss::Reader(w.b.data(),w.b.size()),guest_identity()));
 assert(!compatible_state(ss::Reader(w.b.data(),w.b.size()),identity));
 assert(!compatible_state(ss::Reader(nullptr,0),guest_identity()));assert(compatible_state(ss::Reader(nullptr,0),""));
 w.b.back()^=1;assert(!compatible_state(ss::Reader(w.b.data(),w.b.size()),guest_identity()));
}
