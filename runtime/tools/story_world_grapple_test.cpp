#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>
// SPDX-License-Identifier: MPL-2.0
#include "mods/story_world_grapple.h"
#include "runtime.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {
constexpr uint32_t Arena=0x60000000,Base=0x20000000,Parent=Base+0x1000,Proxy=Base+0x2000,Rope=Base+0x4000,Play=Base+0x8000,Link=Base+0x10000;
std::map<uint32_t,PpcFunc> hooks,methods;uint32_t method_id=0xC2000000,profile=0,layer=99,spawns=0,nextpid=100;
bool owner=true,rope_live=true,proxy_live=false,emit_events=false,detach=false,synchronous=false,select_inside=false,pending_lookup=false;
unsigned native_candidates=0,native_moves=0,native_events=0,proxy_deletes=0,rope_deletes=0;
native_actor_profiles::BaseAllocated allocated=nullptr;
void map(uint32_t a,uint32_t n){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(a),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(a),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void method(unsigned n,Cpu*c,uint32_t a){c->r[3]=a;methods.at(ld32(profile+0x30+n*4))(c);}
void original(Cpu*c,uint32_t fn){if(fn==0x0216DD3C){++native_candidates;c->r[3]=0;return;}if(fn==0x025B8B68){++native_events;return;}assert(fn==0x0216F7FC);++native_moves;const uint32_t rope=c->r[3];if(select_inside)st32(rope+0x267C,Proxy);if(emit_events)for(uint32_t bit:{0x540u,0x580u,0x420u,0x123u}){Cpu event=*c;event.r[3]=Play;event.r[4]=bit;hooks.at(0x025B8B68)(&event);}if(detach&&(ld32(Link+0x3C0)&0x40000000)){st32(rope+0x267C,0);st32(rope+0x3F8,0);}}
uint32_t invoke(Cpu*c,uint32_t fn,std::initializer_list<uint32_t> args){std::vector<uint32_t>a(args);switch(fn){
case 0x025DED64:return layer;
case 0x025DEAB4:layer=a.at(0);return 0;
case 0x025D5834:{assert(a.at(0)==story_world_grapple::kProfile&&a.at(1)==0x00FF0000&&layer==123);assert(!proxy_live);++spawns;proxy_live=true;std::memset(mem::ptr(Proxy),0,story_world_grapple::kProxyBytes);st32(Proxy+4,nextpid);st16(Proxy+0xE,story_world_grapple::kProfile);st32(Proxy+0xB0,a.at(1));allocated(0,Proxy,story_world_grapple::kProxyBytes+64);if(synchronous){method(0,c,Proxy);assert(c->r[3]==4);}return nextpid++;}
case 0x025D4ED0:return 0;
case 0x025D54C4:{if(pending_lookup&&a.at(0)==ld32(Proxy+4)){st32(a.at(1),0);return 1;}uint32_t found=0;if(a.at(0)==ld32(Proxy+4)&&proxy_live)found=Proxy;if(a.at(0)==77&&rope_live)found=Rope;st32(a.at(1),found);return found!=0;}
case 0x025200D4:return Play;
case 0x025D57E0:if(a.at(0)==Proxy)++proxy_deletes;else{assert(a.at(0)==Rope);++rope_deletes;}return 1;
default:assert(!"unexpected native ABI");return 0;}}
}
namespace dispatch {uint32_t register_host(PpcFunc f,const char*){const uint32_t a=method_id;method_id+=4;methods[a]=f;return a;}}
namespace native_actor_profiles {
bool valid_storage(uint32_t a){return a>=mem::kRuntimeStart&&a<=mem::kHostStart-0x30&&!(a&3);}
bool add(uint16_t id,uint32_t a,BaseAllocated f){assert(id==story_world_grapple::kProfile);profile=a;allocated=f;return true;}
}
int main(){using namespace story_world_grapple;map(Arena,kStorageBytes);map(Base,0x20000);map(0x101F0000,0x10000);st32(0x101F3EE0+0xFA*4,Base);st32(Parent+4,42);st32(Parent+0x2C,123);st32(Play+0x5B34,Link);st32(Rope+4,77);st32(Rope+0x3F8,11);
 story_actors::identity::Fingerprint fp{};fp[0]=42;auto good=[](uint32_t){return std::string();};auto conflict=[](uint32_t f){return f==0x025B8B68?std::string("foreign"):std::string();};
 assert(configure(original,invoke,Arena,[&](uint32_t a,uint32_t p,const auto&f){return owner&&a==Parent&&p==42&&f==fp;}));assert(!available(conflict)&&hooks.empty());assert(available(good));assert(install(good,[](uint32_t f,PpcFunc p){hooks[f]=p;}));assert(hooks.size()==3&&ld32(profile+0x10)==kProxyBytes);
 Cpu c{};Target target{{20,300,40},90};assert(update(&c,Parent,42,fp,{&target,1})&&spawns==1&&layer==99);method(0,&c,Proxy);assert(c.r[3]==4&&ldf32(Proxy+0x318)==300&&ld16(Proxy+0x322)==16384&&ld32(Proxy+0xB0)==0x00FF0000);
 // Native health is mutable actor state, never proxy ownership metadata.
 for(unsigned health=0;health<4;++health){st8(Proxy+0x3A1,uint8_t(health));st16(Proxy+0x3AA,uint16_t(health));assert(update(&c,Parent,42,fp,{&target,1})&&spawns==1&&proxy_deletes==0);method(2,&c,Proxy);assert(c.r[3]==1&&proxy_deletes==0);st8(Rope+0x281C,0);c.r[3]=Proxy;c.r[4]=Rope;hooks.at(0x0216DD3C)(&c);assert(ld8(Rope+0x281C)==1&&native_candidates==0);}
 st8(Rope+0x281C,0);
 c.r[3]=Proxy;c.r[4]=Rope;hooks.at(0x0216DD3C)(&c);assert(c.r[3]==0&&ld8(Rope+0x281C)==1&&ld32(Rope+0x268C)==Proxy&&native_candidates==0);st8(Rope+0x281C,100);c.r[3]=Proxy;c.r[4]=Rope;hooks.at(0x0216DD3C)(&c);assert(ld8(Rope+0x281C)==100);
 c.r[3]=Parent;c.r[4]=Rope;hooks.at(0x0216DD3C)(&c);assert(native_candidates==1);
 st32(Rope+0x267C,Proxy);emit_events=true;c.r[3]=Rope;hooks.at(0x0216F7FC)(&c);assert(native_moves==1&&native_events==0);
 // Ordinary native posts request/coiling use signed demo -1 then2 at state10.
 for(int demo:{-1,2,0}){st8(Rope+0x2B09,uint8_t(demo));st32(Rope+0x3F8,10);const unsigned before_move=native_moves;c.r[3]=Rope;hooks.at(0x0216F7FC)(&c);assert(native_moves==before_move+1&&native_events==0&&rope_deletes==0&&proxy_deletes==0);assert(update(&c,Parent,42,fp,{&target,1})&&spawns==1);}
 st32(Rope+0x3F8,11);
 const std::vector<uint8_t> saved(mem::ptr(Base),mem::ptr(Base)+0x20000),metadata(mem::ptr(Arena),mem::ptr(Arena)+kStorageBytes);
 st8(Rope+0x2B09,0xFF);destroy(&c,Parent,42);assert((ld32(Link+0x3C0)&0x40000000)&&rope_deletes==0);method(3,&c,Proxy);assert(c.r[3]==0&&proxy_deletes==0);owner=false;detach=true;c.r[3]=Rope;hooks.at(0x0216F7FC)(&c);assert(native_events==0&&proxy_deletes==1);method(1,&c,Proxy);assert(c.r[3]==1);proxy_live=false;
 // Full-state restore needs no host ownership reconstruction.
 std::memcpy(mem::ptr(Base),saved.data(),saved.size());std::memcpy(mem::ptr(Arena),metadata.data(),metadata.size());state_loaded();proxy_live=true;owner=true;detach=false;assert(update(&c,Parent,42,fp,{&target,1})&&spawns==1);c.r[3]=Rope;hooks.at(0x0216F7FC)(&c);assert(native_events==0);
 // Selection inside the native call is guarded and remembered before returning.
 st32(Rope+0x267C,0);select_inside=true;c.r[3]=Rope;hooks.at(0x0216F7FC)(&c);select_inside=false;assert(native_events==0);destroy(&c,Parent,42);assert(ld32(Link+0x3C0)&0x40000000);method(3,&c,Proxy);assert(c.r[3]==0);
 std::memcpy(mem::ptr(Base),saved.data(),saved.size());std::memcpy(mem::ptr(Arena),metadata.data(),metadata.size());state_loaded();
 // A stale guest row cannot delete a foreign actor that recycled the address.
 st32(Proxy+4,999);st16(Proxy+0xE,0xFA);unsigned deletes_before=proxy_deletes;destroy(&c,Parent,42);assert(proxy_deletes==deletes_before&&ld32(Proxy+4)==999);
 std::memcpy(mem::ptr(Base),saved.data(),saved.size());std::memcpy(mem::ptr(Arena),metadata.data(),metadata.size());state_loaded();
 // Losing the arena magic still guards progress and retains an attached target.
 st32(Arena+0x50,0);unsigned moves_before=native_moves;c.r[3]=Rope;hooks.at(0x0216F7FC)(&c);assert(native_moves==moves_before&&native_events==0);method(3,&c,Proxy);assert(c.r[3]==0);unsigned corruption_deletes=rope_deletes;assert(corruption_deletes>0);
 unsigned orphan_delete_before=proxy_deletes;method(2,&c,Proxy);assert(proxy_deletes==orphan_delete_before);rope_live=false;method(2,&c,Proxy);assert(proxy_deletes==orphan_delete_before+1);method(2,&c,Proxy);assert(proxy_deletes==orphan_delete_before+1);method(3,&c,Proxy);assert(c.r[3]==1);method(1,&c,Proxy);rope_live=true;
 std::memcpy(mem::ptr(Base),saved.data(),saved.size());std::memcpy(mem::ptr(Arena),metadata.data(),metadata.size());state_loaded();
 // Positive boss/demo states remain forbidden even with ordinary parameters.
 st8(Rope+0x2B09,3);unsigned boss_moves=native_moves;c.r[3]=Rope;hooks.at(0x0216F7FC)(&c);assert(native_moves==boss_moves&&native_events==0&&rope_deletes==corruption_deletes+1);corruption_deletes=rope_deletes;
 std::memcpy(mem::ptr(Base),saved.data(),saved.size());std::memcpy(mem::ptr(Arena),metadata.data(),metadata.size());state_loaded();
 // Corrupted immutable parameters never enter the native special/demo branch.
 st32(Proxy+0xB0,0xF0);unsigned before=native_moves;c.r[3]=Rope;hooks.at(0x0216F7FC)(&c);assert(native_moves==before&&rope_deletes==corruption_deletes+1&&native_events==0);method(3,&c,Proxy);assert(c.r[3]==0);rope_live=false;method(3,&c,Proxy);assert(c.r[3]==1);method(1,&c,Proxy);proxy_live=false;
 // Native retail ropes remain untouched, including retail progress writes.
 st32(Rope+0x267C,Parent);c.r[3]=Rope;hooks.at(0x0216F7FC)(&c);assert(native_events==4);
 // Synchronous allocation/create uses the temporary allocation scope, never params.
 synchronous=true;owner=true;assert(update(&c,Parent,42,fp,{&target,1})&&spawns==2&&ld32(Proxy+0xB0)==0x00FF0000);destroy(&c,Parent,42);method(1,&c,Proxy);proxy_live=false;
 // A genuine pending request is retained; a failed native creation is reclaimed.
 synchronous=false;assert(update(&c,Parent,42,fp,{&target,1})&&spawns==3);pending_lookup=true;proxy_live=false;assert(update(&c,Parent,42,fp,{&target,1})&&spawns==3);pending_lookup=false;assert(update(&c,Parent,42,fp,{&target,1})&&spawns==4);method(0,&c,Proxy);assert(c.r[3]==4);destroy(&c,Parent,42);method(1,&c,Proxy);proxy_live=false;
 target.position.x=INFINITY;assert(!update(&c,Parent,42,fp,{&target,1})&&spawns==4);
}
