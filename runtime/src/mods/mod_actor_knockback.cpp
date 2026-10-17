// SPDX-License-Identifier: MPL-2.0
#include "mod_actor_knockback.h"
#include "runtime.h"
#include "guest_addr.h"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
namespace mod_actor_knockback {namespace {
Invoke invoke=nullptr;VerifyActor verify=nullptr;
bool span(uint32_t a,uint32_t n){return !(a&3)&&a>=mem::kMem2Start&&a<=mem::kMem2End-n;}
bool overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn){return a<b+bn&&b<a+an;}
bool valid(const Context&x){if(!invoke||!verify||!span(x.actor,0x3AC)||!x.pid||x.pid==0xFFFFFFFFu||ld32(x.actor+4)!=x.pid)return false;const uint32_t p[]={x.acch,x.circles,x.old_position,x.speed},n[]={kAcchBytes,kCircleBytes*kCircles,12,12};for(unsigned i=0;i<4;++i){if(!span(p[i],n[i])||overlap(p[i],n[i],x.actor,0x3AC))return false;for(unsigned j=0;j<i;++j)if(overlap(p[i],n[i],p[j],n[j]))return false;}try{return verify(x);}catch(...){return false;}}
struct Keep {Cpu*c;Cpu saved;explicit Keep(Cpu*p):c(p),saved(*p){}~Keep(){*c=saved;}};
Point point(uint32_t a){return {u32_as_f32(ld32(a)),u32_as_f32(ld32(a+4)),u32_as_f32(ld32(a+8))};}
void point(uint32_t a,Point p){st32(a,f32_as_u32(p.x));st32(a+4,f32_as_u32(p.y));st32(a+8,f32_as_u32(p.z));}
bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool bound(const Context&x){return ld32(x.acch+0x2C)==x.actor+0x314&&ld32(x.acch+0x30)==x.old_position&&ld32(x.acch+0x34)==x.speed&&ld32(x.acch+0x80)==x.actor&&ld32(x.acch+8)==x.pid&&ld32(x.acch+0x84)==kCircles&&ld32(x.acch+0x88)==x.circles&&ld32(x.acch+0x38)==0&&ld32(x.acch+0x3C)==0;}
}
bool configure(Invoke i,VerifyActor v){if(!i||!v)return false;if(invoke&&(invoke!=i||verify!=v))return false;invoke=i;verify=v;return true;}
bool initialize(Cpu*c,const Context&x,float radius,float height){if(!c||!valid(x)||!std::isfinite(radius)||!std::isfinite(height)||radius<=0||radius>200||height<=0||height>400||!finite(point(x.actor+0x314)))return false;Keep cpu(c);unsigned constructed=0;bool acch=false;try{invoke(c,GC(0x024F0474),{x.acch});acch=true;for(unsigned i=0;i<kCircles;++i){const uint32_t p=x.circles+i*kCircleBytes;invoke(c,GC(0x024EFE94),{p});++constructed;st32(p+0x30,f32_as_u32(height*(float(i)+0.5f)/float(kCircles)));st32(p+0x34,f32_as_u32(radius));}point(x.old_position,point(x.actor+0x314));point(x.speed,{0,0,0});st32(x.acch+0x2C,x.actor+0x314);st32(x.acch+0x30,x.old_position);st32(x.acch+0x34,x.speed);st32(x.acch+0x38,0);st32(x.acch+0x3C,0);st32(x.acch+0x80,x.actor);st32(x.acch+8,x.pid);st32(x.acch+0x84,kCircles);st32(x.acch+0x88,x.circles);st32(x.acch+0x28,(ld32(x.acch+0x28)&~(4u|0x4000u))|0x2000u|0x400u);return true;}catch(...){for(unsigned i=0;i<constructed;++i)try{invoke(c,GC(0x02018034),{x.circles+i*kCircleBytes+0x14,2});}catch(...){}if(acch)try{invoke(c,GC(0x024EFD9C),{x.acch,0});}catch(...){}return false;}}
bool move(Cpu*c,const Context&x,uint32_t play,Point desired,float max_step,float max_ground_delta,const std::function<bool(Point)>& accept_position){if(!c||!valid(x)||!bound(x)||!span(play,0x12A4)||!finite(desired)||!std::isfinite(max_step)||max_step<=0||max_step>50||!std::isfinite(max_ground_delta)||max_ground_delta<0||max_ground_delta>60)return false;const Point old=point(x.actor+0x314);if(!finite(old)||std::hypot(desired.x-old.x,desired.z-old.z)>max_step||std::fabs(desired.y-old.y)>max_ground_delta)return false;Keep cpu(c);std::array<uint8_t,kAcchBytes> a;std::array<uint8_t,kCircleBytes*kCircles> circles;std::array<uint8_t,24> scratch;std::memcpy(a.data(),mem::ptr(x.acch),a.size());std::memcpy(circles.data(),mem::ptr(x.circles),circles.size());std::memcpy(scratch.data(),mem::ptr(x.old_position),12);std::memcpy(scratch.data()+12,mem::ptr(x.speed),12);auto rollback=[&]{point(x.actor+0x314,old);std::memcpy(mem::ptr(x.acch),a.data(),a.size());std::memcpy(mem::ptr(x.circles),circles.data(),circles.size());std::memcpy(mem::ptr(x.old_position),scratch.data(),12);std::memcpy(mem::ptr(x.speed),scratch.data()+12,12);};try{
// Native GroundCheck sets GROUND_HIT only for floor > candidateY. A purely
// horizontal move supplies no gravity, so first measure bounded support using
// the owned embedded GndChk and penetrate one representable float below it.
point(x.acch+0xF8,{desired.x,old.y+max_ground_delta+60,desired.z});st32(x.acch+0xDC,x.pid);
invoke(c,GC(0x02008974),{play+0x12A0,x.acch+0xD4});const float support=float(c->f[1].ps0);
if(!std::isfinite(support)||std::fabs(support-old.y)>max_ground_delta||int(invoke(c,GC(0x024EF130),{play+0x12A0,x.acch+0xE8}))!=int(int8_t(ld8(x.actor+0x326)))){rollback();return false;}
desired.y=std::nextafter(support,-std::numeric_limits<float>::infinity());
if(!std::isfinite(desired.y)){rollback();return false;}
point(x.old_position,old);point(x.speed,{desired.x-old.x,desired.y-old.y,desired.z-old.z});point(x.actor+0x314,desired);invoke(c,GC(0x024F08A8),{x.acch,play+0x12A0});const Point result=point(x.actor+0x314);const float ground=u32_as_f32(ld32(x.acch+0x94));if(!bound(x)||!finite(result)||!std::isfinite(ground)||!(ld32(x.acch+0x28)&0x20)||std::fabs(result.y-old.y)>max_ground_delta||std::fabs(result.y-ground)>0.1f||std::hypot(result.x-old.x,result.z-old.z)>max_step+0.01f||int(invoke(c,GC(0x024EF130),{play+0x12A0,x.acch+0xE8}))!=int(int8_t(ld8(x.actor+0x326)))){rollback();return false;}if(accept_position&&!accept_position(result)){rollback();return false;}return true;}catch(...){rollback();return false;}}
bool update_dimensions(const Context&x,float radius,float height){if(!valid(x)||!bound(x)||!std::isfinite(radius)||!std::isfinite(height)||radius<=0||radius>200||height<=0||height>400)return false;for(unsigned i=0;i<kCircles;++i){st32(x.circles+i*kCircleBytes+0x30,f32_as_u32(height*(float(i)+0.5f)/float(kCircles)));st32(x.circles+i*kCircleBytes+0x34,f32_as_u32(radius));}return true;}
bool destroy(Cpu*c,const Context&x){if(!c||!valid(x)||!bound(x))return false;Keep cpu(c);bool ok=true;try{invoke(c,GC(0x024EFD9C),{x.acch,0});}catch(...){ok=false;}for(unsigned i=0;i<kCircles;++i)try{invoke(c,GC(0x02018034),{x.circles+i*kCircleBytes+0x14,2});}catch(...){ok=false;}return ok;}
}
