// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <functional>
#include <string>
struct Cpu;
namespace story_enemy_projectiles {
enum class Kind:uint32_t { Arrow=1,Bomb,Grapple,Hookshot,Boomerang };
struct Descriptor {uint16_t profile;uint32_t bytes,parameters,object,stts,attack_type;uint8_t power;};
const Descriptor& descriptor(Kind);
// PURE exact current native play+5B34 Link read; no native calls or guessed owner.
using CurrentLink=std::function<bool(uint32_t& actor,uint32_t& pid)>;
bool configure(void(*original)(Cpu*,uint32_t),CurrentLink,bool combat_enabled=false);
bool install(const std::function<std::string(uint32_t)>& owner,const std::function<void(uint32_t,void(*)(Cpu*))>& replace);
// Called by the provider BEFORE native class destruction, with verified identity.
void before_delete(uint32_t actor,uint32_t pid,uint16_t profile) noexcept;
void state_loaded();
void room_changed();
// True only for exact admitted outgoing collider with live rooted lineage.
bool owns_attack(uint32_t object);
// A fastCreate is synchronous; high Link hooks prove the changed actual Keep,
// then snapshot queued PID and constructor-complete address in one bounded record.
}
