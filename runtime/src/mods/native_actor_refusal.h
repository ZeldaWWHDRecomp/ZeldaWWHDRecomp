// SPDX-License-Identifier: MPL-2.0
// A create the port turns down (an owned enemy, weapon or bomb whose record does not match).
// Retail creates construct the actor first (fopAcM_SetupActor runs its constructor) and may fail
// after; fopAc_Delete then asserts the actor is constructed and runs its virtual destructor. A
// refusal that never runs the game's create would leave a raw actor the game later deletes
// ("constructStatus == CONSTRUCTED" halt). So a refusal runs the actor base constructor
// (fopAc_ac_c::fopAc_ac_c: constructed, the base vtable) before failing the create, and the
// actor's own delete is skipped for such a base-only actor: nothing of its own was built.
#pragma once
#include <cstdint>
#include <initializer_list>
#include "guest_addr.h"
#include "runtime.h"
namespace native_actor_refusal {
inline constexpr uint32_t kConstructState = 0x3AA;  // fopAc_ac_c::mHdConstructState: 0 raw, 1 constructed, 2 destroyed
inline constexpr uint32_t kVtable = 0xB4;           // fopAc_ac_c::__vtbl
inline constexpr uint8_t kRaw = 0, kConstructed = 1;
inline uint32_t base_constructor() { return GC(0x025D4ED0); }  // fopAc_ac_c__ct
inline uint32_t base_vtable() { return GD(0x10057138); }       // l_actorVtable
// fails the create (cPhs_ERROR_e); a raw actor gets its base constructor first
inline void refuse(Cpu* c, uint32_t actor, uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>)) {
    if (actor && call && ld8(actor + kConstructState) == kRaw) { Cpu probe = *c; call(&probe, base_constructor(), {actor}); }
    c->r[3] = 5;
}
// an actor refused before its own create ran: only the base part exists
inline bool base_only(uint32_t actor) { return actor && ld8(actor + kConstructState) == kConstructed && ld32(actor + kVtable) == base_vtable(); }
}  // namespace native_actor_refusal
