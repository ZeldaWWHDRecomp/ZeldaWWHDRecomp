// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <initializer_list>
struct Cpu;
namespace mod_actor_native_combat {
inline constexpr uint32_t kAttackSourceBytes=0x44,kHeartStateBytes=32;
inline constexpr uint32_t kPhysicalAtType=0x800,kPhysicalAtFlags=0xF;
struct AttackSpec {float radius=0,height=0,forward_offset=0,y_offset=0;uint32_t power=0;};
struct SourceIdentity {uint32_t actor=0,pid=0,generation=0,epoch=0,heart_state=0;};
using Invoke=uint32_t(*)(Cpu*,uint32_t,std::initializer_list<uint32_t>);
// Read-only identity callback must validate declaration/profile/PID/generation/epoch and exact actor-owned heart_state offset.
using VerifySource=bool(*)(const SourceIdentity&);
bool configure(Invoke,VerifySource);
bool validate_attack_spec(const AttackSpec&);
// Caller supplies owned writable scratch; initializes native cylinder/status separately.
// Native source is 0x44 bytes, not 64. No native calls; Tg/Co disabled.
bool write_attack_source(uint32_t source,const AttackSpec&);
enum class HeartDisposition:uint32_t {Unissued=0,Issuing=1,Queued=2,Failed=3,Admitted=4};
struct HeartState {HeartDisposition disposition;uint32_t queued_pid,source_pid,generation,epoch,address;};
bool read_heart_state(uint32_t state,HeartState&);
// Call only AFTER own defeat marker committed. Position must be exact actor+314.
// State is guest-owned snapshot storage, zero initialized on fresh actor. No automatic retries.
HeartDisposition request_heart(Cpu*,uint32_t state,const SourceIdentity&,uint32_t position,int32_t room);
// Admission is diagnostic tracking, not permission to mutate/own native item behavior.
bool admit_heart(uint32_t state,uint32_t native_item,uint32_t native_create_result);
}
