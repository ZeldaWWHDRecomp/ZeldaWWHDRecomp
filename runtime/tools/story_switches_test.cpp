// SPDX-License-Identifier: MPL-2.0
// Native hook boundaries with synthetic memory. No native physics/render proof.
#include "mods/story_switches.h"
#include "mods/native_actor_profiles.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <map>
#include <array>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {
constexpr uint32_t Arena=0x20000000,Switch=Arena+0x1000,Second=Arena+0x2000,Door=Arena+0x3000,Retail=Arena+0x4000,Third=Arena+0x5000,Bg=Arena+0x8000;
std::map<uint32_t,void(*)(Cpu*)> hooks;std::map<uint32_t,bool> flags;std::map<uint32_t,uint32_t> retired;
unsigned native_calls=0,commits=0,orders=0,draws=0,rides=0,deletes=0,moves=0,sounds=0;bool fail_write=false,invalid_geometry=false;unsigned copies=0;std::array<uint8_t,96> immutable_source{};
void map(uint32_t a,uint32_t n){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(a),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(a),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void original(Cpu*c,uint32_t f){++native_calls;const uint32_t a=c->r[3];
 if(f==0x023975B0){const uint32_t slot=(a-Switch)/0x1000,bg=Bg+slot*0x100,current=Arena+0xA000+slot*0x1000,previous=current+0x200,source=current+0x400,dzb=current+0x600;
 st32(a+0x3BC,bg);st8(bg+0x6C,3);st32(bg+0x90,current);st32(bg+0xC0,previous);st32(bg+0x94,dzb);st32(dzb,8);st32(dzb+4,invalid_geometry?current:source);stf32(a+0x3F0,100);
 for(unsigned i=0;i<8;++i)for(unsigned j=0;j<3;++j){stf32(current+i*12+j*4,j==1?100.f:float(i));stf32(source+i*12+j*4,float(i*3+j));st32(previous+i*12+j*4,0xFFFFFFFFu);}if(a==Second)std::memcpy(immutable_source.data(),mem::ptr(source),immutable_source.size());st32(a+0x46C,0);st32(a+0x470,0);st32(a+0x474,0);st16(a+0x47A,0xFFFF);stf32(a+0x490,1);c->r[3]=4;}
 else if(f==0x02058590){st32(a+0x3B8,Bg+0x4000);st8(a+0x3BC,0);c->r[3]=4;}
 else if(f==0x023988E8){assert(ldf32(a+0x490)<=0);st8(a+0x48B,0);st32(a+0x470,2);}
 else if(f==0x02398014){const uint32_t mode=ld32(a+0x470);Cpu nested=*c;nested.r[3]=a;hooks.at(mode==0?0x02398608:mode==2?0x02398AB4:0x023988E8)(&nested);c->r[3]=1;}
 else if(f==0x0205834C){Cpu nested=*c;nested.r[3]=a;const unsigned action=ld8(a+0x3BC);if(action==1)hooks.at(0x02058144)(&nested);else if(action==3)hooks.at(0x02058230)(&nested);else if(action==4){int angle=int16_t(ld16(a+0x3BE))+0x400;st16(a+0x3BE,angle>=0x4000?0x4000:angle);if(angle>=0x4000)st8(a+0x3BC,3);}else if(action==2){int angle=int16_t(ld16(a+0x3BE))-0x400;st16(a+0x3BE,angle<=0?0:angle);if(angle<=0)st8(a+0x3BC,1);}c->r[3]=1;}
 else if(f==0x02398504||f==0x023985AC){++orders;st32(a+0x474,99);}
 else if(f==0x02398240||f==0x020582F0){++draws;c->r[3]=1;}
 else if(f==0x0239705C)++rides;
 else if(f==0x02397AD0||f==0x020583D4){++deletes;c->r[3]=1;}
 else if(f==0x02398608||f==0x02398AB4||f==0x023974AC){}
 else assert(false);
}
uint32_t invoke(Cpu*c,uint32_t f,std::initializer_list<uint32_t>args){c->r[4]=0xBAD;c->f[2].ps0=999;const uint32_t a=*args.begin();if(f==0x02397584){st32(a+0x470,2);st8(a+0x48B,0);return 0;}if(f==0x02398348){st32(a+0x470,1);return 0;}if(f==0x02057F50)return 0;if(f==0x024F5B08){Cpu early{};early.r[3]=a;early.r[4]=Second;hooks.at(0x0239705C)(&early);assert(rides==0);++copies;const uint32_t bytes=ld32(ld32(a+0x94))*12;std::memcpy(mem::ptr(ld32(a+0xC0)),mem::ptr(ld32(a+0x90)),bytes);return 0;}
 if(f==0x024F43DC){++moves;if(ld32(a+0x90)){const uint32_t bytes=ld32(ld32(a+0x94))*12;assert(std::memcmp(mem::ptr(ld32(a+0x90)),mem::ptr(ld32(a+0xC0)),bytes)==0);}return 0;}if(f==0x02520540)return 7;if(f==0x025E1A40){++sounds;return 0;}assert(false);return 0;}
void actor(uint32_t a,uint32_t pid,bool door=false){std::memset(mem::ptr(a),0,0x500);st32(a+4,pid);st16(a+0xE,door?story_switches::kDoorProfile:story_switches::kSwitchProfile);st32(a+0xB0,story_switches::parameters(door?story_switches::Kind::Door:story_switches::Kind::Switch));st8(a+0x2DD,0xFF);}
void run(uint32_t f,uint32_t a){Cpu c{};c.r[3]=a;c.r[4]=0x12345678;c.f[2].ps0=42;hooks.at(f)(&c);assert(c.r[4]==0x12345678&&c.f[2].ps0==42);}
}
int main(){using namespace story_switches;
 static_assert(parameters(Kind::Switch)==0xFFFF&&parameters(Kind::Door)==0xFF);assert(valid_door_state(4,0x4000)&&!valid_door_state(5,0)&&!valid_door_state(0,-1)&&!valid_door_state(0,0x4001));
 map(Arena,0x20000);map(0x60000000,0x10000);map(0x101C0000,0x10000);map(0x10180000,0x20000);map(0x10030000,0x10000);for(unsigned i=0;i<4;++i)st8(0x100303E8+i,uint8_t(i*2));
 st32(0x101CD034+0x10,kSwitchClassBytes);st32(0x1018FFE8+0x10,kDoorClassBytes);
 std::string why;const auto checkpoint_before=checkpoint();const uint32_t stale=declare_switch("m","provisional",why);rollback(checkpoint_before);const uint32_t sw=declare_switch("m","switch",why);assert(sw&&sw!=stale&&!available(stale));const uint32_t second=declare_switch("m","second",why);assert(!declare_door("foreign","door",sw,why));const uint32_t third=declare_switch("m","invalid.geometry",why);flags[third]=true;const uint32_t door=declare_door("m","door",sw,why);assert(door&&!declare_switch("m","switch",why));flags[sw]=false;flags[second]=true;
 configure(original,invoke,0x60001000,0x60001030,[](uint32_t token,bool&value){auto it=flags.find(token);if(it==flags.end())return false;value=it->second;return true;},[](uint32_t token,bool value){++commits;if(fail_write)return false;flags[token]=value;return true;},[](uint32_t token,uint32_t pid){retired[token]=pid;});
 const auto owner=[](uint32_t){return std::string();};const auto record=[](uint32_t f,void(*fn)(Cpu*)){hooks[f]=fn;};
 // A hook another feature owns refuses with its address and owner, before anything is registered.
 {std::string reason;assert(!install([](uint32_t f){return f==0x02058230?std::string("quick-doors"):std::string();},record,&reason));assert(reason=="hook 02058230 owned by quick-doors"&&hooks.empty());}
 // The --editor case: every enemy kind, the props, the mod actor, world objects and grapple targets
 // registered their profiles first; the switch and door profiles still find room (all reserved IDs fit).
 for(uint16_t id=0x7FD0;id<=0x7FFE;++id){if(id==kSwitchProfile||id==kDoorProfile)continue;const uint32_t at=0x60004000+uint32_t(id-0x7FD0)*0x40;st16(at+8,id);std::string reason;assert(native_actor_profiles::add(id,at,nullptr,&reason)&&reason.empty());}
 {std::string reason;assert(install(owner,record,&reason)&&reason.empty());}assert(!declare_switch("m","late",why));
 actor(Switch,10);actor(Second,11);actor(Door,12,true);bind(sw,10);bind(second,11);bind(door,12);run(0x023975B0,Switch);run(0x023975B0,Second);run(0x02058590,Door);assert(ld32(Second+0x470)==2&&ldf32(Second+0x490)==0&&ldf32(Second+0x3FC)==1);assert(ld8(Door+0x3BC)==1&&ld16(Door+0x3BE)==0&&moves==2);
 const uint32_t bg=ld32(Second+0x3BC),current=ld32(bg+0x90),source=ld32(ld32(bg+0x94)+4);assert(copies==1&&std::memcmp(immutable_source.data(),mem::ptr(source),immutable_source.size())==0);for(unsigned i=0;i<8;++i)assert(ldf32(current+i*12+4)==(i%2==0?64.5f:100.f));
 actor(Third,13);bind(third,13);invalid_geometry=true;const auto moves_before=moves;run(0x023975B0,Third);assert(!available(third)&&moves==moves_before&&ldf32(ld32(ld32(Third+0x3BC)+0x90)+4)==100.f);invalid_geometry=false;
 // Real settled-state signal is required: mere creation/draw never commits.
 run(0x02398240,Switch);assert(commits==0&&draws==1);st32(Switch+0x470,1);stf32(Switch+0x490,0);st8(Switch+0x48B,1);run(0x023988E8,Switch);assert(flags[sw]&&commits==1);run(0x02398AB4,Switch);assert(commits==1);run(0x0205834C,Door);assert(ld8(Door+0x3BC)==4&&sounds==1);for(unsigned i=0;i<16;++i)run(0x0205834C,Door);assert(ld8(Door+0x3BC)==3&&ld16(Door+0x3BE)==0x4000);flags[sw]=false;run(0x0205834C,Door);assert(ld8(Door+0x3BC)==2);
 run(0x02398504,Switch);run(0x023985AC,Switch);assert(orders==0&&ld32(Switch+0x474)==0);
 // Callback admitted by exact background, actor identity and PID, outside execute scope.
 Cpu ride{};ride.r[3]=ld32(Switch+0x3BC);ride.r[4]=Switch;hooks.at(0x0239705C)(&ride);assert(rides==1);ride.r[4]=Second;hooks.at(0x0239705C)(&ride);assert(rides==1);
 // Malformed restored demo quarantines before native dispatch/global event calls.
 state_loaded();bind(sw,10,true);bind(second,11,true);bind(door,12,true);st32(Switch+0x474,1);const auto calls=native_calls;run(0x02398014,Switch);assert(native_calls==calls&&!available(sw)&&orders==0);retry(sw);st32(Switch+0x474,0);run(0x02398014,Switch);assert(available(sw));
 // Store failure publishes neither flag nor a native completion; no duplicate retry this epoch.
 flags[sw]=false;fail_write=true;st32(Switch+0x470,1);stf32(Switch+0x490,0);st8(Switch+0x48B,1);const auto before=native_calls;run(0x023988E8,Switch);assert(!flags[sw]&&!available(sw)&&native_calls==before);const auto attempts=commits;run(0x023988E8,Switch);assert(commits==attempts);
 // Even a quarantined live copy retires before native destruction; stale callbacks stay denied.
 run(0x02397AD0,Switch);assert(retired[sw]==10&&deletes==1);const auto old=native_calls;run(0x023988E8,Switch);assert(native_calls==old);state_loaded();bind(sw,10,true);st32(Switch+4,999);run(0x02397AD0,Switch);assert(retired[sw]==10);st32(Switch+4,10);
 // Ordinary retail callbacks remain delegated, including their native event behavior.
 actor(Retail,20);st16(Retail+0xE,0x1B);run(0x02398504,Retail);assert(orders==1&&ld32(Retail+0x474)==99);
 // Door malformed action cannot index native action table.
 bind(door,12,true);st8(Door+0x3BC,255);const auto old_calls=native_calls;run(0x0205834C,Door);assert(native_calls==old_calls&&!available(door));
}
