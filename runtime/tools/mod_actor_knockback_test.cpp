// SPDX-License-Identifier: MPL-2.0
#include "mods/mod_actor_knockback.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace K=mod_actor_knockback;
namespace {
constexpr uint32_t Actor=0x20000000,Play=0x20100000;
K::Context context{Actor,7,Actor+0xC80,Actor+0xE44,Actor+0xF04,Actor+0xF10};
unsigned ctors=0,dtors=0,corrections=0;int room=11;bool throw_query=false,ground=true,wall=false,valid=true;float ground_y=0;unsigned probes=0;
bool verify(const K::Context&x){return valid&&std::memcmp(&x,&context,sizeof x)==0;}
uint32_t invoke(Cpu*c,uint32_t f,std::initializer_list<uint32_t> args){auto i=args.begin();uint32_t p=*i;c->r[3]=99;c->pc=88;c->f[1].ps0=77;
 if(f==0x024F0474){assert(p==context.acch);std::memset(mem::ptr(p),0,K::kAcchBytes);st32(p+0x28,8);++ctors;}
 else if(f==0x024EFE94){assert(p>=context.circles&&p<context.circles+3*0x40);std::memset(mem::ptr(p),0,0x40);++ctors;}
 else if(f==0x024F08A8){assert(p==context.acch&&*(i+1)==Play+0x12A0);++corrections;if(throw_query)throw std::runtime_error("query");const float candidate=u32_as_f32(ld32(Actor+0x318));const bool native_ground_hit=ground&&ground_y>candidate;st32(p+0x28,(ld32(p+0x28)&~0x20u)|(native_ground_hit?0x20u:0));st32(p+0x94,f32_as_u32(ground_y));if(native_ground_hit)st32(Actor+0x318,f32_as_u32(ground_y));if(wall){st32(Actor+0x314,ld32(context.old_position));st32(p+0x28,ld32(p+0x28)|0x10);}}
 else if(f==0x02008974){assert(p==Play+0x12A0&&*(i+1)==context.acch+0xD4);assert(ld32(context.acch+0xDC)==context.pid);assert(u32_as_f32(ld32(context.acch+0xFC))==u32_as_f32(ld32(Actor+0x318))+100);++probes;c->f[1].ps0=ground?ground_y:-1e9;}
 else if(f==0x024EF130){assert(*(i+1)==context.acch+0xE8);return uint32_t(room);}
 else if(f==0x024EFD9C){assert(p==context.acch&&*(i+1)==0);++dtors;}
 else if(f==0x02018034){assert(*(i+1)==2);++dtors;}
 else assert(false);return 0;
}
void map(uint32_t a,uint32_t n){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(a),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(a),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
}
int main(){map(Actor,0x10000);map(Play,0x10000);st32(Actor+4,7);st8(Actor+0x326,11);Cpu c{};c.r[3]=1;c.pc=2;c.f[1].ps0=3;Cpu saved=c;assert(K::configure(invoke,verify));assert(K::initialize(&c,context,30,90)&&ctors==4);assert(std::memcmp(&c,&saved,sizeof c)==0);assert(ld32(context.acch+0x2C)==Actor+0x314&&ld32(context.acch+0x3C)==0&&ld32(context.acch+0x84)==3);assert(u32_as_f32(ld32(context.circles+0x30))==15&&u32_as_f32(ld32(context.circles+0x80+0x30))==75);
assert(K::move(&c,context,Play,{10,0,0}));assert(u32_as_f32(ld32(Actor+0x314))==10&&corrections==1);wall=true;assert(K::move(&c,context,Play,{20,0,0}));assert(u32_as_f32(ld32(Actor+0x314))==10);wall=false;
uint8_t before[K::kAcchBytes];std::memcpy(before,mem::ptr(context.acch),sizeof before);room=12;assert(!K::move(&c,context,Play,{20,0,0}));assert(u32_as_f32(ld32(Actor+0x314))==10&&std::memcmp(before,mem::ptr(context.acch),sizeof before)==0);room=11;ground=false;assert(!K::move(&c,context,Play,{20,0,0}));ground=true;throw_query=true;assert(!K::move(&c,context,Play,{20,0,0}));assert(std::memcmp(before,mem::ptr(context.acch),sizeof before)==0);throw_query=false;
// Callback sees corrected position; rejection/fault restores all native owned state.
uint8_t circle_before[K::kCircleBytes*3],old_before[12],speed_before[12];std::memcpy(circle_before,mem::ptr(context.circles),sizeof circle_before);std::memcpy(old_before,mem::ptr(context.old_position),12);std::memcpy(speed_before,mem::ptr(context.speed),12);
auto restored=[&]{assert(u32_as_f32(ld32(Actor+0x314))==10);assert(std::memcmp(before,mem::ptr(context.acch),sizeof before)==0);assert(std::memcmp(circle_before,mem::ptr(context.circles),sizeof circle_before)==0);assert(std::memcmp(old_before,mem::ptr(context.old_position),12)==0);assert(std::memcmp(speed_before,mem::ptr(context.speed),12)==0);};
wall=true;assert(!K::move(&c,context,Play,{20,0,0},30,40,[](K::Point p){assert(p.x==10);return false;}));restored();
assert(!K::move(&c,context,Play,{20,0,0},30,40,[](K::Point p)->bool{assert(p.x==10);throw std::runtime_error("accept");}));restored();wall=false;
assert(K::update_dimensions(context,40,120));assert(u32_as_f32(ld32(context.circles+0x34))==40&&u32_as_f32(ld32(context.circles+0x80+0x30))==100);assert(!K::update_dimensions(context,201,120));assert(!K::update_dimensions(context,40,std::numeric_limits<float>::quiet_NaN()));
unsigned count=corrections;assert(!K::move(&c,context,Play,{100,0,0})&&corrections==count);assert(!K::move(&c,context,Play,{std::numeric_limits<float>::quiet_NaN(),0,0}));auto forged=context;forged.speed=forged.old_position;assert(!K::initialize(&c,forged,30,90));st32(Actor+4,8);assert(!K::move(&c,context,Play,{20,0,0}));st32(Actor+4,7);
// Restored native binding is accepted directly, without another constructor.
// Downhill support also grounds through native strict-greater correction.
ground_y=-2;assert(K::move(&c,context,Play,{15,0,0}));assert(u32_as_f32(ld32(Actor+0x318))==-2);ground_y=-100;assert(!K::move(&c,context,Play,{20,-2,0}));assert(u32_as_f32(ld32(Actor+0x318))==-2);ground_y=std::numeric_limits<float>::quiet_NaN();assert(!K::move(&c,context,Play,{20,-2,0}));ground_y=0;
assert(K::move(&c,context,Play,{15,0,0})&&ctors==4);assert(K::destroy(&c,context)&&dtors==4);assert(std::memcmp(&c,&saved,sizeof c)==0);
}
