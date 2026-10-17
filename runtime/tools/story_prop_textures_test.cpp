// SPDX-License-Identifier: MPL-2.0
#include "mods/story_prop_textures.h"
#include "mods/story_actor_textures.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace P=story_prop_textures;namespace T=story_actor_textures;
namespace {constexpr uint32_t A=0x20000000,M=A+0x1000,Fmdl=A+0x2000,Mat=A+0x3000,R=A+0x4000,Names=A+0x5000,Refs=A+0x6000,Ftex=A+0x7000,Toon=A+0x8000,Strings=A+0x9000;bool owner=true,fail_bind=false;unsigned bindings=0;T::Binding captured{};
void rel(uint32_t at,uint32_t target){st32(at,target-at);}void str(uint32_t at,const char*s){std::memcpy(mem::ptr(at),s,std::strlen(s)+1);}
bool verify(const P::Context&c){return owner&&c.actor==A&&c.pid==12&&c.epoch==3&&c.generation==8&&c.private_begin==M&&c.private_end==A+0x7000;}
void fixture(uint32_t type,const char*model,const char*material,const char*texture,unsigned count){std::memset(mem::ptr(A),0,0x10000);st32(A+4,12);st16(A+0xE,0x7FFD);st32(A+0xB0,(type<<24)|0x007FFF3F);st32(A+0xB4,0x10040BFC);st32(A+0x790,type);st8(A+0x2DD,0xFF);st32(A+0x3B4,M);st32(M+0x14,Fmdl);st16(M+0x2A,count);st32(M+0x34,Mat);st32(Fmdl,0x464D444C);str(Strings,model);rel(Fmdl+4,Strings);st32(Mat,R);st32(Mat+0x2C,Refs);st32(R,0x464D4154);str(Strings+64,material);rel(R+4,Strings+64);st8(R+0x11,2);rel(R+0x28,Names);str(Strings+128,texture);str(Strings+192,"ZAtoon");rel(Names,Strings+128);rel(Names+8,Strings+192);rel(Names+4,Ftex);rel(Names+12,Toon);st32(Ftex,0x46544558);st32(Toon,0x46544558);st32(Refs,Ftex);st32(Refs+4,Toon);}
}
namespace story_actor_textures {bool cached(Cpu*,uint32_t token,Texture&out){if(token!=1)return false;out.ftex=0x60000000;return true;}bool cached_ready(uint32_t token,Texture&out){return cached(nullptr,token,out);}bool bind(const Binding&b,const Texture&t){if(fail_bind)return false;++bindings;captured=b;st32(Refs,t.ftex);return true;}}
int main(){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(A),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(A),0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
assert(P::configure(verify));Cpu c{};P::Context ctx{A,12,3,8,0,M,A+0x7000};
for(auto type:{0u,3u,15u}){ctx.type=type;const char*model=type==0?"obm_kotubo1":type==3?"ktaru_01":"kkiba_00";const char*material=type==0?"mkotubo1_v":type==3?"lambert48":"lambert49";const char*texture=type==0?"Txm_tsubo02":type==3?"k_taru04":"k_taru02";fixture(type,model,material,texture,type==3?2:1);assert(P::apply(&c,ctx,4,{{texture,1}}));assert(captured.slot==0&&captured.resource==R&&captured.original_ftex==Ftex&&ld32(Refs+4)==Toon);
// Full-state rebind after native-managed toon replacement preserves live slot1.
st32(Refs+4,0x24001000);assert(P::apply(&c,ctx,4,{{texture,1}})&&ld32(Refs+4)==0x24001000);str(Strings+192,"wrong_source");assert(!P::apply(&c,ctx,4,{{texture,1}}));str(Strings+192,"ZAtoon");st32(Toon,0);assert(!P::apply(&c,ctx,4,{{texture,1}}));st32(Toon,0x46544558);
assert(!P::apply(&c,ctx,1,{{texture,1}}));assert(!P::apply(&c,ctx,4,{{"ZAtoon",1}}));st32(A+4,13);assert(!P::apply(&c,ctx,4,{{texture,1}}));st32(A+4,12);owner=false;assert(!P::apply(&c,ctx,4,{{texture,1}}));owner=true;st32(A+0x3B8,123);assert(!P::apply(&c,ctx,4,{{texture,1}}));st32(A+0x3B8,0);str(Strings+64,"foreign_material");assert(!P::apply(&c,ctx,4,{{texture,1}}));}
assert(bindings==6&&!P::supported(7,"k_taru04"));
}
