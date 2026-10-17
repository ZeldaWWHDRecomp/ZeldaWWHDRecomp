// SPDX-License-Identifier: MPL-2.0
#include "mods/story_particles.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cassert>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
// Nonidentity code/data mapping catches missing and double translations.
extern "C" {
extern const GuestStep g_guest_code_steps[]={{0,0},{0x02000000,0x100000},{0x03000000,0}};
extern const unsigned g_guest_code_step_count=3;
extern const uint32_t g_guest_code_lo=0x02000000,g_guest_code_hi=0x03000000;
extern const GuestStep g_guest_data_steps[]={{0,0},{0x10000000,0x10000},{0x10500000,0}};
extern const unsigned g_guest_data_step_count=3;
extern const uint32_t g_guest_data_lo=0x10000000,g_guest_data_hi=0x10500000;
}
std::string last_log;
void log_msg(const char*fmt,...){char buffer[512];va_list args;va_start(args,fmt);vsnprintf(buffer,sizeof buffer,fmt,args);va_end(args);last_log=buffer;}
namespace P=story_particles;
namespace {
constexpr uint32_t Play=0x20000000,Control=Play+0x6000,Manager=Play+0xC000,Wrapper=Play+0xD000,Table=Play+0xD100,Entries=Play+0xD200,Resource=Play+0xD300,Emitter=Play+0xE000,Scratch=0x60030000,SP=0x60020000;
unsigned get_calls=0,set_calls=0;bool fault=false,reenter=false,bad_metadata=false;uint32_t returned=Emitter;
P::Burst request{P::Effect::DoorDebris,{100,1040,-201500},1.5f,170,11};
void map(uint32_t p,uint32_t n){
#ifdef _WIN32
 assert(VirtualAlloc(mem::ptr(p),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
 assert(mmap(mem::ptr(p),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
uint32_t call(Cpu*c,uint32_t fn,std::initializer_list<uint32_t> args){
 if(fn==GC(0x025200D4)){assert(args.size()==0);++get_calls;c->r[1]=0;c->pc=123;c->f[1].ps0=999;return Play;}
 assert(fn==GC(0x025A847C));++set_calls;assert(c->r[1]==SP);
 assert((std::vector<uint32_t>(args)==std::vector<uint32_t>{Control,0,request.effect==P::Effect::BeaconSparkle?0x8152u:request.effect==P::Effect::AuctionSparkle?0x8153u:0x81B2u,Scratch,0,Scratch+12,request.alpha,0}));
 assert(ld32(SP-0x40+8)==uint32_t(request.room));for(unsigned i=12;i<=20;i+=4)assert(ld32(SP-0x40+i)==0);
 const float expected[]={request.position.x,request.position.y,request.position.z,request.scale,request.scale,request.scale};for(unsigned i=0;i<6;++i)assert(u32_as_f32(ld32(Scratch+i*4))==expected[i]);
 if(reenter)assert(!P::burst(c,request));
 // Simulate guest_call's outgoing native frame, and arbitrary returned CPU.
 st32(SP-0x40,SP);c->r[1]=17;c->r[7]=234;c->lr=456;c->ctr=789;c->f[3].ps1=42;c->cr[3]=1;
 if(fault)throw std::runtime_error("native fault");
 st16(Emitter+0x260,bad_metadata?0x2022:request.effect==P::Effect::BeaconSparkle?0x8152:request.effect==P::Effect::AuctionSparkle?0x8153:0x81B2);st8(Emitter+0x262,0);st8(Emitter+0x263,1);st32(Emitter+0x1E4,0);return returned;
}
void ready(){st32(Play+0x5AB0,Control);st32(GD(0x1047B2D4),Manager);st32(0x1047B2D4,0);st32(Manager+0x30,0);st32(Manager+0x34,Wrapper);st32(Wrapper+4,Table);st32(Table,1);st32(Table+8,Entries);st32(Entries,Resource);st16(Resource+6,0x81B2);}
void unchanged(Cpu&c,bool expected){Cpu before=c;std::array<uint8_t,64> frame;std::array<uint8_t,P::kScratchBytes> scratch;std::memcpy(frame.data(),mem::ptr(SP-64),64);std::memcpy(scratch.data(),mem::ptr(Scratch),scratch.size());assert(P::burst(&c,request)==expected);assert(std::memcmp(&before,&c,sizeof c)==0);assert(std::memcmp(frame.data(),mem::ptr(SP-64),64)==0);assert(std::memcmp(scratch.data(),mem::ptr(Scratch),scratch.size())==0);}
}
int main(int argc,char**argv){
#ifdef _WIN32
 _putenv_s("WWHD_PARTICLE_TRACE","1");
#else
 setenv("WWHD_PARTICLE_TRACE","1",1);
#endif
 map(Play,0x20000);map(0x10470000,0x30000);map(0x60010000,0x21000);ready();
 P::Effect effect=P::Effect::Dust;assert(P::from_name("door-debris",effect)&&effect==P::Effect::DoorDebris);assert(!P::from_name("dust",effect)&&!P::from_name("81B2",effect)&&!P::from_name("unknown",effect));assert(P::name(P::Effect::DoorDebris)=="door-debris"&&P::name(P::Effect::Dust).empty());
 assert(!P::configure(nullptr,Scratch,P::kScratchBytes));assert(!P::configure(call,0x68000000,P::kScratchBytes));assert(!P::configure(call,Scratch,P::kScratchBytes-1));assert(!P::configure(call,Scratch,P::kScratchBytes,P::kDustMask));
 const bool masked=argc>1&&std::string(argv[1])=="--masked";const uint32_t mask=masked?0:P::kAllEffectsMask;
 assert(P::configure(call,Scratch,P::kScratchBytes,mask));assert(P::configure(call,Scratch,P::kScratchBytes,mask));assert(!P::configure(call,Scratch+16,P::kScratchBytes,mask));assert(!P::admitted(P::Effect::Dust));
 Cpu c{};std::memset(&c,0x5A,sizeof c);c.r[1]=SP;std::memset(mem::ptr(SP-64),0xA5,64);std::memset(mem::ptr(Scratch),0xC3,P::kScratchBytes);
 if(masked){unchanged(c,false);assert(!P::burst_named(&c,"auction-sparkle",{request.position,request.room}));assert(get_calls==0&&set_calls==0);return 0;}
 assert(P::admitted(P::Effect::DoorDebris));reenter=true;unchanged(c,true);assert(set_calls==1);assert(last_log.find("reason=accepted")!=std::string::npos);reenter=false;
 const auto original=request;
 for(unsigned k=0;k<8;++k){request=original;switch(k){case 0:request.position.x=std::numeric_limits<float>::quiet_NaN();break;case 1:request.position.z=1000001;break;case 2:request.scale=0.01f;break;case 3:request.scale=std::numeric_limits<float>::infinity();break;case 4:request.alpha=256;break;case 5:request.room=64;break;case 6:request.effect=P::Effect::Dust;break;case 7:request.effect=P::Effect(9);break;}auto n=get_calls;unchanged(c,false);assert(get_calls==n);}
 request=original;
 for(unsigned k=0;k<7;++k){ready();switch(k){case 0:st32(Manager+0x34,0);break;case 1:st32(Wrapper+4,0);break;case 2:st32(Table,4097);break;case 3:st32(Table+8,0x4FFFFFFCu);st32(Table,2);break;case 4:st32(Entries,0x68000000);break;case 5:st16(Resource+6,0x2022);break;case 6:st32(Table,2);st32(Entries+4,Resource);break;}auto n=set_calls;unchanged(c,false);assert(set_calls==n);const char*reasons[]={"bank-wrapper","resource-table","resource-count","resource-entries","resource-entry","missing-id","duplicate-id"};assert(last_log.find(std::string("reason=")+reasons[k])!=std::string::npos);}
 ready();request.room=-1;unchanged(c,true);request=original;
 for(uint32_t ret:{0u,0xFFFFFFFFu,0x68000000u}){returned=ret;unchanged(c,false);}returned=Emitter;bad_metadata=true;unchanged(c,false);bad_metadata=false;
 fault=true;unchanged(c,false);fault=false;unchanged(c,true); // exception clears reentrancy guard
 Cpu bad=c;bad.r[1]=SP+4;auto n=get_calls;assert(!P::burst(&bad,request)&&get_calls==n);bad=c;bad.r[1]=0x68001000;assert(!P::burst(&bad,request)&&get_calls==n);bad=c;bad.r[1]=Scratch+64;assert(!P::burst(&bad,request)&&get_calls==n);
 assert(P::from_name("auction-sparkle",effect)&&effect==P::Effect::AuctionSparkle);assert(P::name(effect)=="auction-sparkle");
 assert(P::admitted(effect));request.effect=effect;st16(Resource+6,0x8153);unchanged(c,true);
 request.effect=P::Effect::DoorDebris;auto before_missing=set_calls;unchanged(c,false);assert(set_calls==before_missing); // loaded8153 does not authorize missing81B2
 assert(P::from_name("beacon-sparkle",effect)&&effect==P::Effect::BeaconSparkle);assert(P::name(effect)=="beacon-sparkle");request.effect=effect;st16(Resource+6,0x8152);unchanged(c,true);
 request.effect=P::Effect::AuctionSparkle;auto before_beacon=set_calls;unchanged(c,false);assert(set_calls==before_beacon);
 request=original;ready();
 P::Placement placement{request.position,request.room};P::Options options{request.scale,request.alpha};
 Cpu named_before=c;const auto named_count=set_calls;assert(P::burst_named(&c,"door-debris",placement,options));assert(set_calls==named_count+1&&std::memcmp(&c,&named_before,sizeof c)==0);
 for(const auto text:{"dust","unknown","81B2",""}){auto before=set_calls;assert(!P::burst_named(&c,text,placement,options));assert(set_calls==before);}
 auto bad_placement=placement;bad_placement.position.y=std::numeric_limits<float>::quiet_NaN();auto before_named=get_calls;assert(!P::burst_named(&c,"door-debris",bad_placement,options)&&get_calls==before_named);
 options.alpha=256;assert(!P::burst_named(&c,"door-debris",placement,options)&&get_calls==before_named);
 assert(!P::burst(nullptr,request));assert(ld32(Manager+0x34)==Wrapper&&ld32(Wrapper+4)==Table&&ld16(Resource+6)==0x81B2);
}
