/* Public CC0 a874e1a9 d_a_tsubo_dmg.cpp and d_a_obj_homen.cpp.
 * Rotation parameters: MIT TWWHD Randomizer1.2.0 flexible_item_locations.asm,
 * Copyright(c)2024 SuperDude88. No item tables or actor parameters are rewritten. */
#pragma once
WWHD_GAME_FUNC(0x02048038,s32,randomizer_pot_parameter,(u32 actor,u32 width,u32 shift));
WWHD_GAME_FUNC(0x02359AB0,s32,randomizer_stone_parameter,(u32 actor,u32 width,u32 shift));
WWHD_GAME_FUNC(0x0201A478,void,randomizer_drop_angles,(u32 angles,s16 x,s16 y,s16 z));
WWHD_GAME_FUNC(0x025D8120,u32,randomizer_drop_table,(u32 position,s32 item,s32 bit,s32 room,s32 type,u32 angles,s32 action,u32 scale));
WWHD_GAME_FUNC(0x025D8870,u32,randomizer_drop_item,(u32 position,s32 item,s32 bit,s32 room,s32 type,u32 angles,s32 action,u32 scale));
WWHD_GAME_FUNC(0x025D9D24,void,randomizer_pot_cancel_carry,(u32 actor));
WWHD_GAME_FUNC(0x024CBF64,void,randomizer_pot_kill_special,(u32 actor));
WWHD_GAME_FUNC(0x024CBE2C,void,randomizer_pot_break_sound,(u32 actor,u32 polygon));
WWHD_GAME_FUNC(0x025DADA4,u32,randomizer_pot_break_pillar,(s16 type,s32 room,u32 position,u32 scale,u32 callback));
WWHD_GAME_FUNC(0x0273AA24,void,randomizer_pot_assert,(u32 file,s32 line,u32 condition));
#ifndef RANDOMIZER_DROP_EFFECT_CALL
#define RANDOMIZER_DROP_EFFECT_CALL(target,self) ((void(*)(u32))(target))((self))
#endif
static void randomizer_drop_effect(u32 actor,u32 descriptor) {
    const s16 index=*(volatile s16*)(descriptor+2);
    const u32 self=actor+(u32)(s32)*(volatile s16*)descriptor;
    u32 target;
    if(index<0)target=*(volatile u32*)(descriptor+4);
    else {const u32 table=*(volatile u32*)(self+(u32)(s32)*(volatile s16*)(descriptor+6));target=*(volatile u32*)(table+(u32)(s32)index*8+4);}
    RANDOMIZER_DROP_EFFECT_CALL(target,self);
}
WWHD_REPLACE(0x024CC004,void,randomizer_pot_damaged,(u32 actor,s32 requested_action,u32 polygon,u32 effects,u32 effect_position)) {
    const s32 action=requested_action<0?0:requested_action>=16?15:requested_action;
    const s32 table=randomizer_pot_parameter(actor,6,0);
    const s32 bit=randomizer_pot_parameter(actor,7,16);
    s16 angles[3];randomizer_drop_angles((u32)angles,0,*(volatile s16*)(actor+0x2FA),0);
    const s32 room=*(volatile s8*)(actor+0x2FE);
    const u32 custom=(*(volatile u16*)(actor+0x798)>>8)&255;
    if(custom!=0 && custom!=255)(void)randomizer_drop_item(actor+0x314,(s32)custom,bit,room,3,(u32)angles,5,0);
    else (void)randomizer_drop_table(actor+0x314,table,bit,room,0,(u32)angles,action,0);
    randomizer_pot_cancel_carry(actor);
    *(volatile u32*)(actor+0x2E0)&=~0x00100000u;
    if(effects) {
        if(*(volatile s32*)(actor+0x790)==2 && effect_position)
            (void)randomizer_pot_break_pillar(0x17,(s32)*(volatile s8*)(actor+0x1C9),effect_position,0,0);
        if((u32)*(volatile s32*)(actor+0x790)>=16)
            randomizer_pot_assert((u32)&WWHD_GAME_DATA(0x10040CAC,u8),0xD62,(u32)&WWHD_GAME_DATA(0x10040CBC,u8));
        const u32 type=(u32)*(volatile s32*)(actor+0x790);
        randomizer_drop_effect(actor,(u32)&WWHD_GAME_DATA(0x10040CE0,u8)+type*8);
        randomizer_pot_break_sound(actor,polygon);
    } else randomizer_pot_kill_special(actor);
}
WWHD_REPLACE(0x023589FC,void,randomizer_stone_make_item,(u32 actor)) {
    s16 angles[3];f32 scale[3];randomizer_drop_angles((u32)angles,0,0,0);
    const f32 one=WWHD_GAME_DATA(0x1002A9E0,f32);scale[2]=one;scale[0]=one;scale[1]=one;
    const s32 table=randomizer_stone_parameter(actor,6,12);
    /* Upstream removes the native table63 early exit, even in fallback mode. */
    const s32 bit=randomizer_stone_parameter(actor,7,0);
    const s32 room=*(volatile s8*)(actor+0x326);
    const u32 custom=*(volatile u16*)(actor+0x2F8)&255;
    if(custom!=0 && custom!=255)(void)randomizer_drop_item(actor+0x314,(s32)custom,bit,room,3,(u32)angles,5,(u32)scale);
    else (void)randomizer_drop_table(actor+0x314,table,bit,room,0,(u32)angles,1,(u32)scale);
}
