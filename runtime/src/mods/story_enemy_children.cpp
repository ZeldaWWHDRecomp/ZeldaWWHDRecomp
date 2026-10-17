// SPDX-License-Identifier: MPL-2.0
#include "story_enemy_children.h"
#include "native_actor_carry.h"
#include "native_actor_profiles.h"
#include "guest_addr.h"
#include "runtime.h"
#include <array>
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
const uint32_t Queue=GC(0x025D5834),Create=GC(0x020C25A4),Execute=GC(0x020C1D0C),Delete=GC(0x020C1D84),Wait=GC(0x020C25A8),BkSearch=GC(0x0209AB64),MoSearch=GC(0x021C872C),BkCandidate=GC(0x0209A624),MoCandidate=GC(0x021C826C);
bool memory(uint32_t a,uint32_t n){return !(a&3)&&a>=0x10000000u&&a<=0x50000000u-n;}
bool parent(const Source&s){if(s.kind!=Kind::Bokoblin&&s.kind!=Kind::Moblin)return false;auto&d=own::descriptor(s.kind);return s.token&&s.epoch==own::source_epoch(s.token)&&memory(s.actor,d.bytes)&&ld32(s.actor+4)==s.pid&&s.pid&&s.pid!=0xFFFFFFFFu&&ld16(s.actor+0xE)==d.profile&&ld32(s.actor+0xB0)==(s.kind==Kind::Bokoblin?0xFFFF0004u:0xFFFF0000u)&&ld8(s.actor+0x2DD)==0xFF;}
bool claimed(uint32_t a){return memory(a,0xA34)&&ld16(a+0xE)==0x1CF&&own::lineage_claimed(ld32(a+4),own::Lineage::EnemyChild);}
bool child(uint32_t a,own::LineageInfo& info,bool admitted){if(!memory(a,0xA34)||ld16(a+0xE)!=0x1CF||!own::lineage_info(ld32(a+4),info)||info.role!=own::Lineage::EnemyChild||!info.source_valid||info.epoch!=own::source_epoch(info.source)||info.profile!=0x1CF||(info.parameters!=0&&info.parameters!=4)||ld32(a+0xB0)!=info.parameters||ld8(a+0x2DD)!=0xFF||ld32(a+0x2E8)!=0xFFFFFFFFu)return false;return admitted?info.address==a:(!info.address||info.address==a);}
void mask(uint32_t a){st32(a+0x39C,ld32(a+0x39C)&~0x10u);st32(a+0x2E0,ld32(a+0x2E0)&~(0x400u|0x80000u|0x200000u));}
void queue(Cpu*c){if(!depth||c->r[3]!=0x1CF){original(c,Queue);return;}const auto s=scopes[depth-1];const uint32_t params=c->r[4];if(!parent(s)||params!=(s.kind==Kind::Bokoblin?0u:4u)){c->r[3]=0xFFFFFFFFu;return;}
 const uint32_t ticket=own::reserve_child_pending(s.token,s.actor,s.pid,0x1CF,params,s.epoch);if(!ticket){c->r[3]=0xFFFFFFFFu;return;}
 try{original(c,Queue);}catch(...){own::cancel_child_pending(ticket);throw;}
 const uint32_t pid=c->r[3];if(!pid||pid==0xFFFFFFFFu){own::cancel_child_pending(ticket);return;}
 if(!own::commit_child_pending(ticket,pid))throw std::runtime_error("Native enemy child PID could not bind its reserved ownership record");
}
void create(Cpu*c){const uint32_t a=c->r[3];own::LineageInfo info{};if(!child(a,info,false)){if(claimed(a)){c->r[3]=5;return;}original(c,Create);return;}original(c,Create);const Cpu result=*c;if(result.r[3]==4){if(!own::admit_lineage(info.pid,a))throw std::runtime_error("Native enemy child constructor ownership mismatch");mask(a);}*c=result;}
void execute(Cpu*c){const uint32_t a=c->r[3];own::LineageInfo info{};if(!child(a,info,false)){if(claimed(a)){c->r[3]=1;return;}original(c,Execute);return;}if(info.address!=a){c->r[3]=1;return;}mask(a);original(c,Execute);const Cpu result=*c;own::LineageInfo live{};if(own::lineage_info(info.pid,live)&&live.address==a&&live.source_valid&&child(a,live,true))mask(a);*c=result;}
void wait(Cpu*c){const uint32_t a=c->r[3];own::LineageInfo info{};const bool mine=child(a,info,false);if(!mine&&claimed(a)){c->r[3]=1;return;}original(c,Wait);const Cpu result=*c;own::LineageInfo live{};if(mine&&own::lineage_info(info.pid,live)&&live.address==a&&live.source_valid&&child(a,live,true))mask(a);*c=result;}
void before_delete(uint32_t a,uint32_t pid,uint16_t profile) noexcept {
 if(profile==0x1CF&&own::lineage_claimed(pid,own::Lineage::EnemyChild)){
  own::LineageInfo info{};const bool exact=own::lineage_info(pid,info)&&(!info.address||info.address==a);
  if(exact){own::retire_lineage(pid);if(info.source_valid&&retired)try{retired(info);}catch(...){}}
 }
 if(profile==0x7FF9||profile==0x7FF8){for(unsigned i=0;i<depth;++i)if(scopes[i].actor==a&&scopes[i].pid==pid)scopes[i]={};if(search.actor==a&&search.pid==pid)search={};}
}
void destroy(Cpu*c){const uint32_t a=c->r[3];if(memory(a,0xA34))before_delete(a,ld32(a+4),ld16(a+0xE));original(c,Delete);}
void candidate(Cpu*c,uint32_t fn,Kind kind){const uint32_t a=c->r[3];own::LineageInfo info{};const bool mine=child(a,info,true);if(search.token){if(search.kind!=kind||!parent(search)||!mine||info.source!=search.token||info.source_actor!=search.actor||info.source_pid!=search.pid||info.epoch!=search.epoch){c->r[3]=0;return;}}else if(mine||claimed(a)){c->r[3]=0;return;}original(c,fn);}
void bk_candidate(Cpu*c){candidate(c,BkCandidate,Kind::Bokoblin);}void mo_candidate(Cpu*c){candidate(c,MoCandidate,Kind::Moblin);}
void search_weapon(Cpu*c,uint32_t fn,Kind kind){const uint32_t a=c->r[3];Source source{};source.token=own::lookup_actor(a);source.kind=kind;source.actor=a;if(source.token){source.pid=ld32(a+4);source.epoch=own::source_epoch(source.token);if(!parent(source)){c->r[3]=0;return;}}
 else if(memory(a,0x10)&&(ld16(a+0xE)==0x7FF9||ld16(a+0xE)==0x7FF8)){c->r[3]=0;return;}
 const Source prior=search;search=source;const uint32_t count=GD(kind==Kind::Bokoblin?0x10462320:0x10465498),items=GD(kind==Kind::Bokoblin?0x10462330:0x104654A8);std::array<uint8_t,44> saved{};
 if(source.token){std::memcpy(saved.data(),mem::ptr(count),4);std::memcpy(saved.data()+4,mem::ptr(items),40);}
 try{original(c,fn);}catch(...){if(source.token){std::memcpy(mem::ptr(count),saved.data(),4);std::memcpy(mem::ptr(items),saved.data()+4,40);}search=prior;throw;}
 if(source.token){std::memcpy(mem::ptr(count),saved.data(),4);std::memcpy(mem::ptr(items),saved.data()+4,40);}search=prior;
}
void bk_search(Cpu*c){search_weapon(c,BkSearch,Kind::Bokoblin);}void mo_search(Cpu*c){search_weapon(c,MoSearch,Kind::Moblin);}
}
bool enter(Token t,Kind kind,uint32_t a,uint32_t epoch){if(depth==scopes.size()||!memory(a,0x10))return false;Source s{t,kind,a,ld32(a+4),epoch};if(!parent(s))return false;scopes[depth++]=s;return true;}
void leave(){if(depth)scopes[--depth]={};}
bool owned(uint32_t a){own::LineageInfo info{};return child(a,info,true)||claimed(a);}
bool ready(Token token,uint32_t a){if(!installed||own::lookup_actor(a)!=token)return false;const uint16_t profile=ld16(a+0xE);const bool bk=profile==0x7FF9;if(!bk&&profile!=0x7FF8)return false;const uint32_t pid=ld32(a+(bk?0x1360:0x2D60));
 if(!pid||pid==0xFFFFFFFFu)return bk?(!ld32(a+0xC90)&&!ld32(a+0xC94)&&!ld8(a+0x1374)):(!ld8(a+0x10B3)&&ld8(a+0x10B0)<=1&&ld8(a+0x10B1)<=2);
 own::LineageInfo info{};return own::lineage_info(pid,info)&&info.role==own::Lineage::EnemyChild&&info.source_valid&&info.source==token&&info.source_actor==a&&info.source_pid==ld32(a+4)&&info.epoch==own::source_epoch(token)&&info.address&&child(info.address,info,true);
}
void retire(Token token,uint32_t a){for(unsigned i=0;i<depth;++i)if(scopes[i].token==token&&scopes[i].actor==a)scopes[i]={};if(search.token==token&&search.actor==a)search={};}
bool configure(void(*o)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>),Retired notify){if(!o||!call||(original&&(original!=o||invoke!=call)))return false;original=o;invoke=call;retired=std::move(notify);return native_actor_carry::configure(o,call)&&native_actor_carry::register_guard("enemy-children",owned)&&native_actor_profiles::add_before_delete_listener("enemy-children",before_delete);}
bool available(const std::function<std::string(uint32_t)>&owner){if(!original||!invoke||!native_actor_carry::available(owner))return false;if(installed)return true;for(uint32_t f:{Queue,Create,Execute,Delete,Wait,BkSearch,MoSearch,BkCandidate,MoCandidate})if(!owner(f).empty())return false;return true;}
bool install(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(installed)return true;if(!available(owner))return false;const std::pair<uint32_t,void(*)(Cpu*)> hooks[]={{Queue,queue},{Create,create},{Execute,execute},{Delete,destroy},{Wait,wait},{BkSearch,bk_search},{MoSearch,mo_search},{BkCandidate,bk_candidate},{MoCandidate,mo_candidate}};if(!native_actor_carry::install(owner,replace))return false;for(const auto&h:hooks)replace(h.first,h.second);installed=true;return true;}
void state_loaded(){depth=0;search={};}
}
