/* Public CC0 d_a_obj_firewall.cpp/_create and pinned HD layout.
 * MIT randomizer flexible_item_locations.asm 02341B0C..1C replaces the event
 * query with room0 switch5. Its beq target02341B90 is ERROR5, not default Wait.
 * Keep the complete native constructor/resource/collision and sound prefix;
 * no global event or switch-query override is installed. */
#pragma once
WWHD_GAME_FUNC(0x02341614,u32,randomizer_firewall_ctor,(u32 actor));
WWHD_GAME_FUNC(0x02520460,s32,randomizer_firewall_load,(u32 phase,u32 name));
WWHD_GAME_FUNC(0x025D63E8,s32,randomizer_firewall_heap,(u32 actor,u32 callback,u32 size));
WWHD_GAME_FUNC(0x023418D0,void,randomizer_firewall_matrix,(u32 actor));
WWHD_GAME_FUNC(0x02515F14,void,randomizer_firewall_status,(u32 status,u32 weight,u32 flags,u32 actor));
WWHD_GAME_FUNC(0x02516518,void,randomizer_firewall_cylinder,(u32 cylinder,u32 source));
WWHD_GAME_FUNC(0x02342FA8,u32,randomizer_firewall_parameter,(u32 actor,u32 width,u32 shift));
WWHD_GAME_FUNC(0x025BA0C0,s32,randomizer_firewall_switch,(u32 save,s32 bit,s32 room));
WWHD_GAME_FUNC(0x02543F10,s32,randomizer_firewall_event_index,(u32 manager,u32 name,u32 hint));
/* Clang contracts this single multiply-add to native fmadds without a libm
 * import. The preceding radial products remain separately rounded f32 values. */
#ifndef RANDOMIZER_FIREWALL_FMADD
static f32 randomizer_firewall_fmadds(f32 a,f32 b,f32 c) {
#pragma clang fp contract(fast)
    return a*b+c;
}
#define RANDOMIZER_FIREWALL_FMADD randomizer_firewall_fmadds
#endif
WWHD_REPLACE(0x023419A0,s32,randomizer_firewall_create,(u32 actor)) {
    u32 condition=*(volatile unsigned int*)(actor+0x2E4);
    if(!(condition&8)) {
        if(actor) {
            (void)randomizer_firewall_ctor(actor);
            condition=*(volatile unsigned int*)(actor+0x2E4);
        }
        *(volatile unsigned int*)(actor+0x2E4)=condition|8;
    }
    const s32 phase=randomizer_firewall_load(actor+0x3AC,
        (u32)&WWHD_GAME_DATA(0x10028A20,u8));
    if(phase!=4)return phase;
    if(!randomizer_firewall_heap(actor,0x023418CC,0x1420))return 5;
    const u32 model=*(volatile u32*)(actor+0x520);
    *(volatile u32*)(actor+0x348)=model?model+0xC8:0;
    randomizer_firewall_matrix(actor);
    randomizer_firewall_status(actor+0x3B4,255,255,actor);
    randomizer_firewall_cylinder(actor+0x3F0,
        (u32)&WWHD_GAME_DATA(0x10028AA4,u8));
    /* Native captures this multiplier once after cylinder Set, before flags. */
    const f32 radius=WWHD_GAME_DATA(0x10028B0C,f32);
    const u32 flags=*(volatile unsigned int*)(actor+0x430);
    *(volatile u32*)(actor+0x434)=actor+0x3B4;
    *(volatile unsigned int*)(actor+0x430)=flags|2;
    const u32 trig=(u32)&WWHD_GAME_DATA(0x104A44F8,u8);
    for(u32 i=0;i<8;i++) {
        const f32 scale=*(volatile f32*)(actor+0x330);
        const f32 sine=*(volatile f32*)(trig+i*0x2000);
        const f32 x=*(volatile f32*)(actor+0x314);
        const f32 sx=sine*radius;
        const f32 cosine=*(volatile f32*)(trig+i*0x2000+4);
        const f32 px=RANDOMIZER_FIREWALL_FMADD(sx,scale,x);
        const unsigned int y=*(volatile unsigned int*)(actor+0x318);
        const f32 cz=cosine*radius;
        const f32 z=*(volatile f32*)(actor+0x31C);
        *(volatile f32*)(actor+0x68C+i*12)=px;
        *(volatile unsigned int*)(actor+0x690+i*12)=y;
        *(volatile f32*)(actor+0x694+i*12)=RANDOMIZER_FIREWALL_FMADD(cz,scale,z);
    }
    *(volatile u8*)(actor+0x6EC)=1;
    const u32 parameter=randomizer_firewall_parameter(actor,8,0);
    *(volatile unsigned int*)(actor+0x644)=parameter;
    const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
    /* Preserve full-word comparison: 0,2,-1,etc enter Wait2; only1 errors. */
    if(randomizer_firewall_switch(save+0x20,5,0)==1)return 5;
    *(volatile unsigned int*)(actor+0x6F0)=1;
    const u32 name=WWHD_GAME_DATA(0x101C9440,u32);
    const u32 manager=wwhd_dComIfG_getGameInfo()+0x52C4;
    const s32 event=randomizer_firewall_event_index(manager,name,255);
    *(volatile u16*)(actor+0x688)=(u16)event;
    *(volatile u16*)(actor+0x680)=0;
    *(volatile unsigned int*)(actor+0x684)=0x02342990;
    *(volatile u16*)(actor+0x682)=0xFFFF;
    return phase;
}
