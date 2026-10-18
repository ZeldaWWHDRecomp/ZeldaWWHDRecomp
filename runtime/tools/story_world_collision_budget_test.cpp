#include <cassert>
#include <cstdint>
#include <limits>
#include "mods/story_world_collision_budget.h"
using namespace story_world_collision;
int main(){
    // Eight original box vertices, twelve triangles, one leaf/group/material.
    constexpr auto box=storage({8,12,1,1,1,1});
    static_assert(box.source==358&&box.working==546&&box.body==188&&box.payload()==1092);
    static_assert(storage({8,12,1,1,1,1},false).payload()==996);
    assert(slots_available(240,3,13));assert(!slots_available(241,3,13));
    assert(!slots_available(257,0,0));assert(!slots_available(0,1,256));
    constexpr auto max=std::numeric_limits<uint32_t>::max();
    constexpr auto huge=storage({max,max,max,max,max,max});
    static_assert(huge.payload()>uint64_t(max));
}
