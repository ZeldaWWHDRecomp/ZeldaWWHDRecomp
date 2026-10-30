// Real host adapter on synthetic guest objects; no game assets or user saves.
#include "mods/map_hud.h"
#include "mods/map_hud_rules.h"
#include "mods/hud_visibility.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <map>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace mods {GameUi gates;GameUi game_ui_now(){return gates;}}
using namespace guestmods::maphud;
namespace {
constexpr uint32_t U=0x20000000,H=U+0x1000,C=U+0x2000,M=U+0x3000,K=U+0x4000,P=U+0x5000,E=U+0x6000,S=U+0x7000;
std::map<uint32_t,Hook> hooks;
int drawn=0,native_choice=-1;bool owned=true,opened=true,decoded=true;
void mapping(uint32_t at,uint32_t n){
#ifdef _WIN32
 assert(VirtualAlloc(mem::ptr(at),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
 assert(mmap(mem::ptr(at),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void original(Cpu* c,uint32_t fn){
 switch(fn){
 case 0x02687A64:c->r[3]=E;break;
 case 0x0268A570:c->r[3]=owned;break;
 case 0x0268A5DC:c->r[3]=opened;break;
 case 0x025B8308:c->r[3]=decoded;break;
 case 0x026F8B54:drawn++;break;
 case 0x0269B238:if(native_choice>=0){st32(M+0xB0,ld32(M+0xAC));st32(M+0xAC,native_choice);native_choice=-1;}break;
 case 0x026986C4:case 0x0264C4BC:break;
 default:assert(false);
 }
}
void pane(uint32_t p,float x){st32(p+8,U+0xD00);st32(U+0xD00+0x94,0x02874D54);st8(p+0x44,1);st8(p+0x45,255);st8(p+0x46,255);st8(p+0x47,4);st32(p+0x3C,f32_as_u32(100));st32(p+0x40,f32_as_u32(100));st32(p+0x48,f32_as_u32(1));st32(p+0x5C,f32_as_u32(1));st32(p+0x54,f32_as_u32(x));st32(p+0x64,f32_as_u32(-200));}
}
int main(){
 mapping(0x101F0000,0x10000);mapping(0x10460000,0x30000);mapping(U,0x10000);
 st32(0x101F8344,U);st32(U+0x178,H);st32(H+0x48,13);st32(H+0x4C,U+0x800);st32(U+0x810,C);
 st32(C+4,0x100EE998);st32(C+0x44,U+0x900);st32(C+0x50,P);st32(C+0x54,P+0x200);
 st32(U+0x218,K);st32(K+0x28,M);st32(M+4,0x100FB8B8);st32(M+0x44,U+0xA00);st32(M+0xAC,3);
 st32(0x1046F0B0+0x5CD8,0x10000);st8(0x1047B093,1);memcpy(mem::ptr(0x1046F0B0+0x5134),"sea",4);
 st32(0x101F84DC,S);st8(E,1);st8(E+1,1);st8(E+3,0);pane(P,-400);pane(P+0x200,-300);
 st32(U+0x900+4,U+0xC00);st32(U+0xC00+0xC,P+0x400);
 st8(P+0x400+0x44,1);st8(P+0x400+0x45,255);st32(P+0x400+0x14,P);
 st32(P,P+0x200);st32(P+0x200,P+0x400+0x14);st32(P+0xC,P+0x400);st32(P+0x200+0xC,P+0x400);
 install(original,[](uint32_t,bool){return true;},[](uint32_t a,Hook h){hooks[a]=h;});assert(capability()==1);
 Cpu c{};assert(select(&c,1,4,false)==Phase);begin_logic(&c,1./30);
 assert(select(&c,1,4,true)==Phase);assert(compasses(&c,1,1,true)==Phase);
 assert(compasses(&c,1,1,false)==1);Rect r{};assert(rectangle(1,r)==1&&r.x==190&&r.y==510&&r.w==200&&r.h==100);
 finish_logic();c.r[3]=C;if(!hide_pane(P+0x400))drawn++;assert(drawn==0);
 begin_logic(&c,1./30);finish_logic();c.r[3]=C;if(!hide_pane(P+0x400))drawn++;assert(drawn==1);
 begin_logic(&c,1./30);assert(select(&c,1,4,false)==1);assert(cancel(&c,1,false)==1);finish_logic();
 c.r[3]=M;hooks.at(0x0269B238)(&c);assert(ld32(M+0xAC)==3);
 begin_logic(&c,1./30);owned=false;assert(select(&c,1,4,false)==Invalid);owned=true;opened=false;assert(select(&c,1,4,false)==Invalid);opened=true;decoded=false;assert(select(&c,1,4,false)==Invalid);decoded=true;
 assert(select(&c,1,4,false)==1);finish_logic();c.r[3]=M;hooks.at(0x0269B238)(&c);assert(ld32(M+0xAC)==4&&ld32(M+0xB0)==3&&ld32(M+0xB8)==1);
 // An active animation accepts a replacement request but cannot commit it yet.
 st32(M+0x90,2);begin_logic(&c,.01);assert(select(&c,1,5,false)==1);finish_logic();c.r[3]=M;hooks.at(0x0269B238)(&c);assert(ld32(M+0xAC)==4);
 begin_logic(&c,.01);assert(select(&c,1,6,false)==1);finish_logic();st32(M+0x90,0);c.r[3]=M;hooks.at(0x0269B238)(&c);assert(ld32(M+0xAC)==6);
 begin_logic(&c,.01);assert(select(&c,1,7,false)==1);finish_logic();native_choice=8;c.r[3]=M;hooks.at(0x0269B238)(&c);assert(ld32(M+0xAC)==8); // native input wins
 begin_logic(&c,.01);assert(select(&c,1,9,false)==1);state_loaded();c.r[3]=M;hooks.at(0x0269B238)(&c);assert(ld32(M+0xAC)==8);
 begin_logic(&c,.01);assert(select(&c,1,9,false)==1);st32(M+0x44,U+0xB00);finish_logic();c.r[3]=M;hooks.at(0x0269B238)(&c);assert(ld32(M+0xAC)==8);
 begin_logic(&c,.01);assert(compasses(&c,1,1,false)==1);mods::gates.event=true;finish_logic();c.r[3]=C;if(!hide_pane(P+0x400))drawn++;assert(drawn==2);
 mods::gates={};
 begin_logic(&c,.01);assert(compasses(&c,1,1,false)==1);assert(rectangle(2,r)==0);revoke(1);finish_logic();
 begin_logic(&c,.01);assert(compasses(&c,1,1,false)==1);st32(0x1046F0B0+0x5CD8,0x12000);finish_logic();assert(rectangle(1,r)==0); // first person
 st32(0x1046F0B0+0x5CD8,0x10000);
 begin_logic(&c,.01);assert(select(&c,1,9,false)==1);memcpy(mem::ptr(0x1046F0B0+0x5134),"Siren",6);finish_logic();c.r[3]=M;hooks.at(0x0269B238)(&c);assert(ld32(M+0xAC)==8);
 begin_logic(&c,.01);assert(select(&c,1,9,false)==1);finish_logic();c.r[3]=M;hooks.at(0x026986C4)(&c);hooks.at(0x0269B238)(&c);assert(ld32(M+0xAC)==8);
 begin_logic(&c,.01);assert(compasses(&c,1,1,false)==1);state_loaded();assert(rectangle(1,r)==0);
 begin_logic(&c,.01);assert(compasses(&c,1,1,false)==1);st32(H+0x4C,U+0x900);finish_logic();assert(rectangle(1,r)==0); // HUD child replacement
 st32(H+0x4C,U+0x800);pane(P,-620);
 begin_logic(&c,.01);assert(compasses(&c,1,1,false)==1);assert(rectangle(1,r)==1&&r.x==0&&r.w==390);finish_logic();
 puts("map HUD native adapter: ok");
}
