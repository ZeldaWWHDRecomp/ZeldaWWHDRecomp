// SPDX-License-Identifier: MPL-2.0
#include "mods/mod_actor_native_combat.h"
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
namespace N=mod_actor_native_combat;
namespace {
constexpr uint32_t Actor=0x20000000,Item=0x20001000,State=0x60010000,Source=State+0x100;
N::SourceIdentity identity{Actor,12,7,3,State};unsigned calls=0;uint32_t result=21;bool fault=false,valid=true,reenter=false;
bool verify(const N::SourceIdentity&i){return valid&&i.actor==identity.actor&&i.pid==identity.pid&&i.generation==identity.generation&&i.epoch==identity.epoch&&i.heart_state==identity.heart_state;}
uint32_t invoke(Cpu*c,uint32_t f,std::initializer_list<uint32_t> args){assert(f==0x025D8870);const int32_t item_bit=int32_t(args.begin()[2]);assert((item_bit>=-1&&item_bit<=79)||item_bit==127);assert((std::vector<uint32_t>(args)==std::vector<uint32_t>{Actor+0x314,0,0xFFFFFFFFu,11,0,0,4,0}));++calls;N::HeartState h{};assert(N::read_heart_state(identity.heart_state,h)&&h.disposition==N::HeartDisposition::Issuing);if(reenter)assert(N::request_heart(c,identity.heart_state,identity,Actor+0x314,11)==N::HeartDisposition::Issuing);c->r[3]=999;c->pc=123;c->f[2].ps0=88;if(fault)throw std::runtime_error("native fault");return result;}
void clear(){std::memset(mem::ptr(State),0,32);}
void map(uint32_t p,uint32_t n){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(p),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(p),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
}
int main(){map(Actor,0x10000);map(State,0x10000);assert(!N::configure(nullptr,verify));assert(N::configure(invoke,verify)&&N::configure(invoke,verify));
N::AttackSpec spec{30,60,20,0,2};assert(N::validate_attack_spec(spec));assert(N::write_attack_source(Source,spec));assert(ld32(Source+4)==0x800&&ld8(Source+8)==2&&ld32(Source+12)==0xF&&ld8(Source+0x1E)==0&&ld32(Source+0x14)==0&&ld32(Source+0x18)==0);assert(u32_as_f32(ld32(Source+0x40))==60);spec.power=5;assert(!N::write_attack_source(Source,spec));spec.power=1;spec.radius=std::numeric_limits<float>::quiet_NaN();assert(!N::validate_attack_spec(spec));
Cpu c{};c.r[3]=44;c.pc=55;c.f[2].ps0=4;Cpu before=c;reenter=true;assert(N::request_heart(&c,State,identity,Actor+0x314,11)==N::HeartDisposition::Queued);assert(calls==1&&std::memcmp(&c,&before,sizeof c)==0);assert(N::request_heart(&c,State,identity,Actor+0x314,11)==N::HeartDisposition::Queued&&calls==1);
N::HeartState h{};assert(N::read_heart_state(State,h)&&h.queued_pid==21);uint8_t snapshot[32];std::memcpy(snapshot,mem::ptr(State),32);
st32(Item+4,21);st16(Item+0xE,0xFF);st32(Item+0xB0,0x10FFFF00);st32(Item+0xB4,0x10011FF8);st32(Item+0x744,0x7F);assert(!N::admit_heart(State,Item,0));assert(N::admit_heart(State,Item,4));assert(!N::admit_heart(State,Item,4));std::memcpy(mem::ptr(State),snapshot,32);st32(Item+4,22);assert(!N::admit_heart(State,Item,4));st32(Item+4,21);st32(Item+0xB0,0);assert(!N::admit_heart(State,Item,4));
// Snapshot rewind preserves queued latch. Async Create failure/HeroMode suppression cannot retry.
assert(N::request_heart(&c,State,identity,Actor+0x314,11)==N::HeartDisposition::Queued&&calls==1);auto stale=identity;stale.epoch++;assert(N::request_heart(&c,State,stale,Actor+0x314,11)==N::HeartDisposition::Failed&&calls==1);
for(uint32_t bad:{0u,0xFFFFFFFFu}){clear();result=bad;unsigned n=calls;assert(N::request_heart(&c,State,identity,Actor+0x314,11)==N::HeartDisposition::Failed);assert(calls==n+1);assert(N::request_heart(&c,State,identity,Actor+0x314,11)==N::HeartDisposition::Failed&&calls==n+1);assert(std::memcmp(&c,&before,sizeof c)==0);}
clear();fault=true;unsigned n=calls;assert(N::request_heart(&c,State,identity,Actor+0x314,11)==N::HeartDisposition::Failed&&calls==n+1);assert(std::memcmp(&c,&before,sizeof c)==0);fault=false;clear();valid=false;assert(N::request_heart(&c,State,identity,Actor+0x314,11)==N::HeartDisposition::Failed&&calls==n+1);valid=true;assert(N::request_heart(&c,State,identity,Actor+0x318,11)==N::HeartDisposition::Failed);assert(N::request_heart(&c,State,identity,Actor+0x314,64)==N::HeartDisposition::Failed);assert(!N::write_attack_source(0x68000000,spec));
identity.heart_state=Actor+0xA20;result=33;reenter=true;unsigned old_calls=calls;
assert(N::request_heart(&c,State,identity,Actor+0x314,11)==N::HeartDisposition::Failed&&calls==old_calls);
assert(N::request_heart(&c,identity.heart_state,identity,Actor+0x314,11)==N::HeartDisposition::Queued&&calls==old_calls+1);
assert(N::read_heart_state(identity.heart_state,h)&&h.queued_pid==33);
assert(N::request_heart(&c,identity.heart_state,identity,Actor+0x314,11)==N::HeartDisposition::Queued&&calls==old_calls+1);
assert(std::memcmp(&c,&before,sizeof c)==0);

}
