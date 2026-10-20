// SPDX-License-Identifier: MPL-2.0
#include "native_actor_refusal.h"
#include "story_enemy_children.h"
#include "story_enemy_variants.h"
#include "story_enemies_more.h"
#include "native_actor_carry.h"
#include "native_actor_profiles.h"
#include "guest_addr.h"
#include "runtime.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <utility>
namespace story_enemy_children { namespace {
namespace own=story_enemy_ownership;
struct Source {Token token=0;Kind kind=Kind::Bokoblin;uint32_t actor=0,pid=0,epoch=0;};
thread_local std::array<Source,8> scopes{};thread_local unsigned depth=0;
thread_local Source search{};
void(*original)(Cpu*,uint32_t)=nullptr;
uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;
Retired retired;bool installed=false;
// a rat's bomb (d_a_bomb daBomb_Create): admitted as the rat's child when it is constructed
const uint32_t BombCreate=GC(0x020C685C),BombDelete=GC(0x020C51BC);
const uint32_t Queue=GC(0x025D5834),Create=GC(0x020C25A4),Execute=GC(0x020C1D0C),Delete=GC(0x020C1D84),Wait=GC(0x020C25A8),BkSearch=GC(0x0209AB64),MoSearch=GC(0x021C872C),BkCandidate=GC(0x0209A624),MoCandidate=GC(0x021C826C);
// The Darknut: search_wepon (its weapon view check) and s_w_sub (its candidate filter); its cape, d_a_mant:
// daMant_Create and daMant_Execute.
const uint32_t TnSearch=GC(0x024BAFAC),TnCandidate=GC(0x024BAA8C),CapeCreate=GC(0x021BE134),CapeExecute=GC(0x021BC3C4);
constexpr uint16_t Sword=0x1CF,Cape=own::kCapeProfile,Bomb=0x126;
bool memory(uint32_t a,uint32_t n){return !(a&3)&&a>=0x10000000u&&a<=0x50000000u-n;}
// a Stalfos is a parent kind too: its club is a Boko child (parameter 2) like a Bokoblin's weapon; a
// Wizzrobe's children are its fireballs (queue_fireball); a Big Octo's scope only refuses its whirlpool
// (its eyes are story_enemies' owned children)
// a bomb rat (d_a_nz type 1, hole-born arg0 1): the one rat that creates a child (its bomb)
bool bomb_rat(uint32_t params){return (params&0xFF)==1&&((params>>8)&0xFF)==1;}
bool parent(const Source&s){if(s.kind==Kind::Rat){auto&d=own::descriptor(s.kind);return s.token&&s.epoch==own::source_epoch(s.token)&&memory(s.actor,d.bytes)&&ld32(s.actor+4)==s.pid&&s.pid&&s.pid!=0xFFFFFFFFu&&ld16(s.actor+0xE)==d.profile&&bomb_rat(ld32(s.actor+0xB0))&&ld8(s.actor+0x2DD)==0xFF;}
 if(s.kind!=Kind::Bokoblin&&s.kind!=Kind::Moblin&&s.kind!=Kind::Darknut&&s.kind!=Kind::Stalfos&&s.kind!=Kind::Wizzrobe&&s.kind!=Kind::BigOcto)return false;auto&d=own::descriptor(s.kind);return s.token&&s.epoch==own::source_epoch(s.token)&&memory(s.actor,d.bytes)&&ld32(s.actor+4)==s.pid&&s.pid&&s.pid!=0xFFFFFFFFu&&ld16(s.actor+0xE)==d.profile&&(s.kind==Kind::Bokoblin?story_enemy_variants::declared_params("bokoblin",ld32(s.actor+0xB0)):s.kind==Kind::Darknut?story_enemy_variants::declared_params("darknut",ld32(s.actor+0xB0)):s.kind==Kind::BigOcto?story_enemy_variants::declared_params("big_octo",ld32(s.actor+0xB0)):s.kind==Kind::Wizzrobe?story_enemy_variants::declared_params("wizzrobe",ld32(s.actor+0xB0)):s.kind==Kind::Stalfos?ld32(s.actor+0xB0)==story_enemies_more::params(s.kind):(ld32(s.actor+0xB0)==0xFFFF0000u||ld32(s.actor+0xB0)==story_enemy_variants::moblin_patrol(0xFFFF0000u)||ld32(s.actor+0xB0)==story_enemy_variants::kRiderParams))&&ld8(s.actor+0x2DD)==0xFF;}
// the child kinds: daBoko weapons (the Bokoblin's stick 0 or machete 1, the Stalfos's club 2, the Moblin's
// spear 4, the Darknut's sword 3) and the Darknut's cape (d_a_mant type 0)
uint32_t child_bytes(uint16_t profile){return profile==Sword?0xA34:profile==Cape?own::kCapeBytes:0;}
bool child_kind(uint16_t profile,uint32_t params){return profile==Sword?(params==0||params==1||params==2||params==3||params==4):profile==Cape&&params==0;}
bool claimed(uint32_t a){if(!memory(a,0x10))return false;const uint16_t profile=ld16(a+0xE);return child_bytes(profile)&&memory(a,child_bytes(profile))&&own::lineage_claimed(ld32(a+4),own::Lineage::EnemyChild);}
bool child(uint32_t a,own::LineageInfo& info,bool admitted){if(!memory(a,0x10))return false;const uint16_t profile=ld16(a+0xE);if(!child_bytes(profile)||!memory(a,child_bytes(profile))||!own::lineage_info(ld32(a+4),info)||info.role!=own::Lineage::EnemyChild||!info.source_valid||info.epoch!=own::source_epoch(info.source)||info.profile!=profile||!child_kind(profile,info.parameters)||ld32(a+0xB0)!=info.parameters||ld8(a+0x2DD)!=0xFF||ld32(a+0x2E8)!=0xFFFFFFFFu)return false;return admitted?info.address==a:(!info.address||info.address==a);}
// A Darknut's child is made only while its Darknut is the live one that asked for it: one whose Darknut was
// removed, defeated or unloaded before the child was made is refused (no leftover sword or cape).
bool source_live(const own::LineageInfo& info){own::Binding b{};return own::binding_state(info.source,b)&&b.state==own::BindingState::Active&&b.actor==info.source_actor&&b.pid==info.source_pid;}
bool darknut_child(const own::LineageInfo& info){return info.profile==Cape||(info.profile==Sword&&info.parameters==3);}
void mask(uint32_t a){st32(a+0x39C,ld32(a+0x39C)&~0x10u);st32(a+0x2E0,ld32(a+0x2E0)&~(0x400u|0x80000u|0x200000u));}
// the child a parent may ask for: its variant's weapon, a caped Darknut's cape, a bomb rat's bomb
bool expected(const Source&s,uint16_t profile,uint32_t params){
 if(s.kind==Kind::Rat)return profile==Bomb;
 if(s.kind==Kind::Darknut)return profile==Sword?params==3:profile==Cape&&params==0&&((story_enemy_variants::angle_x("darknut",ld32(s.actor+0xB0))>>5)&7)>=4;
 return profile==Sword&&params==(s.kind==Kind::Bokoblin?story_enemy_variants::bokoblin_weapon(ld32(s.actor+0xB0)):s.kind==Kind::Stalfos?story_enemies_more::kStalfosClubParams:4u);}
std::function<bool(uint32_t,uint32_t)> create_filter;
// A Wizzrobe's fireball (its own WZ class, the game's profile 0xD0, story_enemies_more.h): queued in its
// owned parent's scope, it becomes an owned child (ownership lineage)
void queue_fireball(Cpu*c,const Source&s){if(!parent(s)||c->r[4]!=story_enemies_more::kWizzrobeFireParams){c->r[3]=0xFFFFFFFFu;return;}
 const uint32_t ticket=own::reserve_child_pending(s.token,s.actor,s.pid,story_enemies_more::kWizzrobeFire,c->r[4],s.epoch);if(!ticket){c->r[3]=0xFFFFFFFFu;return;}
 try{original(c,Queue);}catch(...){own::cancel_child_pending(ticket);throw;}
 const uint32_t pid=c->r[3];if(!pid||pid==0xFFFFFFFFu){own::cancel_child_pending(ticket);return;}
 if(!own::commit_child_pending(ticket,pid))throw std::runtime_error("Native enemy fireball PID could not bind its reserved ownership record");
}
std::function<bool(Cpu*)> summon_filter;
void queue(Cpu*c){
 if(summon_filter&&summon_filter(c))return;  // a summoner's door: its summons are the summoner's pool
 // an owned Big Octo creates nothing this way but its whirlpool (Obj_Auzu, which pulls the boat): refused
 if(depth&&scopes[depth-1].kind==Kind::BigOcto){c->r[3]=0xFFFFFFFFu;return;}
 if(depth&&scopes[depth-1].kind==Kind::Wizzrobe){if(c->r[3]==story_enemies_more::kWizzrobeFire){queue_fireball(c,scopes[depth-1]);return;}original(c,Queue);return;}
 // an owned Stalfos's split (a second native Stalfos, the upper body) is refused: story_enemies turns it into the ordinary collapse
 if(depth&&scopes[depth-1].kind==Kind::Stalfos&&c->r[3]==story_enemies_more::kStalfosUpperBody){c->r[3]=0xFFFFFFFFu;return;}
 if(create_filter&&create_filter(c->r[3],c->r[4])){c->r[3]=0xFFFFFFFFu;return;}if(!depth||(c->r[3]!=Sword&&!(c->r[3]==Cape&&scopes[depth-1].kind==Kind::Darknut)&&!(c->r[3]==Bomb&&scopes[depth-1].kind==Kind::Rat))){original(c,Queue);return;}const auto s=scopes[depth-1];const uint16_t profile=uint16_t(c->r[3]);const uint32_t params=c->r[4];if(!parent(s)||!expected(s,profile,params)){c->r[3]=0xFFFFFFFFu;return;}
 const uint32_t ticket=own::reserve_child_pending(s.token,s.actor,s.pid,profile,params,s.epoch);if(!ticket){c->r[3]=0xFFFFFFFFu;return;}
 try{original(c,Queue);}catch(...){own::cancel_child_pending(ticket);throw;}
 const uint32_t pid=c->r[3];if(!pid||pid==0xFFFFFFFFu){own::cancel_child_pending(ticket);return;}
 if(!own::commit_child_pending(ticket,pid))throw std::runtime_error("Native enemy child PID could not bind its reserved ownership record");
}
void make(Cpu*c,uint32_t fn){const uint32_t a=c->r[3];own::LineageInfo info{};if(!child(a,info,false)){if(claimed(a)){native_actor_refusal::refuse(c,a,invoke);return;}original(c,fn);return;}
 if(darknut_child(info)&&!info.address&&!source_live(info)){native_actor_refusal::refuse(c,a,invoke);return;}
 original(c,fn);const Cpu result=*c;if(result.r[3]==4){if(!own::admit_lineage(info.pid,a))throw std::runtime_error("Native enemy child constructor ownership mismatch");if(info.profile==Sword)mask(a);}*c=result;}
void create(Cpu*c){make(c,Create);}
void cape_create(Cpu*c){make(c,CapeCreate);}
// a claimed cape runs only while it is the admitted child of its Darknut
void cape_execute(Cpu*c){const uint32_t a=c->r[3];own::LineageInfo info{};if(!child(a,info,true)&&claimed(a)){c->r[3]=1;return;}original(c,CapeExecute);}
void execute(Cpu*c){const uint32_t a=c->r[3];own::LineageInfo info{};if(!child(a,info,false)){if(claimed(a)){c->r[3]=1;return;}original(c,Execute);return;}if(info.address!=a){c->r[3]=1;return;}mask(a);original(c,Execute);const Cpu result=*c;own::LineageInfo live{};if(own::lineage_info(info.pid,live)&&live.address==a&&live.source_valid&&child(a,live,true))mask(a);*c=result;}
void wait(Cpu*c){const uint32_t a=c->r[3];own::LineageInfo info{};const bool mine=child(a,info,false);if(!mine&&claimed(a)){c->r[3]=1;return;}original(c,Wait);const Cpu result=*c;own::LineageInfo live{};if(mine&&own::lineage_info(info.pid,live)&&live.address==a&&live.source_valid&&child(a,live,true))mask(a);*c=result;}
void before_delete(uint32_t a,uint32_t pid,uint16_t profile) noexcept {
 if((child_bytes(profile)||profile==Bomb||profile==story_enemies_more::kWizzrobeFire||profile==story_enemies_more::kGy||profile==story_enemies_more::kOctoEye)&&own::lineage_claimed(pid,own::Lineage::EnemyChild)){
  own::LineageInfo info{};const bool exact=own::lineage_info(pid,info)&&(!info.address||info.address==a);
  if(exact){own::retire_lineage(pid);static const bool trace=[]{const char*v=std::getenv("WWHD_ENEMY_TRACE");return v&&*v=='1';}();if(trace)LOG("[native_enemy] child retired pid=%08X profile=%04X params=%u source=%u",pid,unsigned(profile),info.parameters,info.source);if(info.source_valid&&retired)try{retired(info);}catch(...){}}
 }
 if(profile==0x7FF9||profile==0x7FF8||profile==0x7FF1||profile==0x7FF2||profile==own::descriptor(Kind::Stalfos).profile||profile==own::descriptor(Kind::Wizzrobe).profile||profile==own::descriptor(Kind::BigOcto).profile){for(unsigned i=0;i<depth;++i)if(scopes[i].actor==a&&scopes[i].pid==pid)scopes[i]={};if(search.actor==a&&search.pid==pid)search={};}
}
// a rat's bomb: admitted at construction like a weapon; a claimed bomb whose record does not match is never built
void bomb_create(Cpu*c){const uint32_t a=c->r[3];own::LineageInfo info{};const bool mine=memory(a,0xB50)&&own::lineage_info(ld32(a+4),info)&&info.role==own::Lineage::EnemyChild&&info.profile==0x126&&info.source_valid;
 if(!mine){if(memory(a,0x10)&&own::lineage_claimed(ld32(a+4),own::Lineage::EnemyChild)){native_actor_refusal::refuse(c,a,invoke);return;}original(c,BombCreate);return;}
 original(c,BombCreate);const Cpu result=*c;if(result.r[3]==4&&!own::admit_lineage(info.pid,a))throw std::runtime_error("Native enemy bomb ownership mismatch");*c=result;}
void destroy(Cpu*c){const uint32_t a=c->r[3];if(memory(a,0xA34))before_delete(a,ld32(a+4),ld16(a+0xE));if(native_actor_refusal::base_only(a)){c->r[3]=1;return;}original(c,Delete);}
// a rat's bomb leaves its lineage record when it is deleted (its blast is over, or its room unloads)
void bomb_destroy(Cpu*c){const uint32_t a=c->r[3];if(memory(a,0xB50)&&ld16(a+0xE)==0x126)before_delete(a,ld32(a+4),0x126);if(native_actor_refusal::base_only(a)){c->r[3]=1;return;}original(c,BombDelete);}
void candidate(Cpu*c,uint32_t fn,Kind kind){const uint32_t a=c->r[3];own::LineageInfo info{};const bool mine=child(a,info,true);if(search.token){if(search.kind!=kind||!parent(search)||!mine||info.source!=search.token||info.source_actor!=search.actor||info.source_pid!=search.pid||info.epoch!=search.epoch){c->r[3]=0;return;}}else if(mine||claimed(a)){c->r[3]=0;return;}original(c,fn);}
void bk_candidate(Cpu*c){candidate(c,BkCandidate,Kind::Bokoblin);}void mo_candidate(Cpu*c){candidate(c,MoCandidate,Kind::Moblin);}void tn_candidate(Cpu*c){candidate(c,TnCandidate,Kind::Darknut);}
void search_weapon(Cpu*c,uint32_t fn,Kind kind){const uint32_t a=c->r[3];Source source{};source.token=own::lookup_actor(a);source.kind=kind;source.actor=a;if(source.token){source.pid=ld32(a+4);source.epoch=own::source_epoch(source.token);if(!parent(source)){c->r[3]=0;return;}}
 else if(memory(a,0x10)&&(ld16(a+0xE)==0x7FF9||ld16(a+0xE)==0x7FF8||ld16(a+0xE)==0x7FF1)){c->r[3]=0;return;}
 const Source prior=search;search=source;const uint32_t count=GD(kind==Kind::Bokoblin?0x10462320:kind==Kind::Darknut?0x1046E718:0x10465498),items=GD(kind==Kind::Bokoblin?0x10462330:kind==Kind::Darknut?0x1046E740:0x104654A8);std::array<uint8_t,44> saved{};
 if(source.token){std::memcpy(saved.data(),mem::ptr(count),4);std::memcpy(saved.data()+4,mem::ptr(items),40);}
 try{original(c,fn);}catch(...){if(source.token){std::memcpy(mem::ptr(count),saved.data(),4);std::memcpy(mem::ptr(items),saved.data()+4,40);}search=prior;throw;}
 if(source.token){std::memcpy(mem::ptr(count),saved.data(),4);std::memcpy(mem::ptr(items),saved.data()+4,40);}search=prior;
}
void bk_search(Cpu*c){search_weapon(c,BkSearch,Kind::Bokoblin);}void mo_search(Cpu*c){search_weapon(c,MoSearch,Kind::Moblin);}void tn_search(Cpu*c){search_weapon(c,TnSearch,Kind::Darknut);}
}
bool enter(Token t,Kind kind,uint32_t a,uint32_t epoch){if(depth==scopes.size()||!memory(a,0x10))return false;Source s{t,kind,a,ld32(a+4),epoch};if(!parent(s))return false;scopes[depth++]=s;return true;}
void leave(){if(depth)scopes[--depth]={};}
bool owned(uint32_t a){own::LineageInfo info{};if(memory(a,0x10)&&ld16(a+0xE)==Bomb&&own::lineage_claimed(ld32(a+4),own::Lineage::EnemyChild))return true;return child(a,info,true)||claimed(a);}
bool ready(Token token,uint32_t a){if(!installed||own::lookup_actor(a)!=token)return false;const uint16_t profile=ld16(a+0xE);if(profile==0x7FF2)return true;  // a rat's bomb is looked after by its lineage
 // a Darknut runs once the sword it refers to (mBokoPcId, -1: none) and its cape (mMantPcId, 0xFFFF: none)
 // are its own admitted children
 if(profile==0x7FF1){auto admitted=[&](uint32_t pid,uint16_t want){own::LineageInfo info{};return own::lineage_info(pid,info)&&info.role==own::Lineage::EnemyChild&&info.source_valid&&info.source==token&&info.source_actor==a&&info.source_pid==ld32(a+4)&&info.epoch==own::source_epoch(token)&&info.address&&info.profile==want&&child(info.address,info,true);};
  const uint32_t sword=ld32(a+0x1560),cape=ld32(a+0x1598);return (sword==0xFFFFFFFFu||admitted(sword,Sword))&&(cape==0xFFFFu||cape==0xFFFFFFFFu||admitted(cape,Cape));}
 const bool bk=profile==0x7FF9;if(!bk&&profile!=0x7FF8)return false;const uint32_t pid=ld32(a+(bk?0x1360:0x2D60));
 if(!pid||pid==0xFFFFFFFFu)return bk?(!ld32(a+0xC90)&&!ld32(a+0xC94)&&!ld8(a+0x1374)):(!ld8(a+0x10B3)&&ld8(a+0x10B0)<=1&&ld8(a+0x10B1)<=2);
 own::LineageInfo info{};return own::lineage_info(pid,info)&&info.role==own::Lineage::EnemyChild&&info.source_valid&&info.source==token&&info.source_actor==a&&info.source_pid==ld32(a+4)&&info.epoch==own::source_epoch(token)&&info.address&&child(info.address,info,true);
}
void retire(Token token,uint32_t a){for(unsigned i=0;i<depth;++i)if(scopes[i].token==token&&scopes[i].actor==a)scopes[i]={};if(search.token==token&&search.actor==a)search={};}
bool configure(void(*o)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>),Retired notify){if(!o||!call||(original&&(original!=o||invoke!=call)))return false;original=o;invoke=call;retired=std::move(notify);return native_actor_carry::configure(o,call)&&native_actor_carry::register_guard("enemy-children",owned)&&native_actor_profiles::add_before_delete_listener("enemy-children",before_delete);}
bool available(const std::function<std::string(uint32_t)>&owner){if(!original||!invoke||!native_actor_carry::available(owner))return false;if(installed)return true;for(uint32_t f:{Queue,Create,Execute,Delete,Wait,BkSearch,MoSearch,BkCandidate,MoCandidate,TnSearch,TnCandidate,CapeCreate,CapeExecute,BombCreate,BombDelete})if(!owner(f).empty())return false;return true;}
bool install(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(installed)return true;if(!available(owner))return false;const std::pair<uint32_t,void(*)(Cpu*)> hooks[]={{Queue,queue},{Create,create},{Execute,execute},{Delete,destroy},{Wait,wait},{BkSearch,bk_search},{MoSearch,mo_search},{BkCandidate,bk_candidate},{MoCandidate,mo_candidate},{TnSearch,tn_search},{TnCandidate,tn_candidate},{CapeCreate,cape_create},{CapeExecute,cape_execute},{BombCreate,bomb_create},{BombDelete,bomb_destroy}};if(!native_actor_carry::install(owner,replace))return false;for(const auto&h:hooks)replace(h.first,h.second);installed=true;return true;}
void state_loaded(){depth=0;search={};}
void set_create_filter(std::function<bool(uint32_t,uint32_t)> deny){create_filter=std::move(deny);}
void set_summon_filter(std::function<bool(Cpu*)> summon){summon_filter=std::move(summon);}
}
