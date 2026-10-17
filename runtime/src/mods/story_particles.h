// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string_view>
struct Cpu;
namespace story_particles {
// Dust is reserved but disabled: native resource maxFrame0 is infinite without
// its retail callback. DoorDebris remains an unavailable/internal candidate.
// AuctionSparkle8153 and BeaconSparkle8152 are certified at30Hz: native bursts and
// exact emitter return to the native free list; public integration enables only
// their fixed masks. Sources: d_a_auction.cpp1094 and d_a_npc_pf1.cpp1240.
// Bank absence still fails closed.
enum class Effect:uint32_t {Dust=0,DoorDebris=1,AuctionSparkle=2,BeaconSparkle=3};
inline constexpr uint32_t kDustMask=1,kDoorDebrisMask=2,kAuctionSparkleMask=4,kBeaconSparkleMask=8,kAllEffectsMask=14;
inline constexpr uint32_t kScratchBytes=0x80;
struct Position {float x=0,y=0,z=0;};
struct Burst {Effect effect=Effect::Dust;Position position;float scale=1;uint32_t alpha=255;int32_t room=-1;};
// Trusted integration resolves an owned actor/place/active scene before making
// this copied placement. Room and Y must be resolved explicitly; quest places
// containing only XZ do not establish ground height by themselves.
struct Placement {Position position;int32_t room=-1;};
struct Options {float scale=1;uint32_t alpha=255;};
using Invoke=uint32_t(*)(Cpu*,uint32_t,std::initializer_list<uint32_t>);
// Invoke must use the existing guest_call ABI: an aligned0x40-byte PPC frame,
// r3..r10 integer arguments, caller-provided outgoing words at incomingSP-40+8.
// Scratch must be exclusively reserved runtime_alloc storage (not host_alloc).
// Admission mask defaults to0. Trusted runtime integration enables a fixed
// effect only after the native certification gate; never accept guest raw IDs.
bool configure(Invoke,uint32_t scratch,uint32_t bytes,uint32_t admission_mask=0);
bool from_name(std::string_view,Effect&);
std::string_view name(Effect);
bool admitted(Effect);
bool valid(const Burst&);
// One-shot copied position, group0, null callbacks/angles/colors. Does not retain
// the emitter pointer or offer follow/cancel semantics. Native expiry owns its
// lifetime. False is explicit failure, including unavailable common/room banks.
// Actor/place/scene callers must first resolve their own identity/lease, copy
// the position/room, enforce a request budget and reject Draw callbacks. This
// low-level function is internal, not a guest-pointer ownership bypass.
// Entire CPU, outgoing stack area and scratch bytes are restored on every exit.
bool burst(Cpu*,const Burst&);
// Synchronous named facade for actor/place/scene integration. Unknown, disabled
// or unavailable effects return false; no identity or placement is retained.
bool burst_named(Cpu*,std::string_view effect,const Placement&,Options={});
}
