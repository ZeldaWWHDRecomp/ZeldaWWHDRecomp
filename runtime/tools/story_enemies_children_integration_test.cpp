// SPDX-License-Identifier: MPL-2.0
// Real adapter/children/ownership/carry/provider composition with a synthetic
// native bridge and item store. This is not native game or save-file proof.
#include "mods/story_enemies.h"
#include "mods/story_enemy_children.h"
#include "mods/native_actor_carry.h"
#include "mods/native_actor_profiles.h"
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
namespace own=story_enemy_ownership;
namespace enemy=story_enemies;
namespace children=story_enemy_children;
namespace {
constexpr uint32_t Arena=0x60010000,Profiles=0x60000000,BK=0x20000000,MO=0x20004000,CC=0x20008000;
constexpr uint32_t Child1=0x20010000,Child2=0x20012000,Child3=0x20014000,Data=0x21000000,Retail=0x20020000,Play=0x20018000,Link=0x20022000;
std::map<uint32_t,void(*)(Cpu*)> hooks;std::map<uint32_t,bool> defeated;
unsigned next_marker=0,claims=0,bk_exec=0,mo_exec=0,cc_exec=0,destructors=0,requests=0,refuse_requests=2;
uint32_t queued_pid=50;bool terminal_cc=false;
void map_memory(uint32_t a,uint32_t n){
#ifdef _WIN32
 assert(VirtualAlloc(mem::ptr(a),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
 assert(mmap(mem::ptr(a),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
uint32_t run(uint32_t fn,uint32_t a){Cpu c{};c.r[3]=a;c.r[4]=123;c.pc=456;c.f[2].ps0=42;hooks.at(fn)(&c);assert(c.r[4]==123&&c.pc==456&&c.f[2].ps0==42);return c.r[3];}
void original(Cpu*c,uint32_t fn){const uint32_t a=c->r[3];
 if(fn==0x025D5834){c->r[3]=queued_pid;return;}
 if(fn==0x0209F918){assert(ld8(0x10191468)==1);Cpu queue=*c;queue.r[3]=0x1CF;queue.r[4]=0;hooks.at(0x025D5834)(&queue);st32(a+0x1360,queue.r[3]);st8(a+0x1374,1);Cpu early=*c;early.r[3]=a;hooks.at(0x0209BD1C)(&early);assert(bk_exec==0);c->r[3]=4;return;}
 if(fn==0x021CCB00){assert(ld8(0x101BA690)==1);st8(a+0x10B0,1);c->r[3]=4;return;}
 if(fn==0x02110D4C||fn==0x020C25A4){c->r[3]=4;return;}
 if(fn==0x0209BD1C){++bk_exec;c->r[3]=1;return;}
 if(fn==0x021C9B1C){++mo_exec;c->r[3]=1;return;}
 if(fn==0x0210DD3C){++cc_exec;if(terminal_cc){st8(a+0x3FC,4);st8(a+0x3FD,51);Cpu drop=*c;drop.r[3]=a;hooks.at(0x025D99E8)(&drop);Cpu del=*c;del.r[3]=a;hooks.at(0x025D57E0)(&del);}c->r[3]=1;return;}
 if(fn==0x025D57E0){++requests;if(requests<=refuse_requests){c->r[3]=0;return;}st8(a+0xC,3);c->r[3]=1;return;}
 if(fn==0x025D4B4C){const uint16_t profile=ld16(a+0xE);const uint32_t destructor=profile==0x7FF9?0x0209F164:profile==0x7FF8?0x021CC44C:profile==0x7FFA?0x021104B4:0x020C1D84;hooks.at(destructor)(c);return;}
 if(fn==0x0209F164||fn==0x021CC44C||fn==0x021104B4||fn==0x020C1D84){++destructors;c->r[3]=1;return;}
 if(fn==0x025D99E8||fn==0x025D9154)assert(false); // owned native drops must never delegate
 c->r[3]=1;
}
uint32_t invoke(Cpu*c,uint32_t fn,std::initializer_list<uint32_t> args){if(fn==0x025200D4){c->r[4]=999;c->pc=999;return Play;}if(fn==0x025E3268||fn==0x025E2FBC||fn==0x025EE048||fn==0x025EE060)return 0;unsigned r=3;for(uint32_t a:args)c->r[r++]=a;if(hooks.count(fn))hooks.at(fn)(c);else original(c,fn);c->r[4]=999;c->pc=999;return c->r[3];}
void parent(uint32_t a,uint32_t pid,own::Kind kind){const auto&d=own::descriptor(kind);std::memset(mem::ptr(a),0,d.bytes);st32(a+4,pid);st16(a+0xE,d.profile);st32(a+0xB0,enemy::parameters(kind));st32(a+0xB4,kind==own::Kind::Bokoblin?0x1000957C:kind==own::Kind::Moblin?0x10014E9C:0x1000C5A0);st8(a+0x2DD,255);st16(a+0x2D8,0xFFFF);st16(a+0x2FC,enemy::home_z(kind));
 if(kind==own::Kind::Bokoblin){st8(a+0x3D0,4);st8(a+0x3D2,255);st8(a+0x3D3,255);st32(a+0x49C,a);st16(a+0x49C+8,2);}else if(kind==own::Kind::Moblin){st8(a+0x3DA,255);st8(a+0x3DB,255);st32(a+0x8BC,a);st16(a+0x8BC+8,1);}else st8(a+0x3FB,255);
 for(size_t i=0;i<d.collider_count;++i){const auto&x=d.colliders[i];st32(a+x.object+0x44,a+x.stts);st32(a+x.stts+0xC,a);st32(a+x.stts+0x10,pid);if(x.helper)st32(a+x.helper,a);}
}
void child(uint32_t a,uint32_t pid,uint32_t params){std::memset(mem::ptr(a),0,0xA34);st32(a+4,pid);st16(a+0xE,0x1CF);st32(a+0xB0,params);st8(a+0x2DD,255);st32(a+0x2E8,0xFFFFFFFFu);for(uint32_t off:{0x700u,0x82Cu})st32(a+off+0x44,a+0x6C4);st32(a+0x6D0,a);st32(a+0x6D4,pid);}
void queue_child(own::Token t,own::Kind kind,uint32_t a,uint32_t address,uint32_t pid){assert(children::enter(t,kind,a,own::source_epoch(t)));queued_pid=pid;Cpu queue{};queue.r[3]=0x1CF;queue.r[4]=kind==own::Kind::Bokoblin?0:4;hooks.at(0x025D5834)(&queue);children::leave();assert(queue.r[3]==pid);child(address,pid,kind==own::Kind::Bokoblin?0:4);st32(a+(kind==own::Kind::Bokoblin?0x1360:0x2D60),pid);assert(run(0x020C25A4,address)==4);assert(children::ready(t,a));}
struct Snapshot {std::vector<uint8_t> ownership,bk,mo;};
std::vector<uint8_t> bytes(uint32_t a,size_t size){auto*p=static_cast<uint8_t*>(mem::ptr(a));return {p,p+size};}
Snapshot snapshot(){return {bytes(Arena,own::kStorageBytes),bytes(BK,0x19A4),bytes(MO,0x343C)};}
void restore(const Snapshot&s,own::Token bk,own::Token mo){std::memcpy(mem::ptr(Arena),s.ownership.data(),s.ownership.size());std::memcpy(mem::ptr(BK),s.bk.data(),s.bk.size());std::memcpy(mem::ptr(MO),s.mo.data(),s.mo.size());own::state_loaded();enemy::state_loaded();children::state_loaded();enemy::bind(bk,10,true);enemy::bind(mo,11,true);}
}
namespace story_items {
std::optional<Item> find(const std::string&,const std::string&){return std::nullopt;}
EnemyMarkerHandle register_enemy_marker(const std::string&,const std::string&,const std::string&,std::string&){return {++next_marker};}
bool forget_prop_marker(PropMarkerHandle){return true;}
bool enemy_defeated(EnemyMarkerHandle h){return defeated[h.value];}
EnemyClaimStatus claim_enemy_drops(EnemyMarkerHandle h,const std::string&,const std::vector<PropDrop>&,bool){++claims;if(defeated[h.value])return EnemyClaimStatus::AlreadyDefeated;defeated[h.value]=true;return EnemyClaimStatus::Committed;}
}
int main(){map_memory(Profiles,0x20000);map_memory(BK,0x30000);map_memory(Data,0x10000);map_memory(0x101F0000,0x10000);map_memory(0x10190000,0x10000);map_memory(0x101B0000,0x10000);map_memory(0x10460000,0x10000);
 assert(own::configure(Arena,own::kStorageBytes,original,enemy::verify_actor));own::initialize_storage();enemy::configure(original,invoke,{Profiles,Profiles+0x30,Profiles+0x60,Profiles+0x90});
 assert(native_actor_carry::configure(original,invoke));assert(native_actor_carry::register_guard("enemy-adapter",enemy::deny_carry));assert(children::configure(original,invoke,enemy::child_retired));assert(enemy::configure_children({children::enter,children::leave,children::ready,children::retire},true));
 std::string why;enemy::Spec b{own::Kind::Bokoblin,"m","bk","bk.dead",{}, {},1},m{own::Kind::Moblin,"m","mo","mo.dead",{}, {},1},c{own::Kind::ChuChu,"m","cc","cc.dead",{}, {},1};auto bt=enemy::declare(b,why),mt=enemy::declare(m,why),ct=enemy::declare(c,why);assert(bt&&mt&&ct);
 for(auto kind:{own::Kind::ChuChu,own::Kind::Bokoblin,own::Kind::Moblin,own::Kind::Keese}){const auto&d=own::descriptor(kind);uint32_t native=Data+unsigned(kind)*0x100;st32(0x101F3EE0+4*d.native_profile,native);st32(native+0x10,d.bytes);}
 auto owner=[](uint32_t f){return hooks.count(f)?std::string("test-installed"):std::string();};auto replace=[](uint32_t f,void(*h)(Cpu*)){assert(!hooks.count(f));hooks[f]=h;};assert(enemy::install(owner,replace));assert(children::install(owner,replace));assert(own::install(owner,replace));
 parent(BK,10,own::Kind::Bokoblin);parent(MO,11,own::Kind::Moblin);parent(CC,12,own::Kind::ChuChu);enemy::bind(bt,10);enemy::bind(mt,11);enemy::bind(ct,12);assert(run(0x0209F918,BK)==4);assert(!children::ready(bt,BK));child(Child1,50,0);assert(run(0x020C25A4,Child1)==4&&children::ready(bt,BK));assert(run(0x021CCB00,MO)==4&&children::ready(mt,MO));assert(run(0x02110D4C,CC)==4);assert(own::pair_policy(Child1+0x700,Retail)==own::PairPolicy::Reject);st32(Play+0x5B34,Link);st32(Link+0x65B8,Child1);assert(run(0x023ED0F0,Link)==0);
 // Actual callback from the real provider/child module normalizes native BK loss.
 st32(BK+0xC90,1);st32(BK+0xC94,2);st8(BK+0x1374,1);assert(run(0x025D4B4C,Child1)==1);assert(ld32(BK+0x1360)==0xFFFFFFFFu&&!ld32(BK+0xC90)&&!ld32(BK+0xC94)&&!ld8(BK+0x1374)&&children::ready(bt,BK)&&enemy::available(bt)&&claims==0);run(0x0209BD1C,BK);assert(bk_exec==1);
 // Same-epoch orphan retirement cannot change the newer selected own child.
 queue_child(bt,own::Kind::Bokoblin,BK,Child1,51);queue_child(bt,own::Kind::Bokoblin,BK,Child2,52);auto before=bytes(BK,0x19A4);assert(run(0x025D4B4C,Child1)==1);assert(bytes(BK,0x19A4)==before&&children::ready(bt,BK));assert(run(0x025D4B4C,Child2)==1&&children::ready(bt,BK));
 // Native Moblin pickup sets innate1 before child Delete; actual callback preserves it.
 queue_child(mt,own::Kind::Moblin,MO,Child3,60);st8(MO+0x10B0,1);st8(MO+0x10B1,2);st8(MO+0x10B3,1);assert(run(0x025D4B4C,Child3)==1);assert(ld32(MO+0x2D60)==0xFFFFFFFFu&&ld8(MO+0x10B0)==1&&ld8(MO+0x10B1)==2&&!ld8(MO+0x10B3)&&children::ready(mt,MO));
 queue_child(mt,own::Kind::Moblin,MO,Child3,61);st8(MO+0x10B0,0);st8(MO+0x10B1,1);st8(MO+0x10B3,1);assert(run(0x025D4B4C,Child3)==1);assert(ld8(MO+0x10B0)==0&&ld8(MO+0x10B1)==1&&!ld8(MO+0x10B3)&&children::ready(mt,MO));run(0x021C9B1C,MO);assert(mo_exec==1&&claims==0);
 // Captured normalized native fields + real ownership arena rebind consistently.
 auto normalized=snapshot();st32(BK+0x1360,999);st32(MO+0x2D60,999);assert(!children::ready(bt,BK)&&!children::ready(mt,MO));restore(normalized,bt,mt);assert(children::ready(bt,BK)&&children::ready(mt,MO)&&ld8(MO+0x10B0)==0&&ld8(MO+0x10B1)==1);enemy::bind(ct,12,true);
 // Refused native deletion is retried on Execute only; no AI or duplicate reward.
 terminal_cc=true;run(0x0210DD3C,CC);assert(claims==1&&requests==1&&cc_exec==1&&ld8(CC+0xC)!=3);run(0x0210BFEC,CC);assert(requests==1);run(0x0210DD3C,CC);assert(requests==2&&claims==1&&cc_exec==1&&ld8(CC+0xC)!=3);run(0x0210DD3C,CC);assert(requests==3&&claims==1&&cc_exec==1&&ld8(CC+0xC)==3);run(0x0210DD3C,CC);assert(requests==3);const unsigned prior=destructors;assert(run(0x025D4B4C,CC)==1&&destructors==prior+1&&!own::lookup_actor(CC)&&claims==1);
 // Epoch change does not prevent the old class destructor from releasing resources.
 assert(enemy::begin_epoch(bt,2));const unsigned old_destructors=destructors;assert(run(0x025D4B4C,BK)==1&&destructors==old_destructors+1&&claims==1);
 return 0;
}
