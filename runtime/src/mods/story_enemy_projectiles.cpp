// SPDX-License-Identifier: MPL-2.0
#include "story_enemy_projectiles.h"
#include "story_enemy_ownership.h"
#include "native_actor_profiles.h"
#include "runtime.h"
#include "guest_addr.h"
#include <utility>
namespace story_enemy_projectiles { namespace {
using namespace story_enemy_ownership;
// Source class layouts and native SetStts sites are recorded in the private
// source-audit receipt. Values are fixed native attack semantics, not resources.
constexpr Descriptor descriptors[]={
 {0x1D8,0x81C,0,0x4B8,0x47C,0,0},
 {0x126,0xB50,0x80000003,0x920,0x8E4,0x20,4},
 {0x1BE,0x2D68,0,0x2550,0x2514,0x08000000,1},
 {0xA9,0xD7B0,0,0xD630,0xD5F4,0x8000,1},
 {0x1B0,0x264C8,0,0x262C8,0x2628C,0x40,1}};
void(*original)(Cpu*,uint32_t)=nullptr;CurrentLink current_link;bool enabled=false,installed=false;
const uint32_t MakeItem=GC(0x023DF600),MakeArrow=GC(0x023EB120),ChangeArrow=GC(0x0205584C);
bool memory(uint32_t a,uint32_t n){return n&&!(a&3)&&a>=0x10000000u&&a<=0x50000000u-n;}
bool current(uint32_t& a,uint32_t& pid){try{return current_link&&current_link(a,pid)&&memory(a,0x8260)&&pid&&pid!=0xFFFFFFFFu&&ld32(a+4)==pid&&ld16(a+0xE)==0xA8;}catch(...){return false;}}
bool root_matches(const LineageInfo&i){uint32_t a=0,p=0;return current(a,p)&&a==i.source_actor&&p==i.source_pid&&i.root_generation&&i.root_generation==link_generation();}
bool actor_valid(Kind kind,uint32_t a,uint32_t pid){const auto&d=descriptor(kind);if(!d.bytes||!memory(a,d.bytes)||!pid||pid==0xFFFFFFFFu||ld32(a+4)!=pid||ld16(a+0xE)!=d.profile||ld32(a+0xB0)!=d.parameters||ld32(a+0x2E8)!=0xFFFFFFFFu)return false;if(kind==Kind::Arrow&&(ld8(a+0x3AC)||ld8(a+0x71D)>3))return false;uint32_t s=a+d.stts;return ld32(a+d.object+0x44)==s&&ld32(s+0xC)==a&&ld32(s+0x10)==pid;}
bool capture(Kind kind,uint32_t a,uint32_t pid,uint32_t projectile,uint32_t keep_pid){uint32_t actual=0,actual_pid=0;if(!current(actual,actual_pid)||actual!=a||actual_pid!=pid||!actor_valid(kind,projectile,keep_pid)||!set_link(a,pid))return false;LineageInfo existing{};if(lineage_info(keep_pid,existing))return existing.role==Lineage::LinkProjectile&&existing.address==projectile&&root_matches(existing)&&existing.source==unsigned(kind);if(!queue_lineage(Lineage::LinkProjectile,unsigned(kind),a,pid,keep_pid,descriptor(kind).profile,descriptor(kind).parameters,link_generation()))return false;if(!admit_lineage(keep_pid,projectile)){retire_lineage(keep_pid);return false;}return true;}
void make_item(Cpu*c){uint32_t a=c->r[3],actual=0,pid=0;const bool own=current(actual,pid)&&actual==a;uint16_t item=own?ld16(a+0x69B0):0;uint32_t keep=0;Kind kind=Kind::Arrow;if(item==0x25){kind=Kind::Grapple;keep=0x6590;}else if(item==0x2F){kind=Kind::Hookshot;keep=0x6590;}else if(item==0x2D){kind=Kind::Boomerang;keep=0x6590;}else if(item==0x31){kind=Kind::Bomb;keep=0x65A0;}uint32_t old=own&&keep?ld32(a+keep+4):0,old_pid=own&&keep?ld32(a+keep):0;original(c,MakeItem);if(own&&keep){uint32_t pointer=ld32(a+keep+4),id=ld32(a+keep);if(pointer!=old||id!=old_pid)capture(kind,a,pid,pointer,id);}}
void make_arrow(Cpu*c){uint32_t a=c->r[3],actual=0,pid=0;bool own=current(actual,pid)&&actual==a;uint32_t old=own?ld32(a+0x6594):0,id=own?ld32(a+0x6590):0;original(c,MakeArrow);if(own&&(old!=ld32(a+0x6594)||id!=ld32(a+0x6590)))capture(Kind::Arrow,a,pid,ld32(a+0x6594),ld32(a+0x6590));}
void change_arrow(Cpu*c){uint32_t old=c->r[3];LineageInfo saved{};bool inherited=lookup_lineage_actor(old,Lineage::LinkProjectile,saved)&&saved.source==unsigned(Kind::Arrow)&&root_matches(saved)&&actor_valid(Kind::Arrow,old,saved.pid);original(c,ChangeArrow);uint32_t replacement=c->r[3];if(inherited&&replacement!=old&&memory(replacement,0x81C))capture(Kind::Arrow,saved.source_actor,saved.source_pid,replacement,ld32(replacement+4));}
}
const Descriptor& descriptor(Kind k){static constexpr Descriptor invalid{};unsigned i=unsigned(k);return i>=1&&i<=5?descriptors[i-1]:invalid;}
bool configure(void(*fn)(Cpu*,uint32_t),CurrentLink read,bool combat){if(!fn||!read||!native_actor_profiles::add_before_delete_listener("sdk-enemy-projectiles",before_delete))return false;original=fn;current_link=std::move(read);enabled=combat;set_projectile_verifier(owns_attack,enabled);return true;}
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace){if(installed)return true;if(!original||!current_link)return false;for(uint32_t f:{MakeItem,MakeArrow,ChangeArrow})if(!owner(f).empty())return false;replace(MakeItem,make_item);replace(MakeArrow,make_arrow);replace(ChangeArrow,change_arrow);installed=true;return true;}
bool owns_attack(uint32_t object){if(!memory(object,0x48))return false;uint32_t s=ld32(object+0x44);if(!memory(s,0x14))return false;uint32_t a=ld32(s+0xC);LineageInfo i{};if(!lookup_lineage_actor(a,Lineage::LinkProjectile,i)||!root_matches(i)||i.source<1||i.source>5)return false;Kind kind=Kind(i.source);auto&d=descriptor(kind);if(object!=a+d.object||s!=a+d.stts||!actor_valid(kind,a,i.pid))return false;uint32_t type=d.attack_type;uint8_t power=d.power;if(kind==Kind::Arrow){uint32_t row=GD(0x100073AC)+uint32_t(ld8(a+0x71D))*8;type=ld32(row);power=ld8(row+4);}return ld32(object+0x10)==type&&ld8(object+0x14)==power;}
void before_delete(uint32_t a,uint32_t pid,uint16_t profile) noexcept{LineageInfo i{};if(lineage_info(pid,i)&&i.role==Lineage::LinkProjectile&&i.address==a&&i.profile==profile)retire_lineage(pid);if(profile==0xA8)retire_link(a,pid);}
void state_loaded(){uint32_t a=0,p=0;if(current(a,p))set_link(a,p);visit_lineage([](const LineageInfo&i){if(i.role!=Lineage::LinkProjectile)return;if(i.source<1||i.source>5||!root_matches(i)||!actor_valid(Kind(i.source),i.address,i.pid))retire_lineage(i.pid);});}
void room_changed(){visit_lineage([](const LineageInfo&i){if(i.role==Lineage::LinkProjectile)retire_lineage(i.pid);});}
}
