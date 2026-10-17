// SPDX-License-Identifier: MPL-2.0
#include "story_enemy_ownership.h"
#include "runtime.h"
#include "guest_addr.h"
#include <algorithm>
#include <vector>
#include <utility>
namespace story_enemy_ownership { namespace {
// Audited native class fields, including shared ice/fire helpers. helper is the
// embedded response object whose first field must point back to the same actor.
constexpr Collider cc[]={{0x7C4,0x788,0},{0x990,0x954,0x924},{0xDB8,0xD7C,0xCDC}};
constexpr Collider bk[]={{0xCE8,0xBF4,0},{0xE18,0xBF4,0},{0xF48,0xBF4,0},{0x1074,0xBF4,0},{0x11A0,0xBF4,0},{0x1428,0x13EC,0x13BC},{0x1850,0x1814,0x1774}};
constexpr Collider mo[]={{0x2494,0x1014,0},{0x25C4,0x1014,0},{0x26F4,0x1014,0},{0x2820,0x1014,0},{0x294C,0x1014,0},{0x2A78,0x1014,0},{0x2EC0,0x2E84,0x2E54},{0x32E8,0x32AC,0x320C}};
constexpr Collider ki[]={{0x734,0x6F8,0},{0x860,0x6F8,0},{0x98C,0x6F8,0},{0xB44,0xB08,0xAD8},{0xF6C,0xF30,0xE90}};
constexpr Descriptor descriptors[]={{0x7FFA,0xCE,0xF10,cc,3},{0x7FF9,0xBD,0x19A4,bk,7},{0x7FF8,0xBC,0x343C,mo,8},{0x7FF7,0xD7,0x10BC,ki,5}};
struct Entry {Kind kind;uint32_t params,epoch;Fingerprint fingerprint;Token token;uint32_t pid=0,address=0;bool active=false,quarantined=false;};
std::vector<Entry> entries;Token next_token=1;uint32_t arena=0,link=0,link_pid=0;void(*original)(Cpu*,uint32_t)=nullptr;bool installed=false;VerifyActor verify_actor;ProjectileVerifier projectile_verifier;bool projectile_enabled=false;ModColliderVerifier mod_collider_verifier;
constexpr uint32_t Magic=0x454F5731,RetiredMagic=0x454F5752,PendingMagic=0x454F5750,Version=2,ActorOffset=256,LineageOffset=ActorOffset+128*128;
const uint32_t NoAtTg=GC(0x0200CE78);
bool memory(uint32_t a,uint32_t n){return !(a&3)&&n&&a>=0x10000000u&&a<=0x50000000u-n;}
bool runtime_memory(uint32_t a,uint32_t n){return !(a&3)&&n&&a>=mem::kRuntimeStart&&a<=mem::kHostStart-n;}
Entry* get(Token t){for(auto& e:entries)if(e.token==t)return &e;return nullptr;}
size_t index(const Entry& e){return size_t(&e-entries.data());}
uint32_t record(const Entry& e){return arena+ActorOffset+uint32_t(index(e))*128;}
uint32_t checksum(uint32_t a,uint32_t n){uint32_t h=2166136261u;for(uint32_t i=0;i<n;++i)h=(h^ld8(a+i))*16777619u;return h;}
bool reserved(uint16_t p){return p>=0x7FF7&&p<=0x7FFA;}
bool identity(const Entry& e,uint32_t a){auto& d=descriptors[unsigned(e.kind)];return memory(a,d.bytes)&&e.pid&&e.pid!=0xFFFFFFFFu&&ld32(a+4)==e.pid&&ld16(a+0xE)==d.profile&&ld32(a+0xB0)==e.params&&ld8(a+0x2DD)==0xFF;}
bool checked(Entry&e,uint32_t a){if(!identity(e,a)||!verify_actor){if(e.active)e.quarantined=true;return false;}try{if(verify_actor(e.token,e.kind,a))return true;}catch(...){}e.quarantined=true;return false;}
bool valid(Entry& e){return e.active&&!e.quarantined&&e.address&&checked(e,e.address);}
void write_record(const Entry& e){uint32_t r=record(e);for(uint32_t i=0;i<128;++i)st8(r+i,0);st32(r,Magic);st32(r+4,Version);st32(r+8,e.token);st32(r+12,unsigned(e.kind));st32(r+16,e.params);st32(r+20,e.epoch);st32(r+24,e.pid);st32(r+28,e.address);for(unsigned i=0;i<32;++i)st8(r+32+i,e.fingerprint[i]);st32(r+124,checksum(r,124));}
void write_pending(const Entry&e){write_record(e);uint32_t r=record(e);st32(r,PendingMagic);st32(r+124,checksum(r,124));}
bool definition_matches(const Entry& e,bool inactive){uint32_t r=record(e);uint32_t magic=ld32(r);if((magic!=Magic&&(!inactive||(magic!=RetiredMagic&&magic!=PendingMagic)))||ld32(r+4)!=Version||ld32(r+8)!=e.token||ld32(r+12)!=unsigned(e.kind)||ld32(r+16)!=e.params||!ld32(r+20)||ld32(r+124)!=checksum(r,124))return false;for(unsigned i=0;i<32;++i)if(ld8(r+32+i)!=e.fingerprint[i])return false;return true;}
bool stored(const Entry&e){return definition_matches(e,false);}
bool object_valid(Entry& e,const Collider& c){uint32_t a=e.address,o=a+c.object,s=a+c.stts;return valid(e)&&memory(o,0x48)&&ld32(o+0x44)==s&&ld32(s+0xC)==a&&ld32(s+0x10)==e.pid&&(!c.helper||ld32(a+c.helper)==a);}
bool link_object(uint32_t o){
 if(!memory(link,0x8260)||ld32(link+4)!=link_pid||ld16(link+0xE)!=0xA8)return false;
 // d_a_player_main.h and native Link SetStts route: all use mStts7620.
 constexpr uint32_t offsets[]={0x765C,0x778C,0x78BC,0x79EC,0x7B1C,0x7C54,0x7D8C,0x7EC4,0x7FFC,0x8128};
 for(uint32_t x:offsets)if(o==link+x)return ld32(o+0x44)==link+0x7620&&ld32(link+0x762C)==link&&ld32(link+0x7630)==link_pid;
 return false;
}
bool suspected_owned(uint32_t o){
 // Object-map misses do not confer ownership. This bounded owner read only
 // recognizes a reserved actor to deny it while its registration is absent.
 for(const auto&e:entries)if(e.address&&identity(e,e.address)&&o>=e.address&&o<e.address+descriptor(e.kind).bytes)return true;
 if(!memory(o,0x48))return false;uint32_t s=ld32(o+0x44);if(!memory(s,0x14))return false;uint32_t a=ld32(s+0xC);return memory(a,0x10)&&reserved(ld16(a+0xE));
}
bool owned_child_object(uint32_t object){
 if(arena)for(size_t n=0;n<kChildCapacity;++n){uint32_t r=arena+LineageOffset+uint32_t(n)*64;if(ld32(r)!=Magic||ld32(r+8)!=unsigned(Lineage::EnemyChild))continue;uint32_t a=ld32(r+40),pid=ld32(r+20);if(pid&&pid!=0xFFFFFFFFu&&memory(a,0xA34)&&ld32(a+4)==pid&&ld16(a+0xE)==0x1CF&&object>=a&&object<a+0xA34)return true;}
 // A damaged saved address must not erase a claimed child. The actual native
 // Stts owner and PID confer denial only; a recycled actor with a new PID passes.
 if(memory(object,0x48)){uint32_t stts=ld32(object+0x44);if(memory(stts,0x14)){uint32_t a=ld32(stts+0xC);if(memory(a,0xA34)&&ld16(a+0xE)==0x1CF&&stts==a+0x6C4&&(object==a+0x700||object==a+0x82C)&&ld32(stts+0x10)==ld32(a+4)&&lineage_claimed(ld32(a+4),Lineage::EnemyChild))return true;}}
 return false;
}
bool fire_child(uint32_t object,int16_t& timer){
 if(!memory(object,0x48))return false;uint32_t stts=ld32(object+0x44);if(!memory(stts,0x14))return false;uint32_t a=ld32(stts+0xC);LineageInfo info{};
 if(!memory(a,0xA34)||object!=a+0x700||stts!=a+0x6C4||!lookup_lineage_actor(a,Lineage::EnemyChild,info)||!info.source_valid||info.profile!=0x1CF||info.parameters!=0||ld32(stts+0x10)!=info.pid||ld8(a+0x2DD)!=0xFF||ld32(a+0x2E8)!=0xFFFFFFFFu||ld32(a+0xB4)!=GD(0x1000A478)||ld8(a+0x430)>2||ld8(a+0x431)||ld8(a+0x432)||ld32(object+0x10)!=0x20000||ld8(object+0x14)!=0)return false;
 auto*source=get(info.source);if(!source||source->kind!=Kind::Bokoblin||source->params!=0xFFFF0004u||!valid(*source)||source->address!=info.source_actor||source->pid!=info.source_pid||source->epoch!=info.epoch)return false;
 timer=int16_t(ld16(a+0x43C));return timer>=0&&timer<=900;
}
bool ignition_source(uint32_t object){if(!memory(object,0x48))return false;if(link_object(object))return true;if(projectile_enabled&&projectile_verifier)try{return projectile_verifier(object);}catch(...){}return false;}
void hook(Cpu*c){if(pair_policy(c->r[4],c->r[5])==PairPolicy::Reject)c->r[3]=1;else original(c,NoAtTg);}
}
const Descriptor& descriptor(Kind k){static constexpr Descriptor invalid={0,0,0,nullptr,0};return unsigned(k)<4?descriptors[unsigned(k)]:invalid;}
bool configure(uint32_t a,uint32_t bytes,void(*fn)(Cpu*,uint32_t),VerifyActor verify){if(bytes<kStorageBytes||!runtime_memory(a,kStorageBytes)||!fn||!verify)return false;arena=a;original=fn;verify_actor=std::move(verify);return true;}
void initialize_storage(){if(!arena)return;for(uint32_t i=0;i<kStorageBytes;++i)st8(arena+i,0);st32(arena,Magic);st32(arena+4,Version);for(const auto&e:entries)write_pending(e);}
Token declare_actor(Kind k,uint32_t params,const Fingerprint& fp,uint32_t epoch,std::string& why){if(unsigned(k)>=4||entries.size()>=kActorCapacity||!epoch||!next_token){why="Invalid or full enemy ownership declaration";return 0;}Token t=next_token++;entries.push_back({k,params,epoch,fp,t});if(arena&&ld32(arena)==Magic&&ld32(arena+4)==Version)write_pending(entries.back());return t;}
size_t checkpoint(){return entries.size();}
void rollback(size_t n)noexcept{if(n<=entries.size())entries.resize(n);}
bool binding_state(Token token,Binding& out){out={};auto*e=get(token);if(!e)return false;out.epoch=e->epoch;if(!arena||ld32(arena)!=Magic||ld32(arena+4)!=Version)return true;out.state=BindingState::Quarantined;if(!definition_matches(*e,true))return true;uint32_t r=record(*e);if(ld32(r)==RetiredMagic){out.state=BindingState::Retired;return true;}if(e->quarantined)return true;out.pid=e->pid;out.actor=e->address;if(e->active){if(valid(*e))out.state=BindingState::Active;return true;}if(ld32(r)==PendingMagic){if(!e->pid&&!e->address)out.state=BindingState::Fresh;else if(e->pid&&e->pid!=0xFFFFFFFFu)out.state=BindingState::Queued;}return true;}
bool can_spawn(Token token){Binding state;return binding_state(token,state)&&state.state==BindingState::Fresh;}
bool bind_pending(Token t,uint32_t pid){auto*e=get(t);if(!e||e->active||e->quarantined||!pid||pid==0xFFFFFFFFu||(e->pid&&e->pid!=pid))return false;for(auto& x:entries)if(x.token!=t&&x.pid==pid)return false;e->pid=pid;if(arena)write_pending(*e);return true;}
bool admit_created(Token t,uint32_t a){auto*e=get(t);if(!arena||!e||e->active||e->quarantined||!checked(*e,a)||(e->address&&e->address!=a))return false;e->address=a;e->active=true;write_record(*e);return true;}
bool enroll_colliders(Token t){auto*e=get(t);if(!e||!valid(*e))return false;auto&d=descriptor(e->kind);for(size_t i=0;i<d.collider_count;++i)if(object_valid(*e,d.colliders[i]))return true;return false;}
Token lookup_actor(uint32_t a){for(auto&e:entries)if(e.address==a&&valid(e))return e.token;return 0;}
Token lookup_collider(uint32_t o){for(auto&e:entries)if(valid(e)){auto&d=descriptor(e.kind);for(size_t i=0;i<d.collider_count;++i)if(o==e.address+d.colliders[i].object&&object_valid(e,d.colliders[i]))return e.token;}return 0;}
void retire_before_delete(uint32_t a){for(auto&e:entries)if(e.address==a&&identity(e,a)){e.active=false;e.address=0;e.pid=0;if(arena){uint32_t r=record(e);st32(r,RetiredMagic);st32(r+124,checksum(r,124));}}}
void invalidate_epoch(uint32_t epoch){if(arena){st32(arena+8,0);st32(arena+12,0);for(size_t i=kChildCapacity;i<kChildCapacity+kProjectileCapacity;++i)st32(arena+LineageOffset+uint32_t(i)*64,0);}for(auto&e:entries)if(e.epoch!=epoch){e.active=false;e.quarantined=true;}link=link_pid=0;}
bool begin_epoch(Token t,uint32_t epoch){auto*e=get(t);if(!e||!epoch||epoch==e->epoch)return false;e->epoch=epoch;e->active=false;e->quarantined=false;e->pid=e->address=0;if(arena)write_pending(*e);return true;}
void state_loaded(){link=link_pid=0;for(auto&e:entries){e.active=false;e.address=0;e.pid=0;e.quarantined=true;if(!arena||ld32(arena)!=Magic||ld32(arena+4)!=Version||!definition_matches(e,true))continue;uint32_t r=record(e);e.epoch=ld32(r+20);uint32_t magic=ld32(r);if(magic==RetiredMagic)continue;e.pid=ld32(r+24);uint32_t a=ld32(r+28);if(magic==PendingMagic){if(!e.pid&&!a){e.quarantined=false;continue;}if(e.pid&&e.pid!=0xFFFFFFFFu&&(!a||identity(e,a))){e.address=a;e.quarantined=false;}continue;}if(checked(e,a)){e.address=a;e.active=true;e.quarantined=false;}}}
bool set_link(uint32_t a,uint32_t pid){if(!memory(a,0x8260)||!pid||pid==0xFFFFFFFFu||ld32(a+4)!=pid||ld16(a+0xE)!=0xA8)return false;if(arena){if(ld32(arena+8)!=a||ld32(arena+12)!=pid){uint32_t generation=ld32(arena+16)+1;if(!generation)return false;st32(arena+8,a);st32(arena+12,pid);st32(arena+16,generation);}}link=a;link_pid=pid;return true;}
uint32_t link_generation(){return arena&&link&&ld32(arena+8)==link&&ld32(arena+12)==link_pid?ld32(arena+16):0;}
void retire_link(uint32_t a,uint32_t pid){if(link==a&&link_pid==pid){link=link_pid=0;if(arena){st32(arena+8,0);st32(arena+12,0);}}}
void set_projectile_verifier(ProjectileVerifier verifier,bool enabled){projectile_verifier=std::move(verifier);projectile_enabled=enabled;}
void set_mod_collider_verifier(ModColliderVerifier verifier){mod_collider_verifier=std::move(verifier);}
PairPolicy pair_policy(uint32_t at,uint32_t tg){
 if(mod_collider_verifier){
  ModCollider a=ModCollider::Invalid,t=ModCollider::Invalid;
  try{a=mod_collider_verifier(at);t=mod_collider_verifier(tg);}catch(...){return PairPolicy::Reject;}
  if(a!=ModCollider::Foreign||t!=ModCollider::Foreign){
   if(a==ModCollider::Attack&&t==ModCollider::Foreign&&link_object(tg))return PairPolicy::Delegate;
   if(t==ModCollider::Body&&a==ModCollider::Foreign){
    if(link_object(at))return PairPolicy::Delegate;
    if(projectile_enabled&&projectile_verifier)try{if(projectile_verifier(at))return PairPolicy::Delegate;}catch(...){}
   }
   return PairPolicy::Reject;
  }
 }
const bool child_at=owned_child_object(at),child_tg=owned_child_object(tg);if(child_at||child_tg){if(child_at&&child_tg)return PairPolicy::Reject;int16_t timer=0;if(child_tg&&fire_child(tg,timer)&&timer==0&&ignition_source(at)&&(ld32(at+0x10)&0x20200u))return PairPolicy::Delegate;if(child_at&&fire_child(at,timer)&&timer>0&&(ld32(at)&1u)&&link_object(tg))return PairPolicy::Delegate;return PairPolicy::Reject;}Token a=lookup_collider(at),t=lookup_collider(tg);bool ao=a||suspected_owned(at),to=t||suspected_owned(tg);if(!ao&&!to)return PairPolicy::Delegate;if(ao&&to)return PairPolicy::Reject;if(a&&link_object(tg))return PairPolicy::Delegate;if(t&&link_object(at))return PairPolicy::Delegate;if(t&&projectile_enabled&&projectile_verifier){try{if(projectile_verifier(at))return PairPolicy::Delegate;}catch(...){}}return PairPolicy::Reject;}
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace){if(installed)return true;if(!arena||!original||!owner(NoAtTg).empty())return false;replace(NoAtTg,hook);installed=true;return true;}
bool queue_impl(bool pending,Lineage role,Token source,uint32_t a,uint32_t pid,uint32_t queued,uint16_t profile,uint32_t params,uint32_t epoch){if(!arena||(!queued&&!pending)||queued==0xFFFFFFFFu||!epoch||!profile)return false;bool child=role==Lineage::EnemyChild;if(child){auto*e=get(source);if(!e||e->quarantined||e->pid!=pid||e->epoch!=epoch||!identity(*e,a)||(e->address&&e->address!=a)||(!pending&&!valid(*e)))return false;if(!definition_matches(*e,true)){uint32_t saved=e->address;e->address=a;write_record(*e);e->address=saved;uint32_t r=record(*e);st32(r,PendingMagic);st32(r+124,checksum(r,124));}}else if(role!=Lineage::LinkProjectile||a!=link||pid!=link_pid||!set_link(a,pid))return false;size_t begin=child?0:kChildCapacity,end=child?kChildCapacity:kChildCapacity+kProjectileCapacity;uint32_t free=0;for(size_t i=0;i<kChildCapacity+kProjectileCapacity;++i){uint32_t r=arena+LineageOffset+uint32_t(i)*64;if(queued&&ld32(r)==Magic&&ld32(r+20)==queued)return false;if(i>=begin&&i<end&&!free&&ld32(r)!=Magic)free=r;}if(!free)return false;for(unsigned i=0;i<64;++i)st8(free+i,0);st32(free,Magic);st32(free+4,Version);st32(free+8,unsigned(role));st32(free+12,source);st32(free+16,pid);st32(free+20,queued);st32(free+24,a);st32(free+28,profile);st32(free+32,params);st32(free+36,epoch);uint32_t generation=ld32(arena+24)+1;if(!generation){st32(free,0);return false;}st32(arena+24,generation);st32(free+44,generation);st32(free+52,child?0:link_generation());st32(free+56,1);st32(free+60,checksum(free,60));return true;}
uint32_t source_epoch(Token token){auto*e=get(token);return e?e->epoch:0;}
bool queue_lineage(Lineage role,Token source,uint32_t a,uint32_t pid,uint32_t queued,uint16_t profile,uint32_t params,uint32_t epoch){return queue_impl(false,role,source,a,pid,queued,profile,params,epoch);}
uint32_t reserve_child_pending(Token source,uint32_t a,uint32_t pid,uint16_t profile,uint32_t params,uint32_t epoch){if(!queue_impl(true,Lineage::EnemyChild,source,a,pid,0,profile,params,epoch))return 0;return ld32(arena+24);}
bool commit_child_pending(uint32_t ticket,uint32_t pid){if(!arena||!ticket||!pid||pid==0xFFFFFFFFu)return false;for(size_t i=0;i<kChildCapacity+kProjectileCapacity;++i){uint32_t r=arena+LineageOffset+uint32_t(i)*64;if(ld32(r)==Magic&&ld32(r+20)==pid)return false;}for(size_t i=0;i<kChildCapacity;++i){uint32_t r=arena+LineageOffset+uint32_t(i)*64;if(ld32(r)==Magic&&ld32(r+8)==unsigned(Lineage::EnemyChild)&&ld32(r+44)==ticket&&!ld32(r+20)&&ld32(r+60)==checksum(r,60)){st32(r+20,pid);st32(r+60,checksum(r,60));return true;}}return false;}
void cancel_child_pending(uint32_t ticket){if(!arena||!ticket)return;for(size_t i=0;i<kChildCapacity;++i){uint32_t r=arena+LineageOffset+uint32_t(i)*64;if(ld32(r)==Magic&&ld32(r+44)==ticket&&!ld32(r+20))st32(r,0);}}
bool queue_child_pending(Token source,uint32_t a,uint32_t pid,uint32_t queued,uint16_t profile,uint32_t params,uint32_t epoch){return queue_impl(true,Lineage::EnemyChild,source,a,pid,queued,profile,params,epoch);}
bool lineage_claimed(uint32_t pid,Lineage role){if(!arena||!pid||pid==0xFFFFFFFFu)return false;for(size_t i=0;i<kChildCapacity+kProjectileCapacity;++i){uint32_t r=arena+LineageOffset+uint32_t(i)*64;if(ld32(r)==Magic&&ld32(r+8)==unsigned(role)&&ld32(r+20)==pid)return true;}return false;}
bool lineage_pending(uint32_t pid,Lineage role){if(!arena)return false;for(size_t i=0;i<kChildCapacity+kProjectileCapacity;++i){uint32_t r=arena+LineageOffset+uint32_t(i)*64;if(ld32(r)==Magic&&ld32(r+4)==Version&&ld32(r+8)==unsigned(role)&&ld32(r+20)==pid&&ld32(r+60)==checksum(r,60))return true;}return false;}
bool lineage_info(uint32_t pid,LineageInfo& out){if(!arena||!pid||pid==0xFFFFFFFFu)return false;for(size_t i=0;i<kChildCapacity+kProjectileCapacity;++i){uint32_t r=arena+LineageOffset+uint32_t(i)*64;if(ld32(r)!=Magic||ld32(r+4)!=Version||ld32(r+20)!=pid||ld32(r+60)!=checksum(r,60))continue;uint32_t role=ld32(r+8);if(role!=unsigned(Lineage::EnemyChild)&&role!=unsigned(Lineage::LinkProjectile))return false;out={Lineage(role),ld32(r+12),ld32(r+24),ld32(r+16),pid,ld32(r+40),ld32(r+28),ld32(r+32),ld32(r+36),ld32(r+44),ld32(r+52),true};if(out.role==Lineage::EnemyChild){auto*e=get(out.source);out.source_valid=e&&definition_matches(*e,true);}return true;}return false;}
bool lookup_lineage_actor(uint32_t a,Lineage role,LineageInfo& out){if(!memory(a,0x2EC)||!lineage_info(ld32(a+4),out)||out.role!=role||!out.source_valid||out.address!=a||out.profile!=ld16(a+0xE)||out.parameters!=ld32(a+0xB0))return false;return true;}
bool admit_lineage(uint32_t pid,uint32_t a){LineageInfo info{};if(!lineage_info(pid,info)||!info.source_valid||!memory(a,0x2EC)||ld32(a+4)!=pid||ld16(a+0xE)!=info.profile||ld32(a+0xB0)!=info.parameters)return false;for(size_t i=0;i<kChildCapacity+kProjectileCapacity;++i){uint32_t r=arena+LineageOffset+uint32_t(i)*64;if(ld32(r)==Magic&&ld32(r+20)==pid){if(ld32(r+40)&&ld32(r+40)!=a)return false;st32(r+40,a);st32(r+56,2);st32(r+60,checksum(r,60));return true;}}return false;}
void visit_lineage(const std::function<void(const LineageInfo&)>& visitor){if(!arena||!visitor)return;for(size_t i=0;i<kChildCapacity+kProjectileCapacity;++i){uint32_t r=arena+LineageOffset+uint32_t(i)*64;if(ld32(r)!=Magic)continue;LineageInfo info{};if(lineage_info(ld32(r+20),info))visitor(info);}}
void retire_lineage(uint32_t pid){if(arena)for(size_t i=0;i<kChildCapacity+kProjectileCapacity;++i){uint32_t r=arena+LineageOffset+uint32_t(i)*64;if(ld32(r)==Magic&&ld32(r+20)==pid)st32(r,0);}}
}
