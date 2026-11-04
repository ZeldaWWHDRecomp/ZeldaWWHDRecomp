/* Invisible sword walls: MIT TWWHD Randomizer make_game_nonlinear.asm.
 * Complete appearance predicate: public CC0 d_a_obj_akabe.cpp a874e1a9. */
#pragma once
WWHD_GAME_FUNC(0x02316A94,u32,randomizer_wall_parameter,(u32 actor,u32 width,u32 shift));
WWHD_GAME_FUNC(0x025BA0C0,s32,randomizer_wall_switch,(u32 save,u32 index,s32 room));
WWHD_REPLACE(0x0231645C,u32,randomizer_wall_appears,(u32 actor)) {
    if(randomizer_wall_parameter(actor,1,12))return 1;
    const u32 index=randomizer_wall_parameter(actor,8,0);
    if(index==255)return 0;
    return !randomizer_wall_switch(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x20,
                                   index,(s32)*(volatile s8*)(actor+0x2FE));
}
