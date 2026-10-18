// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "native_actor_profiles.h"
#include "story_actor_identity.h"
#include <span>
namespace story_world_grapple {
inline constexpr uint16_t kProfile=0x7FF5;
inline constexpr uint32_t kProxyBytes=0x420,kStorageBytes=0x4000;
struct Point {float x=0,y=0,z=0;};
struct Target {Point position;float yaw=0;};
using OwnerAlive=std::function<bool(uint32_t,uint32_t,const story_actors::identity::Fingerprint&)>;
bool configure(void(*original)(Cpu*,uint32_t),uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>),uint32_t guest_storage,OwnerAlive);
bool available(const native_actor_profiles::Owner&);
// Registers the profile before the caller installs the shared native profile hooks.
bool install(const native_actor_profiles::Owner&,const native_actor_profiles::Replace&);
bool update(Cpu*,uint32_t parent,uint32_t parent_pid,const story_actors::identity::Fingerprint&,std::span<const Target>);
void destroy(Cpu*,uint32_t parent,uint32_t parent_pid);
// Ownership is entirely in snapshot-owned guest memory; loading does not discard it.
void state_loaded();
}
