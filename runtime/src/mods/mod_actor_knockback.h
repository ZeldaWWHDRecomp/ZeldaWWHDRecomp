// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <initializer_list>
#include <functional>
struct Cpu;
namespace mod_actor_knockback {
inline constexpr uint32_t kAcchBytes=0x1C4,kCircleBytes=0x40,kCircles=3;
struct Point {float x,y,z;};
struct Context {uint32_t actor,pid,acch,circles,old_position,speed;};
using Invoke=uint32_t(*)(Cpu*,uint32_t,std::initializer_list<uint32_t>);
// Must prove exact live owned actor and all immutable appended buffer offsets.
using VerifyActor=bool(*)(const Context&);
bool configure(Invoke,VerifyActor);
// Fresh instance only. Restores retain native objects/pointers; never reinitialize them.
bool initialize(Cpu*,const Context&,float radius,float height);
// Applies native Acch wall/ground response. Failed steps restore all owned geometry/state.
// Caller rechecks exact Link footprint before accepting; this helper only checks BG/room.
bool move(Cpu*,const Context&,uint32_t play,Point desired,float max_step=30,float max_ground_delta=40,const std::function<bool(Point)>& accept_position={});
bool update_dimensions(const Context&,float radius,float height);
// Call before ownership retirement; no deletion flag passed to embedded destructors.
bool destroy(Cpu*,const Context&);
}
