#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
// SPDX-License-Identifier: MPL-2.0
#include "mods/story_world_visuals.h"
#include "mods/story_chart.h"
#include "runtime.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {
using namespace story_world_visuals;
constexpr uint32_t Arena=0x60000000,Base=0x20000000;
struct Process {uint32_t token=0,actor=0;int room=0;bool pending=true,deleting=false;};
std::map<uint32_t,Process> processes;
uint32_t nextpid=10,layer=0x12345678,spawns=0,deletes=0;bool enabled=true,deny_owns=false,throw_create=false;
uint32_t scene(int room){return Base+uint32_t(room)*0x1000;}
void map(uint32_t a,uint32_t n){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(a),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(a),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
uint32_t invoke(Cpu*,uint32_t fn,std::initializer_list<uint32_t> args){const std::vector<uint32_t>a(args);switch(fn){
case 0x025DC80C:return scene(int(a.at(0)));
case 0x025DED64:return layer;
case 0x025DEAB4:layer=a.at(0);return 0;
case 0x025D5834:{if(throw_create)throw std::runtime_error("mock create failure");assert(a.at(0)==0x7FFE&&layer==scene(int(a.at(3)))+0xC0);const uint32_t pid=nextpid++;processes[pid]={a.at(1),Base+0x80000+pid*0x1000,int(a.at(3)),true,false};++spawns;return pid;}
case 0x025D54C4:{const auto it=processes.find(a.at(0));st32(a.at(1),it==processes.end()||it->second.pending?0:it->second.actor);return it!=processes.end();}
case 0x025D57E0:for(auto&[pid,p]:processes)if(p.actor==a.at(0)){assert(!p.pending);p.deleting=true;++deletes;return 1;}assert(!"delete foreign process");return 0;
default:assert(!"unexpected native function");return 0;}}
void finish(uint32_t pid){auto&p=processes.at(pid);p.pending=false;st32(p.actor+4,pid);st16(p.actor+0xE,0x7FFE);st32(p.actor+0xB0,p.token);}
void erased(){std::erase_if(processes,[](const auto&p){return p.second.deleting;});}
Spec spec(Role role,uint32_t token){Spec s;s.mod="mod";s.id="island";s.role=role;s.token=token;s.home_room=12;s.position={75000,0,-201500};s.identity[0]=uint8_t(token);return s;}
}
namespace story_chart {SquarePlace square_place(float x,float z){const int c=int(std::lround(x/kSquare)),r=int(std::lround(z/kSquare));return {c,r,x-c*kSquare,z-r*kSquare};}}
int main(){using namespace story_world_visuals;map(Arena,kStorageBytes);map(Base,0x200000);map(0x10470000,0x20000);for(int i=1;i<=49;++i)st32(0x1047E8F0+uint32_t(i)*0x22C,uint32_t(i));
 Config c{invoke,Arena,[](const auto&,const auto&){return enabled;},[](uint32_t a,uint32_t token){return !deny_owns&&ld32(a+0xB0)==token;}};assert(configure(c));std::string why;const Spec initial[]={spec(Role::Distant,1),spec(Role::Shoreline,2)};assert(replace("mod",initial,why));Cpu cpu{};cpu.r[3]=456;
 // A native create exception restores the caller's layer without publishing a PID.
 throw_create=true;bool threw=false;try{service(&cpu,"sea",11);}catch(const std::runtime_error&){threw=true;}assert(threw&&layer==0x12345678&&spawns==0&&processes.empty()&&ld32(Arena+0x100)==0);throw_create=false;
 // Distant representation appears only in neighboring squares, on the current room's layer.
 service(&cpu,"sea",11);assert(spawns==1&&layer==0x12345678&&cpu.r[3]==456);const uint32_t first=nextpid-1;service(&cpu,"sea",11);assert(spawns==1);finish(first);service(&cpu,"sea",11);assert(deletes==0);
 const std::vector<uint8_t> snapshot(mem::ptr(Arena),mem::ptr(Arena)+kStorageBytes);const auto saved_processes=processes;
 // Room change while a native deletion is queued cannot duplicate that key.
 service(&cpu,"sea",12);assert(deletes==1&&processes.at(first).deleting&&spawns==2); // independent home-only shoreline
 const uint32_t shore=nextpid-1;service(&cpu,"sea",11);assert(spawns==2); // returning rapidly waits for the old distant PID
 finish(shore);service(&cpu,"sea",11);assert(deletes==2);erased();service(&cpu,"sea",11);assert(spawns==3);uint32_t replacement=nextpid-1;finish(replacement);
 // Open ocean has native room0 while geography still identifies square11.
 service(&cpu,"sea",0,11);assert(processes.at(replacement).deleting);erased();const unsigned ocean_before=spawns;service(&cpu,"sea",0,11);assert(spawns==ocean_before+1&&processes.at(nextpid-1).room==0&&processes.at(nextpid-1).token==1);const auto ocean=nextpid-1;finish(ocean);service(&cpu,"sea",0,11);assert(spawns==ocean_before+1);
 // Crossing the geographic coast/home boundary in the SAME native room swaps roles.
 service(&cpu,"sea",0,12);assert(processes.at(ocean).deleting);const auto ocean_shore=nextpid-1;assert(processes.at(ocean_shore).token==2&&processes.at(ocean_shore).room==0);finish(ocean_shore);erased();service(&cpu,"sea",0,11);assert(processes.at(ocean_shore).deleting);erased();service(&cpu,"sea",0,11);assert(processes.at(nextpid-1).token==1);
 // Full restore adopts the saved PID through guest rows; it does not spawn a duplicate.
 std::memcpy(mem::ptr(Arena),snapshot.data(),snapshot.size());processes=saved_processes;const uint32_t before_restore=spawns;state_loaded();service(&cpu,"sea",11);assert(spawns==before_restore&&processes.size()==1);
 // Exact native profile, PID and token are required before deleting a restored row.
 const uint32_t a=processes.at(first).actor;st16(a+0xE,0xFA);enabled=false;unsigned before_delete=deletes;service(&cpu,"sea",11);assert(deletes==before_delete);st16(a+0xE,0x7FFE);st32(a+4,999);service(&cpu,"sea",11);assert(deletes==before_delete);st32(a+4,first);st32(a+0xB0,999);service(&cpu,"sea",11);assert(deletes==before_delete);st32(a+0xB0,1);deny_owns=true;service(&cpu,"sea",11);assert(deletes==before_delete);deny_owns=false;service(&cpu,"sea",11);assert(deletes==before_delete+1);erased();service(&cpu,"sea",11);enabled=true;
 // A draining room or parent scene never receives children.
 st8(scene(11)+0x5C,1);service(&cpu,"sea",11);assert(spawns==before_restore);st8(scene(11)+0x5C,0);const uint32_t parent_layer=Base+0x60000,parent_scene=Base+0x62000;st32(scene(11)+0x2C,parent_layer);st32(parent_layer+0x18,parent_scene);st8(parent_scene+0x5C,1);service(&cpu,"sea",11);assert(spawns==before_restore);st8(parent_scene+0x5C,0);service(&cpu,"sea",11);assert(spawns==before_restore+1);replacement=nextpid-1;
 // Pending creation survives a declaration replacement until its old PID disappears.
 auto updated=initial[0];updated.token=3;updated.identity[0]=3;const Spec changed[]={updated,initial[1]};assert(replace("mod",changed,why));service(&cpu,"sea",11);assert(spawns==before_restore+1);finish(replacement);service(&cpu,"sea",11);assert(processes.at(replacement).deleting);erased();service(&cpu,"sea",11);assert(spawns==before_restore+2);finish(nextpid-1);
 // Eligibility removal retires the exact owned visual, with no native progress writes.
 enabled=false;service(&cpu,"sea",11);erased();service(&cpu,"sea",11);enabled=true;
 // Portable recreation starts with fresh guest rows/native processes and creates once.
 processes.clear();std::memset(mem::ptr(Arena+0x100),0,kStorageBytes-0x100);state_loaded();const unsigned portable_before=spawns;service(&cpu,"sea",11);service(&cpu,"sea",11);assert(spawns==portable_before+1);finish(nextpid-1);
 // Receipt-history preflight is identical to commit and has no side effects.
 // The distant key already has identities1 and3. Reach its bounded128 receipts.
 Spec history=updated;
 for(unsigned revision=4;revision<=128;++revision){history.identity={};history.identity[0]=uint8_t(revision);const Spec batch[]={history,initial[1]};assert(replace("mod",batch,why,false));assert(replace("mod",batch,why));}
 // Two alternative previews at the last free receipt prove successful preflight
 // does not itself consume that receipt or change the desired declaration.
 history.identity[0]=129;const Spec last_receipt[]={history,initial[1]};auto alternative=history;alternative.identity[0]=130;const Spec alternative_batch[]={alternative,initial[1]};assert(replace("mod",last_receipt,why,false)&&replace("mod",alternative_batch,why,false));assert(replace("mod",last_receipt,why));
 const Spec current_history[]={history,initial[1]};auto overflow=history;overflow.identity={};overflow.identity[0]=130;const Spec overflow_batch[]={overflow,initial[1]};assert(!replace("mod",overflow_batch,why,false));assert(!replace("mod",overflow_batch,why));assert(replace("mod",current_history,why,false)&&replace("mod",current_history,why));
 // Tombstoned keys remain stable, and failed capacity admission changes nothing.
 assert(replace("mod",{},why));service(&cpu,"sea",11);erased();service(&cpu,"sea",11);
 std::vector<Spec> many;for(unsigned i=0;i<127;++i){auto s=spec(Role::Distant,100+i);s.mod="bulk";s.id="v"+std::to_string(i);many.push_back(s);}assert(!replace("bulk",many,why));const unsigned empty_before=spawns;service(&cpu,"sea",11);assert(spawns==empty_before);many.pop_back();assert(replace("bulk",many,why));auto extra=spec(Role::Distant,999);extra.mod="extra";assert(!replace("extra",{&extra,1},why));auto invalid=many;invalid[0].position.x=NAN;assert(!replace("bulk",invalid,why));service(&cpu,"sea",11);assert(spawns==empty_before+kMaxActive);service(&cpu,"sea",11);assert(spawns==empty_before+kMaxActive);
}
