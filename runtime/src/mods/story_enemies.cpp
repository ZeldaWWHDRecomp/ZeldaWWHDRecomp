// SPDX-License-Identifier: MPL-2.0
#include "story_enemies.h"
#include "native_actor_profiles.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
#include <cstring>
#include <utility>
namespace story_enemies {namespace {
struct Entry {Spec spec;Token token=0;story_items::EnemyMarkerHandle marker{};uint32_t pid=0xFFFFFFFFu,address=0;bool quarantine=false,claiming=false,search_known=false;uint8_t search_mode=0;};
std::vector<Entry> entries;std::array<uint32_t,4> profiles{};
void(*original)(Cpu*,uint32_t)=nullptr;uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;
std::function<void(Token,uint32_t)> retired;bool installed=false,editor_allowed=false;ChildBridge children;bool weapon_admission=false;
thread_local Entry* executing=nullptr;thread_local Entry* ice_owner=nullptr;
struct DrowningScope {Entry*entry=nullptr;uint32_t helper=0;bool queued=false;};thread_local DrowningScope* drowning=nullptr;
const uint32_t CCcreate=GC(0x02110D4C),CCexec=GC(0x0210DD3C),CCdraw=GC(0x0210BFEC),CCdelete=GC(0x021104B4);
const uint32_t KIcreate=GC(0x0219D568),KIexec=GC(0x0219AAE8),KIdraw=GC(0x02199748),KIdelete=GC(0x0219CE98);
const uint32_t BKcreate=GC(0x0209F918),BKexec=GC(0x0209BD1C),BKdraw=GC(0x0209971C),BKdelete=GC(0x0209F164);
const uint32_t MOcreate=GC(0x021CCB00),MOexec=GC(0x021C9B1C),MOdraw=GC(0x021C7344),MOdelete=GC(0x021CC44C);
const uint32_t Disappear=GC(0x025D99E8),Ice=GC(0x020402C8),TableItem=GC(0x025D9154),KiSearch=GC(0x021992DC),RoomSearch=GC(0x025D9898);
Entry* get(Token t){for(auto& e:entries)if(e.token==t)return &e;return nullptr;}
bool memory(uint32_t a,uint32_t bytes){return !(a&3)&&a>=0x10000000u&&bytes&&a<=0x50000000u-bytes;}
bool reserved(uint32_t a){return memory(a,0x10)&&ld16(a+0xE)>=0x7FF7&&ld16(a+0xE)<=0x7FFA;}
uint32_t native_vtable(Kind k){return GD(k==Kind::ChuChu?0x1000C5A0:k==Kind::Bokoblin?0x1000957C:k==Kind::Moblin?0x10014E9C:0x10012C24);}
bool cleanup_identity(uint32_t a,uint32_t fn){for(Kind k:{Kind::ChuChu,Kind::Bokoblin,Kind::Moblin,Kind::Keese}){const auto&d=story_enemy_ownership::descriptor(k);const uint32_t destructor=k==Kind::ChuChu?CCdelete:k==Kind::Bokoblin?BKdelete:k==Kind::Moblin?MOdelete:KIdelete;if(fn==destructor&&memory(a,d.bytes)&&ld16(a+0xE)==d.profile&&ld32(a+0xB4)==native_vtable(k)&&ld32(a+0xB0)==parameters(k)&&ld8(a+0x2DD)==0xFF&&ld16(a+0x2D8)==0xFFFF){if(k==Kind::Bokoblin)st8(a+0x137D,0);if(k==Kind::Moblin)st8(a+0x2E0E,0);return true;}}return false;}
uint32_t ice_offset(Kind k){return k==Kind::ChuChu?0x924:k==Kind::Bokoblin?0x13BC:k==Kind::Moblin?0x2E54:0xAD8;}
Entry* find(uint32_t a,bool initialized=true){if(!memory(a,0x10))return nullptr;for(auto&e:entries){const auto&d=story_enemy_ownership::descriptor(e.spec.kind);if(!memory(a,d.bytes)||e.pid==0xFFFFFFFFu||ld32(a+4)!=e.pid||ld16(a+0xE)!=d.profile||ld32(a+0xB0)!=parameters(e.spec.kind)||ld8(a+0x2DD)!=0xFF)continue;if(!initialized||verify_actor(e.token,e.spec.kind,a))return &e;}return nullptr;}
void disable(Entry&e,uint32_t a){e.quarantine=true;if(a)story_enemy_ownership::retire_before_delete(a);}
void trace(const Entry&e,uint32_t a,const char*what,uint32_t value=0){LOG("[native_enemy] %s:%s event=%s actor=%08X pid=%08X profile=%04X kind=%u value=%u",e.spec.mod.c_str(),e.spec.id.c_str(),what,a,e.pid,unsigned(ld16(a+0xE)),unsigned(e.spec.kind),value);}
void no_carry(uint32_t a){st32(a+0x39C,ld32(a+0x39C)&~0x10u);st8(a+0x3A8,0xFF);st8(a+0x3A9,0);}
bool claim(Entry&e,uint32_t a){if(e.claiming)return false;e.claiming=true;struct ClaimGuard{bool&flag;~ClaimGuard(){flag=false;}}claim_guard{e.claiming};const auto result=story_items::claim_enemy_drops(e.marker,e.spec.mod,e.spec.drops);e.claiming=false;trace(e,a,"defeat-claim",unsigned(result));if(result==story_items::EnemyClaimStatus::Committed||result==story_items::EnemyClaimStatus::AlreadyDefeated)return true;disable(e,a);return false;}
// These globals are native temporary search results, not progress. Preserve them
// through exceptions and nested native execution; owned cohorts cannot leak out.
struct Scope {Entry* prior;Entry&entry;std::array<uint32_t,13> saved{};std::array<uint32_t,28> weapon_saved{};Scope(Entry&e):prior(executing),entry(e){executing=&e;saved[0]=ld32(GD(0x10462CF0));for(unsigned i=0;i<10;++i)saved[i+1]=ld32(GD(0x10462D00)+4*i);saved[11]=ld32(GD(0x10464B28));saved[12]=ld32(GD(0x10464B2C));if(e.spec.kind==Kind::Bokoblin||e.spec.kind==Kind::Moblin){const uint32_t base=GD(e.spec.kind==Kind::Bokoblin?0x10462320:0x10465498);weapon_saved[0]=ld32(base);for(unsigned i=0;i<10;++i)weapon_saved[i+1]=ld32(base+0x10+4*i);weapon_saved[11]=ld32(GD(0x1046231C));weapon_saved[12]=ld32(GD(0x1046232C));weapon_saved[13]=ld8(GD(0x104654A4));weapon_saved[14]=ld8(GD(0x101BA692));weapon_saved[20]=ld16(GD(0x10462474));const uint32_t offsets[]={0x28,0x34,0x3C,0x44,0x48};for(unsigned i=0;i<5;++i)weapon_saved[15+i]=ld32(GD(0x104654E0)+offsets[i]);if(e.search_known)st8(GD(e.spec.kind==Kind::Bokoblin?0x1046232D:0x104654A4),e.search_mode);}}~Scope(){st32(GD(0x10462CF0),saved[0]);for(unsigned i=0;i<10;++i)st32(GD(0x10462D00)+4*i,saved[i+1]);st32(GD(0x10464B28),saved[11]);st32(GD(0x10464B2C),saved[12]);if(entry.spec.kind==Kind::Bokoblin||entry.spec.kind==Kind::Moblin){const uint32_t base=GD(entry.spec.kind==Kind::Bokoblin?0x10462320:0x10465498);st32(base,weapon_saved[0]);for(unsigned i=0;i<10;++i)st32(base+0x10+4*i,weapon_saved[i+1]);st32(GD(0x1046231C),weapon_saved[11]);st32(GD(0x1046232C),weapon_saved[12]);st8(GD(0x104654A4),uint8_t(weapon_saved[13]));st8(GD(0x101BA692),uint8_t(weapon_saved[14]));st16(GD(0x10462474),uint16_t(weapon_saved[20]));const uint32_t offsets[]={0x28,0x34,0x3C,0x44,0x48};for(unsigned i=0;i<5;++i)st32(GD(0x104654E0)+offsets[i],weapon_saved[15+i]);}executing=prior;}};
void lifecycle(Cpu*c,uint32_t fn,bool create,bool draw,bool destroy){const uint32_t a=c->r[3];Entry*e=find(a,!create&&!destroy);if(!e){if(destroy&&cleanup_identity(a,fn)){story_enemy_ownership::retire_before_delete(a);original(c,fn);return;}if(reserved(a)){c->r[3]=create?5:1;return;}original(c,fn);return;}
 if(destroy){if(e->spec.kind==Kind::Bokoblin)st8(a+0x137D,0);if(e->spec.kind==Kind::Moblin)st8(a+0x2E0E,0);if(children.retire&&(e->spec.kind==Kind::Bokoblin||e->spec.kind==Kind::Moblin))children.retire(e->token,a);const uint32_t pid=e->pid;story_enemy_ownership::retire_before_delete(a);e->address=0;e->pid=0xFFFFFFFFu;trace(*e,a,"delete");original(c,fn);const Cpu result=*c;if(retired)retired(e->token,pid);*c=result;return;}
 if(!supported(e->spec.kind)||e->quarantine){c->r[3]=create?5:1;return;}
 if(!create&&story_items::enemy_defeated(e->marker)){if(!draw&&ld8(a+0xC)!=3){Cpu probe=*c;const uint32_t result=invoke(&probe,GC(0x025D57E0),{a});static unsigned reported=0;if(reported++<128)trace(*e,a,"terminal-delete-retry",result);}c->r[3]=1;return;}
 if(!create&&!draw){if(!story_enemy_ownership::lookup_actor(a)){if(!story_enemy_ownership::admit_created(e->token,a)){disable(*e,a);c->r[3]=1;return;}e->address=a;}e->address=a;
  // Carry is refused by the shared player admission hook, never undone here.
  if(ld32(a+0x2E0)&0x2000u){disable(*e,a);trace(*e,a,"unexpected-carried");c->r[3]=1;return;}no_carry(a);
 }
 struct ChildScope {bool active=false;~ChildScope(){if(active)children.leave();}} child_scope;
 if((create||!draw)&&(e->spec.kind==Kind::Bokoblin||e->spec.kind==Kind::Moblin)){if(!create&&!children.ready(e->token,a)){c->r[3]=1;return;}if(!children.enter(e->token,e->spec.kind,a,story_enemy_ownership::source_epoch(e->token))){disable(*e,a);c->r[3]=create?5:1;return;}child_scope.active=true;}
 struct HioScope{uint32_t address=0;uint8_t old=0;HioScope(bool active,Kind k){if(active){address=GD(k==Kind::Bokoblin?0x10191468:0x101BA690);old=ld8(address);st8(address,1);}}~HioScope(){if(address)st8(address,old);}} hio(create&&(e->spec.kind==Kind::Bokoblin||e->spec.kind==Kind::Moblin),e->spec.kind);
 Scope scope(*e);original(c,fn);const Cpu result=*c;
 if(create&&result.r[3]==4){if(!verify_actor(e->token,e->spec.kind,a)||(!story_enemy_ownership::lookup_actor(a)&&!story_enemy_ownership::admit_created(e->token,a))){disable(*e,a);c->r[3]=5;return;}e->address=a;if(e->spec.kind==Kind::Bokoblin||e->spec.kind==Kind::Moblin){e->search_mode=ld8(GD(e->spec.kind==Kind::Bokoblin?0x1046232D:0x104654A4));e->search_known=true;}no_carry(a);trace(*e,a,"created");}
 else if(create&&result.r[3]==5){disable(*e,a);trace(*e,a,"create-failed");}
 if(!draw&&verify_actor(e->token,e->spec.kind,a))no_carry(a);*c=result;
}
void cc_create(Cpu*c){lifecycle(c,CCcreate,true,false,false);}void cc_exec(Cpu*c){lifecycle(c,CCexec,false,false,false);}void cc_draw(Cpu*c){lifecycle(c,CCdraw,false,true,false);}void cc_delete(Cpu*c){lifecycle(c,CCdelete,false,false,true);}
void ki_create(Cpu*c){lifecycle(c,KIcreate,true,false,false);}void ki_exec(Cpu*c){lifecycle(c,KIexec,false,false,false);}void ki_draw(Cpu*c){lifecycle(c,KIdraw,false,true,false);}void ki_delete(Cpu*c){lifecycle(c,KIdelete,false,false,true);}
void bk_create(Cpu*c){lifecycle(c,BKcreate,true,false,false);}void bk_exec(Cpu*c){lifecycle(c,BKexec,false,false,false);}void bk_draw(Cpu*c){lifecycle(c,BKdraw,false,true,false);}void bk_delete(Cpu*c){lifecycle(c,BKdelete,false,false,true);}
void mo_create(Cpu*c){lifecycle(c,MOcreate,true,false,false);}void mo_exec(Cpu*c){lifecycle(c,MOexec,false,false,false);}void mo_draw(Cpu*c){lifecycle(c,MOdraw,false,true,false);}void mo_delete(Cpu*c){lifecycle(c,MOdelete,false,false,true);}
void disappear(Cpu*c){const uint32_t a=c->r[3];Entry*e=find(a);if(!e){if(reserved(a)){c->r[3]=0xFFFFFFFFu;return;}original(c,Disappear);return;}
 const bool terminal=e->spec.kind==Kind::ChuChu?(ld8(a+0x3FC)==4&&(ld8(a+0x3FD)==51||ld8(a+0x3FD)==53)):e->spec.kind==Kind::Bokoblin?ld16(a+0x49C+6)==20:e->spec.kind==Kind::Moblin?ld16(a+0x8BC+6)==20:ld8(a+0x489)==30;
 if(terminal)claim(*e,a);else {disable(*e,a);trace(*e,a,"unexpected-drop");}
 // Suppress the asynchronous retail drop actor completely: it otherwise loses
 // the source lifetime and can award native inventory after the enemy is gone.
 c->r[3]=0xFFFFFFFFu;
}
void ice(Cpu*c){const uint32_t helper=c->r[3];if(!memory(helper,4)){original(c,Ice);return;}const uint32_t a=ld32(helper);Entry*e=find(a);if(!e){if(reserved(a)){c->r[3]=0;return;}original(c,Ice);return;}
 if(executing!=e||helper!=a+ice_offset(e->spec.kind)||ld8(helper+0x1B1)!=0){disable(*e,a);c->r[3]=0;return;}
 struct Guard{Entry*old;Guard(Entry*e):old(ice_owner){ice_owner=e;}~Guard(){ice_owner=old;}} guard(e);
 original(c,Ice);const Cpu result=*c;no_carry(a);*c=result;
}
void table_item(Cpu*c){Entry*e=ice_owner;if(e&&e==executing&&e->address&&verify_actor(e->token,e->spec.kind,e->address)&&c->r[6]==e->address+0x320&&c->r[4]==ld32(e->address+0x3A4)){claim(*e,e->address);c->r[3]=0xFFFFFFFFu;return;}if(e){disable(*e,e->address);c->r[3]=0xFFFFFFFFu;return;}original(c,TableItem);}
void ki_search(Cpu*c){const uint32_t a=c->r[3];if(executing&&executing->spec.kind==Kind::Keese){Entry*e=find(a);if(e&&!e->quarantine&&e->spec.kind==Kind::Keese&&e->spec.mod==executing->spec.mod&&story_enemy_ownership::lookup_actor(a)){st32(GD(0x10464B28),ld32(GD(0x10464B28))+1);if(ld8(a+0x489)==10&&int8_t(ld8(a+0x48B))<10)st32(GD(0x10464B2C),ld32(GD(0x10464B2C))+1);}c->r[3]=0;return;}original(c,KiSearch);}
void mo_capture(Cpu*c){if(auto*e=find(c->r[3])){if(e->spec.kind==Kind::Moblin){trace(*e,c->r[3],"capture-denied");return;}}if(reserved(c->r[3]))return;original(c,GC(0x021C9AA0));}
void potential_event(Cpu*c){if(auto*e=find(c->r[3])){disable(*e,c->r[3]);trace(*e,c->r[3],"event-denied");c->r[3]=0;return;}if(reserved(c->r[3])){c->r[3]=0;return;}original(c,GC(0x025D7B24));}
void carry_drop(Cpu*c,uint32_t fn){if(auto*e=find(c->r[3])){disable(*e,c->r[3]);trace(*e,c->r[3],"carry-drop-denied");return;}if(reserved(c->r[3]))return;original(c,fn);}
void bk_carry_drop(Cpu*c){carry_drop(c,GC(0x020A47B4));}void mo_carry_drop(Cpu*c){carry_drop(c,GC(0x021D0294));}
void water_fail(Cpu*c){const uint32_t a=c->r[3];auto*e=find(a);if(!e){if(reserved(a))return;original(c,GC(0x020A3B18));return;}if(e->spec.kind!=Kind::Bokoblin||executing!=e||ld16(a+0x49C+6)!=22){disable(*e,a);return;}if(ld16(a+0x49C+4)==1&&ld16(a+0x418+6)==0)claim(*e,a);original(c,GC(0x020A3B18));}
void fire(Cpu*c){const uint32_t h=c->r[3];if(!memory(h,4)){original(c,GC(0x02041570));return;}const uint32_t a=ld32(h);auto*e=find(a);if(!e){if(reserved(a))return;original(c,GC(0x02041570));return;}const uint32_t offset=e->spec.kind==Kind::ChuChu?0xCDC:e->spec.kind==Kind::Bokoblin?0x1774:e->spec.kind==Kind::Moblin?0x320C:0xE90;if(executing!=e||h!=a+offset){disable(*e,a);return;}original(c,GC(0x02041570));}
void reaction(Cpu*c,uint32_t fn){const uint32_t h=c->r[3];if(!memory(h,4)){original(c,fn);return;}const uint32_t a=ld32(h);auto*e=find(a);if(!e){if(reserved(a)){c->r[3]=0;return;}original(c,fn);return;}const uint32_t offset=e->spec.kind==Kind::Bokoblin?0x49C:e->spec.kind==Kind::Moblin?0x8BC:0;const unsigned type=e->spec.kind==Kind::Bokoblin?2:1;if(!offset||h!=a+offset||executing!=e||ld16(h+8)!=type){disable(*e,a);c->r[3]=0;return;}if(fn==GC(0x02043F34)){DrowningScope current{e,h,false};struct Guard{DrowningScope*prior;Guard(DrowningScope*p):prior(drowning){drowning=p;}~Guard(){drowning=prior;}}guard(&current);original(c,fn);const Cpu result=*c;if(current.queued&&find(a)==e&&ld8(a+0xC)==3)claim(*e,a);*c=result;}else original(c,fn);}
void delete_request(Cpu*c){const uint32_t a=c->r[3],lr=c->lr;auto*context=drowning;Entry*e=context?context->entry:nullptr;const bool exact=e&&e==executing&&e->spec.kind==Kind::Bokoblin&&context->helper==a+0x49C&&find(a)==e&&ld32(context->helper)==a&&ld16(context->helper+8)==2&&ld8(a+0x3D4)==0&&lr==GC(0x02044FCC)&&story_enemy_ownership::lookup_actor(a)==e->token;original(c,GC(0x025D57E0));if(exact&&c->r[3]==1&&ld8(a+0xC)==3)context->queued=true;}
void damage_reaction(Cpu*c){reaction(c,GC(0x02041F94));}void damage_set(Cpu*c){reaction(c,GC(0x02043F34));}
void room_search(Cpu*c){if(executing){auto*e=find(c->r[3]);c->r[3]=e&&!e->quarantine&&e->spec.mod==executing->spec.mod?c->r[3]:0;return;}if(reserved(c->r[3])){c->r[3]=0;return;}original(c,RoomSearch);}
}
bool configure_children(ChildBridge b,bool verified){if(installed)return false;if(verified&&(!b.enter||!b.leave||!b.ready||!b.retire))return false;children=std::move(b);weapon_admission=verified;return true;}
void child_retired(const story_enemy_ownership::LineageInfo&info){if(!info.source_valid||info.role!=story_enemy_ownership::Lineage::EnemyChild)return;auto*e=get(info.source);if(!e||(e->spec.kind!=Kind::Bokoblin&&e->spec.kind!=Kind::Moblin)||e->pid!=info.source_pid||!info.source_actor||find(info.source_actor)!=e||story_enemy_ownership::lookup_actor(info.source_actor)!=e->token||story_enemy_ownership::source_epoch(e->token)!=info.epoch)return;const uint32_t a=info.source_actor,reference=e->spec.kind==Kind::Bokoblin?0x1360:0x2D60;if(ld32(a+reference)!=info.pid)return;
 if(e->spec.kind==Kind::Bokoblin){st32(a+0xC90,0);st32(a+0xC94,0);st8(a+0x1374,0);}else{st8(a+0x10B3,0);}
 st32(a+reference,0xFFFFFFFFu);trace(*e,a,"child-retired-native-loss",info.pid);}
bool supported(Kind k){return k==Kind::ChuChu||k==Kind::Keese||(weapon_admission&&(k==Kind::Bokoblin||k==Kind::Moblin));}
bool verify_actor(Token t,Kind kind,uint32_t a){const auto*e=get(t);const auto&d=story_enemy_ownership::descriptor(kind);if(!e||kind!=e->spec.kind||!supported(kind)||!memory(a,d.bytes)||ld16(a+0xE)!=d.profile||ld32(a+0xB4)!=native_vtable(kind)||ld32(a+0xB0)!=parameters(kind)||ld8(a+0x2DD)!=0xFF||ld16(a+0x2D8)!=0xFFFF||ld16(a+0x2FC)!=home_z(kind))return false;
 if(kind==Kind::ChuChu)return ld8(a+0x3F8)==0&&ld8(a+0x3F9)==0&&ld8(a+0x3FA)==0&&ld8(a+0x3FB)==0xFF&&ld8(a+0x3FC)<=4&&ld8(a+0x924+0x1B1)==0;
 if(kind==Kind::Bokoblin){const unsigned action=ld16(a+0x49C+6);return ld8(a+0x3D0)==4&&ld8(a+0x3D1)==0&&ld8(a+0x3D2)==0xFF&&ld8(a+0x3D3)==0xFF&&ld8(a+0x3D4)==0&&ld8(a+0x3D5)==0&&ld8(a+0x3D6)==0&&ld8(a+0x3F0)==0&&ld8(a+0x3F1)==0&&ld8(a+0x3F8)==0&&ld32(a+0x1378)==0&&ld8(a+0x1375)==0&&ld32(a+0x1380)==0&&ld8(a+0x138D)==0&&ld8(a+0x137D)==0&&action<=23&&action!=15&&action!=16&&ld8(a+0x13BC+0x1B1)==0;}
 if(kind==Kind::Moblin){const unsigned action=ld16(a+0x8BC+6);return ld8(a+0x3D8)==0&&ld8(a+0x3D9)==0&&ld8(a+0x3DA)==0xFF&&ld8(a+0x3DB)==0xFF&&ld8(a+0x3DC)==0&&ld8(a+0x3DD)==0&&ld8(a+0x2D70)==0&&ld8(a+0x2D72)==0&&ld32(a+0x2D74)==0&&ld16(a+0x2E4C)==0&&ld8(a+0x2E4E)==0&&ld16(a+0x2E50)==0&&ld8(a+0x2E21)==0&&ld8(a+0x2E0E)==0&&!(ld32(a+0x2E0)&0x04000000u)&&action<=23&&action!=6&&ld8(a+0x2E54+0x1B1)==0;}
 return ld8(a+0x484)==0&&ld8(a+0x485)==0&&ld8(a+0x486)==0xFF&&ld8(a+0x487)==0xFF&&ld8(a+0x48C)==0&&ld8(a+0x48D)==0&&ld32(a+0x490)==0&&(ld8(a+0x489)==0||ld8(a+0x489)==10||ld8(a+0x489)==20||ld8(a+0x489)==30)&&ld8(a+0xAB9)==0&&ld8(a+0xAD8+0x1B1)==0;
}
Token declare(const Spec&s,std::string&why){if(installed||!supported(s.kind)||s.mod.empty()||s.id.empty()||s.defeated_flag.empty()||!s.epoch||entries.size()>=128){why="Unsupported or invalid native enemy declaration";return 0;}for(const auto&e:entries)if(e.spec.mod==s.mod&&(e.spec.id==s.id||e.spec.defeated_flag==s.defeated_flag)){why="Duplicate native enemy identity or defeat flag";return 0;}if(s.drops.size()>8){why="Too many native enemy drops";return 0;}for(size_t i=0;i<s.drops.size();++i){const auto&drop=s.drops[i];if(!drop.quantity||drop.quantity>254||!story_items::find(s.mod,drop.item)){why="Unavailable same-mod enemy item";return 0;}for(size_t j=0;j<i;++j)if(s.drops[j].item==drop.item){why="Duplicate native enemy item";return 0;}}
 auto marker=story_items::register_enemy_marker(s.mod,s.id,s.defeated_flag,why);if(!marker)return 0;Token t=story_enemy_ownership::declare_actor(s.kind,parameters(s.kind),s.fingerprint,s.epoch,why);if(!t)return 0;entries.push_back({s,t,marker});return t;}
size_t checkpoint(){return entries.size();}void rollback(size_t n)noexcept{if(!installed&&n<=entries.size())entries.resize(n);}
void editor_enable(){editor_allowed=true;}
bool live(){return installed&&editor_allowed;}
Token editor_declare(const Spec&s,std::string&why){if(!live()){why="an enemy appears after a restart";return 0;}installed=false;struct Back{~Back(){installed=true;}}back;const Token t=declare(s,why);if(t)LOG("[native_enemy] %s:%s declared by the editor (token %u)",s.mod.c_str(),s.id.c_str(),t);return t;}
bool editor_redeclare(Token t,const Spec&s,std::string&why){auto*e=get(t);if(!live()||!e){why="the enemy takes this change after a restart";return false;}
 if(s.kind!=e->spec.kind||s.mod!=e->spec.mod){why="another kind of enemy is a new declaration";return false;}if(e->pid!=0xFFFFFFFFu||e->address){why="the enemy is still in the game";return false;}
 if(s.drops.size()>8){why="Too many native enemy drops";return false;}for(size_t i=0;i<s.drops.size();++i){if(!s.drops[i].quantity||s.drops[i].quantity>254||!story_items::find(s.mod,s.drops[i].item)){why="Unavailable same-mod enemy item";return false;}for(size_t j=0;j<i;++j)if(s.drops[j].item==s.drops[i].item){why="Duplicate native enemy item";return false;}}
 for(const auto&o:entries)if(o.token!=t&&o.spec.mod==s.mod&&(o.spec.id==s.id||o.spec.defeated_flag==s.defeated_flag)){why="Duplicate native enemy identity or defeat flag";return false;}
 auto marker=e->marker;
 if(s.defeated_flag!=e->spec.defeated_flag||s.id!=e->spec.id){story_items::forget_prop_marker(e->marker.value);marker=story_items::register_enemy_marker(s.mod,s.id,s.defeated_flag,why);if(!marker.value){e->marker=story_items::register_enemy_marker(e->spec.mod,e->spec.id,e->spec.defeated_flag,why);return false;}}
 if(!story_enemy_ownership::editor_refingerprint(t,s.fingerprint)){why="the enemy's ownership record is gone";return false;}
 const uint32_t epoch=e->spec.epoch;e->spec=s;e->spec.epoch=epoch;e->marker=marker;e->quarantine=false;return true;}
bool editor_refingerprint(Token t,const story_enemy_ownership::Fingerprint&fp){auto*e=get(t);if(!e||!story_enemy_ownership::editor_refingerprint(t,fp))return false;e->spec.fingerprint=fp;return true;}
void bind(Token t,uint32_t pid,bool restored){if(auto*e=get(t)){if(!restored&&!story_enemy_ownership::bind_pending(t,pid)){disable(*e,e->address);return;}e->pid=pid;e->address=0;e->quarantine=false;}}
bool available(Token t){auto*e=get(t);return e&&!e->quarantine&&!story_items::enemy_defeated(e->marker);}
void quarantine(Token t){if(auto*e=get(t))disable(*e,e->address);}
bool begin_epoch(Token t,uint32_t epoch){auto*e=get(t);if(!e||!story_enemy_ownership::begin_epoch(t,epoch))return false;e->spec.epoch=epoch;e->pid=0xFFFFFFFFu;e->address=0;e->quarantine=false;return true;}
void state_loaded(){executing=ice_owner=nullptr;drowning=nullptr;for(auto&e:entries){e.address=0;e.pid=0xFFFFFFFFu;e.quarantine=false;e.claiming=false;}}
bool owns_actor(uint32_t a){return find(a)!=nullptr;}
bool deny_carry(uint32_t a){return find(a,false)!=nullptr;}
void configure(void(*o)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>),const std::array<uint32_t,4>&storage,std::function<void(Token,uint32_t)>notify){original=o;invoke=call;profiles=storage;retired=std::move(notify);native_actor_profiles::configure(o,call);}
bool install(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(installed)return true;if((entries.empty()&&!editor_allowed)||!original||!invoke||!native_actor_profiles::available(owner))return false;
 const std::pair<uint32_t,void(*)(Cpu*)>hooks[]={{BKcreate,bk_create},{BKexec,bk_exec},{BKdraw,bk_draw},{BKdelete,bk_delete},{MOcreate,mo_create},{MOexec,mo_exec},{MOdraw,mo_draw},{MOdelete,mo_delete},{CCcreate,cc_create},{CCexec,cc_exec},{CCdraw,cc_draw},{CCdelete,cc_delete},{KIcreate,ki_create},{KIexec,ki_exec},{KIdraw,ki_draw},{KIdelete,ki_delete},{Disappear,disappear},{Ice,ice},{TableItem,table_item},{KiSearch,ki_search},{RoomSearch,room_search},{GC(0x021C9AA0),mo_capture},{GC(0x025D7B24),potential_event},{GC(0x020A47B4),bk_carry_drop},{GC(0x021D0294),mo_carry_drop},{GC(0x020A3B18),water_fail},{GC(0x02041570),fire},{GC(0x02041F94),damage_reaction},{GC(0x02043F34),damage_set},{GC(0x025D57E0),delete_request}};
 for(const auto&h:hooks)if(!owner(h.first).empty())return false;
 for(Kind k:{Kind::ChuChu,Kind::Bokoblin,Kind::Moblin,Kind::Keese}){if(!supported(k))continue;const auto&d=story_enemy_ownership::descriptor(k);uint32_t storage=profiles[unsigned(k)],native=ld32(GD(0x101F3EE0)+4*d.native_profile);if(!native_actor_profiles::valid_storage(storage)||!memory(native,0x30)||ld32(native+0x10)!=d.bytes)return false;}
 for(Kind k:{Kind::ChuChu,Kind::Bokoblin,Kind::Moblin,Kind::Keese}){if(!supported(k))continue;const auto&d=story_enemy_ownership::descriptor(k);uint32_t storage=profiles[unsigned(k)],native=ld32(GD(0x101F3EE0)+4*d.native_profile);std::memcpy(mem::ptr(storage),mem::ptr(native),0x30);st16(storage+8,d.profile);if(!native_actor_profiles::add(d.profile,storage))return false;}
 if(!native_actor_profiles::install(owner,replace))return false;for(const auto&h:hooks)replace(h.first,h.second);installed=true;return true;
}
}
