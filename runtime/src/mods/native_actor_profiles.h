// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
struct Cpu;
// Port-private profiles share one native lookup hook. Retail table remains immutable.
namespace native_actor_profiles {
using Owner=std::function<std::string(uint32_t)>;
using Replace=std::function<void(uint32_t,void(*)(Cpu*))>;
using BaseAllocated=void(*)(uint32_t token,uint32_t actor,uint32_t bytes);
void configure(void(*original)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr);
// Snapshot-owned runtime storage; host-only allocations are excluded.
bool valid_storage(uint32_t guest_profile);
bool available(const Owner&);
// Reserved IDs: 7FFE generic, 7FFD props, 7FFC switch, 7FFB door.
// World objects: 7FF6; owned grapple target proxies: 7FF5.
// Planned enemy IDs: 7FFA ChuChu, 7FF9 Bokoblin, 7FF8 Moblin, 7FF7 KeeseKI, 7FF4 Miniblin, 7FF3 Peahat, 7FF2 Rat,
// 7FF1 Darknut, 7FF0 rat hole; 7FE0..7FEF for later kinds: 7FEA Boko Baba, 7FE9 Bubble, 7FE8 Octorok;
// 7FE0..7FE7 and 7FD0..7FDF the table-driven enemies (story_enemies_more.h; from 7FDF down for the later ones).
// Reserving an ID does not admit an enemy or install its native adapter.
// Once installed, unregistered reserved profiles fail closed at lookup/base-create.
bool add(uint16_t id,uint32_t guest_profile,BaseAllocated=nullptr);
// Called before fopAc_Delete dispatches the native class Delete method.
// Includes retail profiles; callbacks must only retire records and must not throw.
using BeforeDelete=void(*)(uint32_t actor,uint32_t pid,uint16_t profile) noexcept;
bool add_before_delete_listener(const std::string& name,BeforeDelete);
// the free bytes of the archive, game, zelda and command heaps and the actor heap (the one actors' own
// parts come from) now (diagnostics: a summoner's full wave)
std::array<uint32_t,5> heap_free(Cpu*);
bool install(const Owner&,const Replace&);
}
