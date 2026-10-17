// SPDX-License-Identifier: MPL-2.0
// Real production module composition; synthetic native bridge, not native proof.
#include "mods/story_enemy_children.h"
#include "mods/native_actor_profiles.h"
#include "mods/native_actor_carry.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <map>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace o=story_enemy_ownership;
namespace {
constexpr uint32_t Arena=0x60010000,Profile=0x60000000,Parent=0x20000000,Child=0x20008000,Play=0x20010000,Link=0x20020000,Retail=0x20030000;
std::map<uint32_t,void(*)(Cpu*)> hooks;unsigned queued=0,executed=0,deleted=0;uint32_t nextPID=50;
void map(uint32_t a,uint32_t n){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(a),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(a),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void original(Cpu*c,uint32_t f){if(f==0x025D5834){++queued;c->r[3]=nextPID;}else if(f==0x020C25A4){Cpu nested{};nested.r[3]=Child;hooks.at(0x020C1D0C)(&nested);assert(executed==0);c->r[3]=4;}else if(f==0x020C1D0C){++executed;c->r[3]=1;}else if(f==0x025D4B4C){++deleted;c->r[3]=1;}else if(f==0x023ED0F0)c->r[3]=7;else c->r[3]=0;}
uint32_t invoke(Cpu*c,uint32_t f,std::initializer_list<uint32_t>){assert(f==0x025200D4);c->r[4]=999;c->pc=999;return Play;}
void retire_parent(uint32_t a,uint32_t,uint16_t p) noexcept {if(p==0x7FF9)o::retire_before_delete(a);}
void fixture(uint32_t a,uint32_t pid,uint16_t profile,uint32_t params){st32(a+4,pid);st16(a+0xE,profile);st32(a+0xB0,params);st8(a+0x2DD,255);st32(a+0x2E8,0xFFFFFFFF);}
void parent_colliders(){auto&d=o::descriptor(o::Kind::Bokoblin);for(size_t i=0;i<d.collider_count;++i){auto&x=d.colliders[i];st32(Parent+x.object+0x44,Parent+x.stts);st32(Parent+x.stts+0xC,Parent);st32(Parent+x.stts+0x10,10);if(x.helper)st32(Parent+x.helper,Parent);}}
void child_colliders(){for(uint32_t off:{0x700u,0x82Cu})st32(Child+off+0x44,Child+0x6C4);st32(Child+0x6D0,Child);st32(Child+0x6D4,50);}
std::vector<uint8_t> snapshot(){return {static_cast<uint8_t*>(mem::ptr(Arena)),static_cast<uint8_t*>(mem::ptr(Arena))+o::kStorageBytes};}
void restore(const std::vector<uint8_t>&s){std::memcpy(mem::ptr(Arena),s.data(),s.size());o::state_loaded();story_enemy_children::state_loaded();}
uint32_t call(uint32_t f,uint32_t a){Cpu c{};c.r[3]=a;hooks.at(f)(&c);return c.r[3];}
}
int main(){map(0x60000000,0x20000);map(Parent,0x40000);map(0x10460000,0x10000);
assert(o::configure(Arena,o::kStorageBytes,original,[](o::Token,o::Kind,uint32_t){return true;}));o::initialize_storage();
std::string why;o::Fingerprint fp{};fp[0]=17;auto token=o::declare_actor(o::Kind::Bokoblin,0xFFFF0004,fp,3,why);assert(token&&o::bind_pending(token,10));fixture(Parent,10,0x7FF9,0xFFFF0004);parent_colliders();fixture(Child,50,0x1CF,0);child_colliders();
// Register every consumer before provider install, with real shared carry registry.
native_actor_profiles::configure(original,invoke);assert(story_enemy_children::configure(original,invoke));assert(native_actor_profiles::add_before_delete_listener("test-parent",retire_parent));st16(Profile+8,0x7FF9);assert(native_actor_profiles::add(0x7FF9,Profile));
auto owner=[](uint32_t){return std::string();};auto replace=[](uint32_t f,void(*h)(Cpu*)){assert(!hooks.count(f));hooks[f]=h;};assert(story_enemy_children::install(owner,replace));assert(o::install(owner,replace));assert(native_actor_profiles::install(owner,replace));
assert(!native_actor_profiles::add_before_delete_listener("late",retire_parent));
// Pending parent Create can reserve before its real constructor is admitted.
assert(story_enemy_children::enter(token,o::Kind::Bokoblin,Parent,3));
std::vector<uint32_t> reservations;for(size_t i=0;i<o::kChildCapacity;++i){auto t=o::reserve_child_pending(token,Parent,10,0x1CF,0,3);assert(t);reservations.push_back(t);}
Cpu q{};q.r[3]=0x1CF;q.r[4]=0;hooks.at(0x025D5834)(&q);assert(q.r[3]==0xFFFFFFFF&&queued==0);for(auto t:reservations)o::cancel_child_pending(t);
q.r[3]=0x1CF;q.r[4]=0;hooks.at(0x025D5834)(&q);story_enemy_children::leave();assert(q.r[3]==50&&queued==1);st32(Parent+0x1360,50);auto pending=snapshot();assert(!story_enemy_children::ready(token,Parent));
assert(o::admit_created(token,Parent)&&o::enroll_colliders(token));assert(call(0x020C25A4,Child)==4);assert(story_enemy_children::ready(token,Parent));auto live=snapshot();
assert(o::pair_policy(Child+0x700,Retail)==o::PairPolicy::Reject);assert(o::pair_policy(Parent+0xCE8,Retail)==o::PairPolicy::Reject);assert(o::pair_policy(Retail,Retail+0x100)==o::PairPolicy::Delegate);
// Real shared carry resolves the actual Link and preserves all non-return CPU state.
st32(Play+0x5B34,Link);st32(Link+0x65B8,Child);Cpu carry{};carry.r[3]=Link;carry.r[4]=123;carry.pc=456;Cpu expected=carry;expected.r[3]=0;hooks.at(0x023ED0F0)(&carry);assert(!std::memcmp(&carry,&expected,sizeof carry));
// Full snapshot restoration of pending, then async construction again.
restore(pending);assert(!o::lookup_actor(Parent)&&!story_enemy_children::ready(token,Parent));assert(o::admit_created(token,Parent)&&o::enroll_colliders(token));assert(call(0x020C25A4,Child)==4&&story_enemy_children::ready(token,Parent));
restore(live);assert(story_enemy_children::ready(token,Parent));
// Parent deletion retains an independently owned child through full restore.
assert(call(0x025D4B4C,Parent)==1&&deleted==1);assert(!o::lookup_actor(Parent));o::LineageInfo info{};assert(o::lineage_info(50,info)&&info.source_valid&&story_enemy_children::owned(Child));auto orphan=snapshot();restore(orphan);assert(!o::lookup_actor(Parent)&&story_enemy_children::owned(Child));assert(o::pair_policy(Child+0x700,Retail)==o::PairPolicy::Reject);
// A newer source epoch cannot authorize an old orphan Execute.
assert(o::begin_epoch(token,4));unsigned before=executed;assert(call(0x020C1D0C,Child)==1&&executed==before);restore(orphan);
// Reused address/new PID is retail, not inherited ownership.
st32(Child+4,51);assert(!story_enemy_children::owned(Child));assert(o::pair_policy(Child+0x700,Retail)==o::PairPolicy::Delegate);st32(Child+4,50);
// A full restore brings the original child generation back; deletion retires it once.
restore(orphan);assert(call(0x025D4B4C,Child)==1&&!o::lineage_claimed(50,o::Lineage::EnemyChild));restore(orphan);assert(story_enemy_children::owned(Child));
// Adapter's audited missing-child transition permits only clean unarmed state.
restore(live);assert(call(0x025D4B4C,Child)==1);assert(!story_enemy_children::ready(token,Parent));st32(Parent+0x1360,0xFFFFFFFF);st32(Parent+0xC90,0);st32(Parent+0xC94,0);st8(Parent+0x1374,0);assert(story_enemy_children::ready(token,Parent));st32(Parent+0xC90,1);assert(!story_enemy_children::ready(token,Parent));st32(Parent+0xC90,0);assert(story_enemy_children::ready(token,Parent));st32(Parent+0xC94,2);assert(!story_enemy_children::ready(token,Parent));st32(Parent+0xC94,0);assert(story_enemy_children::ready(token,Parent));st8(Parent+0x1374,1);assert(!story_enemy_children::ready(token,Parent));st8(Parent+0x1374,0);assert(story_enemy_children::ready(token,Parent));
}
