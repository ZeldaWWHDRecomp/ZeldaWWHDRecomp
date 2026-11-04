/* Complete CC0 a874e1a9 daNpc_Ji1_c::battleAction0225AEC8.
 * MIT fix_vanilla_bugs skips only timer creation for no equipped sword. */
#pragma once
#ifndef ROC
#define ROC(t,a) (*(volatile t*)(a))
#endif
WWHD_GAME_FUNC(0x025200D4,u32,roc_game,(void));
WWHD_GAME_FUNC(0x025C60F4,void,roc_counter,(s32 mode,u32 limit,u32 type,u32 icon,f32 x,f32 y,f32 width,f32 height));
WWHD_GAME_FUNC(0x0224DFF0,void,roc_animation,(u32 actor,s32 animation,f32 morph,s32 mode));
WWHD_GAME_FUNC(0x020198D8,f64,roc_random,(f64 range));
WWHD_GAME_FUNC(0x0225A2C8,void,roc_wait,(u32 actor));
WWHD_GAME_FUNC(0x0201ADE0,void,roc_subtract,(u32 first,u32 out,u32 second));
WWHD_GAME_FUNC(0x020195B0,s32,roc_angle,(f64 x,f64 z));
WWHD_GAME_FUNC(0x028E8DD0,f64,roc_square,(u32 vector));
WWHD_GAME_FUNC(0x028F4384,f64,roc_sqrt,(f64 value));
WWHD_GAME_FUNC(0x0225A534,s32,roc_guard,(u32 actor));
WWHD_GAME_FUNC(0x0224FB98,void,roc_particle,(u32 actor));
WWHD_GAME_FUNC(0x0224D4F8,s32,roc_attacking,(u32 actor));
WWHD_GAME_FUNC(0x0225AEB4,void,roc_attack_particle,(u32 actor));
static s32 roc_integer(f64 value) {
    if(value!=value||value<=-2147483648.0)return (s32)0x80000000u;
    if(value>=2147483648.0)return 0x7FFFFFFF;
    return (s32)value;
}
#ifndef ROC_ACTION
static void roc_action(u32 actor) {
    const s16 index=ROC(s16,actor+0x3E6),delta=ROC(s16,actor+0x3E4);
    const u32 adjusted=actor+(s32)delta;
    u32 function;
    if(index<0)function=ROC(u32,actor+0x3E8);
    else {
        const s16 offset=ROC(s16,actor+0x3EA);
        const u32 table=ROC(u32,adjusted+(s32)offset);
        function=ROC(u32,table+(u32)index*8+4);
    }
    ((void(*)(u32))function)(adjusted);
}
#define ROC_ACTION(actor) roc_action(actor)
#endif
WWHD_REPLACE(0x0225AEC8,s32,randomizer_orca_battle,(u32 actor,u32 unused)) {
    (void)unused;
    const u32 play=roc_game();
    const s8 mode=ROC(s8,actor+0xEA0);
    const f32 zero=WWHD_GAME_DATA(0x1001AE10,f32);
    const u32 player=ROC(u32,play+0x5B2C);
    if(mode==0) {
        const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        const u8 sword=ROC(u8,save+0x2E);
        if(sword!=0xFF)roc_counter(6,1000,2,sword==0x38?1:2,
            WWHD_GAME_DATA(0x1001B3B8,f32),WWHD_GAME_DATA(0x1001B3BC,f32),
            WWHD_GAME_DATA(0x1001B3C0,f32),WWHD_GAME_DATA(0x1001B3C4,f32));
        ROC(u16,roc_game()+0x5CEC)=0;
        ROC(unsigned int,actor+0xE60)=0;
        roc_animation(actor,5,zero,0);
        ROC(unsigned int,actor+0x39C)|=5;
        ROC(u8,actor+0x38A)=3;
        const f64 random=roc_random(WWHD_GAME_DATA(0x1001B194,f32));
        ROC(unsigned int,actor+0xF98)=0;
        ROC(unsigned int,actor+0xF94)=0;
        ROC(s32,actor+0xE54)=(s32)(s16)roc_integer(random)+30;
        roc_wait(actor);
        const u8 next=ROC(u8,actor+0xEA0);
        ROC(unsigned int,actor+0xE5C)=0;
        ROC(u8,actor+0xEA0)=(u8)(next+1u);
    } else if(mode==-1) {
        const unsigned int flags=ROC(unsigned int,actor+0x39C);
        ROC(u8,actor+0x38A)=0xB5;
        ROC(unsigned int,actor+0x39C)=flags&~4u;
    } else {
        union {f32 values[3];unsigned int words[3];} difference,horizontal;
        roc_subtract(player+0x314,(u32)difference.values,actor+0x314);
        (void)roc_angle(difference.values[0],difference.values[2]);
        horizontal.words[0]=difference.words[0];horizontal.values[1]=zero;horizontal.words[2]=difference.words[2];
        (void)roc_sqrt(roc_square((u32)horizontal.values));
        ROC(unsigned int,actor+0xA9C)&=~1u;
        (void)roc_guard(actor);
        ROC_ACTION(actor);
        if(ROC(s32,actor+0xF94)!=1)roc_particle(actor);
        if(!roc_attacking(actor)||
           (ROC(s16,actor+0x3E6)==-1&&ROC(s16,actor+0x3E4)==0&&ROC(unsigned int,actor+0x3E8)==0x0225C208))
            roc_attack_particle(actor);
        s32 life=(s32)(3u-ROC(unsigned int,actor+0xE60));
        if(life<0)life=0;
        WWHD_GAME_DATA(0x101D5F44,u8)=(u8)life;
    }
    return 1;
}
