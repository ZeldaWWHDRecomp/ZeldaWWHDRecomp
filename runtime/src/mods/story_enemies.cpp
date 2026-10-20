// SPDX-License-Identifier: MPL-2.0
#include "native_actor_refusal.h"
#include "story_enemies.h"
#include "story_enemy_variants.h"
#include "native_actor_profiles.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>
namespace story_enemies {namespace {
namespace more=story_enemies_more;
struct Entry {Spec spec;Token token=0;story_items::EnemyMarkerHandle marker{};uint32_t pid=0xFFFFFFFFu,address=0;bool transient_defeated=false;bool quarantine=false,claiming=false,search_known=false,creating=false;uint8_t search_mode=0;};
std::vector<Entry> entries;std::array<uint32_t,story_enemy_ownership::kKindCount> profiles{};
void(*original)(Cpu*,uint32_t)=nullptr;uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;
std::function<void(Token,uint32_t)> retired;bool installed=false,editor_allowed=false;ChildBridge children;bool weapon_admission=false;
thread_local Entry* executing=nullptr;thread_local Entry* ice_owner=nullptr;thread_local uint32_t executing_actor=0;  // the actor `executing` runs on (also during its create)
struct DrowningScope {Entry*entry=nullptr;uint32_t helper=0;bool queued=false;};thread_local DrowningScope* drowning=nullptr;
const uint32_t CCcreate=GC(0x02110D4C),CCexec=GC(0x0210DD3C),CCdraw=GC(0x0210BFEC),CCdelete=GC(0x021104B4);
const uint32_t KIcreate=GC(0x0219D568),KIexec=GC(0x0219AAE8),KIdraw=GC(0x02199748),KIdelete=GC(0x0219CE98);
const uint32_t BKcreate=GC(0x0209F918),BKexec=GC(0x0209BD1C),BKdraw=GC(0x0209971C),BKdelete=GC(0x0209F164);
const uint32_t MOcreate=GC(0x021CCB00),MOexec=GC(0x021C9B1C),MOdraw=GC(0x021C7344),MOdelete=GC(0x021CC44C);
// d_a_pt (Miniblin): daPt_Create, daPt_Execute, daPt_Draw, daPt_Delete
const uint32_t PTcreate=GC(0x02446E54),PTexec=GC(0x02446374),PTdraw=GC(0x02445EE4),PTdelete=GC(0x024468E4);
// d_a_ph (Peahat): daPH_Create, daPH_Execute, daPH_Draw, daPH_Delete
const uint32_t PHcreate=GC(0x023CF600),PHexec=GC(0x023CC070),PHdraw=GC(0x023C9994),PHdelete=GC(0x023CEF04);
// d_a_nz (Rat): daNZ_Create, daNZ_Execute, daNZ_Draw, daNZ_Delete
const uint32_t NZcreate=GC(0x0230F32C),NZexec=GC(0x0230BEF4),NZdraw=GC(0x0230AE30),NZdelete=GC(0x0230EDD0);
// d_a_tn (Darknut): daTn_Create, daTn_Execute, daTn_Draw, daTn_Delete
const uint32_t TNcreate=GC(0x024C0B0C),TNexec=GC(0x024BD170),TNdraw=GC(0x024BA238),TNdelete=GC(0x024C0194);
// d_a_tn statics: search_sp, s_check (the native boss-Darknut event counter), hio_set, and the weapon/bomb
// search scratch (target_info_count and its ten entries)
const uint32_t TNsearchSp=GD(0x1046E73C),TNsCheck=GD(0x1046E73D),TNhioSet=GD(0x101D2468),TNtargetCount=GD(0x1046E718),TNtargets=GD(0x1046E740);
// d_a_nzg (rat hole): daNZG_Create, daNZG_Execute, daNZG_Draw, daNZG_Delete
const uint32_t NGcreate=GC(0x0231175C),NGexec=GC(0x02311240),NGdraw=GC(0x023111D8),NGdelete=GC(0x023115BC);
// d_a_bo (Boko Baba): daBO_Create, daBO_Execute, daBO_Draw, daBO_Delete
const uint32_t BOcreate=GC(0x020BFC50),BOexec=GC(0x020BCEC8),BOdraw=GC(0x020BBAEC),BOdelete=GC(0x020BF574);
constexpr Kind kKinds[]={Kind::ChuChu,Kind::Bokoblin,Kind::Moblin,Kind::Keese,Kind::Miniblin,Kind::Peahat,Kind::Rat,Kind::Darknut,Kind::RatHole,Kind::BokoBaba,Kind::Bubble,Kind::Octorok};
// d_a_bl (Bubble): daBL_Create, daBL_Execute, daBL_Draw, daBL_Delete
const uint32_t BLcreate=GC(0x020ADA5C),BLexec=GC(0x020AB020),BLdraw=GC(0x020A8BDC),BLdelete=GC(0x020AD59C);
// d_a_oq (Octorok): daOQ_Create, daOQ_Execute, daOQ_Draw, daOQ_Delete (its rock, type 6, runs these too)
const uint32_t OQcreate=GC(0x023C3EC8),OQexec=GC(0x023C1380),OQdraw=GC(0x023C039C),OQdelete=GC(0x023C38AC);
const uint32_t Disappear=GC(0x025D99E8),Ice=GC(0x020402C8),TableItem=GC(0x025D9154),KiSearch=GC(0x021992DC),RoomSearch=GC(0x025D9898);
Entry* get(Token t){for(auto& e:entries)if(e.token==t)return &e;return nullptr;}
uint32_t params_of(const Spec& s){return s.params?s.params:parameters(s.kind);}
std::function<uint32_t(Token)> route_provider;
// a patrolling Bokoblin's path pointer (bk ppd, +0x1378) is its route record; every other Bokoblin has none
uint32_t patrol_route(const Entry&e){return (e.spec.kind==Kind::Bokoblin||e.spec.kind==Kind::Moblin)&&story_enemy_variants::patrol_word(params_of(e.spec))&&route_provider?route_provider(e.token):0u;}
bool memory(uint32_t a,uint32_t bytes){return !(a&3)&&a>=0x10000000u&&bytes&&a<=0x50000000u-bytes;}
bool reserved(uint32_t a){return memory(a,0x10)&&((ld16(a+0xE)>=0x7FF7&&ld16(a+0xE)<=0x7FFA)||ld16(a+0xE)==0x7FF4||(ld16(a+0xE)>=0x7FF0&&ld16(a+0xE)<=0x7FF3)||ld16(a+0xE)==0x7FEA||ld16(a+0xE)==0x7FE9||ld16(a+0xE)==0x7FE8||more::reserved(ld16(a+0xE)));}
uint32_t native_vtable(Kind k){if(const auto*n=more::find(k))return n->vtable();return GD(k==Kind::ChuChu?0x1000C5A0:k==Kind::Bokoblin?0x1000957C:k==Kind::Moblin?0x10014E9C:k==Kind::Miniblin?0x10038234:k==Kind::Peahat?0x10034494:k==Kind::Rat?0x1002442C:k==Kind::Darknut?0x10040490:k==Kind::RatHole?0x10024598:k==Kind::BokoBaba?0x1000A184:k==Kind::Bubble?0x10009964:k==Kind::Octorok?0x10033E10:0x10012C24);}
bool cleanup_identity(uint32_t a,uint32_t fn){for(Kind k:kKinds){const auto&d=story_enemy_ownership::descriptor(k);const uint32_t destructor=k==Kind::ChuChu?CCdelete:k==Kind::Bokoblin?BKdelete:k==Kind::Moblin?MOdelete:k==Kind::Miniblin?PTdelete:k==Kind::Peahat?PHdelete:k==Kind::Rat?NZdelete:k==Kind::Darknut?TNdelete:k==Kind::RatHole?NGdelete:k==Kind::BokoBaba?BOdelete:k==Kind::Bubble?BLdelete:k==Kind::Octorok?OQdelete:KIdelete;if(fn==destructor&&memory(a,d.bytes)&&ld16(a+0xE)==d.profile&&ld32(a+0xB4)==native_vtable(k)&&(story_enemy_variants::has_variants(kind_name(k))?story_enemy_variants::declared_params(kind_name(k),ld32(a+0xB0)):(ld32(a+0xB0)==parameters(k)||(k==Kind::Moblin&&(ld32(a+0xB0)==story_enemy_variants::moblin_patrol(parameters(k))||ld32(a+0xB0)==story_enemy_variants::kRiderParams))))&&ld8(a+0x2DD)==0xFF&&ld16(a+0x2D8)==0xFFFF){if(k==Kind::Bokoblin)st8(a+0x137D,0);if(k==Kind::Moblin)st8(a+0x2E0E,0);return true;}}
 for(const auto&n:more::kNatives){const auto&d=n.descriptor;if(fn==GC(n.destroy)&&memory(a,d.bytes)&&ld16(a+0xE)==d.profile&&ld32(a+0xB4)==n.vtable()&&(story_enemy_variants::has_variants(n.name)?story_enemy_variants::declared_params(n.name,ld32(a+0xB0)):(ld32(a+0xB0)|more::path_mask(n.kind))==n.params)&&ld8(a+0x2DD)==0xFF&&ld16(a+0x2D8)==0xFFFF)return true;}
 return false;}
uint32_t ice_offset(Kind k){if(const auto*n=more::find(k))return n->ice;return k==Kind::ChuChu?0x924:k==Kind::Bokoblin?0x13BC:k==Kind::Moblin?0x2E54:k==Kind::Miniblin?0x8FC:k==Kind::Peahat?0xAB4:k==Kind::Rat?0xBE0:k==Kind::Darknut?0x1674:k==Kind::BokoBaba?0xB18:k==Kind::Bubble?0x82C:k==Kind::Octorok?0xBA4:0xAD8;}
Entry* find(uint32_t a,bool initialized=true){if(!memory(a,0x10))return nullptr;for(auto&e:entries){const auto&d=story_enemy_ownership::descriptor(e.spec.kind);if(!memory(a,d.bytes)||e.pid==0xFFFFFFFFu||ld32(a+4)!=e.pid||ld16(a+0xE)!=d.profile||ld32(a+0xB0)!=params_of(e.spec)||ld8(a+0x2DD)!=0xFF)continue;if(!initialized||verify_actor(e.token,e.spec.kind,a))return &e;}return nullptr;}
void disable(Entry&e,uint32_t a){e.quarantine=true;if(a)story_enemy_ownership::retire_before_delete(a);}
void trace(const Entry&e,uint32_t a,const char*what,uint32_t value=0){LOG("[native_enemy] %s:%s event=%s actor=%08X pid=%08X profile=%04X kind=%u value=%u",e.spec.mod.c_str(),e.spec.id.c_str(),what,a,e.pid,unsigned(ld16(a+0xE)),unsigned(e.spec.kind),value);}
void no_carry(uint32_t a){st32(a+0x39C,ld32(a+0x39C)&~0x10u);st8(a+0x3A8,0xFF);st8(a+0x3A9,0);}
// A black ChuChu turned to stone (CC_ACTION_STONE 6) is the one owned enemy Link may lift and throw: the
// native carry, its throw and the break on landing only move this actor (no save bits, no switches).
bool black_chuchu(const Spec&s){return s.kind==Kind::ChuChu&&((params_of(s)>>8)&0xFF)==3;}
bool liftable_stone(const Spec&s,uint32_t a){return black_chuchu(s)&&ld8(a+0x3FC)==6;}
// a table kind Link may lift now (the curled Magtail): the game's own carry and throw
bool liftable(const Spec&s,uint32_t a){if(liftable_stone(s,a))return true;const auto*n=more::find(s.kind);return n&&n->liftable&&n->liftable(a);}
void keep_owned(const Spec&s,uint32_t a){if(liftable(s,a)){st8(a+0x3A8,0xFF);st8(a+0x3A9,0);}else no_carry(a);}
bool defeated(const Entry&e){return e.spec.transient?e.transient_defeated:story_items::enemy_defeated(e.marker);}
bool claim(Entry&e,uint32_t a){if(e.spec.transient){e.transient_defeated=true;trace(e,a,"summon-defeated");return true;}if(e.claiming)return false;e.claiming=true;struct ClaimGuard{bool&flag;~ClaimGuard(){flag=false;}}claim_guard{e.claiming};const auto result=story_items::claim_enemy_drops(e.marker,e.spec.mod,e.spec.drops);e.claiming=false;trace(e,a,"defeat-claim",unsigned(result));if(result==story_items::EnemyClaimStatus::Committed||result==story_items::EnemyClaimStatus::AlreadyDefeated)return true;disable(e,a);return false;}
// These globals are native temporary search results, not progress. Preserve them
// through exceptions and nested native execution; owned cohorts cannot leak out.
// The Seahat (and the big Octoroks, never offered) count kills in event register 0x7EFF (the save's event bytes
// at [0x101F84DC]+0x644, byte 0x7E; the sea ambushes read it). An owned enemy never changes it: the byte is
// restored after each of its steps.
uint32_t kill_counter(){const uint32_t info=ld32(GD(0x101F84DC));return memory(info,0x700)?info+0x644+0x7E:0;}
struct Scope {Entry* prior;Entry&entry;std::array<uint32_t,13> saved{};std::array<uint32_t,28> weapon_saved{};std::array<uint32_t,11> tn_saved{};uint32_t counter=kill_counter();uint8_t counter_value=counter?ld8(counter):0;Scope(Entry&e):prior(executing),entry(e){executing=&e;if(e.spec.kind==Kind::Darknut){tn_saved[0]=ld32(TNtargetCount);for(unsigned i=0;i<10;++i)tn_saved[i+1]=ld32(TNtargets+4*i);}saved[0]=ld32(GD(0x10462CF0));for(unsigned i=0;i<10;++i)saved[i+1]=ld32(GD(0x10462D00)+4*i);saved[11]=ld32(GD(0x10464B28));saved[12]=ld32(GD(0x10464B2C));if(e.spec.kind==Kind::Bokoblin||e.spec.kind==Kind::Moblin){const uint32_t base=GD(e.spec.kind==Kind::Bokoblin?0x10462320:0x10465498);weapon_saved[0]=ld32(base);for(unsigned i=0;i<10;++i)weapon_saved[i+1]=ld32(base+0x10+4*i);weapon_saved[11]=ld32(GD(0x1046231C));weapon_saved[12]=ld32(GD(0x1046232C));weapon_saved[13]=ld8(GD(0x104654A4));weapon_saved[14]=ld8(GD(0x101BA692));weapon_saved[20]=ld16(GD(0x10462474));const uint32_t offsets[]={0x28,0x34,0x3C,0x44,0x48};for(unsigned i=0;i<5;++i)weapon_saved[15+i]=ld32(GD(0x104654E0)+offsets[i]);if(e.search_known)st8(GD(e.spec.kind==Kind::Bokoblin?0x1046232D:0x104654A4),e.search_mode);}}~Scope(){if(counter&&ld8(counter)!=counter_value){LOG("[native_enemy] %s:%s kill counter 0x7EFF kept at %u (the game wrote %u)",entry.spec.mod.c_str(),entry.spec.id.c_str(),unsigned(counter_value),unsigned(ld8(counter)));st8(counter,counter_value);}if(entry.spec.kind==Kind::Darknut){st32(TNtargetCount,tn_saved[0]);for(unsigned i=0;i<10;++i)st32(TNtargets+4*i,tn_saved[i+1]);}st32(GD(0x10462CF0),saved[0]);for(unsigned i=0;i<10;++i)st32(GD(0x10462D00)+4*i,saved[i+1]);st32(GD(0x10464B28),saved[11]);st32(GD(0x10464B2C),saved[12]);if(entry.spec.kind==Kind::Bokoblin||entry.spec.kind==Kind::Moblin){const uint32_t base=GD(entry.spec.kind==Kind::Bokoblin?0x10462320:0x10465498);st32(base,weapon_saved[0]);for(unsigned i=0;i<10;++i)st32(base+0x10+4*i,weapon_saved[i+1]);st32(GD(0x1046231C),weapon_saved[11]);st32(GD(0x1046232C),weapon_saved[12]);st8(GD(0x104654A4),uint8_t(weapon_saved[13]));st8(GD(0x101BA692),uint8_t(weapon_saved[14]));st16(GD(0x10462474),uint16_t(weapon_saved[20]));const uint32_t offsets[]={0x28,0x34,0x3C,0x44,0x48};for(unsigned i=0;i<5;++i)st32(GD(0x104654E0)+offsets[i],weapon_saved[15+i]);}executing=prior;}};
// test aid: WWHD_ENEMY_TRACE=1 logs each owned enemy's execute path every 30 steps (place, action, what ran)
bool enemy_trace(){static const bool on=[]{const char*v=std::getenv("WWHD_ENEMY_TRACE");return v&&*v=='1';}();return on;}
void exec_trace(uint32_t a,const char*path){if(!enemy_trace())return;static std::map<uint32_t,unsigned> n;if(n[a]++%30)return;Entry*e=find(a,false);const uint16_t prof=ld16(a+0xE);const auto*table=story_enemies_more::find(e?e->spec.kind:Kind::ChuChu);const unsigned action=table?(table->action16?ld16(a+table->action):ld8(a+table->action))*1000u+ld8(a+table->action+(table->action16?2:1)):prof==0x7FF9?ld16(a+0x49C+6):prof==0x7FF8?ld16(a+0x8BC+6):prof==0x7FF1?ld16(a+0x590+6):prof==0x7FF7?ld8(a+0x489):prof==0x7FF2?(unsigned(ld8(a+0x3D8))<<8|ld8(a+0x3D9)):prof==0x7FF3?ld8(a+0x3A1):prof==0x7FEA?ld8(a+0x3E0)*100u+ld8(a+0x3E1):prof==0x7FE9?ld8(a+0x3EE)*100u+ld16(a+0x422):prof==0x7FE8?ld8(a+0x3DF)*256u+ld8(a+0x3E0):ld8(a+0x3FC);  // a rat: its action and mode
 LOG("[native_enemy] trace %s actor=%08X path=%s pos=%.1f %.1f %.1f home=%.1f %.1f %.1f action=%u behaviour=%d verify=%d",e?e->spec.id.c_str():"?",a,path,double(u32_as_f32(ld32(a+0x314))),double(u32_as_f32(ld32(a+0x318))),double(u32_as_f32(ld32(a+0x31C))),double(u32_as_f32(ld32(a+0x2EC))),double(u32_as_f32(ld32(a+0x2F0))),double(u32_as_f32(ld32(a+0x2F4))),action,prof==0x7FF7?int(int8_t(ld8(a+0x48B))):-1,e?int(verify_actor(e->token,e->spec.kind,a)):-1);
 if(prof==0x7FF1)LOG("[native_enemy] trace %s darknut pieces=%X health=%d sword=%08X holding=%u cape=%08X",e?e->spec.id.c_str():"?",unsigned(ld8(a+0x3EC)),int(int8_t(ld8(a+0x3A1))),ld32(a+0x1560),ld32(a+0xD84),ld32(a+0x1598));}
// The live children (sword, cape) a Darknut leaves when it goes: removed with it, so a defeated, removed
// or unloaded Darknut never leaves its pieces behind. Pending ones are refused when they would be made.
std::array<uint32_t,4> live_children(const Entry&e){std::array<uint32_t,4> kids{};unsigned n=0;if(e.spec.kind!=Kind::Darknut||e.pid==0xFFFFFFFFu)return kids;
 story_enemy_ownership::visit_lineage([&](const story_enemy_ownership::LineageInfo&i){if(n<kids.size()&&i.role==story_enemy_ownership::Lineage::EnemyChild&&i.source==e.token&&i.source_pid==e.pid&&i.address&&memory(i.address,0x10)&&ld32(i.address+4)==i.pid&&ld16(i.address+0xE)==i.profile)kids[n++]=i.address;});return kids;}
// test aid: WWHD_TEST_ENEMY_HEALTH=steps:value[,steps:value...] sets an owned enemy's health to value once it
// has run that many execute steps (counted per copy), so a scripted fight needs one landed hit to reach the
// game's own armour-break and defeat paths. Off unless set; never in a normal run.
void test_health(const Entry&e,uint32_t a){static const std::vector<std::pair<unsigned,int>> plan=[]{std::vector<std::pair<unsigned,int>> out;const char*v=std::getenv("WWHD_TEST_ENEMY_HEALTH");while(v&&*v){char*end=nullptr;const unsigned long steps=std::strtoul(v,&end,10);if(end==v||*end!=':')break;v=end+1;const long value=std::strtol(v,&end,10);if(end==v)break;out.push_back({unsigned(steps),int(value)});v=*end==','?end+1:end;if(*end!=',')break;}return out;}();
 if(plan.empty())return;static std::map<uint32_t,unsigned> steps;const unsigned n=++steps[e.pid];for(const auto&p:plan)if(p.first==n){st8(a+0x3A1,uint8_t(int8_t(p.second)));trace(e,a,"test-health",unsigned(p.second));}}
// test aid (with WWHD_ENEMY_TRACE=1): the game's save info (dSv_info_c, 0x12A0 bytes at *0x101F84DC: switches,
// event bits, the zones' actor bits) compared around each owned Darknut create, execute and delete; a change
// is logged with its offset, and the delete logs how many steps were watched
struct SaveWatch{const Entry*e;uint32_t base=0;std::vector<uint8_t> before;static inline unsigned steps=0,changed=0;
 SaveWatch(const Entry*entry,bool on):e(entry){if(!on||!enemy_trace())return;base=ld32(GD(0x101F84DC));if(!memory(base,0x12A0)){base=0;return;}before.assign(static_cast<const uint8_t*>(mem::ptr(base)),static_cast<const uint8_t*>(mem::ptr(base))+0x12A0);}
 void check(const char*what){if(!base)return;++steps;const auto*now=static_cast<const uint8_t*>(mem::ptr(base));for(uint32_t i=0;i<0x12A0;++i)if(now[i]!=before[i]){++changed;LOG("[native_enemy] %s save info changed during %s at +%03X: %02X -> %02X",e->spec.id.c_str(),what,i,unsigned(before[i]),unsigned(now[i]));}}};
// a refused create fails like a retail one (constructed first); a base-only actor skips its own delete
void refuse_or_skip(Cpu*c,uint32_t a,bool create){if(create)native_actor_refusal::refuse(c,a,invoke);else c->r[3]=1;}
void native_delete(Cpu*c,uint32_t fn){if(native_actor_refusal::base_only(c->r[3])){c->r[3]=1;return;}original(c,fn);}
// A Stalfos's club is left lying by the game when it dies; owned, Link could never take it: it goes with it.
void release_club(Cpu*c,const Entry&e,uint32_t a){if(e.spec.kind!=Kind::Stalfos||!invoke)return;const uint32_t pid=ld32(a+0x2014);story_enemy_ownership::LineageInfo info{};
 if(!pid||pid==0xFFFFFFFFu||!story_enemy_ownership::lineage_info(pid,info)||info.role!=story_enemy_ownership::Lineage::EnemyChild||info.source!=e.token||info.profile!=more::kStalfosClub)return;
 Cpu probe=*c;const uint32_t club=invoke(&probe,GC(0x025D5218),{GC(0x025E1234),a+0x2014});if(!memory(club,0x10)||ld32(club+4)!=pid||ld16(club+0xE)!=more::kStalfosClub)return;probe=*c;invoke(&probe,GC(0x025D57E0),{club});trace(e,a,"club-deleted",pid);}
// A Morth removed while it clings to Link: out of the shared cling statics, as the game's own detach does
// (the leader resets them; another one counts itself out); Link drops the heavy state himself.
void release_morth(const Entry&e,uint32_t a){if(e.spec.kind!=Kind::Morth||!more::morth_attached(a))return;const uint32_t count=more::morth_clinging(),shake=more::morth_shake_off();
 if(ld8(a+0x54A)){st32(count,0);st32(shake,0);}else{const int32_t n=std::max<int32_t>(0,int32_t(ld32(count))-1);st32(count,uint32_t(n));if(int32_t(ld32(shake))>n)st32(shake,uint32_t(n));}
 st32(a+0x2E0,ld32(a+0x2E0)&~0x4000u);st32(a+0x7A4,0);trace(e,a,"clinging-released",ld32(count));}
// The Redead's scream and bite and the Poe's possession hold Link through the game (an event that keeps
// him in his demo state, his confusion flag, the shared TORITUKI_ON) and only the actor's own execute
// lets go. release_link lets go for it when it is removed, stopped (quarantined) or left over after a
// state load: the event it holds ends (the game's own event reset, dComIfGp_event_reset), the camera the
// Poe stopped starts again, and the possession flags clear. Bounded reads; native calls only with invoke.
uint32_t play_base(){return GD(0x1046F0B0);}
constexpr uint32_t kEventControl=0x51D0;
bool holds_event(Cpu*c,uint32_t a){const uint32_t p=play_base();if(!invoke||!ld8(p+kEventControl+0xC2))return false;Cpu probe=*c;return invoke(&probe,GC(0x0253EE04),{p+kEventControl,ld32(p+kEventControl+0xC4)})==a;}
void end_event(){const uint32_t f=play_base()+kEventControl+0xE8;st16(f,uint16_t(ld16(f)|8u));}
void release_link(Cpu*c,Kind k,uint32_t a,const char*why){
 if(k==Kind::Redead){if(holds_event(c,a)){end_event();LOG("[native_enemy] redead actor=%08X: its event ended (%s)",a,why);}return;}
 if(k!=Kind::Poe)return;const uint32_t p=play_base();
 if(int16_t(ld16(a+0x482))==3&&holds_event(c,a)){end_event();const uint32_t cam=ld32(p+0x5AF8+uint32_t(int32_t(int8_t(ld8(p+0x5B30))))*0x34);
  if(memory(cam,0x400)){Cpu probe=*c;invoke(&probe,GC(0x02514F38),{cam+0x248});probe=*c;invoke(&probe,GC(0x02515280),{cam+0x248,0});}LOG("[native_enemy] poe actor=%08X: its possession event ended (%s)",a,why);}
 if(ld8(a+0x45F)){st8(a+0x45F,0);st8(more::poe_possessing(),0);const uint32_t link=ld32(p+0x5B34);if(int16_t(ld16(a+0x484))==0x51&&memory(link,0x8260)&&ld16(link+0xE)==0xA8)st32(link+0x3BC,ld32(link+0x3BC)&~0x100u);LOG("[native_enemy] poe actor=%08X: its possession ended (%s)",a,why);}}
// a reserved Redead or Poe the adapter no longer owns (after a state load or a quarantine): its hold on Link ends too
void release_orphan(Cpu*c,uint32_t a){const uint16_t prof=ld16(a+0xE);for(Kind k:{Kind::Redead,Kind::Poe}){const auto*n=more::find(k);if(prof==n->descriptor.profile&&memory(a,n->descriptor.bytes)&&ld32(a+0xB4)==n->vtable())release_link(c,k,a,"left over");}}
// the Poe's lantern (the game's, by its PID 0x48C) goes with the Poe while it still holds it
void release_lantern(Cpu*c,const Entry&e,uint32_t a){if(e.spec.kind!=Kind::Poe||!invoke)return;const uint32_t pid=ld32(a+0x48C);if(!pid||pid==0xFFFFFFFFu)return;Cpu probe=*c;const uint32_t lantern=invoke(&probe,GC(0x025D5218),{GC(0x025E1234),a+0x48C});
 if(!memory(lantern,0x890)||ld32(lantern+4)!=pid||ld16(lantern+0xE)!=more::kPoeLantern||ld8(lantern+0x484)>=5)return;probe=*c;invoke(&probe,GC(0x025D57E0),{lantern});trace(e,a,"lantern-deleted",pid);}
// An Armos Knight removed while it swallows Link's bomb (modes 44..46): the game sets the bomb off only in
// its own death (mode 47), so the held bomb (no-hit, invisible, in its carry state) is deleted with it.
// The PID key is the Knight's own field (fpcSch_JudgeByID takes the id by address); bombs only.
void release_bomb(Cpu*c,const Entry&e,uint32_t a){if(e.spec.kind!=Kind::ArmosKnight||!invoke)return;const uint32_t pid=more::armos_knight_held_bomb(a);if(!pid)return;
 Cpu probe=*c;const uint32_t bomb=invoke(&probe,GC(0x025D5218),{GC(0x025E1234),a+0x400});
 if(!memory(bomb,0x10)||ld32(bomb+4)!=pid||(ld16(bomb+0xE)!=0x126&&ld16(bomb+0xE)!=0x127))return;probe=*c;invoke(&probe,GC(0x025D57E0),{bomb});trace(e,a,"held-bomb-deleted",pid);}
// what an owned enemy holds goes with it when it is deleted
// A Wizzrobe's fireballs (its owned children, by their lineage) go with it.
void release_fireballs(Cpu*c,const Entry&e,uint32_t a){if(e.spec.kind!=Kind::Wizzrobe||!invoke)return;std::vector<uint32_t> balls;
 story_enemy_ownership::visit_lineage([&](const story_enemy_ownership::LineageInfo&info){if(info.role==story_enemy_ownership::Lineage::EnemyChild&&info.source==e.token&&info.source_actor==a&&info.profile==more::kWizzrobeFire&&info.address)balls.push_back(info.address);});
 for(uint32_t ball:balls){if(!memory(ball,more::kWizzrobeBytes)||ld16(ball+0xE)!=more::kWizzrobeFire)continue;Cpu probe=*c;invoke(&probe,GC(0x025D57E0),{ball});trace(e,a,"fireball-deleted",ld32(ball+4));}}
// A warship's flag (the game's, by its PID 0xE6C; the game removes it only when the ship sinks) and its
// owned cannonballs (by their lineage) go with it.
// A wall cannon's owned cannonballs go with it the same way (it has no flag).
void release_warship(Cpu*c,const Entry&e,uint32_t a){if((e.spec.kind!=Kind::Warship&&e.spec.kind!=Kind::WallCannon)||!invoke)return;
 const uint32_t pid=e.spec.kind==Kind::Warship?ld32(a+more::kWarshipFlagPid):0;if(pid&&pid!=0xFFFFFFFFu){Cpu probe=*c;const uint32_t flag=invoke(&probe,GC(0x025D5218),{GC(0x025E1234),a+more::kWarshipFlagPid});
  if(memory(flag,0x10)&&ld32(flag+4)==pid&&ld16(flag+0xE)==more::kWarshipFlag){probe=*c;invoke(&probe,GC(0x025D57E0),{flag});trace(e,a,"flag-deleted",pid);}}
 std::vector<uint32_t> balls;story_enemy_ownership::visit_lineage([&](const story_enemy_ownership::LineageInfo&info){if(info.role==story_enemy_ownership::Lineage::EnemyChild&&info.source==e.token&&info.source_actor==a&&info.profile==more::kCannonball&&info.address)balls.push_back(info.address);});
 for(uint32_t ball:balls){if(!memory(ball,more::kCannonballBytes)||ld16(ball+0xE)!=more::kCannonball)continue;Cpu probe=*c;invoke(&probe,GC(0x025D57E0),{ball});trace(e,a,"cannonball-deleted",ld32(ball+4));}}
// The native children of a table kind that are its owned children by their exact lineage (Gyorg sharks,
// Big Octo eyes, story_enemies_more::kChildClasses). While one runs its parent's name reads the game's
// (the child checks its parent by name); one whose parent is gone deletes itself.
thread_local uint32_t child_executing=0,child_executing_bytes=0;
const more::ChildClass* owned_child(uint32_t a,story_enemy_ownership::LineageInfo&info){if(!memory(a,0x10))return nullptr;const auto*k=more::child_class(ld16(a+0xE));
 if(!k||!memory(a,k->bytes)||!story_enemy_ownership::lineage_claimed(ld32(a+4),story_enemy_ownership::Lineage::EnemyChild)||!story_enemy_ownership::lineage_info(ld32(a+4),info)||info.profile!=k->profile)return nullptr;return k;}
// (a parent that creates its children in its own create, the Big Octo, has no bound address until it first runs:
// its lineage's source actor then stands for it, with the same process id and profile)
Entry* child_source(const story_enemy_ownership::LineageInfo&info,const more::ChildClass&k){if(!info.source_valid)return nullptr;Entry*e=get(info.source);const uint32_t p=info.source_actor;
 if(!e||e->spec.kind!=k.parent||e->pid!=info.source_pid||(e->address&&e->address!=p)||!memory(p,story_enemy_ownership::descriptor(k.parent).bytes)||ld32(p+4)!=e->pid||ld16(p+0xE)!=story_enemy_ownership::descriptor(k.parent).profile)return nullptr;return e;}
struct ParentName{uint32_t parent=0;uint16_t old=0;ParentName(uint32_t p,uint16_t name):parent(p){old=ld16(p+8);st16(p+8,name);}~ParentName(){st16(parent+8,old);}};
void child_lifecycle(Cpu*c,const more::ChildClass&k,uint32_t fn,bool create,bool draw,bool destroy){const uint32_t a=c->r[3];story_enemy_ownership::LineageInfo info{};
 if(destroy||owned_child(a,info)!=&k){original(c,fn);return;}
 Entry*e=child_source(info,k);
 if(!e||(info.address&&info.address!=a)){if(create){c->r[3]=5;return;}if(!draw&&ld8(a+0xC)!=3){Cpu probe=*c;invoke(&probe,GC(0x025D57E0),{a});}c->r[3]=1;return;}
 const uint32_t prior=child_executing,prior_bytes=child_executing_bytes;child_executing=a;child_executing_bytes=k.bytes;
 try{ParentName name(info.source_actor,k.parent_name);original(c,fn);}catch(...){child_executing=prior;child_executing_bytes=prior_bytes;throw;}child_executing=prior;child_executing_bytes=prior_bytes;
 if(create){const Cpu result=*c;if(result.r[3]==4){const bool admitted=story_enemy_ownership::admit_lineage(info.pid,a);if(!admitted){Cpu probe=*c;invoke(&probe,GC(0x025D57E0),{a});}trace(*e,info.source_actor,admitted?"child":"child-refused",info.pid);}*c=result;}}
template<size_t I> void child_create(Cpu*c){const auto&k=more::kChildClasses[I];child_lifecycle(c,k,GC(k.create),true,false,false);}
template<size_t I> void child_exec(Cpu*c){const auto&k=more::kChildClasses[I];child_lifecycle(c,k,GC(k.execute),false,false,false);}
template<size_t I> void child_draw(Cpu*c){const auto&k=more::kChildClasses[I];child_lifecycle(c,k,GC(k.draw),false,true,false);}
template<size_t I> void child_delete(Cpu*c){const auto&k=more::kChildClasses[I];child_lifecycle(c,k,GC(k.destroy),false,false,true);}
template<size_t... I> std::vector<std::pair<uint32_t,void(*)(Cpu*)>> child_hooks(std::index_sequence<I...>){std::vector<std::pair<uint32_t,void(*)(Cpu*)>> out;((out.push_back({GC(more::kChildClasses[I].create),child_create<I>}),out.push_back({GC(more::kChildClasses[I].execute),child_exec<I>}),out.push_back({GC(more::kChildClasses[I].draw),child_draw<I>}),out.push_back({GC(more::kChildClasses[I].destroy),child_delete<I>})),...);return out;}
// a shark's death: counted once in its controller; the game's sea-kill counter (modeDeleteInit) is kept
void gy_death(Cpu*c,uint32_t fn,bool sea_kill){const uint32_t a=c->r[3];story_enemy_ownership::LineageInfo info{};const auto*k=owned_child(a,info);Entry*e=k&&k->profile==more::kGy?child_source(info,*k):nullptr;if(!e){original(c,fn);return;}
 const uint32_t event=ld32(GD(0x101F84DC))+0x644,mode=ld32(a+0x3CC);uint32_t before=0;if(sea_kill){Cpu probe=*c;before=invoke(&probe,GC(more::kGetEventReg),{event,more::kSeaKillRegister})&0xFFu;}
 original(c,fn);const Cpu result=*c;
 if(sea_kill){Cpu probe=*c;invoke(&probe,GC(more::kSetEventReg),{event,more::kSeaKillRegister,before});}
 if(ld32(a+0x3CC)!=mode){const uint32_t ctrl=info.source_actor,deaths=std::min(ld8(ctrl+more::kGyorgDeaths)+1u,255u);st8(ctrl+more::kGyorgDeaths,uint8_t(deaths));trace(*e,ctrl,"shark-down",deaths);}
 *c=result;}
void gy_death_init(Cpu*c){gy_death(c,GC(more::kGyDeathInit),true);}void gy_bomb_death_init(Cpu*c){gy_death(c,GC(more::kGyBombDeathInit),false);}
// an owned Gyorg's sharks and a Big Octo's eyes (a table kind's children of their own class) go with it
void release_gyorg(Cpu*c,const Entry&e,uint32_t a){const auto*k=more::child_class_of(e.spec.kind);if(!k||!invoke)return;std::vector<uint32_t> sharks;
 story_enemy_ownership::visit_lineage([&](const story_enemy_ownership::LineageInfo&info){if(info.role==story_enemy_ownership::Lineage::EnemyChild&&info.source==e.token&&info.source_actor==a&&info.profile==k->profile&&info.address)sharks.push_back(info.address);});
 for(uint32_t shark:sharks){if(!memory(shark,k->bytes)||ld16(shark+0xE)!=k->profile)continue;Cpu probe=*c;invoke(&probe,GC(0x025D57E0),{shark});trace(e,a,"child-deleted",ld32(shark+4));}
 if(e.spec.kind==Kind::BigOcto&&ld8(a+more::kOctoMusic)){Cpu probe=*c;invoke(&probe,GC(more::kSubBgmStop),{});st8(a+more::kOctoMusic,0);trace(e,a,"battle-music-stopped");}}
// A Kargaroc's rider it still carries (state 3, not let go) goes with it; a released one fights on.
void release_rider(Cpu*c,const Entry&e,uint32_t a){if(e.spec.kind!=Kind::Kargaroc||!invoke||!more::kargaroc_carries(a))return;const uint32_t rider=ld32(a+more::kKargarocRider);
 Entry*r=find(rider,false);if(!r||(r->spec.kind!=Kind::Bokoblin&&r->spec.kind!=Kind::Moblin)||params_of(r->spec)!=story_enemy_variants::kRiderParams)return;
 Cpu probe=*c;invoke(&probe,GC(0x025D57E0),{rider});trace(e,a,"rider-deleted",ld32(rider+4));}
std::function<SummonRequest(Token,uint16_t,uint32_t)> summon_provider;std::function<void(Token,uint32_t)> summon_created;std::function<std::vector<Token>(Token)> summon_pool;
bool summoner(const Entry&e){return e.spec.kind==Kind::Wizzrobe&&(params_of(e.spec)&0xFFu)==1;}
// diagnostics: WWHD_SUMMON_HEAPS=1 logs the heaps' free bytes as each summon is made and created (a full wave's cost)
bool summon_heaps_on(){static const bool on=[]{const char*v=std::getenv("WWHD_SUMMON_HEAPS");return v&&*v=='1';}();return on;}
void summon_heaps(Cpu*c,const Entry&e){if(!summon_heaps_on())return;Cpu probe=*c;const auto f=native_actor_profiles::heap_free(&probe);unsigned live=0;
 for(const auto&m:entries)if(m.spec.transient&&m.spec.mod==e.spec.mod&&m.address&&m.pid!=0xFFFFFFFFu)++live;
 LOG("[summon_heaps] %s created; live summons=%u free archive=%u game=%u zelda=%u command=%u actor=%u",e.spec.id.c_str(),live,f[0],f[1],f[2],f[3],f[4]);}
// A summoner's summons still alive go with it (the game's door would wait for them; the summoner is gone).
void release_summons(Cpu*c,const Entry&e,uint32_t a){if(!summoner(e)||!invoke||!summon_pool)return;for(Token t:summon_pool(e.token)){auto*m=get(t);if(!m||!m->address||m->pid==0xFFFFFFFFu||!memory(m->address,0x10)||ld32(m->address+4)!=m->pid)continue;
  Cpu probe=*c;invoke(&probe,GC(0x025D57E0),{m->address});trace(e,a,"summon-deleted",m->pid);}}
void release_held(Cpu*c,const Entry&e,uint32_t a){release_summons(c,e,a);release_rider(c,e,a);release_gyorg(c,e,a);release_warship(c,e,a);release_fireballs(c,e,a);release_bomb(c,e,a);release_club(c,e,a);release_morth(e,a);release_link(c,e.spec.kind,a,"removed");release_lantern(c,e,a);}
// A Wizzrobe's fireball: the WZ class's own methods with the game's profile. An owned one (its exact
// child lineage) is admitted when its create completes and runs as an owned child (never in the area
// manager); one whose Wizzrobe is gone (a state load, a quarantine) deletes itself. Others are the game's.
thread_local uint32_t fireball_executing=0;
// (a summoner's summon ball and door are the same class's children of their own, admitted and run the same way)
bool wz_child_word(uint32_t w){return w==more::kWizzrobeFireParams||w==more::kWizzrobeBallParams||w==more::kWizzrobeDoorParams;}
bool owned_fireball(uint32_t a,story_enemy_ownership::LineageInfo&info){return memory(a,more::kWizzrobeBytes)&&ld16(a+0xE)==more::kWizzrobeFire&&wz_child_word(ld32(a+0xB0))&&story_enemy_ownership::lineage_claimed(ld32(a+4),story_enemy_ownership::Lineage::EnemyChild)&&story_enemy_ownership::lineage_info(ld32(a+4),info)&&info.profile==more::kWizzrobeFire;}
bool fireball(Cpu*c,uint32_t fn,uint32_t a,bool create,bool draw,bool destroy){
 const auto*wz=more::find(Kind::Wizzrobe);if(destroy||!wz||(fn!=GC(wz->create)&&fn!=GC(wz->execute)&&fn!=GC(wz->draw)))return false;story_enemy_ownership::LineageInfo info{};if(!owned_fireball(a,info))return false;
 if(create){original(c,fn);const Cpu result=*c;if(result.r[3]==4){const bool admitted=story_enemy_ownership::admit_lineage(info.pid,a);if(!admitted){Cpu probe=*c;invoke(&probe,GC(0x025D57E0),{a});}if(enemy_trace())LOG("[native_enemy] wizzrobe fireball actor=%08X pid=%08X %s",a,info.pid,admitted?"admitted":"refused");}*c=result;return true;}
 if(!info.source_valid||(info.address&&info.address!=a)){if(!draw&&ld8(a+0xC)!=3){Cpu probe=*c;invoke(&probe,GC(0x025D57E0),{a});}c->r[3]=1;return true;}
 if(draw){original(c,fn);return true;}
 const uint32_t prior=fireball_executing;fireball_executing=a;try{original(c,fn);}catch(...){fireball_executing=prior;throw;}fireball_executing=prior;return true;}
void lifecycle(Cpu*c,uint32_t fn,bool create,bool draw,bool destroy){const uint32_t a=c->r[3];
 // daTn_Create runs daTn_Execute itself (three steps that place the armour pieces): inside its own create
 // the native code runs as it is, under the create's scopes
 if(!create&&!destroy){Entry*n=find(a,false);if(n&&n->creating&&n->spec.kind==Kind::Darknut){original(c,fn);return;}}
 Entry*e=find(a,!create&&!destroy);
 if(!e&&fireball(c,fn,a,create,draw,destroy))return;
 if(!create&&!draw&&!destroy&&enemy_trace()){if(!e)exec_trace(a,find(a,false)?"unverified-original":"not-owned");}if(!e){if(destroy&&cleanup_identity(a,fn)){story_enemy_ownership::retire_before_delete(a);native_delete(c,fn);return;}if(reserved(a)){if(!create&&!draw&&!destroy)release_orphan(c,a);refuse_or_skip(c,a,create);return;}original(c,fn);return;}
 if(destroy){release_held(c,*e,a);if(e->spec.kind==Kind::Bokoblin)st8(a+0x137D,0);if(e->spec.kind==Kind::Moblin)st8(a+0x2E0E,0);if(children.retire&&has_children(e->spec.kind))children.retire(e->token,a);const auto kids=live_children(*e);const uint32_t pid=e->pid;story_enemy_ownership::retire_before_delete(a);e->address=0;e->pid=0xFFFFFFFFu;trace(*e,a,"delete");SaveWatch watch(e,e->spec.kind==Kind::Darknut);native_delete(c,fn);const Cpu result=*c;watch.check("delete");if(watch.base)LOG("[native_enemy] %s save info watched over %u Darknut steps: %u changed bytes",e->spec.id.c_str(),SaveWatch::steps,SaveWatch::changed);if(retired)retired(e->token,pid);
  for(uint32_t kid:kids)if(kid){Cpu probe=result;const uint32_t ok=invoke(&probe,GC(0x025D57E0),{kid});trace(*e,a,"child-removed",ok);}
  *c=result;return;}
 if(!supported(e->spec.kind)||e->quarantine){if(!create&&!draw){exec_trace(a,"quarantined");release_link(c,e->spec.kind,a,"stopped");}refuse_or_skip(c,a,create);return;}
 // a Redead only where the stage has its scream and bite events
 if(create&&e->spec.kind==Kind::BigOcto&&std::strncmp(reinterpret_cast<const char*>(mem::ptr(play_base()+0x5134)),"sea",8)!=0){trace(*e,a,"big-octo-only-on-the-sea");refuse_or_skip(c,a,create);return;}
 if(create&&e->spec.kind==Kind::Gyorg&&std::strncmp(reinterpret_cast<const char*>(mem::ptr(play_base()+0x5134)),"sea",8)!=0){trace(*e,a,"gyorg-only-on-the-sea");refuse_or_skip(c,a,create);return;}
 // (the game's own create refuses a Gyorg until Link has the boomerang: the reason is logged)
 if(create&&e->spec.kind==Kind::Gyorg){Cpu probe=*c;if(!(invoke(&probe,GC(more::kCheckGetItem),{more::kBoomerang})&0xFFu)){static unsigned told=0;if(told++<8)trace(*e,a,"gyorg-needs-the-boomerang");}}
 if(create&&e->spec.kind==Kind::Warship&&std::strncmp(reinterpret_cast<const char*>(mem::ptr(play_base()+0x5134)),"sea",8)!=0){trace(*e,a,"warship-only-on-the-sea");refuse_or_skip(c,a,create);return;}
 if(create&&e->spec.kind==Kind::Wizzrobe){const char*stage=reinterpret_cast<const char*>(mem::ptr(play_base()+0x5134));for(const char*no:more::kNoWizzrobeStages)if(std::strncmp(stage,no,8)==0){trace(*e,a,"no-wizzrobe-on-this-stage");refuse_or_skip(c,a,create);return;}}
 if(create&&e->spec.kind==Kind::Redead){const char*stage=reinterpret_cast<const char*>(mem::ptr(play_base()+0x5134));for(const char*no:more::kNoRedeadStages)if(std::strncmp(stage,no,8)==0){trace(*e,a,"no-redead-events-on-this-stage");refuse_or_skip(c,a,create);return;}}
 if(!create&&defeated(*e)){if(!draw&&ld8(a+0xC)!=3){Cpu probe=*c;const uint32_t result=invoke(&probe,GC(0x025D57E0),{a});static unsigned reported=0;if(reported++<128)trace(*e,a,"terminal-delete-retry",result);}c->r[3]=1;return;}
 if(!create&&!draw){if(!story_enemy_ownership::lookup_actor(a)){if(!story_enemy_ownership::admit_created(e->token,a)){disable(*e,a);c->r[3]=1;return;}e->address=a;}e->address=a;
  // Carry is refused by the shared player admission hook, never undone here.
  if((ld32(a+0x2E0)&0x2000u)&&!liftable(e->spec,a)){disable(*e,a);trace(*e,a,"unexpected-carried");c->r[3]=1;return;}keep_owned(e->spec,a);
 }
 struct ChildScope {bool active=false;~ChildScope(){if(active)children.leave();}} child_scope;
 if((create||!draw)&&(has_children(e->spec.kind)||(e->spec.kind==Kind::Rat&&(params_of(e->spec)&0x100u)))){  // a bomb rat's bomb is its child
 if(!create&&e->spec.kind!=Kind::Stalfos&&e->spec.kind!=Kind::Wizzrobe&&e->spec.kind!=Kind::BigOcto&&!children.ready(e->token,a)){if(!draw)exec_trace(a,"children-not-ready");c->r[3]=1;return;}if(!children.enter(e->token,e->spec.kind,a,story_enemy_ownership::source_epoch(e->token))){disable(*e,a);refuse_or_skip(c,a,create);return;}child_scope.active=true;}
 struct HioScope{uint32_t address=0;uint8_t old=0;HioScope(bool active,Kind k){if(active){address=k==Kind::Bokoblin?GD(0x10191468):k==Kind::Moblin?GD(0x101BA690):TNhioSet;old=ld8(address);st8(address,1);}}~HioScope(){if(address)st8(address,old);}} hio(create&&(e->spec.kind==Kind::Bokoblin||e->spec.kind==Kind::Moblin||e->spec.kind==Kind::Darknut),e->spec.kind);
 // daTn_Create rewrites two statics every Darknut shares: search_sp (from the stage name, or 1 for a boss)
 // and s_check (the native boss-Darknut event counter). A mod Darknut leaves both as they were.
 struct TnStatics{bool active;uint8_t sp=0,check=0;TnStatics(bool on):active(on){if(on){sp=ld8(TNsearchSp);check=ld8(TNsCheck);}}~TnStatics(){if(active){st8(TNsearchSp,sp);st8(TNsCheck,check);}}} tn_statics(create&&e->spec.kind==Kind::Darknut);
 struct Creating{Entry*entry;Creating(Entry*e,bool on):entry(on?e:nullptr){if(entry)entry->creating=true;}~Creating(){if(entry)entry->creating=false;}} creating(e,create);
 if(!create&&!draw){exec_trace(a,"execute");test_health(*e,a);}
 SaveWatch watch(e,e->spec.kind==Kind::Darknut&&!draw);
 struct ActorScope{uint32_t prior;explicit ActorScope(uint32_t a):prior(executing_actor){executing_actor=a;}~ActorScope(){executing_actor=prior;}} actor_scope(a);
 Scope scope(*e);if(fn==NGexec)c->r[3]=1;else original(c,fn);const Cpu result=*c;  // an owned rat hole never spawns on its own
 watch.check(create?"create":"execute");
 // a Bubble's grapple steal (stolenItemCount 0x406, copied into the steal count on a hookshot hit) is off
 if(create&&result.r[3]==4&&e->spec.kind==Kind::Bubble)st8(a+0x406,0);
 if(e->spec.kind==Kind::Stalfos&&!draw&&more::stalfos_refused_split(a)){more::stalfos_collapse(a);trace(*e,a,"split-refused-collapse");}
 if(e->spec.kind==Kind::BigOcto&&!create&&!draw&&more::big_octo_rise(a)){Cpu probe=*c;invoke(&probe,GC(more::kOctoModeProc),{a,0,2});trace(*e,a,"risen");}
 if(e->spec.kind==Kind::Mothula&&!create&&!draw&&more::mothula_strayed(a)){more::mothula_return(a);trace(*e,a,"strayed-turns-home");}
 if(create&&result.r[3]==4&&e->spec.kind==Kind::Gyorg)st32(a+more::kGyorgDeaths,0);
 if(create&&result.r[3]==4){if(!verify_actor(e->token,e->spec.kind,a)||(!story_enemy_ownership::lookup_actor(a)&&!story_enemy_ownership::admit_created(e->token,a))){disable(*e,a);c->r[3]=5;return;}e->address=a;if(e->spec.kind==Kind::Bokoblin||e->spec.kind==Kind::Moblin){e->search_mode=ld8(GD(e->spec.kind==Kind::Bokoblin?0x1046232D:0x104654A4));e->search_known=true;}no_carry(a);trace(*e,a,"created");if(e->spec.transient)summon_heaps(c,*e);}
 else if(create&&result.r[3]==5){disable(*e,a);trace(*e,a,"create-failed");}
 if(!draw&&verify_actor(e->token,e->spec.kind,a))keep_owned(e->spec,a);*c=result;
}
void cc_create(Cpu*c){lifecycle(c,CCcreate,true,false,false);}void cc_exec(Cpu*c){lifecycle(c,CCexec,false,false,false);}void cc_draw(Cpu*c){lifecycle(c,CCdraw,false,true,false);}void cc_delete(Cpu*c){lifecycle(c,CCdelete,false,false,true);}
void ki_create(Cpu*c){lifecycle(c,KIcreate,true,false,false);}void ki_exec(Cpu*c){lifecycle(c,KIexec,false,false,false);}void ki_draw(Cpu*c){lifecycle(c,KIdraw,false,true,false);}void ki_delete(Cpu*c){lifecycle(c,KIdelete,false,false,true);}
void bk_create(Cpu*c){lifecycle(c,BKcreate,true,false,false);}void bk_exec(Cpu*c){lifecycle(c,BKexec,false,false,false);}void bk_draw(Cpu*c){lifecycle(c,BKdraw,false,true,false);}void bk_delete(Cpu*c){lifecycle(c,BKdelete,false,false,true);}
void pt_create(Cpu*c){lifecycle(c,PTcreate,true,false,false);}void pt_exec(Cpu*c){lifecycle(c,PTexec,false,false,false);}void pt_draw(Cpu*c){lifecycle(c,PTdraw,false,true,false);}void pt_delete(Cpu*c){lifecycle(c,PTdelete,false,false,true);}
void ph_create(Cpu*c){lifecycle(c,PHcreate,true,false,false);}void ph_exec(Cpu*c){lifecycle(c,PHexec,false,false,false);}void ph_draw(Cpu*c){lifecycle(c,PHdraw,false,true,false);}void ph_delete(Cpu*c){lifecycle(c,PHdelete,false,false,true);}
// the owned rat hole: its own spawning (daNZG_Execute) never runs; the port creates its rats (story_actors)
void ng_create(Cpu*c){lifecycle(c,NGcreate,true,false,false);}void ng_draw(Cpu*c){lifecycle(c,NGdraw,false,true,false);}void ng_delete(Cpu*c){lifecycle(c,NGdelete,false,false,true);}
void ng_exec(Cpu*c){lifecycle(c,NGexec,false,false,false);}
void nz_create(Cpu*c){lifecycle(c,NZcreate,true,false,false);}void nz_exec(Cpu*c){lifecycle(c,NZexec,false,false,false);}void nz_draw(Cpu*c){lifecycle(c,NZdraw,false,true,false);}void nz_delete(Cpu*c){lifecycle(c,NZdelete,false,false,true);}
void tn_create(Cpu*c){lifecycle(c,TNcreate,true,false,false);}void tn_exec(Cpu*c){lifecycle(c,TNexec,false,false,false);}void tn_draw(Cpu*c){lifecycle(c,TNdraw,false,true,false);}void tn_delete(Cpu*c){lifecycle(c,TNdelete,false,false,true);}
void bo_create(Cpu*c){lifecycle(c,BOcreate,true,false,false);}void bo_exec(Cpu*c){lifecycle(c,BOexec,false,false,false);}void bo_draw(Cpu*c){lifecycle(c,BOdraw,false,true,false);}void bo_delete(Cpu*c){lifecycle(c,BOdelete,false,false,true);}
void bl_create(Cpu*c){lifecycle(c,BLcreate,true,false,false);}void bl_exec(Cpu*c){lifecycle(c,BLexec,false,false,false);}void bl_draw(Cpu*c){lifecycle(c,BLdraw,false,true,false);}void bl_delete(Cpu*c){lifecycle(c,BLdelete,false,false,true);}
void oq_create(Cpu*c){lifecycle(c,OQcreate,true,false,false);}void oq_exec(Cpu*c){lifecycle(c,OQexec,false,false,false);}void oq_draw(Cpu*c){lifecycle(c,OQdraw,false,true,false);}void oq_delete(Cpu*c){lifecycle(c,OQdelete,false,false,true);}
void mo_create(Cpu*c){lifecycle(c,MOcreate,true,false,false);}void mo_exec(Cpu*c){lifecycle(c,MOexec,false,false,false);}void mo_draw(Cpu*c){lifecycle(c,MOdraw,false,true,false);}void mo_delete(Cpu*c){lifecycle(c,MOdelete,false,false,true);}
// the table kinds' four methods: one hook each, by table row
template<size_t I> void more_create(Cpu*c){lifecycle(c,GC(more::kNatives[I].create),true,false,false);}
template<size_t I> void more_exec(Cpu*c){lifecycle(c,GC(more::kNatives[I].execute),false,false,false);}
template<size_t I> void more_draw(Cpu*c){lifecycle(c,GC(more::kNatives[I].draw),false,true,false);}
template<size_t I> void more_delete(Cpu*c){lifecycle(c,GC(more::kNatives[I].destroy),false,false,true);}
template<size_t... I> std::vector<std::pair<uint32_t,void(*)(Cpu*)>> more_hooks(std::index_sequence<I...>){std::vector<std::pair<uint32_t,void(*)(Cpu*)>> out;((out.push_back({GC(more::kNatives[I].create),more_create<I>}),out.push_back({GC(more::kNatives[I].execute),more_exec<I>}),out.push_back({GC(more::kNatives[I].draw),more_draw<I>}),out.push_back({GC(more::kNatives[I].destroy),more_delete<I>})),...);return out;}
std::vector<Kind> all_kinds(){std::vector<Kind> out(std::begin(kKinds),std::end(kKinds));for(const auto&n:more::kNatives)out.push_back(n.kind);return out;}
// An owned Mothula's moths (the game's own Morths) are refused; it fights alone.
// An owned warship's cannonballs (the game's BOMB in its cannonball state, fopAcM_fastCreate in its
// execute) become its owned children; anything else it creates this way is refused.
// A wall cannon's cannonballs are the same BOMB with the same parameters, made the same way (its attack).
void fast_create(Cpu*c){if(!executing||(executing->spec.kind!=Kind::Warship&&executing->spec.kind!=Kind::WallCannon)){original(c,GC(more::kFastCreate));return;}
 Entry&e=*executing;if(c->r[3]!=more::kCannonball||c->r[4]!=more::kCannonballParams){trace(e,e.address,"fast-create-refused",c->r[3]);c->r[3]=0;return;}
 original(c,GC(more::kFastCreate));const Cpu result=*c;const uint32_t ball=result.r[3];
 if(memory(ball,more::kCannonballBytes)&&ld16(ball+0xE)==more::kCannonball){const uint32_t pid=ld32(ball+4);
  if(!story_enemy_ownership::queue_child_pending(e.token,e.address,e.pid,pid,more::kCannonball,more::kCannonballParams,story_enemy_ownership::source_epoch(e.token))||!story_enemy_ownership::admit_lineage(pid,ball)){Cpu probe=*c;invoke(&probe,GC(0x025D57E0),{ball});trace(e,e.address,"cannonball-refused",pid);*c=result;c->r[3]=0;return;}
  if(enemy_trace())trace(e,e.address,"cannonball",pid);}
 *c=result;}
// An owned Floormaster never drags Link down or grabs his feet: when its grab would pull him down it
// lets go and sinks away (MODE_DISAPPEAR, then it hides and comes up again, so Link gets free), a foot
// grab becomes APPEAR (as with the game's no-demo switch), and it never makes a skull pot or plays an
// NPC or player-start demo.
void fm_mode(Cpu*c){const uint32_t a=c->r[3];Entry*e=find(a,false);if(!e||e->spec.kind!=Kind::Floormaster||c->r[4]!=0){original(c,GC(more::kFmModeProc));return;}
 const uint32_t mode=c->r[5];
 if(mode==14||mode==16){Cpu probe=*c;invoke(&probe,GC(more::kFmCancelGrab),{a});c->r[5]=6;trace(*e,a,"grab-let-go",mode);}
 else if(mode==15){c->r[5]=5;trace(*e,a,"foot-grab-refused");}
 else if(mode==11||mode==17){c->r[5]=7;trace(*e,a,"mode-refused",mode);}
 original(c,GC(more::kFmModeProc));}
// An owned Big Octo never swallows the boat (MODE_DEMO 6 is refused: it stays in its mode); its
// modeDeleteInit (every eye dead) is its defeat, and it is removed before its death event can run.
void octo_mode(Cpu*c){const uint32_t a=c->r[3];if(Entry*e=find(a,false);e&&e->spec.kind==Kind::BigOcto&&c->r[4]==0&&c->r[5]==6){static unsigned told=0;if(told++<16)trace(*e,a,"swallow-refused");return;}original(c,GC(more::kOctoModeProc));}
void octo_death(Cpu*c){const uint32_t a=c->r[3];Entry*e=find(a);if(!e||e->spec.kind!=Kind::BigOcto){if(!e&&reserved(a))return;original(c,GC(more::kOctoDeathInit));return;}original(c,GC(more::kOctoDeathInit));const Cpu result=*c;if(executing==e)claim(*e,a);else{disable(*e,a);trace(*e,a,"unexpected-death");}*c=result;}
// ...and it targets only Link (never a bomb or a pot)
void fm_search_other(Cpu*c){if(Entry*e=find(c->r[3],false);e&&e->spec.kind==Kind::Floormaster){c->r[3]=0;return;}original(c,GC(more::kFmSearchOther));}
// An owned Gyorg's sharks become its owned children; anything else it would create this way is refused.
void owned_child_create(Cpu*c,Entry&e,const more::ChildClass&k){const uint32_t actor=e.address?e.address:executing_actor;if(!actor){c->r[3]=0xFFFFFFFFu;return;}if(c->r[3]!=k.profile){trace(e,actor,"child-refused",c->r[3]);c->r[3]=0xFFFFFFFFu;return;}
 const uint32_t ticket=story_enemy_ownership::reserve_child_pending(e.token,actor,e.pid,k.profile,c->r[5],story_enemy_ownership::source_epoch(e.token));if(!ticket){trace(e,actor,"child-not-reserved");c->r[3]=0xFFFFFFFFu;return;}
 try{original(c,GC(more::kCreateChild));}catch(...){story_enemy_ownership::cancel_child_pending(ticket);throw;}
 const uint32_t pid=c->r[3];if(!pid||pid==0xFFFFFFFFu||!story_enemy_ownership::commit_child_pending(ticket,pid)){story_enemy_ownership::cancel_child_pending(ticket);c->r[3]=0xFFFFFFFFu;}}
// A wall cannon's delete mode (deleteInit): its health gone, this is its defeat (it has no native drop);
// the explosion plays (its death switch is 0xFF: no switch). Anything else entering it is unexpected.
void wall_cannon_delete_init(Cpu*c){const uint32_t a=c->r[3];Entry*e=find(a);if(!e){if(reserved(a))return;original(c,GC(more::kWallCannonDeleteInit));return;}
 if(e->spec.kind!=Kind::WallCannon||executing!=e||!more::wall_cannon_destroyed(a)){disable(*e,a);trace(*e,a,"unexpected-wreck");return;}claim(*e,a);original(c,GC(more::kWallCannonDeleteInit));}
std::function<RiderRequest(Token)> rider_provider;std::function<void(Token,uint32_t)> rider_created;
// fpcSCtRq_Request(layer, name, 0, 0, append): an owned Kargaroc's create asks for its rider (MO2 0xBC or BK 0xBD,
// parameter (bb & 0xFF000000) | 0xFFFF05). It becomes the port's rider declaration: its private profile and the
// fixed rider word, bound to the new process. Anything else an owned Kargaroc requests, or a rider whose
// declaration can't appear (defeated), is refused, and the Kargaroc then flies without a rider.
void scene_request(Cpu*c){Entry*e=executing;if(!e||e->spec.kind!=Kind::Kargaroc){original(c,GC(more::kSceneRequest));return;}
 const uint32_t actor=e->address?e->address:executing_actor,append=c->r[7];const uint16_t name=uint16_t(c->r[4]);
 const RiderRequest r=rider_provider?rider_provider(e->token):RiderRequest{};
 if(!r.rider||(name!=0xBC&&name!=0xBD)||!memory(append,0x24)||!r.profile){trace(*e,actor,"rider-refused",name);
  if(memory(actor,0x460)){st8(actor+more::kKargarocCarry,0);st8(actor+more::kKargarocState,0);}c->r[3]=0xFFFFFFFFu;return;}
 // the rider's word, and its z angle (the Moblin's and Bokoblin's death switch: 0xFF, none) as any placed copy's
 c->r[4]=r.profile;st32(append,r.params);st16(append+0x14,home_z(name==0xBC?Kind::Moblin:Kind::Bokoblin));original(c,GC(more::kSceneRequest));const uint32_t pid=c->r[3];
 if(pid!=0xFFFFFFFFu&&rider_created)rider_created(r.rider,pid);trace(*e,actor,"rider-requested",pid);}
// ---- Wizzrobe summoners: the summon ball and door are its owned children (the WZ class, game profile 0xD0,
// fopAcM_createChild: the ball in its execute, the door in the ball's); the door's summons are its pool.
// the summoner an owned WZ child actor (with word `w`) belongs to, or null
Entry* wz_child_summoner(uint32_t a,uint32_t w){story_enemy_ownership::LineageInfo info{};if(!a||!owned_fireball(a,info)||ld32(a+0xB0)!=w||!info.source_valid)return nullptr;auto*e=get(info.source);return e&&summoner(*e)&&e->address==info.source_actor&&e->pid==info.source_pid?e:nullptr;}
void queue_wz_child(Cpu*c,Entry&s,uint32_t word){
 if(!s.address||s.pid==0xFFFFFFFFu||s.quarantine){c->r[3]=0xFFFFFFFFu;return;}
 const uint32_t ticket=story_enemy_ownership::reserve_child_pending(s.token,s.address,s.pid,more::kWizzrobeFire,word,story_enemy_ownership::source_epoch(s.token));if(!ticket){trace(s,s.address,"summon-child-refused",word);c->r[3]=0xFFFFFFFFu;return;}
 try{original(c,GC(more::kCreateChild));}catch(...){story_enemy_ownership::cancel_child_pending(ticket);throw;}
 const uint32_t pid=c->r[3];if(!pid||pid==0xFFFFFFFFu){story_enemy_ownership::cancel_child_pending(ticket);return;}
 if(!story_enemy_ownership::commit_child_pending(ticket,pid))throw std::runtime_error("Wizzrobe summon child PID could not bind its reserved ownership record");
 if(enemy_trace())trace(s,s.address,word==more::kWizzrobeBallParams?"summon-ball":"summon-door",pid);}
bool summon_child(Cpu*c){if(c->r[3]!=more::kWizzrobeFire)return false;const uint32_t word=c->r[5];
 if(word==more::kWizzrobeBallParams&&executing&&summoner(*executing)){queue_wz_child(c,*executing,word);return true;}
 if(word==more::kWizzrobeDoorParams&&fireball_executing){if(Entry*s=wz_child_summoner(fireball_executing,more::kWizzrobeBallParams)){queue_wz_child(c,*s,word);return true;}}
 // any other WZ child of an owned summoner (or of its ball) is refused
 if((executing&&summoner(*executing))||(fireball_executing&&wz_child_summoner(fireball_executing,more::kWizzrobeBallParams))){c->r[3]=0xFFFFFFFFu;return true;}
 return false;}
void create_child(Cpu*c){if(summon_child(c))return;if(executing){if(const auto*k=more::child_class_of(executing->spec.kind)){owned_child_create(c,*executing,*k);return;}}if(executing&&executing->spec.kind==Kind::Mothula&&c->r[3]==more::kMothulaMoth){static unsigned told=0;if(told++<8)trace(*executing,executing->address,"moth-release-refused");c->r[3]=0xFFFFFFFFu;return;}original(c,GC(more::kCreateChild));}
// An owned enemy's colliders never join the area manager (grass and the like): only the pair policy reaches them.
void area_set(Cpu*c){const uint32_t o=c->r[4];if(child_executing&&o>=child_executing&&o<child_executing+child_executing_bytes)return;if(fireball_executing&&o>=fireball_executing&&o<fireball_executing+more::kWizzrobeBytes)return;if(executing&&more::find(executing->spec.kind)&&executing->address&&o>=executing->address&&o<executing->address+story_enemy_ownership::descriptor(executing->spec.kind).bytes)return;original(c,GC(more::kAreaManagerSet));}
// A Morth's death effect (no drop outside Ganon's Tower) is its defeat; the game then deletes it.
void morth_death(Cpu*c){const uint32_t a=c->r[3];Entry*e=find(a);if(!e){if(reserved(a))return;original(c,GC(more::kMorthDeathEffect));return;}
 if(e->spec.kind!=Kind::Morth||executing!=e||!more::morth_dying(a)){disable(*e,a);trace(*e,a,"unexpected-death-effect");return;}claim(*e,a);original(c,GC(more::kMorthDeathEffect));}
// An owned Morth that clings stays in its room's layer (the game moves it to the stage's): it leaves with its room.
void stage_layer(Cpu*c){if(executing&&executing->spec.kind==Kind::Morth&&c->r[3]==executing->address){trace(*executing,c->r[3],"stage-layer-kept-room");return;}if(auto*e=find(c->r[3],false)){if(e->spec.kind==Kind::Morth)return;}original(c,GC(more::kStageLayer));}
void disappear(Cpu*c){const uint32_t a=c->r[3];{story_enemy_ownership::LineageInfo info{};if(owned_child(a,info)){c->r[3]=0xFFFFFFFFu;return;}}Entry*e=find(a);if(!e){if(reserved(a)){c->r[3]=0xFFFFFFFFu;return;}original(c,Disappear);return;}
 const auto*table=more::find(e->spec.kind);
 const bool terminal=table?table->terminal(a):e->spec.kind==Kind::ChuChu?((ld8(a+0x3FC)==4&&(ld8(a+0x3FD)==51||ld8(a+0x3FD)==53))||(liftable_stone(e->spec,a)&&ld8(a+0x3FD)>=0x52&&ld8(a+0x3FD)<=0x54)):e->spec.kind==Kind::Bokoblin?ld16(a+0x49C+6)==20:e->spec.kind==Kind::Moblin?ld16(a+0x8BC+6)==20:e->spec.kind==Kind::Darknut?ld16(a+0x590+6)==20:e->spec.kind==Kind::Miniblin?(int8_t(ld8(a+0x3A1))<=0||ld16(a+0x3EE)==4||ld16(a+0x3EE)==5):e->spec.kind==Kind::Peahat?int8_t(ld8(a+0x3A1))<=0:e->spec.kind==Kind::Rat?ld8(a+0x3D9)==57:
  // Boko Baba: action 2 (dying), mode 21, the stem left (kind byte 1) at the drop frame
  e->spec.kind==Kind::BokoBaba?(ld8(a+0x3E0)==2&&ld8(a+0x3E1)==21&&ld8(a+0x3DC)==1):
  // Bubble: a burning red one put down (retreat step 10), or either colour's skull at step 42, health gone
  e->spec.kind==Kind::Bubble?(int8_t(ld8(a+0x3A1))<=0&&((ld8(a+0x3EE)==1&&ld16(a+0x422)==10&&ld8(a+0x3EF)==1&&ld8(a+0x3EC)==0)||(ld8(a+0x3EE)==4&&ld16(a+0x422)==42))):
  // Octorok: the death sequence (m3DF 0x14) at its last step 0x21
  e->spec.kind==Kind::Octorok?(ld8(a+0x3DF)==0x14&&ld8(a+0x3E0)==0x21):ld8(a+0x489)==30;
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
void table_item(Cpu*c){if(child_executing){c->r[3]=0xFFFFFFFFu;return;}Entry*e=ice_owner;if(e&&e==executing&&e->address&&verify_actor(e->token,e->spec.kind,e->address)&&c->r[6]==e->address+0x320&&c->r[4]==ld32(e->address+0x3A4)){claim(*e,e->address);c->r[3]=0xFFFFFFFFu;return;}if(e){disable(*e,e->address);c->r[3]=0xFFFFFFFFu;return;}original(c,TableItem);}
void ki_search(Cpu*c){const uint32_t a=c->r[3];if(executing&&executing->spec.kind==Kind::Keese){Entry*e=find(a);if(e&&!e->quarantine&&e->spec.kind==Kind::Keese&&e->spec.mod==executing->spec.mod&&story_enemy_ownership::lookup_actor(a)){st32(GD(0x10464B28),ld32(GD(0x10464B28))+1);if(ld8(a+0x489)==10&&int8_t(ld8(a+0x48B))<10)st32(GD(0x10464B2C),ld32(GD(0x10464B2C))+1);}c->r[3]=0;return;}original(c,KiSearch);}
void mo_capture(Cpu*c){if(auto*e=find(c->r[3])){if(e->spec.kind==Kind::Moblin){trace(*e,c->r[3],"capture-denied");return;}}if(reserved(c->r[3]))return;original(c,GC(0x021C9AA0));}
// The Boko Baba's bite asks for an event to grab and chew Link (Link held in a demo, an event camera, both ended
// only by its own later steps). An owned one never grabs: the request is refused and its grab latch (0x3E7) cleared,
// so it goes on biting (the bite's damage stays). An owned Poe's own possession event (its demo action) is
// admitted (release_link ends it when the Poe goes). Any other owned request quarantines the enemy.
void potential_event(Cpu*c){if(auto*e=find(c->r[3])){if(e->spec.kind==Kind::Poe&&executing==e&&ld16(c->r[3]+0x482)==3){trace(*e,c->r[3],"possession-event");original(c,GC(0x025D7B24));return;}if(e->spec.kind==Kind::BokoBaba&&executing==e&&!ld8(c->r[3]+0x3E8)){st8(c->r[3]+0x3E7,0);c->r[3]=0;return;}disable(*e,c->r[3]);trace(*e,c->r[3],"event-denied");c->r[3]=0;return;}if(reserved(c->r[3])){c->r[3]=0;return;}original(c,GC(0x025D7B24));}
// A Kargaroc's rider let go (action 31, carry_drop: it drops, lands and fights) is its own release; the
// Bokoblin's version also sets event bit 0x0004 in the save, which an owned rider never keeps (the save's
// event byte is put back). Any other owned carry drop (Link never carries them) is refused.
uint32_t event_byte0(){const uint32_t info=ld32(GD(0x101F84DC));return memory(info,0x700)?info+0x644:0;}
void carry_drop(Cpu*c,uint32_t fn){if(auto*e=find(c->r[3])){
  if(executing==e&&params_of(e->spec)==story_enemy_variants::kRiderParams&&(e->spec.kind==Kind::Bokoblin||e->spec.kind==Kind::Moblin)){
   const uint32_t ev=event_byte0();const uint8_t kept=ev?ld8(ev):0;original(c,fn);if(ev&&ld8(ev)!=kept){st8(ev,kept);trace(*e,e->address,"rider-event-bit-kept");}return;}
  disable(*e,c->r[3]);trace(*e,c->r[3],"carry-drop-denied");return;}if(reserved(c->r[3]))return;original(c,fn);}
void bk_carry_drop(Cpu*c){carry_drop(c,GC(0x020A47B4));}void mo_carry_drop(Cpu*c){carry_drop(c,GC(0x021D0294));}
void water_fail(Cpu*c){const uint32_t a=c->r[3];auto*e=find(a);if(!e){if(reserved(a))return;original(c,GC(0x020A3B18));return;}if(e->spec.kind!=Kind::Bokoblin||executing!=e||ld16(a+0x49C+6)!=22){disable(*e,a);return;}if(ld16(a+0x49C+4)==1&&ld16(a+0x418+6)==0)claim(*e,a);original(c,GC(0x020A3B18));}
void fire(Cpu*c){const uint32_t h=c->r[3];if(!memory(h,4)){original(c,GC(0x02041570));return;}const uint32_t a=ld32(h);auto*e=find(a);if(!e){if(reserved(a))return;original(c,GC(0x02041570));return;}if(const auto*n=more::find(e->spec.kind)){if(executing!=e||!h||(h!=a+n->fire[0]&&h!=a+n->fire[1])||(!n->fire[0]&&!n->fire[1])){disable(*e,a);return;}original(c,GC(0x02041570));return;}const uint32_t offset=e->spec.kind==Kind::ChuChu?0xCDC:e->spec.kind==Kind::Bokoblin?0x1774:e->spec.kind==Kind::Moblin?0x320C:e->spec.kind==Kind::Miniblin?0xCB4:e->spec.kind==Kind::Peahat?0xE6C:e->spec.kind==Kind::Rat?0xF98:e->spec.kind==Kind::Darknut?0x1A2C:e->spec.kind==Kind::BokoBaba?0xED0:e->spec.kind==Kind::Bubble?0xBE4:e->spec.kind==Kind::Octorok?0xF5C:0xE90;if(executing!=e||(h!=a+offset&&!(e->spec.kind==Kind::Peahat&&h==a+0x1098))){disable(*e,a);return;}original(c,GC(0x02041570));}
void reaction(Cpu*c,uint32_t fn){const uint32_t h=c->r[3];if(!memory(h,4)){original(c,fn);return;}const uint32_t a=ld32(h);auto*e=find(a);if(!e){if(reserved(a)){c->r[3]=0;return;}original(c,fn);return;}const uint32_t offset=e->spec.kind==Kind::Bokoblin?0x49C:e->spec.kind==Kind::Moblin?0x8BC:e->spec.kind==Kind::Darknut?0x590:0;const unsigned type=e->spec.kind==Kind::Bokoblin?2:e->spec.kind==Kind::Darknut?3:1;if(!offset||h!=a+offset||executing!=e||ld16(h+8)!=type){disable(*e,a);c->r[3]=0;return;}if(fn==GC(0x02043F34)){DrowningScope current{e,h,false};struct Guard{DrowningScope*prior;Guard(DrowningScope*p):prior(drowning){drowning=p;}~Guard(){drowning=prior;}}guard(&current);original(c,fn);const Cpu result=*c;if(current.queued&&find(a)==e&&ld8(a+0xC)==3)claim(*e,a);*c=result;}else original(c,fn);}
void delete_request(Cpu*c){const uint32_t a=c->r[3],lr=c->lr;
 // an owned Gyorg deleting itself (deadCheckGy) once every shark died: its defeat
 if(Entry*g=executing;g&&g->spec.kind==Kind::Gyorg&&g->address==a&&find(a)==g&&ld8(a+more::kGyorgDeaths)>=more::gyorg_count(a)&&!story_items::enemy_defeated(g->marker))claim(*g,a);
auto*context=drowning;Entry*e=context?context->entry:nullptr;const bool exact=e&&e==executing&&e->spec.kind==Kind::Bokoblin&&context->helper==a+0x49C&&find(a)==e&&ld32(context->helper)==a&&ld16(context->helper+8)==2&&ld8(a+0x3D4)==0&&lr==GC(0x02044FCC)&&story_enemy_ownership::lookup_actor(a)==e->token;original(c,GC(0x025D57E0));if(exact&&c->r[3]==1&&ld8(a+0xC)==3)context->queued=true;}
void damage_reaction(Cpu*c){reaction(c,GC(0x02041F94));}void damage_set(Cpu*c){reaction(c,GC(0x02043F34));}
void room_search(Cpu*c){if(executing){auto*e=find(c->r[3]);c->r[3]=e&&!e->quarantine&&e->spec.mod==executing->spec.mod?c->r[3]:0;return;}if(reserved(c->r[3])){c->r[3]=0;return;}original(c,RoomSearch);}
}
const char* kind_name(Kind k){if(const auto*n=more::find(k))return n->name;return k==Kind::ChuChu?"chuchu":k==Kind::Bokoblin?"bokoblin":k==Kind::Moblin?"moblin":k==Kind::Keese?"keese":k==Kind::Miniblin?"miniblin":k==Kind::Peahat?"peahat":k==Kind::Rat?"rat":k==Kind::Darknut?"darknut":k==Kind::RatHole?"rat_hole":k==Kind::BokoBaba?"bokobaba":k==Kind::Bubble?"bubble":k==Kind::Octorok?"octorok":"";}
uint32_t parameters(Kind k,const std::string& variant){if(!story_enemy_variants::has_variants(kind_name(k)))return variant.empty()?parameters(k):0u;const auto*v=story_enemy_variants::find(kind_name(k),variant);return v?v->params:0u;}
uint32_t parameters(Token t){const auto*e=get(t);return e?params_of(e->spec):0u;}
uint16_t home_z(Token t){const auto*e=get(t);return !e?0:e->spec.home_z!=0xFFFF?e->spec.home_z:home_z(e->spec.kind);}
uint16_t home_x(Token t){const auto*e=get(t);return !e?0:e->spec.kind==Kind::Darknut?story_enemy_variants::angle_x("darknut",params_of(e->spec)):more::home_x(e->spec.kind);}  // a table kind: its row's (the warship's 0xFFFF)
bool configure_children(ChildBridge b,bool verified){if(installed)return false;if(verified&&(!b.enter||!b.leave||!b.ready||!b.retire))return false;children=std::move(b);weapon_admission=verified;return true;}
void child_retired(const story_enemy_ownership::LineageInfo&info){if(!info.source_valid||info.role!=story_enemy_ownership::Lineage::EnemyChild)return;auto*e=get(info.source);if(!e||!has_children(e->spec.kind)||e->pid!=info.source_pid||!info.source_actor||find(info.source_actor)!=e||story_enemy_ownership::lookup_actor(info.source_actor)!=e->token||story_enemy_ownership::source_epoch(e->token)!=info.epoch)return;const uint32_t a=info.source_actor;
 // a Darknut's sword (mBokoPcId 0x1560, with m0C34/m0C38/m140C its holding state) or cape (mMantPcId 0x1598)
 if(e->spec.kind==Kind::Wizzrobe||e->spec.kind==Kind::BigOcto)return;  // its fireballs or eyes refer to nothing it keeps
 if(e->spec.kind==Kind::Darknut){if(ld32(a+0x1560)==info.pid){st32(a+0x1560,0xFFFFFFFFu);st32(a+0xD84,0);st32(a+0xD88,0);st8(a+0x155C,0);}else if(ld32(a+0x1598)==info.pid)st32(a+0x1598,0xFFFFu);else return;trace(*e,a,"child-retired-native-loss",info.pid);return;}
 const uint32_t reference=e->spec.kind==Kind::Bokoblin?0x1360:0x2D60;if(ld32(a+reference)!=info.pid)return;
 if(e->spec.kind==Kind::Bokoblin){st32(a+0xC90,0);st32(a+0xC94,0);st8(a+0x1374,0);}else{st8(a+0x10B3,0);}
 st32(a+reference,0xFFFFFFFFu);trace(*e,a,"child-retired-native-loss",info.pid);}
bool supported(Kind k){if(const auto*n=more::find(k))return n->offered&&((k!=Kind::Stalfos&&k!=Kind::Wizzrobe&&k!=Kind::BigOcto)||weapon_admission);return k==Kind::ChuChu||k==Kind::Keese||k==Kind::Miniblin||k==Kind::Peahat||k==Kind::Rat||k==Kind::Octorok||k==Kind::Bubble||k==Kind::BokoBaba||k==Kind::RatHole||(weapon_admission&&has_children(k));}
bool verify_actor(Token t,Kind kind,uint32_t a){const auto*e=get(t);const auto&d=story_enemy_ownership::descriptor(kind);if(!e||kind!=e->spec.kind||!supported(kind)||!memory(a,d.bytes)||ld16(a+0xE)!=d.profile||ld32(a+0xB4)!=native_vtable(kind)||ld32(a+0xB0)!=params_of(e->spec)||ld8(a+0x2DD)!=0xFF||ld16(a+0x2D8)!=0xFFFF||ld16(a+0x2FC)!=home_z(t))return false;
 if(const auto*n=more::find(kind))return n->verify(a,params_of(e->spec))&&(!n->ice||ld8(a+n->ice+0x1B1)==0);
 if(kind==Kind::ChuChu)return ld8(a+0x3F8)==0&&ld8(a+0x3F9)==((params_of(e->spec)>>8)&0xFF)&&ld8(a+0x3FA)==0&&ld8(a+0x3FB)==0xFF&&(ld8(a+0x3FC)<=4||(black_chuchu(e->spec)&&ld8(a+0x3FC)==6))&&ld8(a+0x924+0x1B1)==0;
 if(kind==Kind::Bokoblin){const unsigned action=ld16(a+0x49C+6);const uint32_t p=params_of(e->spec);return ld8(a+0x3D0)==(p&0xF)&&ld8(a+0x3D1)==0xFF&&ld8(a+0x3D2)==((p>>16)&0xFF)&&ld8(a+0x3D3)==0xFF&&ld8(a+0x3D4)==0&&ld8(a+0x3D5)==0&&ld8(a+0x3D6)==0&&ld8(a+0x3F0)==(params_of(e->spec)&0x20u)&&ld8(a+0x3F1)==(params_of(e->spec)&0x40u)&&ld8(a+0x3F8)==0&&ld32(a+0x1378)==patrol_route(*e)&&(ld8(a+0x1375)==0||(story_enemy_variants::bokoblin_patrols(p)&&ld8(a+0x1375)==((story_enemy_variants::kPatrolRoute+1)&0xFF)))&&ld32(a+0x1380)==0&&ld8(a+0x138D)==0&&ld8(a+0x137D)==0&&((action<=23&&action!=15&&action!=16)||(p==story_enemy_variants::kRiderParams&&(action==30||action==31)))&&ld8(a+0x13BC+0x1B1)==0;}
 if(kind==Kind::Moblin){const unsigned action=ld16(a+0x8BC+6);const uint32_t p=params_of(e->spec);const bool patrol=story_enemy_variants::patrol_word(p);return ld8(a+0x3D8)==(p&0xFF)&&ld8(a+0x3D9)==((p>>8)&0xFF)&&ld8(a+0x3DA)==((p>>16)&0xFF)&&ld8(a+0x3DB)==0xFF&&ld8(a+0x3DC)==0&&ld8(a+0x3DD)==0&&(ld8(a+0x2D70)==0||(patrol&&ld8(a+0x2D70)==((story_enemy_variants::kPatrolRoute+1)&0xFF)))&&(ld8(a+0x2D72)==0||(patrol&&(ld8(a+0x2D72)==1||ld8(a+0x2D72)==0xFF)))&&ld32(a+0x2D74)==patrol_route(*e)&&ld16(a+0x2E4C)==0&&ld8(a+0x2E4E)==0&&ld16(a+0x2E50)==0&&ld8(a+0x2E21)==0&&ld8(a+0x2E0E)==0&&!(ld32(a+0x2E0)&0x04000000u)&&((action<=23&&action!=6)||(p==story_enemy_variants::kRiderParams&&(action==30||action==31)))&&ld8(a+0x2E54+0x1B1)==0;}
 if(kind==Kind::Rat)return ld8(a+0x3D0)==1&&ld8(a+0x3D1)==((params_of(e->spec)>>8)&0xFF)&&ld8(a+0xBE0+0x1B1)==0;  // hole-born (arg0 1), its own type
 if(kind==Kind::RatHole)return ld8(a+0x3D4)==0&&ld8(a+0x3D5)==1&&ld8(a+0x3D6)==1&&ld8(a+0x405)==0xFF&&ld32(a+0x3F4)==0;  // no range, one rat, no shop keeper, no path
 if(kind==Kind::Peahat)return ld8(a+0x45A)==(params_of(e->spec)&0xFF)&&ld8(a+0xAB4+0x1B1)==0;  // its declared type (Peahat 0, Seahat 1), unfrozen
 // Boko Baba: the ordinary one (variant 0: never a Baba Bud), never holding Link, the stem only while it dies, unfrozen
 if(kind==Kind::BokoBaba)return ld8(a+0x3DD)==0&&(ld8(a+0x3DC)==0||(ld8(a+0x3DC)==1&&ld8(a+0x3E0)==2))&&ld8(a+0x3E8)==0&&ld8(a+0xB18+0x1B1)==0;
 // Bubble: its declared colour (0 red, 1 blue), on the ground, no switch, no path, nothing to steal, unfrozen
 if(kind==Kind::Bubble)return ld8(a+0x3EC)==(params_of(e->spec)&0xFF)&&ld8(a+0x3ED)==0&&ld8(a+0x3F1)==0xFF&&ld8(a+0x405)==0xFF&&ld32(a+0x3F4)==0&&ld8(a+0x406)==0&&ld8(a+0x82C+0x1B1)==0;
 // Octorok: the small one (type 0) that spits rocks (m3DE 0), unfrozen
 if(kind==Kind::Octorok)return ld8(a+0x3DC)==0&&ld8(a+0x3DE)==0&&ld8(a+0xBA4+0x1B1)==0;
 // Darknut: guard behaviour 4, the variant's sight and colour, no path (pointer and index), no spawn or defeat
 // switch (the ice death switch too), the variant's equipment in place and in its x rotation word, never the
 // HIO owner, never a boss (no event camera or music), and an action short of the event-only ones (22: in deep
 // water, where the game leaves a Darknut standing: not a defeat, it comes back after a room change)
 if(kind==Kind::Darknut){const uint32_t p=params_of(e->spec);const uint16_t ax=story_enemy_variants::angle_x("darknut",p);const unsigned action=ld16(a+0x590+6);
  return story_enemy_variants::declared_params("darknut",p)&&ld8(a+0x3D8)==4&&ld8(a+0x3D9)==((p>>8)&0xFF)&&ld8(a+0x3DA)==0xFF&&ld8(a+0x3DB)==0xFF&&ld8(a+0x3DC)==0&&ld8(a+0x3DD)==0&&ld8(a+0x4DC)==((p>>4)&0xF)&&ld8(a+0x4DD)==((ax>>5)&7)&&ld16(a+0x2F8)==ax&&ld32(a+0x1578)==0&&ld8(a+0x1574)==0&&ld8(a+0x1582)==0&&ld8(a+0x1648)==0&&!(ld32(a+0x2E0)&0x04000000u)&&action<=22&&ld8(a+0x1674+0x1B1)==0;}
 if(kind==Kind::Miniblin)return ld8(a+0x3D0)==1&&ld8(a+0x3D1)==10&&ld8(a+0x3D2)==0&&ld8(a+0x3D3)==0xFF&&ld8(a+0x3D4)==0xFF&&ld8(a+0x3D6)==0&&ld8(a+0x8FC+0x1B1)==0;
 // Keese: mDamageType 0xAB9 is 0x80 for the Fire Keese variant (parameter bit 15), 0 for the ordinary one
 return ld8(a+0x484)==0&&ld8(a+0x485)==0&&ld8(a+0x486)==0xFF&&ld8(a+0x487)==0xFF&&ld8(a+0x48C)==0&&ld8(a+0x48D)==0&&ld32(a+0x490)==0&&(ld8(a+0x489)==0||ld8(a+0x489)==10||ld8(a+0x489)==20||ld8(a+0x489)==30)&&ld8(a+0xAB9)==((params_of(e->spec)>>8)&0x80u)&&ld8(a+0xAD8+0x1B1)==0;
}
Token declare(const Spec&s,std::string&why){if(installed||!supported(s.kind)||s.mod.empty()||s.id.empty()||s.defeated_flag.empty()||!s.epoch||entries.size()>=128){why="Unsupported or invalid native enemy declaration";return 0;}for(const auto&e:entries)if(e.spec.mod==s.mod&&(e.spec.id==s.id||e.spec.defeated_flag==s.defeated_flag)){why="Duplicate native enemy identity or defeat flag";return 0;}if(s.drops.size()>8){why="Too many native enemy drops";return 0;}for(size_t i=0;i<s.drops.size();++i){const auto&drop=s.drops[i];if(!drop.quantity||drop.quantity>254||!story_items::find(s.mod,drop.item)){why="Unavailable same-mod enemy item";return 0;}for(size_t j=0;j<i;++j)if(s.drops[j].item==drop.item){why="Duplicate native enemy item";return 0;}}
 auto marker=story_items::register_enemy_marker(s.mod,s.id,s.defeated_flag,why);if(!marker)return 0;Token t=story_enemy_ownership::declare_actor(s.kind,params_of(s),s.fingerprint,s.epoch,why);if(!t)return 0;entries.push_back({s,t,marker});return t;}
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
 if(params_of(s)!=params_of(e->spec)&&!story_enemy_ownership::editor_reparameter(t,params_of(s))){why="the enemy's ownership record is gone";return false;}
 if(!story_enemy_ownership::editor_refingerprint(t,s.fingerprint)){why="the enemy's ownership record is gone";return false;}
 const uint32_t epoch=e->spec.epoch;e->spec=s;e->spec.epoch=epoch;e->marker=marker;e->quarantine=false;return true;}
bool editor_refingerprint(Token t,const story_enemy_ownership::Fingerprint&fp){auto*e=get(t);if(!e||!story_enemy_ownership::editor_refingerprint(t,fp))return false;e->spec.fingerprint=fp;return true;}
void bind(Token t,uint32_t pid,bool restored){if(auto*e=get(t)){if(!restored&&!story_enemy_ownership::bind_pending(t,pid)){disable(*e,e->address);return;}e->pid=pid;e->address=0;e->quarantine=false;}}
namespace {bool routes_installed=false;const uint32_t RoomPath=GC(0x025AAF88);
std::function<uint32_t(Token,uint32_t,int)> room_path_provider;
// dPath_GetRoomPath(number, room): the game's own lookup first, always; only when it finds no path does
// an owned enemy that is running get its patrol route (the route number) or its mod path (its number)
void room_path(Cpu*c){const uint32_t number=c->r[3];const int room=int(int32_t(c->r[4]));original(c,RoomPath);if(c->r[3])return;Entry*e=executing;if(!e)return;
 if((e->spec.kind==Kind::Bokoblin||e->spec.kind==Kind::Moblin)&&story_enemy_variants::patrol_word(params_of(e->spec))&&number==story_enemy_variants::kPatrolRoute){if(route_provider)c->r[3]=route_provider(e->token);return;}
 if(room_path_provider)c->r[3]=room_path_provider(e->token,number,room);}}
// dPath_GetNextRoomPath(path, room): a mod path's record answers with the record of its next path (the
// game's own would look the number up in the room's table and assert); every other path is the game's.
std::function<bool(uint32_t,int,uint32_t&)> next_path_provider;const uint32_t NextRoomPath=GC(0x025AB070);
void next_room_path(Cpu*c){uint32_t out=0;if(next_path_provider&&next_path_provider(c->r[3],int(int32_t(c->r[4])),out)){c->r[3]=out;return;}original(c,NextRoomPath);}
void set_route_provider(std::function<uint32_t(Token)> route){route_provider=std::move(route);}
void set_room_path_provider(std::function<uint32_t(Token,uint32_t,int)> provider){room_path_provider=std::move(provider);}
void set_rider_provider(std::function<RiderRequest(Token)> request,std::function<void(Token,uint32_t)> created){rider_provider=std::move(request);rider_created=std::move(created);}
uint32_t room_path_function(){return RoomPath;}
bool install_routes(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(routes_installed)return true;if(!original||!owner(RoomPath).empty()||!owner(NextRoomPath).empty())return false;replace(RoomPath,room_path);replace(NextRoomPath,next_room_path);routes_installed=true;return true;}
void set_next_path_provider(std::function<bool(uint32_t,int,uint32_t&)> provider){next_path_provider=std::move(provider);}
void set_summon_provider(std::function<SummonRequest(Token,uint16_t,uint32_t)> request,std::function<void(Token,uint32_t)> created,std::function<std::vector<Token>(Token)> pool){summon_provider=std::move(request);summon_created=std::move(created);summon_pool=std::move(pool);}
// fopAcM_create(name, word, pos, room, angle, scale, subtype, ...) while an owned summoner's door runs: the
// summon becomes a free member of the summoner's pool (its private profile, fixed word and z angle), bound
// to the new process; with no free member (or a kind outside the pool) it is refused, as a full game would.
bool summon_create(Cpu*c){if(!fireball_executing)return false;Entry*s=wz_child_summoner(fireball_executing,more::kWizzrobeDoorParams);if(!s)return false;
 const uint16_t name=uint16_t(c->r[3]);const SummonRequest r=summon_provider?summon_provider(s->token,name,c->r[4]):SummonRequest{};
 if(!r.summon||!r.profile){static unsigned told=0;if(told++<32)trace(*s,s->address,"summon-refused",name);c->r[3]=0xFFFFFFFFu;return true;}
 c->r[3]=r.profile;c->r[4]=r.params;if(memory(c->r[7],8))st16(c->r[7]+4,r.home_z);original(c,GC(0x025D5834));const uint32_t pid=c->r[3];
 if(pid!=0xFFFFFFFFu&&summon_created)summon_created(r.summon,pid);if(enemy_trace())trace(*s,s->address,"summoned",pid);
 // diagnostics: WWHD_SUMMON_HEAPS=1 logs the heaps' free bytes at each summon (a full wave's cost)
 static const bool heaps=[]{const char*v=std::getenv("WWHD_SUMMON_HEAPS");return v&&*v=='1';}();
 if(heaps){Cpu probe=*c;const auto f=native_actor_profiles::heap_free(&probe);unsigned live=0;if(summon_pool)for(Token t:summon_pool(s->token)){auto*m=get(t);if(m&&m->pid!=0xFFFFFFFFu)++live;}
  LOG("[summon_heaps] %s summons=%u free archive=%u game=%u zelda=%u command=%u actor=%u",s->spec.id.c_str(),live,f[0],f[1],f[2],f[3],f[4]);}
 return true;}
bool is_defeated(Token t){auto*e=get(t);return e&&defeated(*e);}
// an enemy the port retires as defeated without its own defeat (a closed rat hole): its marker, no drops;
// its actor removes itself on its next step like any defeated one
bool settle(Token t){auto*e=get(t);if(!e||e->quarantine||story_items::enemy_defeated(e->marker))return false;const auto result=story_items::claim_enemy_drops(e->marker,e->spec.mod,{});if(e->address)trace(*e,e->address,"settle",unsigned(result));return result==story_items::EnemyClaimStatus::Committed;}
bool available(Token t){auto*e=get(t);return e&&!e->quarantine&&(e->spec.transient||!story_items::enemy_defeated(e->marker));}
void quarantine(Token t){if(auto*e=get(t))disable(*e,e->address);}
bool begin_epoch(Token t,uint32_t epoch){auto*e=get(t);if(!e||!story_enemy_ownership::begin_epoch(t,epoch))return false;e->spec.epoch=epoch;e->pid=0xFFFFFFFFu;e->address=0;e->quarantine=false;e->transient_defeated=false;return true;}
void state_loaded(){executing=ice_owner=nullptr;drowning=nullptr;for(auto&e:entries){e.address=0;e.pid=0xFFFFFFFFu;e.quarantine=false;e.claiming=false;}}
bool owns_actor(uint32_t a){return find(a)!=nullptr;}
bool deny_create(uint32_t,uint32_t){return executing&&executing->spec.kind==Kind::BokoBaba;}
bool deny_carry(uint32_t a){const Entry*e=find(a,false);return e&&!liftable(e->spec,a);}
void configure(void(*o)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>),const std::array<uint32_t,story_enemy_ownership::kKindCount>&storage,std::function<void(Token,uint32_t)>notify){original=o;invoke=call;profiles=storage;retired=std::move(notify);native_actor_profiles::configure(o,call);}
bool install(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(installed)return true;if((entries.empty()&&!editor_allowed)||!original||!invoke||!native_actor_profiles::available(owner))return false;
 std::vector<std::pair<uint32_t,void(*)(Cpu*)>> hooks={{BKcreate,bk_create},{BKexec,bk_exec},{BKdraw,bk_draw},{BKdelete,bk_delete},{MOcreate,mo_create},{MOexec,mo_exec},{MOdraw,mo_draw},{MOdelete,mo_delete},{CCcreate,cc_create},{CCexec,cc_exec},{CCdraw,cc_draw},{CCdelete,cc_delete},{KIcreate,ki_create},{KIexec,ki_exec},{KIdraw,ki_draw},{KIdelete,ki_delete},{PTcreate,pt_create},{PTexec,pt_exec},{PTdraw,pt_draw},{PTdelete,pt_delete},{TNcreate,tn_create},{TNexec,tn_exec},{TNdraw,tn_draw},{TNdelete,tn_delete},{PHcreate,ph_create},{PHexec,ph_exec},{PHdraw,ph_draw},{PHdelete,ph_delete},{NZcreate,nz_create},{NZexec,nz_exec},{NZdraw,nz_draw},{NZdelete,nz_delete},{NGcreate,ng_create},{NGexec,ng_exec},{NGdraw,ng_draw},{NGdelete,ng_delete},{BOcreate,bo_create},{BOexec,bo_exec},{BOdraw,bo_draw},{BOdelete,bo_delete},{BLcreate,bl_create},{BLexec,bl_exec},{BLdraw,bl_draw},{BLdelete,bl_delete},{OQcreate,oq_create},{OQexec,oq_exec},{OQdraw,oq_draw},{OQdelete,oq_delete},{Disappear,disappear},{Ice,ice},{TableItem,table_item},{KiSearch,ki_search},{RoomSearch,room_search},{GC(0x021C9AA0),mo_capture},{GC(0x025D7B24),potential_event},{GC(0x020A47B4),bk_carry_drop},{GC(0x021D0294),mo_carry_drop},{GC(0x020A3B18),water_fail},{GC(0x02041570),fire},{GC(0x02041F94),damage_reaction},{GC(0x02043F34),damage_set},{GC(0x025D57E0),delete_request},{GC(more::kMorthDeathEffect),morth_death},{GC(more::kStageLayer),stage_layer},{GC(more::kCreateChild),create_child},{GC(more::kAreaManagerSet),area_set},{GC(more::kFastCreate),fast_create},{GC(more::kSceneRequest),scene_request},{GC(more::kWallCannonDeleteInit),wall_cannon_delete_init},{GC(more::kGyDeathInit),gy_death_init},{GC(more::kGyBombDeathInit),gy_bomb_death_init},{GC(more::kFmModeProc),fm_mode},{GC(more::kFmSearchOther),fm_search_other},{GC(more::kOctoModeProc),octo_mode},{GC(more::kOctoDeathInit),octo_death}};
 for(const auto&h:child_hooks(std::make_index_sequence<std::size(more::kChildClasses)>{}))hooks.push_back(h);  // their owned children's classes
 for(const auto&h:more_hooks(std::make_index_sequence<std::size(more::kNatives)>{}))hooks.push_back(h);  // the table kinds' methods (a retail copy passes through)
 for(const auto&h:hooks)if(!owner(h.first).empty())return false;
 for(Kind k:all_kinds()){if(!supported(k))continue;const auto&d=story_enemy_ownership::descriptor(k);uint32_t storage=profiles[unsigned(k)],native=ld32(GD(0x101F3EE0)+4*d.native_profile);if(!native_actor_profiles::valid_storage(storage)||!memory(native,0x30)||ld32(native+0x10)!=d.bytes)return false;}
 for(Kind k:all_kinds()){if(!supported(k))continue;const auto&d=story_enemy_ownership::descriptor(k);uint32_t storage=profiles[unsigned(k)],native=ld32(GD(0x101F3EE0)+4*d.native_profile);std::memcpy(mem::ptr(storage),mem::ptr(native),0x30);st16(storage+8,d.profile);if(!native_actor_profiles::add(d.profile,storage))return false;}
 if(!native_actor_profiles::install(owner,replace))return false;for(const auto&h:hooks)replace(h.first,h.second);installed=true;return true;
}
}
