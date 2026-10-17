// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
struct Cpu;
namespace story_enemy_ownership {
enum class Kind:uint32_t { ChuChu,Bokoblin,Moblin,Keese };
struct Collider {uint32_t object,stts,helper;};
struct Descriptor {uint16_t profile,native_profile;uint32_t bytes;const Collider* colliders;size_t collider_count;};
const Descriptor& descriptor(Kind);
using Fingerprint=std::array<uint8_t,32>;
using Token=uint32_t;
inline constexpr size_t kActorCapacity=128,kChildCapacity=256,kProjectileCapacity=128;
inline constexpr uint32_t kStorageBytes=256+128*128+(256+128)*64;
// Caller appends this arena to snapshot-owned runtime_alloc storage. Never host_alloc.
// PURE verifier: bounded native reads only; no native calls, mutations, or
// recursive ownership lookups. Failure quarantines this actor generation.
using VerifyActor=std::function<bool(Token,Kind,uint32_t)>;
bool configure(uint32_t arena,uint32_t bytes,void(*original)(Cpu*,uint32_t),VerifyActor);
void initialize_storage();
Token declare_actor(Kind,uint32_t parameters,const Fingerprint&,uint32_t epoch,std::string& why);
size_t checkpoint();void rollback(size_t) noexcept;
enum class BindingState { Uninitialized,Fresh,Queued,Active,Retired,Quarantined };
struct Binding {BindingState state=BindingState::Uninitialized;uint32_t pid=0,actor=0,epoch=0;};
bool binding_state(Token,Binding&);
bool can_spawn(Token);
bool bind_pending(Token,uint32_t pid);
bool admit_created(Token,uint32_t actor);
bool enroll_colliders(Token);
Token lookup_actor(uint32_t actor);
Token lookup_collider(uint32_t object);
void retire_before_delete(uint32_t actor);
void invalidate_epoch(uint32_t epoch);
// Caller must prove a real stage/room epoch change; duplicate notifications cannot retry.
bool begin_epoch(Token,uint32_t epoch);
// Rebuild only after comparing the restored record with the immutable declaration.
void state_loaded();
// Caller supplies the independently resolved current native Link. Exact profile/PID
// and exact class collider/Stts offsets are still checked here on every pair.
bool set_link(uint32_t actor,uint32_t pid);
uint32_t link_generation();
void retire_link(uint32_t actor,uint32_t pid);
using ProjectileVerifier=std::function<bool(uint32_t attack_object)>;
void set_projectile_verifier(ProjectileVerifier,bool enabled=false);
// Pure generic-actor classifier, composed with the same early native pair hook.
// Invalid includes recognizable private colliders with stale or damaged identity.
enum class ModCollider { Foreign,Body,Attack,Invalid };
using ModColliderVerifier=std::function<ModCollider(uint32_t object)>;
void set_mod_collider_verifier(ModColliderVerifier);
enum class PairPolicy { Delegate,Reject };
PairPolicy pair_policy(uint32_t attack,uint32_t target);
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
// Staged lineage records are snapshot-owned and bounded, but never authorize hits.
// Adapter must not admit BK/MO child-dependent execution until its route is audited.
enum class Lineage:uint32_t { EnemyChild=1,LinkProjectile=2 };
uint32_t reserve_child_pending(Token source,uint32_t source_actor,uint32_t source_pid,uint16_t profile,uint32_t parameters,uint32_t epoch);
bool commit_child_pending(uint32_t reservation,uint32_t queued_pid);
void cancel_child_pending(uint32_t reservation);
bool queue_child_pending(Token source,uint32_t source_actor,uint32_t source_pid,uint32_t queued_pid,uint16_t profile,uint32_t parameters,uint32_t epoch);
uint32_t source_epoch(Token);
bool queue_lineage(Lineage,Token source,uint32_t source_actor,uint32_t source_pid,uint32_t queued_pid,uint16_t profile,uint32_t parameters,uint32_t epoch);
bool lineage_pending(uint32_t queued_pid,Lineage);
// Denial-only recognition of a retained claimed PID, even if its admission
// checksum/source fingerprint is invalid. This never grants native authority.
bool lineage_claimed(uint32_t pid,Lineage);
struct LineageInfo {Lineage role;Token source;uint32_t source_actor,source_pid,pid,address,profile,parameters,epoch,generation,root_generation;bool source_valid;};
bool admit_lineage(uint32_t pid,uint32_t actor);
bool lineage_info(uint32_t pid,LineageInfo&);
bool lookup_lineage_actor(uint32_t actor,Lineage role,LineageInfo&);
void visit_lineage(const std::function<void(const LineageInfo&)>&);
void retire_lineage(uint32_t pid);
// General thrown/weapon-child attacks remain disabled. A separately checked
// live BK type0 sphere permits only native ignition and its burning Link hazard.
inline constexpr bool kProjectileCombatEnabled=false,kWeaponChildCombatEnabled=false;
}
