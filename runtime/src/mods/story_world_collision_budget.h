// Original sizing model for the native background-collision proof.
#pragma once
#include <cstdint>
namespace story_world_collision {
inline constexpr uint32_t RegistrySlots=256;
struct Counts {uint32_t vertices=0,triangles=0,blocks=0,nodes=0,groups=0,attributes=0;};
struct Storage {
    uint64_t source=0,working=0,body=0;
    constexpr uint64_t payload()const{return source+working+body;}
};
// Payload only: excludes heap headers, array cookies, alignment, group names,
// actor/model storage and matrix storage. Live heap deltas remain authoritative.
constexpr Storage storage(Counts c,bool moving=true){
    const uint64_t v=c.vertices,t=c.triangles,b=c.blocks,n=c.nodes,g=c.groups,a=c.attributes;
    return {0x34+12*v+10*t+2*b+20*n+52*g+16*a,
            (moving?12*v:0)+32*t+6*b+28*n+32*g,0xBC};
}
// A shared registry reserve is separate from a measured heap budget. Never infer
// per-room memory from the number of unoccupied registry slots.
constexpr bool slots_available(uint32_t occupied,uint32_t requested,uint32_t reserve){
    return occupied<=RegistrySlots&&reserve<=RegistrySlots-occupied&&
           requested<=RegistrySlots-occupied-reserve;
}
}
