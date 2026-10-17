// SPDX-License-Identifier: MPL-2.0
// Synthetic creation/restore/collision boundaries; not native weapon acceptance.
#include "mods/story_enemy_projectiles.h"
#include "mods/story_enemy_ownership.h"
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
using namespace story_enemy_projectiles;
constexpr uint32_t Arena=0x60020000,Link=0x20000000,Enemy=0x20010000,Projectile=0x20100000;
std::map<uint32_t,void(*)(Cpu*)> hooks;Kind next_kind=Kind::Arrow;uint32_t next_actor=Projectile,next_pid=500;unsigned calls=0;bool fake_bad=false,replace_failure=false;
void map(uint32_t a,uint32_t n){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(a),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(a),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void projectile(Kind k,uint32_t a,uint32_t pid){auto&d=descriptor(k);std::memset(mem::ptr(a),0,d.bytes);st32(a+4,pid);st16(a+0xE,d.profile);st32(a+0xB0,d.parameters);st32(a+0x2E8,0xFFFFFFFF);st32(a+d.object+0x44,a+d.stts);st32(a+d.stts+0xC,a);st32(a+d.stts+0x10,pid);st32(a+d.object+0x10,k==Kind::Arrow?0x4000:d.attack_type);st8(a+d.object+0x14,k==Kind::Arrow?2:d.power);if(fake_bad)st32(a+d.stts+0xC,Enemy);}
void original(Cpu*c,uint32_t fn){++calls;if(fn==0x0205584C){uint32_t old=c->r[3];if(replace_failure)return;before_delete(old,ld32(old+4),ld16(old+0xE));projectile(Kind::Arrow,next_actor,next_pid);c->r[3]=next_actor;return;}if(fn==0x023DF600||fn==0x023EB120){projectile(next_kind,next_actor,next_pid);uint32_t keep=next_kind==Kind::Bomb?0x65A0:0x6590;st32(c->r[3]+keep,next_pid);st32(c->r[3]+keep+4,next_actor);c->r[7]=0xAABBCCDD;return;}c->r[3]=0;}
void run(uint32_t fn,uint32_t a){Cpu c{};c.r[3]=a;hooks.at(fn)(&c);}
}
int main(){namespace O=story_enemy_ownership;map(Arena,0x10000);map(Link,0x400000);map(0x10000000,0x10000);st32(0x100073AC,0x4000);st8(0x100073B0,2);st32(Link+4,42);st16(Link+0xE,0xA8);assert(O::configure(Arena,O::kStorageBytes,original,[](O::Token,O::Kind,uint32_t){return true;}));O::initialize_storage();assert(O::set_link(Link,42));std::string why;O::Fingerprint fp{};fp[0]=1;auto enemy=O::declare_actor(O::Kind::ChuChu,0xFF000000,fp,1,why);st32(Enemy+4,100);st16(Enemy+0xE,0x7FFA);st32(Enemy+0xB0,0xFF000000);st8(Enemy+0x2DD,0xFF);st32(Enemy+0x7C4+0x44,Enemy+0x788);st32(Enemy+0x794,Enemy);st32(Enemy+0x798,100);assert(O::bind_pending(enemy,100)&&O::admit_created(enemy,Enemy));auto read=[](uint32_t&a,uint32_t&p){a=Link;p=ld32(Link+4);return true;};assert(configure(original,read));assert(install([](uint32_t){return std::string();},[](uint32_t fn,void(*h)(Cpu*)){hooks[fn]=h;}));assert(hooks.size()==3);
 uint32_t arrows=0;for(unsigned n=1;n<=5;++n){next_kind=Kind(n);next_actor=Projectile+n*0x40000;next_pid=500+n;uint16_t item=n==2?0x31:n==3?0x25:n==4?0x2F:0x2D;st16(Link+0x69B0,item);Cpu c{};c.r[3]=Link;hooks.at(n==1?0x023EB120:0x023DF600)(&c);assert(c.r[7]==0xAABBCCDD);auto&d=descriptor(next_kind);assert(owns_attack(next_actor+d.object));assert(O::pair_policy(next_actor+d.object,Enemy+0x7C4)==O::PairPolicy::Reject);if(n==1)arrows=next_actor;}
 // Default native admission flag stays off. Explicit synthetic opt-in tests policy.
 assert(configure(original,read,true));assert(O::pair_policy(arrows+0x4B8,Enemy+0x7C4)==O::PairPolicy::Delegate);assert(O::pair_policy(Enemy+0x7C4,arrows+0x4B8)==O::PairPolicy::Reject);
 // A copied profile/parentFF without captured creation never gains provenance.
 uint32_t unknown=Projectile+0x180000;projectile(Kind::Arrow,unknown,900);assert(!owns_attack(unknown+0x4B8));assert(O::pair_policy(unknown+0x4B8,Enemy+0x7C4)==O::PairPolicy::Reject);
 // Replacement inherits the verified predecessor, even if its native deletion
 // listener retires the old record during the original replacement function.
 next_actor=Projectile+0x1C0000;next_pid=901;run(0x0205584C,arrows);assert(!owns_attack(arrows+0x4B8));assert(owns_attack(next_actor+0x4B8));uint32_t replacement=next_actor;
 next_actor=Projectile+0x200000;next_pid=902;run(0x0205584C,unknown);assert(!owns_attack(next_actor+0x4B8));
 // Capture malformed owner is refused; no record is silently adopted.
 fake_bad=true;next_kind=Kind::Arrow;next_actor=Projectile+0x240000;next_pid=903;run(0x023EB120,Link);assert(!owns_attack(next_actor+0x4B8));fake_bad=false;
 std::array<uint8_t,O::kStorageBytes> snapshot{};std::memcpy(snapshot.data(),mem::ptr(Arena),snapshot.size());before_delete(Link,42,0xA8);assert(!owns_attack(replacement+0x4B8));std::memcpy(mem::ptr(Arena),snapshot.data(),snapshot.size());O::state_loaded();state_loaded();assert(owns_attack(replacement+0x4B8));
 before_delete(replacement,999,0x1D8);assert(owns_attack(replacement+0x4B8));before_delete(replacement,901,0x1D8);assert(!owns_attack(replacement+0x4B8));room_changed();O::LineageInfo i{};assert(!O::lineage_info(502,i));
}
