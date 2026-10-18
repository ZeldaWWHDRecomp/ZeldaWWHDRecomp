// SPDX-License-Identifier: MPL-2.0
// Synthetic native boundary tests; no game physics/render acceptance claim.
#include "mods/story_enemies.h"
#include "mods/native_actor_profiles.h"
#include "runtime.h"
#include "guest_addr.h"
#ifndef STORY_ENEMIES_TEST_CODE_DELTA
#define STORY_ENEMIES_TEST_CODE_DELTA 0u
#endif
#include <cassert>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {
constexpr uint32_t Actor=0x20000000,Second=Actor+0x2000,Retail=Actor+0x4000,Arena=0x60000000,Bokoblin=Actor+0x6000,Moblin=Actor+0xA000;
std::map<uint32_t,void(*)(Cpu*)> hooks;std::map<uint32_t,bool> defeated;
unsigned calls=0,claims=0,creates=0,deletes=0,drops=0,marker_sequence=0;bool terminal=false,freeze_terminal=false,fail_claim=false,throw_execute=false,children_ready=false;unsigned child_scopes=0,child_retires=0;bool drown_mode=false,bad_helper=false,bad_type=false,foreign_helper=false,request_ok=true,request_queued=true,throw_create=false,nested_weapon=false;uint32_t drown_lr=0x02044FCC;
void map_memory(uint32_t a,uint32_t n){
#ifdef _WIN32
 assert(VirtualAlloc(mem::ptr(a),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
 assert(mmap(mem::ptr(a),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void call_hook(Cpu*c,uint32_t f,uint32_t a){Cpu nested=*c;nested.r[3]=a;hooks.at(f)(&nested);}
bool known_address(uint32_t fn){const uint32_t known[]={0x02110D4C,0x0210DD3C,0x0210BFEC,0x021104B4,0x0219D568,0x0219AAE8,0x02199748,0x0219CE98,0x0209F918,0x0209BD1C,0x0209971C,0x0209F164,0x021CCB00,0x021C9B1C,0x021C7344,0x021CC44C,0x025D99E8,0x020402C8,0x025D9154,0x021992DC,0x025D9898,0x021C9AA0,0x025D7B24,0x020A47B4,0x021D0294,0x020A3B18,0x02041570,0x02041F94,0x02043F34,0x025D57E0};bool accepted=false;for(uint32_t expected:known)accepted|=fn==expected;return accepted;}
void original(Cpu*c,uint32_t fn){fn-=STORY_ENEMIES_TEST_CODE_DELTA;assert(known_address(fn));++calls;const uint32_t a=c->r[3];
 if(fn==0x02110D4C||fn==0x0219D568||fn==0x0209F918||fn==0x021CCB00){if(fn==0x0209F918||fn==0x021CCB00){const uint32_t flag=GD(fn==0x0209F918?0x10191468:0x101BA690);if(a!=Retail)assert(ld8(flag)==1);else assert(ld8(flag)==0);if(throw_create)throw std::runtime_error("synthetic create unwind");}++creates;c->r[3]=4;return;}
 if(fn==0x021104B4||fn==0x0219CE98||fn==0x0209F164||fn==0x021CC44C){++deletes;return;}
 if(fn==0x025D57E0){if(request_ok&&request_queued)st8(a+0xC,3);c->r[3]=request_ok?1:0;return;}
 if(fn==0x02043F34){Cpu request=*c;request.r[3]=ld32(a);request.lr=GC(drown_lr);hooks.at(0x025D57E0)(&request);c->r[3]=0x11223344;return;}
 if(fn==0x025D99E8||fn==0x025D9154){++drops;c->r[3]=123;return;}
 if(fn==0x0210DD3C||fn==0x0219AAE8||fn==0x0209BD1C||fn==0x021C9B1C){st32(GD(0x10462CF0),1234);st32(GD(0x10464B28),9876);
  if(fn==0x0209BD1C||fn==0x021C9B1C){const bool bk=fn==0x0209BD1C;st16(GD(0x10462474),bk?0x1111:0x2222);const uint32_t count=GD(bk?0x10462320:0x10465498),items=GD(bk?0x10462330:0x104654A8);st32(count,44);for(unsigned i=0;i<10;++i)st32(items+4*i,100+i);if(bk){st32(GD(0x1046231C),333);st32(GD(0x1046232C),0xAABBCCDD);}else{st8(GD(0x104654A4),66);st8(GD(0x101BA692),77);for(uint32_t off:{0x28u,0x34u,0x3Cu,0x44u,0x48u})st32(GD(0x104654E0)+off,0xABC00000u+off);}}
  if(throw_execute)throw std::runtime_error("synthetic native unwind");
  if(fn==0x0209BD1C&&nested_weapon){call_hook(c,0x021C9B1C,Moblin);assert(ld16(GD(0x10462474))==0x1111);}
  if(fn==0x0219AAE8){st32(GD(0x10464B28),0);call_hook(c,0x021992DC,Second);assert(ld32(GD(0x10464B28))==1);call_hook(c,0x021992DC,Retail);assert(ld32(GD(0x10464B28))==1);}
  if(drown_mode&&a==Bokoblin){const uint32_t h=foreign_helper?Retail+0x49C:bad_helper?a+0x500:a+0x49C;st32(h,foreign_helper?Retail:a);st16(h+8,bad_type?1:2);call_hook(c,0x02043F34,h);}
  if(terminal){if(a==Bokoblin)st16(a+0x49C+6,20);else if(a==Moblin)st16(a+0x8BC+6,20);else{st8(a+0x3FC,4);st8(a+0x3FD,51);st8(a+0x489,30);}call_hook(c,0x025D99E8,a);}
  if(freeze_terminal)call_hook(c,0x020402C8,a+(a==Actor?0x924:0xAD8));
  c->r[3]=1;return;
 }
 if(fn==0x020402C8){const uint32_t owner=ld32(a);Cpu item=*c;item.r[3]=owner+0x314;item.r[4]=ld32(owner+0x3A4);item.r[6]=owner+0x320;hooks.at(0x025D9154)(&item);st32(owner+0x39C,ld32(owner+0x39C)|0x10);c->r[3]=1;return;}
 c->r[3]=1;
}
uint32_t invoke(Cpu*c,uint32_t fn,std::initializer_list<uint32_t>args){assert(fn==GC(0x025D57E0)&&args.size()==1);c->r[3]=*args.begin();original(c,fn);c->r[4]=999;c->pc=999;return c->r[3];}
void actor(uint32_t a,uint32_t pid,story_enemies::Kind k){const auto&d=story_enemy_ownership::descriptor(k);std::memset(mem::ptr(a),0,d.bytes);st32(a+4,pid);st16(a+0xE,d.profile);st32(a+0xB4,GD(k==story_enemies::Kind::ChuChu?0x1000C5A0:k==story_enemies::Kind::Bokoblin?0x1000957C:k==story_enemies::Kind::Moblin?0x10014E9C:0x10012C24));st32(a+0xB0,story_enemies::parameters(k));st8(a+0x2DD,0xFF);st16(a+0x2D8,0xFFFF);st16(a+0x2FC,story_enemies::home_z(k));
 if(k==story_enemies::Kind::ChuChu){st8(a+0x3FB,0xFF);st32(a+0x924,a);}else if(k==story_enemies::Kind::Bokoblin){st8(a+0x3D0,4);st8(a+0x3D2,0xFF);st8(a+0x3D3,0xFF);st32(a+0x49C,a);st16(a+0x49C+8,2);st32(a+0x13BC,a);}else if(k==story_enemies::Kind::Moblin){st8(a+0x3DA,0xFF);st8(a+0x3DB,0xFF);st32(a+0x8BC,a);st16(a+0x8BC+8,1);st32(a+0x2E54,a);}else{st8(a+0x486,0xFF);st8(a+0x487,0xFF);st32(a+0xAD8,a);}
 for(size_t i=0;i<d.collider_count;++i){const auto&x=d.colliders[i];st32(a+x.object+0x44,a+x.stts);st32(a+x.stts+0xC,a);st32(a+x.stts+0x10,pid);if(x.helper)st32(a+x.helper,a);}
}
const std::pair<uint32_t,size_t> scratch_ranges[]={{0x10462320,4},{0x10462330,40},{0x1046231C,4},{0x1046232C,4},{0x10465498,4},{0x104654A8,40},{0x104654A4,1},{0x101BA692,1},{0x104654E0+0x28,0x24},{0x10462474,2}};
std::vector<uint8_t> weapon_snapshot(){std::vector<uint8_t> out;for(auto range:scratch_ranges){const auto*p=static_cast<const uint8_t*>(mem::ptr(GD(range.first)));out.insert(out.end(),p,p+range.second);}return out;}
void initialize_weapon_scratch(){for(auto range:scratch_ranges)std::memset(mem::ptr(GD(range.first)),0x5A,range.second);st16(GD(0x10462474),0x7777);}
void run(uint32_t fn,uint32_t a){Cpu c{};c.r[3]=a;c.r[4]=0x12345678;c.f[2].ps0=42;hooks.at(fn)(&c);assert(c.r[4]==0x12345678&&c.f[2].ps0==42);}
}
namespace native_actor_profiles {
void configure(void(*)(Cpu*,uint32_t),uint32_t(*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)){}
bool valid_storage(uint32_t a){return a>=Arena&&a<Arena+0x20000;}
bool available(const Owner&){return true;}bool add(uint16_t,uint32_t,BaseAllocated){return true;}bool install(const Owner&,const Replace&){return true;}
}
namespace story_items {
std::optional<Item> find(const std::string&,const std::string&){return std::nullopt;}
EnemyMarkerHandle register_enemy_marker(const std::string&,const std::string&,const std::string&,std::string&){return EnemyMarkerHandle{++marker_sequence};}
bool forget_prop_marker(PropMarkerHandle){return true;}
bool enemy_defeated(EnemyMarkerHandle h){return defeated[h.value];}
EnemyClaimStatus claim_enemy_drops(EnemyMarkerHandle h,const std::string&,const std::vector<PropDrop>&,bool){++claims;if(fail_claim)return EnemyClaimStatus::InvalidDefinition;if(defeated[h.value])return EnemyClaimStatus::AlreadyDefeated;defeated[h.value]=true;return EnemyClaimStatus::Committed;}
}
int main(int argc,char**argv){using namespace story_enemies;const bool drowning_test=argc==2&&std::strcmp(argv[1],"--drowning")==0;const bool weapons=drowning_test||(argc==2&&std::strcmp(argv[1],"--weapons")==0);
 map_memory(Actor,0x10000);map_memory(Arena,0x20000);map_memory(GD(0x101F0000),0x10000);map_memory(GD(0x10460000),0x10000);
#if STORY_ENEMIES_TEST_CODE_DELTA
 for(uint32_t canonical:{0x101F0000u,0x10460000u,0x10190000u,0x101B0000u}){map_memory(canonical,0x10000);std::memset(mem::ptr(canonical),0xA5,0x10000);}
#endif
 st32(GD(0x101F3EE0)+0xCE*4,Actor+0x8000);st32(Actor+0x8010,0xF10);st32(GD(0x101F3EE0)+0xD7*4,Actor+0x8100);st32(Actor+0x8110,0x10BC);
 assert(story_enemy_ownership::configure(Arena,story_enemy_ownership::kStorageBytes,original,verify_actor));story_enemy_ownership::initialize_storage();
 std::string why;Spec cc{Kind::ChuChu,"m","cc","cc.dead",{}, {},1},ki{Kind::Keese,"m","ki","ki.dead",{}, {},1};
 const Token first=declare(cc,why),second=declare(ki,why);assert(first&&second&&first!=second);Spec unsupported=cc;unsupported.kind=Kind::Moblin;assert(!declare(unsupported,why));
 Token bk=0,mo=0;if(weapons){assert(configure_children({[](Token,Kind,uint32_t,uint32_t epoch){assert(epoch>=1);++child_scopes;return true;},[]{assert(child_scopes);--child_scopes;},[](Token,uint32_t){return children_ready;},[](Token,uint32_t){++child_retires;}},true));Spec b{Kind::Bokoblin,"m","bk","bk.dead",{}, {},1},m{Kind::Moblin,"m","mo","mo.dead",{}, {},1};bk=declare(b,why);mo=declare(m,why);assert(bk&&mo);st32(GD(0x101F3EE0)+0xBD*4,Actor+0x8200);st32(Actor+0x8210,0x19A4);st32(GD(0x101F3EE0)+0xBC*4,Actor+0x8300);st32(Actor+0x8310,0x343C);map_memory(GD(0x101B0000),0x10000);map_memory(GD(0x10190000),0x10000);}
 configure(original,invoke,{Arena+0xB000,Arena+0xB030,Arena+0xB060,Arena+0xB090});assert(install([](uint32_t){return std::string();},[](uint32_t f,void(*fn)(Cpu*)){const uint32_t canonical=f-STORY_ENEMIES_TEST_CODE_DELTA;assert(known_address(canonical)&&!hooks.count(canonical));hooks[canonical]=fn;}));assert(hooks.size()==30);
 if(weapons){actor(Bokoblin,12,Kind::Bokoblin);actor(Moblin,13,Kind::Moblin);bind(bk,12);bind(mo,13);throw_create=true;try{run(0x0209F918,Bokoblin);assert(false);}catch(const std::runtime_error&){}throw_create=false;assert(ld8(GD(0x10191468))==0&&child_scopes==0);run(0x0209F918,Bokoblin);run(0x021CCB00,Moblin);assert(ld8(GD(0x10191468))==0&&ld8(GD(0x101BA690))==0&&child_scopes==0);const unsigned prior=calls;run(0x0209BD1C,Bokoblin);assert(calls==prior);children_ready=true;run(0x0209BD1C,Bokoblin);run(0x021C9B1C,Moblin);assert(child_scopes==0);const unsigned before_callback=calls;run(0x021C9AA0,Moblin);assert(calls==before_callback);// Exact two-byte native BK learning scratch restores through nested and exceptional calls.
 initialize_weapon_scratch();const auto saved_scratch=weapon_snapshot();nested_weapon=true;run(0x0209BD1C,Bokoblin);nested_weapon=false;assert(ld16(GD(0x10462474))==0x7777&&child_scopes==0&&weapon_snapshot()==saved_scratch);throw_execute=true;try{run(0x0209BD1C,Bokoblin);assert(false);}catch(const std::runtime_error&){}throw_execute=false;assert(ld16(GD(0x10462474))==0x7777&&child_scopes==0&&weapon_snapshot()==saved_scratch);throw_execute=true;try{run(0x021C9B1C,Moblin);assert(false);}catch(const std::runtime_error&){}throw_execute=false;assert(weapon_snapshot()==saved_scratch&&child_scopes==0);run(0x0209971C,Bokoblin);run(0x021C7344,Moblin);assert(weapon_snapshot()==saved_scratch);
 // Fixed raw params cannot authorize stale derived retail trigger/path metadata.
 st8(Bokoblin+0x3D6,1);assert(!verify_actor(bk,Kind::Bokoblin,Bokoblin));st8(Bokoblin+0x3D6,0);st8(Moblin+0x2D72,1);assert(!verify_actor(mo,Kind::Moblin,Moblin));st8(Moblin+0x2D72,0);st32(Moblin+0x2D74,Actor);assert(!verify_actor(mo,Kind::Moblin,Moblin));st32(Moblin+0x2D74,0);st8(Moblin+0x2D70,1);assert(!verify_actor(mo,Kind::Moblin,Moblin));st8(Moblin+0x2D70,0);
 // Only the selected child of the current generation may normalize native references.
 story_enemy_ownership::LineageInfo gone{};gone.role=story_enemy_ownership::Lineage::EnemyChild;gone.source=bk;gone.source_actor=Bokoblin;gone.source_pid=12;gone.epoch=1;gone.source_valid=true;gone.pid=99;
 st32(Bokoblin+0x1360,99);st32(Bokoblin+0xC90,1);st32(Bokoblin+0xC94,2);st8(Bokoblin+0x1374,1);std::array<uint8_t,0x19A4> unchanged{};std::memcpy(unchanged.data(),mem::ptr(Bokoblin),unchanged.size());gone.pid=100;child_retired(gone);assert(std::memcmp(unchanged.data(),mem::ptr(Bokoblin),unchanged.size())==0);gone.pid=99;gone.epoch=2;child_retired(gone);assert(std::memcmp(unchanged.data(),mem::ptr(Bokoblin),unchanged.size())==0);gone.epoch=1;child_retired(gone);assert(ld32(Bokoblin+0x1360)==0xFFFFFFFFu&&ld32(Bokoblin+0xC90)==0&&ld32(Bokoblin+0xC94)==0&&ld8(Bokoblin+0x1374)==0&&ld8(Bokoblin+0x1353)==0&&ld16(Bokoblin+0x49C+6)==0&&available(bk)&&claims==0);run(0x0209BD1C,Bokoblin);
 gone.source=mo;gone.source_actor=Moblin;gone.source_pid=13;gone.pid=98;st32(Moblin+0x2D60,98);st8(Moblin+0x10B0,1);st8(Moblin+0x10B1,2);st8(Moblin+0x10B3,1);child_retired(gone);assert(ld32(Moblin+0x2D60)==0xFFFFFFFFu&&ld8(Moblin+0x10B0)==1&&ld8(Moblin+0x10B1)==2&&ld8(Moblin+0x10B3)==0&&available(mo)&&claims==0);
 gone.pid=97;st32(Moblin+0x2D60,97);st8(Moblin+0x10B0,0);st8(Moblin+0x10B1,1);st8(Moblin+0x10B3,1);child_retired(gone);assert(ld32(Moblin+0x2D60)==0xFFFFFFFFu&&ld8(Moblin+0x10B0)==0&&ld8(Moblin+0x10B1)==1&&ld8(Moblin+0x10B3)==0&&available(mo)&&claims==0);run(0x021C9B1C,Moblin);
 if(drowning_test){unsigned epoch=1,pid=100;auto fresh=[&](){assert(begin_epoch(bk,++epoch));actor(Bokoblin,++pid,Kind::Bokoblin);bind(bk,pid);defeated[3]=false;claims=0;run(0x0209F918,Bokoblin);};
 drown_mode=true;drown_lr=0x02044FD0;run(0x0209BD1C,Bokoblin);assert(claims==0);fresh();drown_lr=0x02044FCC;foreign_helper=true;run(0x0209BD1C,Bokoblin);assert(claims==0);foreign_helper=false;fresh();bad_helper=true;run(0x0209BD1C,Bokoblin);assert(claims==0&&!available(bk));bad_helper=false;fresh();bad_type=true;run(0x0209BD1C,Bokoblin);assert(claims==0&&!available(bk));bad_type=false;fresh();request_ok=false;run(0x0209BD1C,Bokoblin);assert(claims==0);request_ok=true;fresh();request_queued=false;run(0x0209BD1C,Bokoblin);assert(claims==0);request_queued=true;fresh();run(0x0209BD1C,Bokoblin);assert(claims==1&&!available(bk));run(0x0209BD1C,Bokoblin);assert(claims==1);fresh();fail_claim=true;run(0x0209BD1C,Bokoblin);assert(claims==1&&!available(bk)&&!defeated[3]);fail_claim=false;fresh();drown_mode=false;run(0x0209F164,Bokoblin);assert(claims==0);fresh();claims=0;
 }
 terminal=true;run(0x0209BD1C,Bokoblin);run(0x021C9B1C,Moblin);assert(claims==2&&drops==0);terminal=false;run(0x0209F164,Bokoblin);run(0x021CC44C,Moblin);assert(child_retires==(drowning_test?3u:2u));st16(Retail+0xE,0xBD);run(0x0209F918,Retail);assert(ld8(GD(0x10191468))==0);claims=creates=deletes=0;}
 actor(Actor,10,Kind::ChuChu);actor(Second,11,Kind::Keese);bind(first,10);bind(second,11);run(0x02110D4C,Actor);run(0x0219D568,Second);assert(creates==2&&claims==0&&owns_actor(Actor)&&deny_carry(Actor));
 st32(GD(0x10462CF0),7);st32(GD(0x10464B28),9);run(0x0210DD3C,Actor);assert(ld32(GD(0x10462CF0))==7&&ld32(GD(0x10464B28))==9&&claims==0);
 throw_execute=true;try{run(0x0210DD3C,Actor);assert(false);}catch(const std::runtime_error&){}throw_execute=false;assert(ld32(GD(0x10462CF0))==7&&ld32(GD(0x10464B28))==9);
 run(0x0219AAE8,Second);assert(ld32(GD(0x10464B28))==9);
 // Drawing and ordinary deletion cannot award defeat rewards.
 run(0x0210BFEC,Actor);run(0x02199748,Second);assert(claims==0);run(0x0219CE98,Second);assert(claims==0&&deletes==1);
 terminal=true;run(0x0210DD3C,Actor);assert(claims==1&&drops==0&&!available(first));run(0x0210DD3C,Actor);assert(claims==1);terminal=false;
 // Restored snapshot reopens its own transactional marker and identity.
 defeated[1]=false;story_enemy_ownership::state_loaded();state_loaded();bind(first,10,true);freeze_terminal=true;run(0x0210DD3C,Actor);assert(claims==2&&drops==0&&!(ld32(Actor+0x39C)&0x10));freeze_terminal=false;
 // Retail paths are delegated byte-for-byte and still create their own drops.
 std::memset(mem::ptr(Retail),0,0x1800);st16(Retail+0xE,0xCE);run(0x025D99E8,Retail);assert(drops==1);
 // A forged reserved identity is denied rather than falling through to retail.
 st8(Actor+0x3F9,7);assert(!verify_actor(first,Kind::ChuChu,Actor));const unsigned before=calls;run(0x0210DD3C,Actor);assert(calls==before);
 // An obsolete generation still releases its exact native class resources.
 assert(begin_epoch(first,2));const unsigned deleted_before=deletes,claimed_before=claims;run(0x021104B4,Actor);assert(deletes==deleted_before+1&&claims==claimed_before);st32(Actor+0xB4,0x12345678);run(0x021104B4,Actor);assert(deletes==deleted_before+1&&claims==claimed_before);
 #if STORY_ENEMIES_TEST_CODE_DELTA
 assert(ld32(0x101F3EE0)==0xA5A5A5A5u&&ld16(0x10462474)==0xA5A5&&ld8(0x10191468)==0xA5);
#endif
 return 0;
}
