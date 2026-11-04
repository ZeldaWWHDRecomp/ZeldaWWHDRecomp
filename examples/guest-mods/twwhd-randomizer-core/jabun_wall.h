/* Public CC0 daObjAjav::Act_c::_create, d_a_obj_ajav.cpp.
 * MIT randomizer policy at 023149EC: ignore the Endless Night query result.
 * Keep the entire native constructor/resource/collision consumer. For a fresh
 * actor, arm only after its final hint-cylinder constructor array returns;
 * no callback occurs between that return and the target event query. Already
 * constructed actors reach that query without any preceding native calls.
 */
#pragma once
#include "jabun_wall_state.h"
WWHD_GAME_ORIGINAL(0x023148B4,s32,randomizer_native_jabun_wall_create,(u32 actor));
WWHD_REPLACE(0x023148B4,s32,randomizer_jabun_wall_create,(u32 actor)) {
    const randomizer_jabun_wall_scope previous=randomizer_jabun_wall;
    randomizer_jabun_wall=(randomizer_jabun_wall_scope){
        actor,(*(volatile u32*)(actor+0x2E4)&8)!=0};
    const s32 result=randomizer_native_jabun_wall_create(actor);
    randomizer_jabun_wall=previous;
    return result;
}
WWHD_HOOK_RETURN(0x028EFFD0,randomizer_jabun_wall_constructed,
                 (u32 array,u32 count,u32 stride,u32 constructor)) {
    if(randomizer_jabun_wall.actor && array==randomizer_jabun_wall.actor+0x710 &&
       count==2 && stride==0x130 && constructor==0x02315DB8)
        randomizer_jabun_wall.pending=1;
}
