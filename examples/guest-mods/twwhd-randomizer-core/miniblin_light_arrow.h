/* Complete CC0 a874e1a9 daPt_Create02446E54. MIT light-arrow switch intent,
 * corrected to use actor (upstream helper wrongly uses actor+644 in r29).
 * Official second seed confirms3BBE0644 and helper881D03D0/981D0AAD. */
#pragma once
#define RML8(a,o) (*(volatile u8*)((a)+(o)))
#define RML16(a,o) (*(volatile s16*)((a)+(o)))
#define RML32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define RMLF(a,o) (*(volatile f32*)((a)+(o)))
#ifndef RML_PTR
#define RML_PTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
WWHD_GAME_FUNC(0x02446CF4,u32,rml_constructor,(u32 actor));
WWHD_GAME_FUNC(0x02520460,s32,rml_load,(u32 phase,u32 archive));
WWHD_GAME_FUNC(0x025BA0C0,s32,rml_switch,(u32 save,u32 index,s32 room));
WWHD_GAME_FUNC(0x0200E814,u32,rml_character,(u32 manager,u32 name,u32 argument));
WWHD_GAME_FUNC(0x025D63E8,s32,rml_heap,(u32 actor,u32 callback,u32 size));
WWHD_GAME_FUNC(0x025F0A10,u32,rml_hio,(u32 name,u32 data));
WWHD_GAME_FUNC(0x024F06B4,void,rml_acch,(u32 acch,u32 current,u32 old,u32 actor,s32 count,u32 circle,u32 speed,u32 angle,u32 shape));
WWHD_GAME_FUNC(0x024EFF44,void,rml_wall,(u32 circle,f32 height,f32 radius));
WWHD_GAME_FUNC(0x02515F14,void,rml_status,(u32 status,s32 weight,s32 damage,u32 actor));
WWHD_GAME_FUNC(0x0251677C,void,rml_sphere,(u32 sphere,u32 source));
WWHD_GAME_FUNC(0x02446374,s32,rml_execute,(u32 actor));
WWHD_REPLACE(0x02446E54,s32,randomizer_miniblin_create,(u32 actor)) {
    if(!(RML32(actor,0x2E4)&8)) {
        if(actor)(void)rml_constructor(actor);
        RML32(actor,0x2E4)|=8;
    }
    const s32 result=rml_load(actor+0x3C8,0x100382AC);
    if(result!=4)return result;
    const unsigned int parameters=RML32(actor,0xB0);
    const s16 delay=RML16(actor,0x320);
    RML8(actor,0x3D2)=(parameters>>5)&7;
    RML8(actor,0x3D0)=parameters&15;
    RML8(actor,0x3D1)=(u8)(parameters>>8);
    if(parameters&16)RML8(actor,0x3D5)=1;
    const unsigned int reloaded=RML32(actor,0xB0);
    RML16(actor,0x3F4)=delay;
    RML16(actor,0x320)=0;
    RML8(actor,0x3D4)=(u8)(reloaded>>16);
    const u8 enable=(u8)(reloaded>>24);
    RML8(actor,0x3D3)=enable;
    if(enable!=255) {
        const u32 save=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
        const s8 room=WWHD_GAME_DATA(0x1047E6C8,s8);
        if(!rml_switch(save+0x20,enable,room))RML8(actor,0x3D5)=1;
    }
    RML32(actor,0x3A4)=(unsigned int)rml_character(wwhd_dComIfG_getGameInfo()+0x50AC,0x100382B0,0);
    if(!rml_heap(actor,0x02446A4C,0x4B000))return 5;
    if(!WWHD_GAME_DATA(0x101CEF78,u8)) {
        RML8(actor,0xEE0)=1;
        WWHD_GAME_DATA(0x101CEF78,u8)=1;
        WWHD_GAME_DATA(0x1046D334,u8)=(u8)rml_hio(0x100382B8,(u32)&WWHD_GAME_DATA(0x1046D334,u8));
    }
    RML16(actor,0x3EE)=0;
    const unsigned int status=RML32(actor,0x2E0);
    const u32 morf=RML_PTR(actor,0x3DC);
    RML32(actor,0x2E0)=status|0x100;
    RML32(actor,0x39C)=4;
    const u32 model=RML_PTR(morf,0x90);
    RML32(actor,0x348)=model?(unsigned int)(model+0xC8):0;
    rml_acch(actor+0x480,actor+0x314,actor+0x300,actor,1,actor+0x440,actor+0x33C,0,0);
    rml_wall(actor+0x440,50.0f,50.0f);
    RML8(actor,0x3A1)=2;RML8(actor,0x3A0)=2;
    rml_status(actor+0x644,100,0,actor);
    rml_sphere(actor+0x680,(u32)&WWHD_GAME_DATA(0x101CEF9C,u8));
    RML32(actor,0x6C4)=(unsigned int)(actor+0x644);
    rml_sphere(actor+0x7AC,(u32)&WWHD_GAME_DATA(0x101CEFDC,u8));
    /* Read live fields after the replaced Sph.Set, before original setup resumes.
     * Do not reproduce upstream's erroneous A14/A18/10F1 accesses. */
    if(RML8(actor,0x3D0))RML8(actor,0xAAD)=RML8(actor,0x3D4);
    RML32(actor,0x7F0)=(unsigned int)(actor+0x644);
    RML8(actor,0x3A9)=2;
    RML32(actor,0x8FC)=(unsigned int)actor;
    const u32 regs=(u32)&WWHD_GAME_DATA(0x1047B608,u8);
    const f32 thirty=WWHD_GAME_DATA(0x10038278,f32);
    RMLF(actor,0xA9C)=RMLF(regs,0x18)+thirty;
    RMLF(actor,0xA98)=RMLF(regs,0x1C)+thirty;
    RMLF(actor,0xAA8)=RMLF(regs,0x28)+WWHD_GAME_DATA(0x100382A8,f32);
    const f32 forty=WWHD_GAME_DATA(0x1003828C,f32),offset=RMLF(regs,0x2C);
    const u32 fire_morf=RML_PTR(actor,0x3DC);
    RML32(actor,0xCC0)=(unsigned int)fire_morf;
    RML32(actor,0xCB4)=(unsigned int)actor;
    RMLF(actor,0x904)=offset+forty;
    for(unsigned int i=0;i<10;i++) {
        RML8(actor,0xCC4+i)=RML8((u32)&WWHD_GAME_DATA(0x101CF044,u8),i);
        RML32(actor,0xCD0+4*i)=RML32((u32)&WWHD_GAME_DATA(0x101CF01C,u8),4*i);
    }
    (void)rml_execute(actor);
    return result;
}
