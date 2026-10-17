// SPDX-License-Identifier: MPL-2.0
#include "mods/mod_actor_combat_core.h"
#include <cassert>
#include <limits>
using namespace mod_actor::combat;
int main(){
    Config c{10,2,3,5,4,2,6};State s;assert(initialize(s,c)&&valid(s,c));
    for(float bad:{-1.0f,901.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){auto invalid=c;invalid.stun_frames=bad;assert(!valid(invalid));}
    for(uint32_t h:{0u,256u}){auto invalid=c;invalid.max_health=h;assert(!initialize(s,invalid));}
    assert(initialize(s,c));assert(hit(s,c,0,1)==HitResult::Invalid);assert(hit(s,c,1,0)==HitResult::Invalid&&s.last_sequence==0);
    assert(begin_attack(s,c)&&attacking(s));assert(!begin_attack(s,c));assert(hit(s,c,1,2)==HitResult::Damaged&&s.health==8&&!attacking(s)&&s.phase==uint32_t(Phase::Flinch));
    assert(hit(s,c,2,2)==HitResult::Invulnerable&&s.last_sequence==2);assert(tick(s,c,1)&&tick(s,c,1));assert(hit(s,c,2,2)==HitResult::Stale&&s.health==8);assert(hit(s,c,1,2)==HitResult::Stale);
    assert(hit(s,c,3,1,true)==HitResult::Damaged&&s.phase==uint32_t(Phase::Stunned)&&s.reaction_left==5);
    State before=s;assert(!tick(s,c,2)&&s.reaction_left==before.reaction_left);assert(!tick(s,c,std::numeric_limits<float>::quiet_NaN()));
    assert(tick(s,c,0)&&s.reaction_left==5);assert(tick(s,c,1,true)&&s.reaction_left==5);assert(hit(s,c,4,1,false,true)==HitResult::Blocked&&s.health==7&&s.last_sequence==4);
    for(int i=0;i<5;++i)assert(tick(s,c,1));assert(s.phase==uint32_t(Phase::Alive));assert(hit(s,c,4,1)==HitResult::Stale);
    State full=s,half=s;assert(begin_attack(full,c)&&begin_attack(half,c));for(int i=0;i<6;++i)assert(tick(full,c,1));for(int i=0;i<12;++i)assert(tick(half,c,.5));assert(full.active_left==half.active_left&&full.cooldown_left==half.cooldown_left&&full.phase==half.phase);
    assert(begin_attack(full,c));assert(tick(full,c,0,true)&&!attacking(full)&&full.cooldown_left==6);assert(!begin_attack(full,c,true));
    State snapshot=s;assert(hit(s,c,5,255)==HitResult::Lethal&&!alive(s)&&!attacking(s)&&!reward_pending(s));assert(hit(s,c,6,1)==HitResult::Ignored);for(int i=0;i<4;++i)assert(tick(s,c,1));assert(reward_pending(s));assert(!settle_reward(s,true));assert(reserve_reward(s)&&!reserve_reward(s));assert(settle_reward(s,true)&&!reward_pending(s));assert(!settle_reward(s,true));assert(!begin_attack(s,c));
    s=snapshot;assert(valid(s,c)&&hit(s,c,5,255)==HitResult::Lethal);for(int i=0;i<4;++i)assert(tick(s,c,1));assert(reserve_reward(s));assert(settle_reward(s,false)&&s.reward==uint32_t(Reward::Failed));assert(!settle_reward(s,true));for(int i=0;i<20;++i)assert(tick(s,c,1));assert(s.reward==uint32_t(Reward::Failed));
    auto immediate=c;immediate.death_frames=0;assert(initialize(s,immediate));assert(hit(s,immediate,1,10)==HitResult::Lethal&&reward_pending(s));
    assert(initialize(s,c));s.last_sequence=std::numeric_limits<uint32_t>::max()-1;assert(hit(s,c,std::numeric_limits<uint32_t>::max(),1)==HitResult::Damaged);assert(hit(s,c,1,1)==HitResult::Stale);assert(hit(s,c,std::numeric_limits<uint32_t>::max(),1)==HitResult::Stale);
    auto corrupt=s;corrupt.phase=99;assert(!valid(corrupt,c));corrupt=s;corrupt.health=0;assert(!valid(corrupt,c));corrupt=s;corrupt.reward=uint32_t(Reward::Pending);assert(!valid(corrupt,c));
    // No callbacks exist in the pure API; duplicated/reentrant settlement is
    // represented by repeated calls and cannot issue a second external intent.
    assert(initialize(s,c));assert(hit(s,c,1,255)==HitResult::Lethal);for(int i=0;i<4;++i)assert(tick(s,c,1));State restored=s;assert(reserve_reward(s));State inflight=s;assert(abandon_inflight_reward(inflight)&&!reserve_reward(inflight)&&!settle_reward(inflight,true));assert(settle_reward(s,true));assert(!settle_reward(s,false));assert(reward_pending(restored));
    assert(initialize(s,c)&&begin_attack(s,c));assert(stun_hit(s,c,0)==HitResult::Invalid&&s.last_sequence==0);
    assert(stun_hit(s,c,1)==HitResult::Stunned&&s.health==10&&s.phase==uint32_t(Phase::Stunned)&&s.reaction_left==5&&!attacking(s));
    assert(stun_hit(s,c,2)==HitResult::Invulnerable&&s.health==10&&s.last_sequence==2);
    assert(tick(s,c,1)&&tick(s,c,1));assert(stun_hit(s,c,2)==HitResult::Stale&&s.reaction_left==3);
    assert(stun_hit(s,c,3,true)==HitResult::Blocked&&s.health==10&&s.reaction_left==3&&s.last_sequence==3);
    for(int i=0;i<3;++i)assert(tick(s,c,1));assert(s.phase==uint32_t(Phase::Alive)&&s.health==10);
    assert(stun_hit(s,c,3)==HitResult::Stale);assert(stun_hit(s,c,4)==HitResult::Stunned&&s.health==10);
    for(int i=0;i<5;++i)assert(tick(s,c,1));assert(hit(s,c,5,255)==HitResult::Lethal);assert(stun_hit(s,c,6)==HitResult::Ignored&&s.health==0);for(int i=0;i<4;++i)assert(tick(s,c,1));assert(stun_hit(s,c,7)==HitResult::Ignored&&reward_pending(s));
    auto no_stun=c;no_stun.stun_frames=0;assert(initialize(s,no_stun)&&begin_attack(s,no_stun));assert(stun_hit(s,no_stun,1)==HitResult::Ignored&&s.health==10&&attacking(s)&&s.last_sequence==1);assert(stun_hit(s,no_stun,1)==HitResult::Stale);

}
