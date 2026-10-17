// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
namespace mod_actor::combat {
inline constexpr uint32_t Version=1;
enum class Phase:uint32_t {Alive,Flinch,Stunned,Dying,Defeated};
enum class Reward:uint32_t {None,Pending,Claiming,Committed,Failed};
enum class HitResult:uint32_t {Invalid,Stale,Ignored,Invulnerable,Blocked,Damaged,Lethal,Stunned};
// Durations are native 30 Hz logic frames, never display frames. A zero active
// duration disables attacks. Cooldown includes the active window.
struct Config {
    uint32_t max_health=1;
    float invulnerability_frames=0,flinch_frames=0,stun_frames=0,death_frames=0;
    float active_frames=0,cooldown_frames=0;
};
// Serialize fields explicitly as guest BE words; do not memcpy host objects.
// This state belongs to the guest snapshot, including sequence and reward latch.
struct State {
    uint32_t version=Version,phase=uint32_t(Phase::Alive),health=1,last_sequence=0,reward=uint32_t(Reward::None);
    float invulnerability_left=0,reaction_left=0,death_left=0,active_left=0,cooldown_left=0;
};
static_assert(std::is_trivially_copyable_v<Config> && sizeof(Config)==28);
static_assert(std::is_trivially_copyable_v<State> && sizeof(State)==40);
inline bool duration(float n){return std::isfinite(n)&&n>=0&&n<=900;}
inline bool valid(const Config& c){return c.max_health>=1&&c.max_health<=255&&duration(c.invulnerability_frames)&&duration(c.flinch_frames)&&duration(c.stun_frames)&&duration(c.death_frames)&&duration(c.active_frames)&&duration(c.cooldown_frames)&&c.cooldown_frames>=c.active_frames;}
inline bool valid(const State& s,const Config& c){
    if(!valid(c)||s.version!=Version||s.phase>uint32_t(Phase::Defeated)||s.reward>uint32_t(Reward::Failed)||s.health>c.max_health||!duration(s.invulnerability_left)||!duration(s.reaction_left)||!duration(s.death_left)||!duration(s.active_left)||!duration(s.cooldown_left))return false;
    if(s.invulnerability_left>c.invulnerability_frames||s.reaction_left>std::max(c.flinch_frames,c.stun_frames)||s.death_left>c.death_frames||s.active_left>c.active_frames||s.cooldown_left>c.cooldown_frames||s.active_left>s.cooldown_left)return false;
    const auto p=Phase(s.phase);const auto r=Reward(s.reward);
    if(p==Phase::Alive)return s.health>0&&r==Reward::None&&s.reaction_left==0&&s.death_left==0;
    if(p==Phase::Flinch||p==Phase::Stunned)return s.reaction_left<=(p==Phase::Stunned?c.stun_frames:c.flinch_frames)&&s.health>0&&r==Reward::None&&s.reaction_left>0&&s.death_left==0&&s.active_left==0;
    if(p==Phase::Dying)return s.health==0&&r==Reward::None&&s.death_left>0&&s.reaction_left==0&&s.active_left==0&&s.cooldown_left==0;
    return s.health==0&&r!=Reward::None&&s.death_left==0&&s.reaction_left==0&&s.active_left==0&&s.cooldown_left==0;
}
inline bool initialize(State& s,const Config& c){if(!valid(c))return false;s=State{};s.health=c.max_health;return true;}
inline bool alive(const State& s){return s.version==Version&&s.phase<=uint32_t(Phase::Stunned)&&s.health>0&&s.health<=255;}
inline bool attacking(const State& s){return s.version==Version&&s.phase==uint32_t(Phase::Alive)&&s.health>0&&s.health<=255&&duration(s.active_left)&&s.active_left>0;}
inline void cancel_attack(State& s){s.active_left=0;}
inline bool begin_attack(State& s,const Config& c,bool blocked=false){
    if(!valid(s,c)||blocked||s.phase!=uint32_t(Phase::Alive)||s.cooldown_left>0||c.active_frames==0)return false;
    s.active_left=c.active_frames;s.cooldown_left=c.cooldown_frames;return true;
}
inline void finish_death(State& s){s.phase=uint32_t(Phase::Defeated);s.death_left=0;s.reward=uint32_t(Reward::Pending);}
inline HitResult hit(State& s,const Config& c,uint32_t sequence,uint32_t damage,bool stun=false,bool blocked=false){
    if(!valid(s,c)||!sequence||!damage||damage>255)return HitResult::Invalid;
    if(sequence<=s.last_sequence)return HitResult::Stale;
    // Consume even iframe/lease rejection. An old contact must never turn into
    // damage later. UINT32_MAX saturates naturally; a new snapshot may rewind it.
    s.last_sequence=sequence;
    if(!alive(s))return HitResult::Ignored;
    if(blocked)return HitResult::Blocked;
    if(s.invulnerability_left>0)return HitResult::Invulnerable;
    s.health=damage>=s.health?0:s.health-damage;s.active_left=0;
    s.invulnerability_left=c.invulnerability_frames;
    if(!s.health){s.reaction_left=0;s.cooldown_left=0;s.phase=uint32_t(Phase::Dying);s.death_left=c.death_frames;if(s.death_left==0)finish_death(s);return HitResult::Lethal;}
    s.reaction_left=stun?c.stun_frames:c.flinch_frames;
    s.phase=uint32_t(s.reaction_left>0?(stun?Phase::Stunned:Phase::Flinch):Phase::Alive);
    return HitResult::Damaged;
}
// Damage-free curated stun ingress (for example a verified boomerang). Never
// synthesize attack power to drive a reaction. Rejected contacts consume their
// monotonic sequence just like damaging hit ingress.
inline HitResult stun_hit(State& s,const Config& c,uint32_t sequence,bool blocked=false){
    if(!valid(s,c)||!sequence)return HitResult::Invalid;
    if(sequence<=s.last_sequence)return HitResult::Stale;
    s.last_sequence=sequence;
    if(!alive(s))return HitResult::Ignored;
    if(blocked)return HitResult::Blocked;
    if(s.invulnerability_left>0)return HitResult::Invulnerable;
    if(c.stun_frames==0)return HitResult::Ignored;
    s.active_left=0;s.invulnerability_left=c.invulnerability_frames;
    s.reaction_left=c.stun_frames;s.phase=uint32_t(Phase::Stunned);
    return HitResult::Stunned;
}
inline bool tick(State& s,const Config& c,float dt,bool blocked=false){
    if(!valid(s,c)||!std::isfinite(dt)||dt<0||dt>1)return false;
    if(blocked){cancel_attack(s);return true;} // scene/quarantine pauses combat clocks
    auto advance=[dt](float& n){n=std::max(0.0f,n-dt);};
    advance(s.invulnerability_left);advance(s.cooldown_left);advance(s.active_left);
    if(s.phase==uint32_t(Phase::Flinch)||s.phase==uint32_t(Phase::Stunned)){advance(s.reaction_left);if(s.reaction_left==0)s.phase=uint32_t(Phase::Alive);}
    else if(s.phase==uint32_t(Phase::Dying)){advance(s.death_left);if(s.death_left==0)finish_death(s);}
    return true;
}
inline bool reward_pending(const State& s){return s.version==Version&&s.phase==uint32_t(Phase::Defeated)&&s.health==0&&s.reward==uint32_t(Reward::Pending)&&s.death_left==0&&s.reaction_left==0&&s.active_left==0&&s.cooldown_left==0;}
// The adapter performs the atomic transaction. Already committed own markers
// count as success without another grant. Failure stays terminal/quarantined;
// there is no automatic retry and this function never calls external code.
inline bool reserve_reward(State& s){if(!reward_pending(s))return false;s.reward=uint32_t(Reward::Claiming);return true;}
inline bool abandon_inflight_reward(State& s){if(s.phase!=uint32_t(Phase::Defeated)||s.reward!=uint32_t(Reward::Claiming))return false;s.reward=uint32_t(Reward::Failed);return true;}
inline bool settle_reward(State& s,bool committed_or_already_done){if(s.phase!=uint32_t(Phase::Defeated)||s.health||s.reward!=uint32_t(Reward::Claiming))return false;s.reward=uint32_t(committed_or_already_done?Reward::Committed:Reward::Failed);return true;}
}
