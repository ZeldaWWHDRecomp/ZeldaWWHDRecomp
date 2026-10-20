// SPDX-License-Identifier: MPL-2.0
#include "story_enemy_ownership.h"
#include "story_enemies_more.h"
#include "runtime.h"
#include "guest_addr.h"
#include <algorithm>
#include <cstdlib>
#include <vector>
#include <utility>
namespace story_enemy_ownership { namespace {
// Audited native class fields, including shared ice/fire helpers. helper is the
// embedded response object whose first field must point back to the same actor.
constexpr Collider cc[]={{0x7C4,0x788,0},{0x990,0x954,0x924},{0xDB8,0xD7C,0xCDC}};
constexpr Collider bk[]={{0xCE8,0xBF4,0},{0xE18,0xBF4,0},{0xF48,0xBF4,0},{0x1074,0xBF4,0},{0x11A0,0xBF4,0},{0x1428,0x13EC,0x13BC},{0x1850,0x1814,0x1774}};
constexpr Collider mo[]={{0x2494,0x1014,0},{0x25C4,0x1014,0},{0x26F4,0x1014,0},{0x2820,0x1014,0},{0x294C,0x1014,0},{0x2A78,0x1014,0},{0x2EC0,0x2E84,0x2E54},{0x32E8,0x32AC,0x320C}};
// d_a_pt (Miniblin): body and attack spheres share mStts 0x644; enemyice 0x8FC, enemyfire 0xCB4.
constexpr Collider pt[]={{0x680,0x644,0},{0x7AC,0x644,0},{0x968,0x92C,0x8FC},{0xD90,0xD54,0xCB4}};
// d_a_ph (Peahat): body sphere, attack and target cylinders on mStts 0x6EC; enemyice 0xAB4; two enemyfire
// helpers (body 0xE6C, propeller 0x1098).
constexpr Collider ph[]={{0x728,0x6EC,0},{0x854,0x6EC,0},{0x984,0x6EC,0},{0xB20,0xAE4,0xAB4},{0xF48,0xF0C,0xE6C},{0x1174,0x1138,0x1098}};
// d_a_nz (Rat): body cylinder on mStts 0xA60; enemyice 0xBE0; enemyfire 0xF98. The adapter is complete but
// not offered to mods: a rat created without its hole parent runs off in a straight line with no gravity
// (daNZ_Create) instead of going for Link.
constexpr Collider nz[]={{0xA9C,0xA60,0},{0xC4C,0xC10,0xBE0},{0x1074,0x1038,0xF98}};
// d_a_tn (Darknut): co/tg cylinders, head, defence and the two weapon spheres on the damage reaction's
// mStts 0xCE8 (0x590+0x758); enemyice 0x1674, enemyfire 0x1A2C.
constexpr Collider tn[]={{0xDC4,0xCE8,0},{0xEF4,0xCE8,0},{0x1024,0xCE8,0},{0x1150,0xCE8,0},{0x127C,0xCE8,0},{0x13A8,0xCE8,0},{0x16E0,0x16A4,0x1674},{0x1B08,0x1ACC,0x1A2C}};
// d_a_nzg (rat hole): its body cylinder on mStts 0x408 (only pushes; it has no attack).
constexpr Collider nzg[]={{0x444,0x408,0}};
constexpr Collider ki[]={{0x734,0x6F8,0},{0x860,0x6F8,0},{0x98C,0x6F8,0},{0xB44,0xB08,0xAD8},{0xF6C,0xF30,0xE90}};
// d_a_bo (Boko Baba): head sphere (the bite), base sphere and stem cylinder (the killing hurtbox) on mStts 0x754;
// enemyice 0xB18, enemyfire 0xED0.
constexpr Collider bo[]={{0x790,0x754,0},{0x8BC,0x754,0},{0x9E8,0x754,0},{0xB84,0xB48,0xB18},{0xFAC,0xF70,0xED0}};
// d_a_bl (Bubble): one body sphere (attack, target and contact) on mStts 0x64C; enemyice 0x82C, enemyfire 0xBE4.
constexpr Collider bl[]={{0x688,0x64C,0},{0x898,0x85C,0x82C},{0xCC0,0xC84,0xBE4}};
// d_a_oq (Octorok, type 0): body cylinder and bite cylinder on mStts 0x6B0; enemyice 0xBA4, enemyfire 0xF5C. Its rock
// is a separate retail actor (the same class, type 6), not one of these colliders.
constexpr Collider oq[]={{0x6EC,0x6B0,0},{0x81C,0x6B0,0},{0xC10,0xBD4,0xBA4},{0x1038,0xFFC,0xF5C}};
constexpr Descriptor descriptors[]={{0x7FFA,0xCE,0xF10,cc,3},{0x7FF9,0xBD,0x19A4,bk,7},{0x7FF8,0xBC,0x343C,mo,8},{0x7FF7,0xD7,0x10BC,ki,5},{0x7FF4,0xF7,0xEE4,pt,4},{0x7FF3,0xD1,0x12D8,ph,6},{0x7FF2,0xC6,0x11CC,nz,3},{0x7FF1,0xBF,0x1C5C,tn,8},{0x7FF0,0xC7,0x574,nzg,1},{0x7FEA,0xD6,0x1104,bo,5},{0x7FE9,0xCF,0xE18,bl,3},{0x7FE8,0xE3,0x1194,oq,4}};
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
bool reserved(uint16_t p){return (p>=0x7FF7&&p<=0x7FFA)||(p>=0x7FF0&&p<=0x7FF4)||p==0x7FEA||p==0x7FE9||p==0x7FE8||story_enemies_more::reserved(p);}
bool identity(const Entry& e,uint32_t a){auto& d=descriptor(e.kind);return memory(a,d.bytes)&&e.pid&&e.pid!=0xFFFFFFFFu&&ld32(a+4)==e.pid&&ld16(a+0xE)==d.profile&&ld32(a+0xB0)==e.params&&ld8(a+0x2DD)==0xFF;}
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
// a Darknut cape's colliders: the wind sphere and nine mesh spheres on its mStts 0x365C
bool cape_object(uint32_t a,uint32_t object){if(object==a+0x3698)return true;for(uint32_t i=0;i<9;++i)if(object==a+0x37C4+i*0x12C)return true;return false;}
// the class extent of a table kind's child of its own class (story_enemies_more.h)
uint32_t table_child_bytes(uint16_t profile){return profile==story_enemies_more::kWizzrobeFire?story_enemies_more::kWizzrobeBytes:profile==story_enemies_more::kGy?story_enemies_more::kGyBytes:profile==story_enemies_more::kOctoEye?story_enemies_more::kOctoEyeBytes:0u;}
bool owned_child_object(uint32_t object){
 if(arena)for(size_t n=0;n<kChildCapacity;++n){uint32_t r=arena+LineageOffset+uint32_t(n)*64;if(ld32(r)!=Magic||ld32(r+8)!=unsigned(Lineage::EnemyChild))continue;uint32_t a=ld32(r+40),pid=ld32(r+20);if(pid&&pid!=0xFFFFFFFFu&&memory(a,0xA34)&&ld32(a+4)==pid&&ld16(a+0xE)==0x1CF&&object>=a&&object<a+0xA34)return true;if(pid&&pid!=0xFFFFFFFFu&&memory(a,kCapeBytes)&&ld32(a+4)==pid&&ld16(a+0xE)==kCapeProfile&&object>=a&&object<a+kCapeBytes)return true;}
 // a rat's bomb (daBomb_c, 0x126): its explosion sphere 0x920 on mStts 0x8E4
 if(arena)for(size_t n=0;n<kChildCapacity;++n){uint32_t r=arena+LineageOffset+uint32_t(n)*64;if(ld32(r)!=Magic||ld32(r+8)!=unsigned(Lineage::EnemyChild)||ld32(r+28)!=0x126)continue;uint32_t a=ld32(r+40),pid=ld32(r+20);if(pid&&pid!=0xFFFFFFFFu&&memory(a,0xB50)&&ld32(a+4)==pid&&ld16(a+0xE)==0x126&&object==a+0x920&&ld32(object+0x44)==a+0x8E4)return true;}
 // the table kinds' children of their own classes (a Wizzrobe's fireballs): the whole class extent
 if(arena)for(size_t n=0;n<kChildCapacity;++n){uint32_t r=arena+LineageOffset+uint32_t(n)*64;if(ld32(r)!=Magic||ld32(r+8)!=unsigned(Lineage::EnemyChild))continue;const uint32_t bytes=table_child_bytes(uint16_t(ld32(r+28)));uint32_t a=ld32(r+40),pid=ld32(r+20);if(bytes&&pid&&pid!=0xFFFFFFFFu&&memory(a,bytes)&&ld32(a+4)==pid&&uint32_t(ld16(a+0xE))==ld32(r+28)&&object>=a&&object<a+bytes)return true;}
 // A damaged saved address must not erase a claimed child. The actual native
 // Stts owner and PID confer denial only; a recycled actor with a new PID passes.
 if(memory(object,0x48)){uint32_t stts=ld32(object+0x44);if(memory(stts,0x14)){uint32_t a=ld32(stts+0xC);if(memory(a,story_enemies_more::kWizzrobeBytes)&&ld16(a+0xE)==story_enemies_more::kWizzrobeFire&&stts==a+story_enemies_more::kWizzrobeStts&&ld32(stts+0x10)==ld32(a+4)&&lineage_claimed(ld32(a+4),Lineage::EnemyChild))return true;if(memory(a,story_enemies_more::kGyBytes)&&ld16(a+0xE)==story_enemies_more::kGy&&(stts==a+story_enemies_more::kGyStts||stts==a+story_enemies_more::kGyIceStts)&&lineage_claimed(ld32(a+4),Lineage::EnemyChild))return true;if(memory(a,story_enemies_more::kOctoEyeBytes)&&ld16(a+0xE)==story_enemies_more::kOctoEye&&stts==a+story_enemies_more::kOctoEyeStts&&lineage_claimed(ld32(a+4),Lineage::EnemyChild))return true;if(memory(a,0xA34)&&ld16(a+0xE)==0x1CF&&stts==a+0x6C4&&(object==a+0x700||object==a+0x82C)&&ld32(stts+0x10)==ld32(a+4)&&lineage_claimed(ld32(a+4),Lineage::EnemyChild))return true;if(memory(a,kCapeBytes)&&ld16(a+0xE)==kCapeProfile&&stts==a+0x365C&&cape_object(a,object)&&ld32(stts+0x10)==ld32(a+4)&&lineage_claimed(ld32(a+4),Lineage::EnemyChild))return true;}}
 return false;
}
// Link's boat (the play's third player pointer): a collider whose Stts owner is that actor.
uint32_t play_ship(){return ld32(GD(0x1046F0B0)+0x5B3C);}
bool ship_object(uint32_t object){if(!memory(object,0x48))return false;const uint32_t stts=ld32(object+0x44);if(!memory(stts,0x14))return false;const uint32_t ship=play_ship();return ship&&memory(ship,0x10)&&ld32(stts+0xC)==ship;}
// A cannonball sphere (the game's BOMB): `params` tells an owned warship's (enemy cannonball) from the one
// Link's boat fires. The low byte is the bomb's state (change_state rewrites it in flight and when it
// explodes): only the bits above it are compared.
bool cannonball(uint32_t object,uint32_t params,uint32_t& actor){
 namespace more=story_enemies_more;if(!memory(object,0x48))return false;const uint32_t stts=ld32(object+0x44);if(!memory(stts,0x14))return false;const uint32_t a=ld32(stts+0xC);
 if(!memory(a,more::kCannonballBytes)||object!=a+more::kCannonballSphere||stts!=a+more::kCannonballStts||ld16(a+0xE)!=more::kCannonball||(ld32(a+0xB0)&0xFFFFFF00u)!=(params&0xFFFFFF00u)||ld32(stts+0x10)!=ld32(a+4))return false;actor=a;return true;}
// An owned warship's (or wall cannon's) cannonball: its exact lineage child of a live owned warship or wall
// cannon; it may hit Link and his boat only.
bool warship_cannonball(uint32_t object){
 namespace more=story_enemies_more;uint32_t a=0;LineageInfo info{};if(!cannonball(object,more::kCannonballParams,a)||!lineage_info(ld32(a+4),info)||info.role!=Lineage::EnemyChild||info.address!=a||!info.source_valid||info.profile!=more::kCannonball||info.parameters!=more::kCannonballParams)return false;
 auto*source=get(info.source);return source&&(source->kind==Kind::Warship||source->kind==Kind::WallCannon)&&valid(*source)&&source->address==info.source_actor&&source->pid==info.source_pid&&source->epoch==info.epoch;
}
bool bomb_object(uint32_t object);
// any bomb whose lineage names a warship as its source: only the warship's rule applies to it
bool warship_bomb(uint32_t object){if(!bomb_object(object))return false;const uint32_t a=ld32(ld32(object+0x44)+0xC);LineageInfo info{};if(!lineage_info(ld32(a+4),info)||info.role!=Lineage::EnemyChild)return false;auto*source=get(info.source);return source&&(source->kind==Kind::Warship||source->kind==Kind::WallCannon);}
// The cannonball Link's boat fires (never a claimed child): like Link's own weapons it may hit an owned enemy.
bool boat_cannonball(uint32_t object){uint32_t a=0;return cannonball(object,story_enemies_more::kBoatCannonballParams,a)&&!lineage_claimed(ld32(a+4),Lineage::EnemyChild);}
// A collider of a table kind's owned child (a Gyorg shark, a Big Octo eye): its exact lineage child of a
// live owned parent of that kind.
const story_enemies_more::ChildClass* table_child(uint32_t object){
 namespace more=story_enemies_more;if(!memory(object,0x48))return nullptr;const uint32_t stts=ld32(object+0x44);if(!memory(stts,0x14))return nullptr;const uint32_t a=ld32(stts+0xC);LineageInfo info{};
 if(!memory(a,0x10))return nullptr;const auto*k=more::child_class(ld16(a+0xE));if(!k||!memory(a,k->bytes)||stts<a||stts>=a+k->bytes||object<a||object>=a+k->bytes||!lineage_info(ld32(a+4),info)||info.role!=Lineage::EnemyChild||info.address!=a||!info.source_valid||info.profile!=k->profile)return nullptr;
 auto*source=get(info.source);return source&&source->kind==k->parent&&valid(*source)&&source->address==info.source_actor&&source->pid==info.source_pid&&source->epoch==info.epoch?k:nullptr;
}
// A Wizzrobe's fireball sphere: an owned child of a live owned Wizzrobe (its exact lineage, parameters,
// class and collider), which may hit Link and nothing else.
bool wizzrobe_fire(uint32_t object){
 namespace more=story_enemies_more;if(!memory(object,0x48))return false;const uint32_t stts=ld32(object+0x44);if(!memory(stts,0x14))return false;const uint32_t a=ld32(stts+0xC);LineageInfo info{};
 if(!memory(a,more::kWizzrobeBytes)||object!=a+more::kWizzrobeFireSphere||stts!=a+more::kWizzrobeStts||!lookup_lineage_actor(a,Lineage::EnemyChild,info)||!info.source_valid||info.profile!=more::kWizzrobeFire||info.parameters!=more::kWizzrobeFireParams||ld32(stts+0x10)!=info.pid||ld32(a+0xB4)!=GD(0x10042D90)||ld8(a+0x464)!=10)return false;
 auto*source=get(info.source);return source&&source->kind==Kind::Wizzrobe&&valid(*source)&&source->address==info.source_actor&&source->pid==info.source_pid&&source->epoch==info.epoch;
}
bool fire_child(uint32_t object,int16_t& timer){
 if(!memory(object,0x48))return false;uint32_t stts=ld32(object+0x44);if(!memory(stts,0x14))return false;uint32_t a=ld32(stts+0xC);LineageInfo info{};
 if(!memory(a,0xA34)||object!=a+0x700||stts!=a+0x6C4||!lookup_lineage_actor(a,Lineage::EnemyChild,info)||!info.source_valid||info.profile!=0x1CF||info.parameters!=0||ld32(stts+0x10)!=info.pid||ld8(a+0x2DD)!=0xFF||ld32(a+0x2E8)!=0xFFFFFFFFu||ld32(a+0xB4)!=GD(0x1000A478)||ld8(a+0x430)>2||ld8(a+0x431)||ld8(a+0x432)||ld32(object+0x10)!=0x20000||ld8(object+0x14)!=0)return false;
 auto*source=get(info.source);if(!source||source->kind!=Kind::Bokoblin||(source->params&~0x20u)!=0xFFFFFF04u||!valid(*source)||source->address!=info.source_actor||source->pid!=info.source_pid||source->epoch!=info.epoch)return false;
 timer=int16_t(ld16(a+0x43C));return timer>=0&&timer<=900;
}
// An admitted cape of a declared Darknut: Link's own sword may cut or burn it (the cape's hit response
// changes only the cape: its texture, a particle and a sound). Every other pair with a cape is rejected.
bool live_cape(uint32_t object){
 if(!memory(object,0x48))return false;uint32_t stts=ld32(object+0x44);if(!memory(stts,0x14))return false;uint32_t a=ld32(stts+0xC);LineageInfo info{};
 return memory(a,kCapeBytes)&&stts==a+0x365C&&cape_object(a,object)&&lookup_lineage_actor(a,Lineage::EnemyChild,info)&&info.source_valid&&info.profile==kCapeProfile&&info.parameters==0&&ld32(stts+0x10)==info.pid&&get(info.source)&&get(info.source)->kind==Kind::Darknut;
}
bool bomb_object(uint32_t object){if(!memory(object,0x48))return false;const uint32_t stts=ld32(object+0x44);if(!memory(stts,0x14))return false;const uint32_t a=ld32(stts+0xC);return memory(a,0xB50)&&ld16(a+0xE)==0x126&&object==a+0x920&&stts==a+0x8E4;}
// An Octorok's rock (d_a_oq type 6, created with parameters 6; its spheres 0x94C and 0xA78 on mStts 0x6B0) is a game
// actor of its own. Its hit on an owned Octorok's body goes to the game's own rules, as in the original (a rock
// reflected with the shield defeats the Octorok); every other pair with an owned enemy stays refused.
bool octorok_rock(uint32_t object){if(!memory(object,0x48))return false;const uint32_t stts=ld32(object+0x44);if(!memory(stts,0x14))return false;const uint32_t a=ld32(stts+0xC);return memory(a,0x1194)&&ld16(a+0xE)==0xE3&&ld32(a+0xB0)==6&&ld8(a+0x3DC)==6&&(object==a+0x94C||object==a+0xA78)&&stts==a+0x6B0&&ld32(a+0xB4)==GD(0x10033E10)&&ld32(stts+0x10)==ld32(a+4);}
bool ignition_source(uint32_t object){if(!memory(object,0x48))return false;if(link_object(object))return true;if(projectile_enabled&&projectile_verifier)try{return projectile_verifier(object);}catch(...){}return false;}
// test aid: WWHD_ENEMY_TRACE=1 logs what a rat's bomb blast meets (the first 64): the actor's
// profile and whether the game handles the hit (Link, the mod's enemies) or it is refused
void trace_blast(uint32_t other,bool hits){static const bool on=[]{const char*v=std::getenv("WWHD_ENEMY_TRACE");return v&&*v=='1';}();static unsigned n=0;if(!on||n>=64)return;++n;
 const uint32_t stts=memory(other,0x48)?ld32(other+0x44):0,a=memory(stts,0x14)?ld32(stts+0xC):0;LOG("[native_enemy] bomb blast meets actor %08X profile %04X: %s",a,memory(a,0x10)?unsigned(ld16(a+0xE)):0xFFFFu,hits?"hit":"refused");}
// test aid: WWHD_PAIR_TRACE=1 logs refused pairs whose target is an owned enemy (attacker actor profile and parameters)
bool pair_trace(){static const bool on=[]{const char*v=std::getenv("WWHD_PAIR_TRACE");return v&&*v=='1';}();return on;}
void trace_refused(uint32_t at,uint32_t tg){if(!pair_trace()||!lookup_collider(tg)||!memory(at,0x48))return;const uint32_t s=ld32(at+0x44);const uint32_t a=memory(s,0x14)?ld32(s+0xC):0;static unsigned n=0;if(n++<400){bool projectile=false;if(projectile_verifier)try{projectile=projectile_verifier(at);}catch(...){}LOG("[enemy_pair] refused at=%08X actor=%08X profile=%04X params=%08X type=%08X power=%u link_projectile=%d target=%08X",at,a,memory(a,0x10)?unsigned(ld16(a+0xE)):0u,memory(a,0xB4)?ld32(a+0xB0):0u,ld32(at+0x10),unsigned(ld8(at+0x14)),int(projectile),tg);}}
void trace_projectile(uint32_t at,uint32_t tg){if(!pair_trace()||!projectile_verifier||!lookup_collider(tg))return;bool own=false;try{own=projectile_verifier(at);}catch(...){}static unsigned n=0;if(own&&n++<400){const uint32_t a=ld32(ld32(at+0x44)+0xC);LOG("[enemy_pair] link projectile hits at=%08X profile=%04X params=%08X target=%08X",at,unsigned(ld16(a+0xE)),ld32(a+0xB0),tg);}}
void hook(Cpu*c){if(pair_policy(c->r[4],c->r[5])==PairPolicy::Reject){trace_refused(c->r[4],c->r[5]);c->r[3]=1;}else{trace_projectile(c->r[4],c->r[5]);original(c,NoAtTg);}}
}
const Descriptor& descriptor(Kind k){static constexpr Descriptor invalid={0,0,0,nullptr,0};if(const auto*n=story_enemies_more::find(k))return n->descriptor;return unsigned(k)<std::size(descriptors)?descriptors[unsigned(k)]:invalid;}
bool configure(uint32_t a,uint32_t bytes,void(*fn)(Cpu*,uint32_t),VerifyActor verify){if(bytes<kStorageBytes||!runtime_memory(a,kStorageBytes)||!fn||!verify)return false;arena=a;original=fn;verify_actor=std::move(verify);return true;}
void initialize_storage(){if(!arena)return;for(uint32_t i=0;i<kStorageBytes;++i)st8(arena+i,0);st32(arena,Magic);st32(arena+4,Version);for(const auto&e:entries)write_pending(e);}
Token declare_actor(Kind k,uint32_t params,const Fingerprint& fp,uint32_t epoch,std::string& why){if(unsigned(k)>=kKindCount||!descriptor(k).bytes||entries.size()>=kActorCapacity||!epoch||!next_token){why="Invalid or full enemy ownership declaration";return 0;}Token t=next_token++;entries.push_back({k,params,epoch,fp,t});if(arena&&ld32(arena)==Magic&&ld32(arena+4)==Version)write_pending(entries.back());return t;}
size_t checkpoint(){return entries.size();}
bool editor_refingerprint(Token t,const Fingerprint& fp){auto*e=get(t);if(!e)return false;e->fingerprint=fp;if(arena&&ld32(arena)==Magic&&ld32(arena+4)==Version){const uint32_t r=record(*e);const uint32_t magic=ld32(r);write_record(*e);if(magic==RetiredMagic||magic==PendingMagic){st32(r,magic);st32(r+124,checksum(r,124));}}return true;}
bool editor_reparameter(Token t,uint32_t params){auto*e=get(t);if(!e||e->active||e->address||!params)return false;e->params=params;if(arena&&ld32(arena)==Magic&&ld32(arena+4)==Version){const uint32_t r=record(*e);const uint32_t magic=ld32(r);write_record(*e);if(magic==RetiredMagic||magic==PendingMagic){st32(r,magic);st32(r+124,checksum(r,124));}}return true;}
void rollback(size_t n)noexcept{if(n<=entries.size())entries.resize(n);}
bool binding_state(Token token,Binding& out){out={};auto*e=get(token);if(!e)return false;out.epoch=e->epoch;if(!arena||ld32(arena)!=Magic||ld32(arena+4)!=Version)return true;out.state=BindingState::Quarantined;if(!definition_matches(*e,true))return true;uint32_t r=record(*e);if(ld32(r)==RetiredMagic){out.state=BindingState::Retired;return true;}if(e->quarantined)return true;out.pid=e->pid;out.actor=e->address;if(e->active){if(valid(*e))out.state=BindingState::Active;return true;}if(ld32(r)==PendingMagic){if(!e->pid&&!e->address)out.state=BindingState::Fresh;else if(e->pid&&e->pid!=0xFFFFFFFFu)out.state=BindingState::Queued;}return true;}
bool can_spawn(Token token){Binding state;return binding_state(token,state)&&state.state==BindingState::Fresh;}
bool bind_pending(Token t,uint32_t pid){auto*e=get(t);if(!e||e->active||e->quarantined||!pid||pid==0xFFFFFFFFu||(e->pid&&e->pid!=pid))return false;for(auto& x:entries)if(x.token!=t&&x.pid==pid)return false;e->pid=pid;if(arena)write_pending(*e);return true;}
bool admit_created(Token t,uint32_t a){auto*e=get(t);if(!arena||!e||e->active||e->quarantined||!checked(*e,a)||(e->address&&e->address!=a))return false;e->address=a;e->active=true;write_record(*e);return true;}
bool enroll_colliders(Token t){auto*e=get(t);if(!e||!valid(*e))return false;auto&d=descriptor(e->kind);if(!d.collider_count)return true;  // a kind without colliders (the Gyorg controller)
 for(size_t i=0;i<d.collider_count;++i)if(object_valid(*e,d.colliders[i]))return true;return false;}
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
const bool child_at=owned_child_object(at),child_tg=owned_child_object(tg);if(child_at||child_tg){if(child_at&&child_tg)return PairPolicy::Reject;
  // an owned warship's cannonball hits Link and his boat only (before the bomb rule below: it is a BOMB too)
  if(child_at&&warship_bomb(at))return warship_cannonball(at)&&(link_object(tg)||ship_object(tg))?PairPolicy::Delegate:PairPolicy::Reject;
  // a Gyorg shark bites Link and his boat; only Link's weapons, his projectiles and the boat's cannonball hit it
  if(child_at){const auto*k=table_child(at);if(k&&k->profile==story_enemies_more::kGy)return link_object(tg)||ship_object(tg)?PairPolicy::Delegate:PairPolicy::Reject;}
  if(child_tg&&table_child(tg)){if(link_object(at)||boat_cannonball(at))return PairPolicy::Delegate;if(projectile_enabled&&projectile_verifier){try{if(projectile_verifier(at))return PairPolicy::Delegate;}catch(...){}}return PairPolicy::Reject;}
  // a Wizzrobe's fireball hits Link only
  if(child_at&&wizzrobe_fire(at))return link_object(tg)?PairPolicy::Delegate:PairPolicy::Reject;
  // an owned rat bomb's blast reaches Link and the mod's own enemies only; Link may hit (push) the bomb
  if(child_at&&bomb_object(at)){const bool hits=link_object(tg)||lookup_collider(tg);trace_blast(tg,hits);return hits?PairPolicy::Delegate:PairPolicy::Reject;}
  if(child_tg&&bomb_object(tg))return link_object(at)?PairPolicy::Delegate:PairPolicy::Reject;int16_t timer=0;if(child_tg&&fire_child(tg,timer)&&timer==0&&ignition_source(at)&&(ld32(at+0x10)&0x20200u))return PairPolicy::Delegate;if(child_at&&fire_child(at,timer)&&timer>0&&(ld32(at)&1u)&&link_object(tg))return PairPolicy::Delegate;if(child_tg&&!child_at&&live_cape(tg)&&link_object(at))return PairPolicy::Delegate;return PairPolicy::Reject;}Token a=lookup_collider(at),t=lookup_collider(tg);if(t&&!a&&get(t)&&get(t)->kind==Kind::Octorok&&tg==get(t)->address+0x6EC&&octorok_rock(at)){if(pair_trace())LOG("[enemy_pair] rock at=%08X hits the owned Octorok",at);return PairPolicy::Delegate;}bool ao=a||suspected_owned(at),to=t||suspected_owned(tg);if(!ao&&!to)return PairPolicy::Delegate;if(ao&&to)return PairPolicy::Reject;if(a&&link_object(tg))return PairPolicy::Delegate;if(t&&link_object(at))return PairPolicy::Delegate;if(t&&boat_cannonball(at))return PairPolicy::Delegate;if(t&&projectile_enabled&&projectile_verifier){try{if(projectile_verifier(at))return PairPolicy::Delegate;}catch(...){}}return PairPolicy::Reject;}
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
// a fired arrow (0x1D8) or a thrown boomerang (0x1B0) of Link's: its parameters went from 0 to 1 in flight; a bomb
// (0x126) of Link's: its state byte (the low byte, 0..3) changed
bool lookup_lineage_actor(uint32_t a,Lineage role,LineageInfo& out){if(!memory(a,0x2EC)||!lineage_info(ld32(a+4),out)||out.role!=role||!out.source_valid||out.address!=a||out.profile!=ld16(a+0xE))return false;return out.parameters==ld32(a+0xB0)||(role==Lineage::LinkProjectile&&(out.profile==0x1D8||out.profile==0x1B0)&&out.parameters==0&&ld32(a+0xB0)==1)||(role==Lineage::LinkProjectile&&out.profile==0x126&&(out.parameters&0xFFFFFF00u)==(ld32(a+0xB0)&0xFFFFFF00u)&&(ld32(a+0xB0)&0xFFu)<=3);}
bool admit_lineage(uint32_t pid,uint32_t a){LineageInfo info{};if(!lineage_info(pid,info)||!info.source_valid||!memory(a,0x2EC)||ld32(a+4)!=pid||ld16(a+0xE)!=info.profile||ld32(a+0xB0)!=info.parameters)return false;for(size_t i=0;i<kChildCapacity+kProjectileCapacity;++i){uint32_t r=arena+LineageOffset+uint32_t(i)*64;if(ld32(r)==Magic&&ld32(r+20)==pid){if(ld32(r+40)&&ld32(r+40)!=a)return false;st32(r+40,a);st32(r+56,2);st32(r+60,checksum(r,60));return true;}}return false;}
void visit_lineage(const std::function<void(const LineageInfo&)>& visitor){if(!arena||!visitor)return;for(size_t i=0;i<kChildCapacity+kProjectileCapacity;++i){uint32_t r=arena+LineageOffset+uint32_t(i)*64;if(ld32(r)!=Magic)continue;LineageInfo info{};if(lineage_info(ld32(r+20),info))visitor(info);}}
void retire_lineage(uint32_t pid){if(arena)for(size_t i=0;i<kChildCapacity+kProjectileCapacity;++i){uint32_t r=arena+LineageOffset+uint32_t(i)*64;if(ld32(r)==Magic&&ld32(r+20)==pid)st32(r,0);}}
}
