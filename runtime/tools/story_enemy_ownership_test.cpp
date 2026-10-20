// SPDX-License-Identifier: MPL-2.0
// Synthetic native identity/collider boundaries; no native combat proof.
#include "mods/story_enemy_ownership.h"
#include "runtime.h"
#include <cassert>
#include <cstring>
#include <map>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {
constexpr uint32_t Arena=0x60010000,Base=0x20000000;
std::map<uint32_t,void(*)(Cpu*)> hooks;unsigned delegated=0;
void map(uint32_t a,uint32_t n){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(a),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(a),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void original(Cpu*c,uint32_t){++delegated;c->r[3]=0;}
void fixture(story_enemy_ownership::Kind k,uint32_t a,uint32_t pid,uint32_t params){
 auto&d=story_enemy_ownership::descriptor(k);st32(a+4,pid);st16(a+0xE,d.profile);st32(a+0xB0,params);st8(a+0x2DD,0xFF);
 for(size_t i=0;i<d.collider_count;++i){auto&x=d.colliders[i];st32(a+x.object+0x44,a+x.stts);st32(a+x.stts+0xC,a);st32(a+x.stts+0x10,pid);if(x.helper)st32(a+x.helper,a);}
}
}
int main(){using namespace story_enemy_ownership;map(Arena,0x10000);map(Base,0x100000);map(0x10470000,0x10000);assert(!configure(0x68000000,kStorageBytes,original,[](Token,Kind,uint32_t){return true;}));assert(!configure(Arena,kStorageBytes-1,original,[](Token,Kind,uint32_t){return true;}));assert(configure(Arena,kStorageBytes,original,[](Token,Kind,uint32_t){return true;}));initialize_storage();std::string why;Fingerprint fp{};fp[0]=17;
 Token ts[4];uint32_t addresses[4];for(unsigned i=0;i<4;++i){auto k=Kind(i);uint32_t a=Base+0x10000*i;addresses[i]=a;const uint32_t params=i==1?0xFFFFFF04u:0xFFFF0000u+i;fixture(k,a,100+i,params);ts[i]=declare_actor(k,params,fp,1,why);assert(ts[i]);Binding state;assert(binding_state(ts[i],state)&&state.state==BindingState::Fresh&&can_spawn(ts[i]));assert(bind_pending(ts[i],100+i));assert(binding_state(ts[i],state)&&state.state==BindingState::Queued&&state.pid==100+i&&!can_spawn(ts[i]));assert(admit_created(ts[i],a));assert(binding_state(ts[i],state)&&state.state==BindingState::Active&&state.actor==a&&!can_spawn(ts[i]));assert(enroll_colliders(ts[i]));for(size_t n=0;n<descriptor(k).collider_count;++n)assert(lookup_collider(a+descriptor(k).colliders[n].object)==ts[i]);}
 assert(pair_policy(Base+0x7C4,Base+0x10000+0xCE8)==PairPolicy::Reject);uint32_t retail=Base+0x80000;assert(pair_policy(retail,retail+0x100)==PairPolicy::Delegate);assert(pair_policy(Base+0x7C4,retail)==PairPolicy::Reject);
 uint32_t player=Base+0x60000;st32(player+4,900);st16(player+0xE,0xA8);st32(player+0x762C,player);st32(player+0x7630,900);st32(player+0x765C+0x44,player+0x7620);st32(player+0x7B1C+0x44,player+0x7620);assert(set_link(player,900));assert(pair_policy(Base+0x7C4,player+0x765C)==PairPolicy::Delegate);assert(pair_policy(player+0x7B1C,Base+0x7C4)==PairPolicy::Delegate);assert(pair_policy(player+0x100,Base+0x7C4)==PairPolicy::Reject);st32(player+4,901);assert(pair_policy(player+0x7B1C,Base+0x7C4)==PairPolicy::Reject);st32(player+4,900);
 // Generic combat shares the early hook, with separate incoming/outgoing roles.
 const uint32_t generic_body=retail+0x400,generic_attack=retail+0x600,invalid_generic=retail+0x800;
 set_mod_collider_verifier([&](uint32_t o){return o==generic_body?ModCollider::Body:o==generic_attack?ModCollider::Attack:o==invalid_generic?ModCollider::Invalid:ModCollider::Foreign;});
 assert(pair_policy(generic_attack,player+0x765C)==PairPolicy::Delegate);
 assert(pair_policy(player+0x7B1C,generic_body)==PairPolicy::Delegate);
 assert(pair_policy(generic_body,player+0x765C)==PairPolicy::Reject);
 assert(pair_policy(player+0x7B1C,generic_attack)==PairPolicy::Reject);
 assert(pair_policy(generic_attack,retail)==PairPolicy::Reject);
 assert(pair_policy(generic_attack,Base+0x7C4)==PairPolicy::Reject);
 assert(pair_policy(Base+0x7C4,generic_body)==PairPolicy::Reject);
 assert(pair_policy(generic_attack,generic_body)==PairPolicy::Reject);
 assert(pair_policy(invalid_generic,player+0x765C)==PairPolicy::Reject);
 st32(player+4,901);assert(pair_policy(generic_attack,player+0x765C)==PairPolicy::Reject);st32(player+4,900);
 set_mod_collider_verifier([](uint32_t)->ModCollider{throw 1;});assert(pair_policy(retail,retail+0x100)==PairPolicy::Reject);
 set_mod_collider_verifier({});assert(pair_policy(retail,retail+0x100)==PairPolicy::Delegate);
 { // An Octorok's rock (a game actor: d_a_oq type 6) reaches only an owned Octorok's body, through the game's own rules
  const uint32_t octo=Base+0xE0000,rock=Base+0xF0000;fixture(Kind::Octorok,octo,700,0xFFFFFF00u);std::string w;const Token t=declare_actor(Kind::Octorok,0xFFFFFF00u,fp,1,w);assert(t&&bind_pending(t,700)&&admit_created(t,octo)&&lookup_collider(octo+0x6EC)==t);
  st32(rock+4,701);st16(rock+0xE,0xE3);st32(rock+0xB0,6);st8(rock+0x3DC,6);st32(rock+0xB4,0x10033E10);st32(rock+0x94C+0x44,rock+0x6B0);st32(rock+0x6B0+0xC,rock);st32(rock+0x6B0+0x10,701);
  assert(pair_policy(rock+0x94C,octo+0x6EC)==PairPolicy::Delegate);assert(pair_policy(rock+0x94C,octo+0x81C)==PairPolicy::Reject);assert(pair_policy(rock+0x94C,Base+0x7C4)==PairPolicy::Reject);assert(pair_policy(octo+0x81C,rock+0xA78)==PairPolicy::Reject);st32(rock+0xA78+0x44,rock+0x6B0);assert(pair_policy(rock+0xA78,octo+0x6EC)==PairPolicy::Delegate);
  st32(rock+0xB0,0);assert(pair_policy(rock+0x94C,octo+0x6EC)==PairPolicy::Reject);st32(rock+0xB0,6);st32(rock+0x6B0+0x10,702);assert(pair_policy(rock+0x94C,octo+0x6EC)==PairPolicy::Reject);st32(rock+0x6B0+0x10,701);
  st32(rock+0xB4,0x12345678);assert(pair_policy(rock+0x94C,octo+0x6EC)==PairPolicy::Reject);
  retire_before_delete(octo);
 }
 // Known object ownership cannot be erased by replacing its Stts pointer.
 st32(Base+0x7C4+0x44,retail);assert(!lookup_collider(Base+0x7C4));assert(pair_policy(Base+0x7C4,retail)==PairPolicy::Reject);fixture(Kind::ChuChu,Base,100,0xFFFF0000);
 st32(Base+0x924,retail);assert(!lookup_collider(Base+0x990));fixture(Kind::ChuChu,Base,100,0xFFFF0000);
 assert(!lookup_collider(Base+0x100));assert(pair_policy(Base+0x100,retail)==PairPolicy::Reject);
 assert(install([](uint32_t){return std::string();},[](uint32_t a,void(*f)(Cpu*)){hooks[a]=f;}));assert(hooks.size()==1);Cpu c{};c.r[4]=Base+0x7C4;c.r[5]=retail;for(auto h:hooks)h.second(&c);assert(c.r[3]==1&&delegated==0);c.r[4]=retail;c.r[5]=retail+0x100;for(auto h:hooks)h.second(&c);assert(delegated==1);
 // Missed retirement must not capture a retail actor reusing the same address.
 st32(Base+4,777);st16(Base+0xE,0xCE);assert(pair_policy(Base+0x7C4,retail)==PairPolicy::Delegate);c.r[4]=Base+0x7C4;c.r[5]=retail;c.r[6]=0x12345678;for(auto h:hooks)h.second(&c);assert(delegated==2&&c.r[3]==0&&c.r[6]==0x12345678);
 // An unregistered private generation is still denied through actual Stts owner.
 st16(Base+0xE,0x7FFA);assert(pair_policy(Base+0x7C4,retail)==PairPolicy::Reject);fixture(Kind::ChuChu,Base,100,0xFFFF0000);
 // The detected PID mismatch quarantines the old generation until full restore.
 state_loaded();assert(lookup_actor(Base)==ts[0]);assert(set_link(player,900));
 assert(!queue_lineage(Lineage::EnemyChild,ts[0],Base,100,0xFFFFFFFF,0x1CF,0,1));assert(queue_lineage(Lineage::EnemyChild,ts[0],Base,100,1000,0x1CF,0,1));assert(lineage_pending(1000,Lineage::EnemyChild));assert(!queue_lineage(Lineage::EnemyChild,ts[0],Base,100,1000,0x1CF,0,1));retire_lineage(1000);assert(!lineage_pending(1000,Lineage::EnemyChild));static_assert(!kProjectileCombatEnabled&&!kWeaponChildCombatEnabled);
 // Narrow native burning-stick path; all other child combat stays denied.
 const uint32_t stick=Base+0xD0000,sphere=stick+0x700,fire=player+0x7B1C;
 auto fire_ticket=reserve_child_pending(ts[1],addresses[1],101,0x1CF,0,1);assert(fire_ticket&&commit_child_pending(fire_ticket,1500));st32(stick+4,1500);st16(stick+0xE,0x1CF);st32(stick+0xB0,0);st32(stick+0xB4,0x1000A478);st8(stick+0x2DD,0xFF);st32(stick+0x2E8,0xFFFFFFFFu);st8(stick+0x430,2);st32(stick+0x2E0,0x2000);st32(sphere+0x44,stick+0x6C4);st32(stick+0x6D0,stick);st32(stick+0x6D4,1500);st32(sphere+0x10,0x20000);st8(sphere+0x14,0);assert(admit_lineage(1500,stick));st32(fire+0x10,0x20000);
 assert(pair_policy(fire,sphere)==PairPolicy::Delegate);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);assert(pair_policy(fire,stick+0x82C)==PairPolicy::Reject);st32(fire+0x10,2);assert(pair_policy(fire,sphere)==PairPolicy::Reject);st32(fire+0x10,0x20000);
 const uint32_t verified_fire=retail+0x200;st32(verified_fire+0x10,0x200);set_projectile_verifier([&](uint32_t o){return o==verified_fire;},false);assert(pair_policy(verified_fire,sphere)==PairPolicy::Reject);set_projectile_verifier([&](uint32_t o){return o==verified_fire;},true);assert(pair_policy(verified_fire,sphere)==PairPolicy::Delegate);set_projectile_verifier({},false);
 // A Wizzrobe's fireball: an owned child of a live owned Wizzrobe; its sphere hits Link only, and nothing hits it.
 {auto wz_cp=checkpoint();const uint32_t wz=Base+0xE0000,ball=Base+0xF0000,ball_sphere=ball+0x8F0;fixture(Kind::Wizzrobe,wz,600,0xFFFFFF00u);Token tw=declare_actor(Kind::Wizzrobe,0xFFFFFF00u,fp,1,why);assert(tw&&bind_pending(tw,600)&&admit_created(tw,wz)&&enroll_colliders(tw));
  auto ticket=reserve_child_pending(tw,wz,600,0xD0,0xFFFFFF0Au,1);assert(ticket&&commit_child_pending(ticket,1600));
  st32(ball+4,1600);st16(ball+0xE,0xD0);st32(ball+0xB0,0xFFFFFF0Au);st32(ball+0xB4,0x10042D90);st8(ball+0x464,10);st32(ball_sphere+0x44,ball+0x784);st32(ball+0x784+0xC,ball);st32(ball+0x784+0x10,1600);assert(admit_lineage(1600,ball));
  assert(pair_policy(ball_sphere,player+0x765C)==PairPolicy::Delegate);assert(pair_policy(ball_sphere,retail)==PairPolicy::Reject);assert(pair_policy(retail,ball_sphere)==PairPolicy::Reject);assert(pair_policy(ball+0x7C0,player+0x765C)==PairPolicy::Reject);
  st8(ball+0x464,0);assert(pair_policy(ball_sphere,player+0x765C)==PairPolicy::Reject);st8(ball+0x464,10);
  st32(ball+0xB0,0xFFFFFF0Bu);assert(pair_policy(ball_sphere,player+0x765C)==PairPolicy::Reject);st32(ball+0xB0,0xFFFFFF0Au);
  retire_before_delete(wz);assert(pair_policy(ball_sphere,player+0x765C)==PairPolicy::Reject);retire_lineage(1600);rollback(wz_cp);}
 // A warship's cannonball: an owned child; it hits Link and his boat only. The one Link's boat fires hits the warship.
 {auto ws_cp=checkpoint();const uint32_t ws=Base+0xE0000,ball=Base+0xF0000,sphere=ball+0x920,ship=Base+0xF4000;std::memset(mem::ptr(ws),0,0x10000);std::memset(mem::ptr(ball),0,0xC00);
  fixture(Kind::Warship,ws,610,0xFFFFFF00u);Token tw=declare_actor(Kind::Warship,0xFFFFFF00u,fp,1,why);assert(tw&&bind_pending(tw,610)&&admit_created(tw,ws)&&enroll_colliders(tw));
  assert(queue_child_pending(tw,ws,610,1610,0x126,0x80030004u,1));st32(ball+4,1610);st16(ball+0xE,0x126);st32(ball+0xB0,0x80030004u);st32(sphere+0x44,ball+0x8E4);st32(ball+0x8E4+0xC,ball);st32(ball+0x8E4+0x10,1610);assert(admit_lineage(1610,ball));
  assert(pair_policy(sphere,player+0x765C)==PairPolicy::Delegate);assert(pair_policy(sphere,retail)==PairPolicy::Reject);assert(pair_policy(sphere,ws+0x60C)==PairPolicy::Reject);
  // the bomb's state byte changes in flight and when it explodes (state 0); the flag bits above it do not
  st32(ball+0xB0,0x80030000u);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Delegate);st32(ball+0xB0,0x80020000u);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);st32(ball+0xB0,0x80030004u);
  const uint32_t ship_tg=ship+0x100;std::memset(mem::ptr(ship),0,0x200);st32(ship+4,620);st32(ship_tg+0x44,ship+0x40);st32(ship+0x40+0xC,ship);st32(0x1046F0B0+0x5B3C,ship);assert(pair_policy(sphere,ship_tg)==PairPolicy::Delegate);st32(0x1046F0B0+0x5B3C,0);assert(pair_policy(sphere,ship_tg)==PairPolicy::Reject);
  // Link's boat cannonball (not a claimed child) may hit the owned warship; a retail bomb of another kind may not
  const uint32_t boat=Base+0xF2000,boat_sphere=boat+0x920;std::memset(mem::ptr(boat),0,0xC00);st32(boat+4,1620);st16(boat+0xE,0x126);st32(boat+0xB0,0x80020004u);st32(boat_sphere+0x44,boat+0x8E4);st32(boat+0x8E4+0xC,boat);st32(boat+0x8E4+0x10,1620);
  assert(pair_policy(boat_sphere,ws+0x60C)==PairPolicy::Delegate);st32(boat+0xB0,0x80020000u);assert(pair_policy(boat_sphere,ws+0x60C)==PairPolicy::Delegate);st32(boat+0xB0,0x80030004u);assert(pair_policy(boat_sphere,ws+0x60C)==PairPolicy::Reject);
  retire_lineage(1610);retire_before_delete(ws);rollback(ws_cp);}
 // A wall cannon's cannonball: the same BOMB and parameters as the warship's; it hits Link and his boat only,
 // never the cannon, another enemy or a retail actor; the boat's cannonball may hit the owned cannon.
 {auto wc_cp=checkpoint();const uint32_t wc=Base+0xE0000,ball=Base+0xF0000,sphere=ball+0x920,ship=Base+0xF4000;std::memset(mem::ptr(wc),0,0x10000);std::memset(mem::ptr(ball),0,0xC00);
  fixture(Kind::WallCannon,wc,640,0xFFC0FFFFu);Token tc=declare_actor(Kind::WallCannon,0xFFC0FFFFu,fp,1,why);assert(tc&&bind_pending(tc,640)&&admit_created(tc,wc)&&enroll_colliders(tc));
  assert(queue_child_pending(tc,wc,640,1640,0x126,0x80030004u,1));st32(ball+4,1640);st16(ball+0xE,0x126);st32(ball+0xB0,0x80030004u);st32(sphere+0x44,ball+0x8E4);st32(ball+0x8E4+0xC,ball);st32(ball+0x8E4+0x10,1640);assert(admit_lineage(1640,ball));
  assert(pair_policy(sphere,player+0x765C)==PairPolicy::Delegate);assert(pair_policy(sphere,retail)==PairPolicy::Reject);assert(pair_policy(sphere,wc+0x3F4)==PairPolicy::Reject);
  const uint32_t ship_tg=ship+0x100;std::memset(mem::ptr(ship),0,0x200);st32(ship+4,650);st32(ship_tg+0x44,ship+0x40);st32(ship+0x40+0xC,ship);st32(0x1046F0B0+0x5B3C,ship);assert(pair_policy(sphere,ship_tg)==PairPolicy::Delegate);st32(0x1046F0B0+0x5B3C,0);
  const uint32_t boat=Base+0xF2000,boat_sphere=boat+0x920;std::memset(mem::ptr(boat),0,0xC00);st32(boat+4,1650);st16(boat+0xE,0x126);st32(boat+0xB0,0x80020004u);st32(boat_sphere+0x44,boat+0x8E4);st32(boat+0x8E4+0xC,boat);st32(boat+0x8E4+0x10,1650);
  assert(pair_policy(boat_sphere,wc+0x3F4)==PairPolicy::Delegate);
  // a cannon that is gone: its cannonball hits nothing
  retire_before_delete(wc);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);
  retire_lineage(1640);rollback(wc_cp);}
 // A Gyorg shark: an owned child of a live owned Gyorg controller; it bites Link and his boat only, and only
 // Link's weapons and the boat's cannonball hit it.
 {auto gy_cp=checkpoint();const uint32_t gc=Base+0xE0000,shark=Base+0xF0000,head=shark+0x628,body=shark+0x754,ship=Base+0xF4000,boat=Base+0xF2000;std::memset(mem::ptr(gc),0,0x10000);std::memset(mem::ptr(shark),0,0x1000);
  fixture(Kind::Gyorg,gc,630,0xFFFF0531u);Token tg=declare_actor(Kind::Gyorg,0xFFFF0531u,fp,1,why);assert(tg&&bind_pending(tg,630)&&admit_created(tg,gc)&&enroll_colliders(tg));
  auto ticket=reserve_child_pending(tg,gc,630,0xE4,0xFFFFFFFFu,1);assert(ticket&&commit_child_pending(ticket,1630));
  st32(shark+4,1630);st16(shark+0xE,0xE4);st32(shark+0xB0,0xFFFFFFFFu);st32(head+0x44,shark+0x9C4);st32(body+0x44,shark+0x9C4);st32(shark+0x9C4+0xC,shark);st32(shark+0x9C4+0x10,1630);assert(admit_lineage(1630,shark));
  assert(pair_policy(head,player+0x765C)==PairPolicy::Delegate);assert(pair_policy(head,retail)==PairPolicy::Reject);
  assert(pair_policy(player+0x7B1C,body)==PairPolicy::Delegate);assert(pair_policy(retail,body)==PairPolicy::Reject);assert(pair_policy(head,body)==PairPolicy::Reject);
  const uint32_t ship_tg=ship+0x100;std::memset(mem::ptr(ship),0,0x200);st32(ship+4,640);st32(ship_tg+0x44,ship+0x40);st32(ship+0x40+0xC,ship);st32(0x1046F0B0+0x5B3C,ship);assert(pair_policy(head,ship_tg)==PairPolicy::Delegate);st32(0x1046F0B0+0x5B3C,0);assert(pair_policy(head,ship_tg)==PairPolicy::Reject);
  const uint32_t boat_sphere=boat+0x920;std::memset(mem::ptr(boat),0,0xC00);st32(boat+4,1640);st16(boat+0xE,0x126);st32(boat+0xB0,0x80020004u);st32(boat_sphere+0x44,boat+0x8E4);st32(boat+0x8E4+0xC,boat);st32(boat+0x8E4+0x10,1640);assert(pair_policy(boat_sphere,body)==PairPolicy::Delegate);
  retire_before_delete(gc);assert(pair_policy(head,player+0x765C)==PairPolicy::Reject);retire_lineage(1630);rollback(gy_cp);}
 // A Big Octo eye: an owned child of a live owned Big Octo; Link's weapons hit it, it hits nothing.
 {auto bo_cp=checkpoint();const uint32_t bo=Base+0xE0000,eye=Base+0xF0000,sph=eye+0x4D4;std::memset(mem::ptr(bo),0,0x10000);std::memset(mem::ptr(eye),0,0x1000);
  fixture(Kind::BigOcto,bo,650,0xFFFFFF00u);Token tb=declare_actor(Kind::BigOcto,0xFFFFFF00u,fp,1,why);assert(tb&&bind_pending(tb,650)&&admit_created(tb,bo)&&enroll_colliders(tb));
  auto ticket=reserve_child_pending(tb,bo,650,0xE2,0xFFFFFFFFu,1);assert(ticket&&commit_child_pending(ticket,1650));
  st32(eye+4,1650);st16(eye+0xE,0xE2);st32(eye+0xB0,0xFFFFFFFFu);st32(sph+0x44,eye+0x600);st32(eye+0x600+0xC,eye);st32(eye+0x600+0x10,1650);assert(admit_lineage(1650,eye));
  assert(pair_policy(player+0x7B1C,sph)==PairPolicy::Delegate);assert(pair_policy(retail,sph)==PairPolicy::Reject);assert(pair_policy(sph,player+0x765C)==PairPolicy::Reject);
  retire_before_delete(bo);assert(pair_policy(player+0x7B1C,sph)==PairPolicy::Reject);retire_lineage(1650);rollback(bo_cp);}
 st16(stick+0x43C,900);st32(sphere,1);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Delegate);assert(pair_policy(fire,sphere)==PairPolicy::Reject);assert(pair_policy(sphere,retail)==PairPolicy::Reject);assert(pair_policy(sphere,Base+0x7C4)==PairPolicy::Reject);st32(sphere,0);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);st32(sphere,1);
 st32(sphere+0x10,0x4000);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);st32(sphere+0x10,0x20000);st8(sphere+0x14,1);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);st8(sphere+0x14,0);st32(stick+0xB4,0x1000A47C);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);st32(stick+0xB4,0x1000A478);
 st32(stick+0xB0,4);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);st32(stick+0xB0,0);st16(stick+0x43C,901);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);st16(stick+0x43C,0xFFFF);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);st16(stick+0x43C,900);st8(stick+0x430,3);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);st8(stick+0x430,2);
 st32(stick+0x2E8,101);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);st32(stick+0x2E8,0xFFFFFFFFu);st8(stick+0x2DD,0);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);st8(stick+0x2DD,0xFF);st32(sphere+0x44,retail);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);st32(sphere+0x44,stick+0x6C4);
 std::array<uint8_t,kStorageBytes> fire_snapshot{};std::memcpy(fire_snapshot.data(),mem::ptr(Arena),fire_snapshot.size());auto restore_fire=[&](){std::memcpy(mem::ptr(Arena),fire_snapshot.data(),fire_snapshot.size());state_loaded();assert(set_link(player,900));};
 const uint32_t first_lineage=Arena+256+128*128;st8(first_lineage+60,ld8(first_lineage+60)^1);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);restore_fire();assert(pair_policy(sphere,player+0x765C)==PairPolicy::Delegate);
 st32(stick+4,1501);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Delegate);st32(stick+4,1500);assert(begin_epoch(ts[1],2)&&bind_pending(ts[1],101)&&admit_created(ts[1],addresses[1]));assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);restore_fire();retire_before_delete(addresses[1]);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);restore_fire();st8(Arena+256+128+32,0xEF);assert(pair_policy(sphere,player+0x765C)==PairPolicy::Reject);restore_fire();retire_lineage(1500);
 auto ticket=reserve_child_pending(ts[0],Base,100,0x1CF,0,1);assert(ticket);LineageInfo child{};assert(!lineage_info(0,child));assert(!commit_child_pending(ticket,0xFFFFFFFF));assert(commit_child_pending(ticket,1001));uint32_t weapon=Base+0x90000;st32(weapon+4,1001);st16(weapon+0xE,0x1CF);st32(weapon+0xB0,0);st32(weapon+0x700+0x44,weapon+0x6C4);st32(weapon+0x6D0,weapon);st32(weapon+0x6D4,1001);assert(admit_lineage(1001,weapon));assert(lookup_lineage_actor(weapon,Lineage::EnemyChild,child)&&child.source==ts[0]);assert(pair_policy(weapon+0x700,retail)==PairPolicy::Reject);// Denial-only identity survives a corrupted checksum, without capturing retail reuse.
 uint32_t child_row=Arena+256+128*128;st8(child_row+60,ld8(child_row+60)^1);assert(!lineage_info(1001,child));assert(lineage_claimed(1001,Lineage::EnemyChild));assert(pair_policy(weapon+0x700,retail)==PairPolicy::Reject);st32(child_row+40,0);assert(pair_policy(weapon+0x700,retail)==PairPolicy::Reject);st32(child_row+40,weapon+0x100);assert(pair_policy(weapon+0x700,retail)==PairPolicy::Reject);st32(weapon+4,1002);assert(pair_policy(weapon+0x700,retail)==PairPolicy::Delegate);st32(weapon+4,1001);
 retire_lineage(1001);assert(!lineage_info(1001,child));ticket=reserve_child_pending(ts[0],Base,100,0x1CF,0,1);assert(ticket);cancel_child_pending(ticket);assert(!commit_child_pending(ticket,1002));
 std::array<uint32_t,kChildCapacity> reservations{};for(auto&v:reservations){v=reserve_child_pending(ts[0],Base,100,0x1CF,0,1);assert(v);}assert(!reserve_child_pending(ts[0],Base,100,0x1CF,0,1));for(auto v:reservations)cancel_child_pending(v);assert(!commit_child_pending(reservations[0],1003));
 // A freed parent retains immutable config provenance for a live orphan.
 auto orphan_ticket=reserve_child_pending(ts[3],addresses[3],103,0x1CF,4,1);assert(orphan_ticket&&commit_child_pending(orphan_ticket,1004));uint32_t orphan=Base+0xA0000;st32(orphan+4,1004);st16(orphan+0xE,0x1CF);st32(orphan+0xB0,4);assert(admit_lineage(1004,orphan));retire_before_delete(addresses[3]);assert(lineage_info(1004,child)&&child.source_valid);assert(pair_policy(orphan+0x700,retail)==PairPolicy::Reject);state_loaded();assert(!lookup_actor(addresses[3]));Binding retired_state;assert(binding_state(ts[3],retired_state)&&retired_state.state==BindingState::Retired&&!can_spawn(ts[3]));assert(lineage_info(1004,child)&&child.source_valid);st8(Arena+256+3*128+32,0xEF);assert(lineage_info(1004,child)&&!child.source_valid);assert(lineage_claimed(1004,Lineage::EnemyChild));assert(pair_policy(orphan+0x700,retail)==PairPolicy::Reject);retire_lineage(1004);
 state_loaded();for(unsigned i=0;i<3;++i)assert(lookup_actor(addresses[i])==ts[i]);assert(!lookup_actor(addresses[3]));
 // Corrupt snapshot digest never gets overwritten by re-admission.
 st8(Arena+256+32,99);state_loaded();assert(!lookup_actor(Base));Binding rejected;assert(binding_state(ts[0],rejected)&&rejected.state==BindingState::Quarantined&&!can_spawn(ts[0]));assert(!admit_created(ts[0],Base));assert(pair_policy(Base+0x7C4,retail)==PairPolicy::Reject);
 retire_before_delete(addresses[1]);assert(!lookup_actor(addresses[1]));assert(pair_policy(addresses[1]+0xCE8,retail)==PairPolicy::Reject);state_loaded();assert(!lookup_actor(addresses[1]));
 assert(configure(Arena,kStorageBytes,original,[](Token,Kind,uint32_t){return false;}));assert(!lookup_actor(addresses[2]));assert(configure(Arena,kStorageBytes,original,[](Token,Kind,uint32_t){return true;}));assert(!lookup_actor(addresses[2]));assert(!begin_epoch(ts[2],1));assert(begin_epoch(ts[2],2));assert(bind_pending(ts[2],102));assert(admit_created(ts[2],addresses[2]));
 auto cp=checkpoint();Token stale=declare_actor(Kind::Keese,0,fp,1,why);rollback(cp);Token fresh=declare_actor(Kind::Keese,0,fp,1,why);assert(fresh!=stale&&!bind_pending(stale,200));invalidate_epoch(3);assert(!lookup_actor(addresses[2]));
 auto extra_cp=checkpoint();Fingerprint changed=fp;changed[1]=42;uint32_t parent=Base+0xB0000;fixture(Kind::Keese,parent,300,0xFFFF0000);auto old_source=declare_actor(Kind::Keese,0xFFFF0000,fp,7,why);assert(bind_pending(old_source,300));std::array<uint8_t,kStorageBytes> before_constructor{};std::memcpy(before_constructor.data(),mem::ptr(Arena),before_constructor.size());assert(begin_epoch(old_source,8));std::memcpy(mem::ptr(Arena),before_constructor.data(),before_constructor.size());state_loaded();assert(source_epoch(old_source)==7&&!lookup_actor(parent));Binding pending_state;assert(binding_state(old_source,pending_state)&&pending_state.state==BindingState::Queued&&pending_state.pid==300&&!can_spawn(old_source));auto pending_ticket=reserve_child_pending(old_source,parent,300,0x1CF,0,7);assert(pending_ticket&&commit_child_pending(pending_ticket,1005));assert(admit_created(old_source,parent));uint32_t changed_child=Base+0xC0000;st32(changed_child+4,1005);st16(changed_child+0xE,0x1CF);assert(admit_lineage(1005,changed_child));retire_before_delete(parent);rollback(extra_cp);auto new_source=declare_actor(Kind::Keese,0xFFFF0000,changed,7,why);assert(new_source!=old_source);assert(lineage_info(1005,child)&&!child.source_valid);assert(!admit_lineage(1005,changed_child));assert(pair_policy(changed_child+0x700,retail)==PairPolicy::Reject);

}
