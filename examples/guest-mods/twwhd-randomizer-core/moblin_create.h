/* Complete CC0 daMo2_Create021CCB00, public decomp a874e1a9.
 * MIT make_game_nonlinear: skip sword-collection tests021CD230..021CD298.
 * The TU's native jail mode is cleared at its original point. */
#pragma once
#ifndef R_MO2
#define R_MO2(t,address) (*(volatile t*)(address))
#endif
#ifndef RANDOMIZER_MO2_STRING_CALL
#define RANDOMIZER_MO2_STRING_CALL(target,string) ((void(*)(u32))(target))((string))
#endif
WWHD_GAME_FUNC(0x021CC91C,u32,randomizer_mo2_construct,(u32 actor));
WWHD_GAME_FUNC(0x02520460,s32,randomizer_mo2_res_load,(u32 phase,u32 archive));
WWHD_GAME_FUNC(0x025D5600,u32,randomizer_mo2_append,(void));
WWHD_GAME_FUNC(0x025DED64,u32,randomizer_mo2_layer,(void));
WWHD_GAME_FUNC(0x025E14A8,u32,randomizer_mo2_spawn_request,(u32 layer,s32 name,u32 a,u32 b,u32 params));
WWHD_GAME_FUNC(0x025D5218,u32,randomizer_mo2_judge,(u32 callback,u32 key));
WWHD_GAME_FUNC(0x025D57E0,s32,randomizer_mo2_delete,(u32 actor));
WWHD_GAME_FUNC(0x025B8B94,s32,randomizer_mo2_event,(u32 events,u32 bit));
WWHD_GAME_FUNC(0x025BA0C0,s32,randomizer_mo2_switch,(u32 save,s32 index,s32 room));
WWHD_GAME_FUNC(0x025200D4,u32,randomizer_mo2_game,(void));
WWHD_GAME_FUNC(0x025ABBBC,s32,randomizer_mo2_character2,(u32 table,u32 name,s32 mode));
WWHD_GAME_FUNC(0x0200E814,s32,randomizer_mo2_character,(u32 table,u32 name,s32 mode));
WWHD_GAME_FUNC(0x025D63E8,s32,randomizer_mo2_heap,(u32 actor,u32 callback,u32 size));
WWHD_GAME_FUNC(0x025F0A10,s32,randomizer_mo2_hio_child,(u32 name,u32 hio));
WWHD_GAME_FUNC(0x025D672C,void,randomizer_mo2_min,(u32 actor,f32 x,f32 y,f32 z));
WWHD_GAME_FUNC(0x025D673C,void,randomizer_mo2_max,(u32 actor,f32 x,f32 y,f32 z));
WWHD_GAME_FUNC(0x021C6440,void,randomizer_mo2_animation,(u32 actor,s32 index,f32 morf,u32 loop,f32 speed,s32 sound));
WWHD_GAME_FUNC(0x025AAF88,u32,randomizer_mo2_path,(s32 index,s32 room));
WWHD_GAME_FUNC(0x024F06B4,void,randomizer_mo2_acch,(u32 acch,u32 pos,u32 old,u32 actor,s32 enabled,u32 circle,u32 speed,u32 rotation,u32 shape));
WWHD_GAME_FUNC(0x024EFF44,void,randomizer_mo2_wall,(u32 circle,f32 radius,f32 height));
WWHD_GAME_FUNC(0x02515F14,void,randomizer_mo2_status,(u32 status,u32 weight,u32 damage,u32 actor));
WWHD_GAME_FUNC(0x02516518,void,randomizer_mo2_cylinder,(u32 cylinder,u32 source));
WWHD_GAME_FUNC(0x0251677C,void,randomizer_mo2_sphere,(u32 sphere,u32 source));
WWHD_GAME_FUNC(0x021C9B1C,s32,randomizer_mo2_execute,(u32 actor));
static s32 randomizer_mo2_equal(u32 text,u32 other) {
    u32 left[2]={text,0x10014D94},right[2]={other,0x10014D94};
    RANDOMIZER_MO2_STRING_CALL(R_MO2(u32,left[1]+0x14),(u32)left);
    RANDOMIZER_MO2_STRING_CALL(R_MO2(u32,left[1]+0x14),(u32)left);
    RANDOMIZER_MO2_STRING_CALL(R_MO2(u32,right[1]+0x14),(u32)right);
    if(left[0]==right[0])return 1;
    for(u32 i=0;i<0x40001;i++) {
        const u8 a=R_MO2(u8,left[0]+i);
        if(a!=R_MO2(u8,right[0]+i))return 0;
        if(a==0)return 1;
    }
    return 0;
}
static s32 randomizer_mo2_lantern_init(u32 actor) {
    if(R_MO2(s8,actor+0x2E0C)==0) {
        const u32 params=randomizer_mo2_append();
        R_MO2(u32,params+4)=R_MO2(u32,actor+0x314);
        R_MO2(u32,params+8)=R_MO2(u32,actor+0x318);
        R_MO2(u32,params+12)=R_MO2(u32,actor+0x31C);
        R_MO2(s16,params+0x12)=R_MO2(s16,actor+0x322);
        R_MO2(u32,params)=(R_MO2(u32,actor+0xB0)&0xFF000000u)|0xFFFF23;
        R_MO2(s8,params+0x21)=R_MO2(s8,actor+0x326);
        const u32 layer=randomizer_mo2_layer();
        R_MO2(u32,actor+0x460)=randomizer_mo2_spawn_request(layer,0xC1,0,0,params);
        R_MO2(s8,actor+0x2E0C)++;
        return 1;
    }
    if(R_MO2(s8,actor+0x2E0C)!=100) {
        const u32 key=R_MO2(u32,actor+0x460);
        const u32 lantern=key==0xFFFFFFFFu?0:randomizer_mo2_judge(0x025E1234,(u32)&key);
        if(lantern) {
            R_MO2(u32,lantern+0x3C0)=actor?R_MO2(u32,actor+4):0xFFFFFFFFu;
            R_MO2(s8,actor+0x2E0C)=100;
            return 0;
        }
        R_MO2(s8,actor+0x2E0C)++;
        if(R_MO2(s8,actor+0x2E0C)>20)randomizer_mo2_delete(actor);
        return 1;
    }
    return 0;
}
WWHD_REPLACE(0x021CCB00,s32,randomizer_moblin_create,(u32 actor)) {
    if(!(R_MO2(u32,actor+0x2E4)&8)) {
        if(actor)(void)randomizer_mo2_construct(actor);
        R_MO2(u32,actor+0x2E4)|=8;
    }
    const s32 spear=randomizer_mo2_res_load(actor+0x3D0,0x1001518C);
    if(spear!=4)return spear;
    const s32 body=randomizer_mo2_res_load(actor+0x3C8,0x10015184);
    if(body!=4)return body;
    R_MO2(u8,actor+0x3D8)=(u8)R_MO2(u32,actor+0xB0);
    if(R_MO2(u8,actor+0x3D8)==1 && randomizer_mo2_lantern_init(actor))return 0;
    R_MO2(u8,actor+0x2DE)=2;
    if(randomizer_mo2_equal(0x1001517C,randomizer_mo2_game()+0x5134) ||
       randomizer_mo2_equal(0x10015194,randomizer_mo2_game()+0x5134) ||
       randomizer_mo2_equal(0x1001519C,randomizer_mo2_game()+0x5134))
        WWHD_GAME_DATA(0x104654A4,s8)=1;
    else WWHD_GAME_DATA(0x104654A4,s8)=0;
    const u32 params=R_MO2(u32,actor+0xB0);
    const s16 angle=R_MO2(s16,actor+0x324);
    R_MO2(s16,actor+0x324)=0;
    R_MO2(u8,actor+0x3DC)=(u8)angle;
    R_MO2(u8,actor+0x3D9)=(u8)(params>>8);
    R_MO2(s16,actor+0x320)=0;
    R_MO2(u8,actor+0x3DA)=(u8)(params>>16);
    R_MO2(u8,actor+0x3DB)=(u8)(params>>24);
    if((u8)angle==255)R_MO2(u8,actor+0x3DC)=0;
    else if((u8)angle && (u8)angle<=127) {
        R_MO2(u32,actor+0x2E0)|=0x04000000;
        WWHD_GAME_DATA(0x104654A4,s8)=1;
    }
    if(randomizer_mo2_event(randomizer_daily_save()+0x644,0x1101) &&
       R_MO2(u8,actor+0x3DC)!=0 &&
       randomizer_mo2_switch(randomizer_daily_save(),R_MO2(u8,actor+0x3DC),R_MO2(s8,actor+0x326)))return 5;
    if(randomizer_mo2_equal(0x100151A4,randomizer_mo2_game()+0x5134))
        R_MO2(s32,actor+0x3A4)=randomizer_mo2_character2(randomizer_mo2_game()+0x50A0,0x10015188,1);
    else R_MO2(s32,actor+0x3A4)=randomizer_mo2_character(randomizer_mo2_game()+0x50AC,0x10015188,0);
    if(!randomizer_mo2_heap(actor,0x021CC524,0))return 5;
    if(WWHD_GAME_DATA(0x101BA690,u8)==0) {
        const s32 no=randomizer_mo2_hio_child(0x100151AC,0x104654E0);
        WWHD_GAME_DATA(0x104654FC,s16)=0;
        WWHD_GAME_DATA(0x104654E0,s8)=(s8)no;
        WWHD_GAME_DATA(0x101BA693,s8)=0;
        R_MO2(u8,actor+0x2E0E)=1;
        WWHD_GAME_DATA(0x101BA690,u8)=1;
        WWHD_GAME_DATA(0x101BA691,u8)=0;
    }
    randomizer_mo2_min(actor,-200.0f,-50.0f,-100.0f);
    const f32 radius=WWHD_GAME_DATA(0x10015168,f32);
    randomizer_mo2_max(actor,radius,250.0f,250.0f);
    const u32 model=R_MO2(u32,R_MO2(u32,actor+0x3E0)+0x90);
    R_MO2(u32,actor+0x348)=model?model+0xC8:0;
    R_MO2(u32,R_MO2(u32,R_MO2(u32,actor+0x3E0)+0x90)+0xB8)=actor;
    R_MO2(f32,actor+0x3B0)=radius;
    R_MO2(u32,actor+0xFD4)=R_MO2(u32,actor+0x318);
    R_MO2(f32,actor+0x3AC)=162.5f;
    R_MO2(s8,actor+0x10B0)=1;
    R_MO2(u8,actor+0xFFC)=1;
    R_MO2(f32,actor+0x1010)=1000.0f;
    R_MO2(s16,actor+0xDBE)=5;
    WWHD_GAME_DATA(0x101BA692,u8)=0;
    R_MO2(u32,actor+0x39C)=4;
    if(R_MO2(u8,actor+0x3D8)==5) {
        randomizer_mo2_animation(actor,0x4C,1.0f,2,1.0f,0x15);
        R_MO2(s16,actor+0x8C2)=30;
        R_MO2(f32,actor+0x1010)=100000.0f;
    }
    if(R_MO2(u8,actor+0x3DA)!=255) {
        const u32 path=randomizer_mo2_path(R_MO2(u8,actor+0x3DA),R_MO2(s8,actor+0x326));
        R_MO2(u32,actor+0x2D74)=path;
        if(!path)return 5;
        R_MO2(s8,actor+0x2D72)=1;
        R_MO2(s8,actor+0x2D70)=(s8)(R_MO2(u8,actor+0x3DA)+1);
    }
    if(R_MO2(u8,actor+0x3DB)!=255)
        R_MO2(u8,actor+0x3DD)=(u8)(R_MO2(u8,actor+0x3DB)+1);
    if(R_MO2(u8,actor+0x3D8)==15) {
        R_MO2(u8,actor+0x3DD)=0;
        R_MO2(s16,actor+0x8C2)=23;
        const u8 dungeon=R_MO2(u8,randomizer_daily_save()+0x2E);
        if(dungeon!=0x39 && dungeon!=0x3E && dungeon!=0x3A)R_MO2(s16,actor+0x848)=5;
    }
    randomizer_mo2_acch(actor+0xE0C,actor+0x314,actor+0x300,actor,1,actor+0xDCC,actor+0x33C,0,0);
    randomizer_mo2_wall(actor+0xDCC,50.0f,50.0f);
    R_MO2(u32,actor+0xE34)&=~8u;
    R_MO2(u8,actor+0x3A1)=10;
    R_MO2(f32,actor+0xECC)=WWHD_GAME_DATA(0x1047B62C,f32)+100.0f;
    R_MO2(u8,actor+0x3A0)=10;
    const u32 status=actor+0x1014;
    randomizer_mo2_status(status,200,255,actor);
    randomizer_mo2_cylinder(actor+0x2494,0x101BAB48);
    R_MO2(u32,actor+0x24D8)=status;
    randomizer_mo2_cylinder(actor+0x25C4,0x101BAB8C);
    R_MO2(u32,actor+0x2608)=status;
    randomizer_mo2_sphere(actor+0x26F4,0x101BAA14);
    R_MO2(u32,actor+0x2738)=status;
    randomizer_mo2_sphere(actor+0x294C,0x101BAA54);
    R_MO2(u32,actor+0x2990)=status;
    randomizer_mo2_sphere(actor+0x2A78,0x101BAA94);
    R_MO2(u32,actor+0x29A4)=0x021C9AA0;
    R_MO2(u32,actor+0x2AD0)=0x021C9AA0;
    R_MO2(u32,actor+0x2ABC)=status;
    randomizer_mo2_sphere(actor+0x2820,0x101BAAD4);
    R_MO2(u32,actor+0x2864)=status;
    const u32 morf=R_MO2(u32,actor+0x3E0);
    R_MO2(u32,actor+0x2E54)=actor;
    R_MO2(u32,actor+0x368)=R_MO2(u32,morf+0x90);
    R_MO2(f32,actor+0x2FF4)=WWHD_GAME_DATA(0x1047B628,f32)+70.0f;
    R_MO2(u32,actor+0x3218)=morf;
    R_MO2(u32,actor+0x320C)=actor;
    R_MO2(f32,actor+0x3000)=1.5f;
    R_MO2(f32,actor+0x2FF0)=WWHD_GAME_DATA(0x1047B62C,f32)+200.0f;
    R_MO2(u8,actor+0x3005)=R_MO2(u8,actor+0x3DC);
    const volatile s8* joints=&WWHD_GAME_DATA(0x101BAB3C,s8);
    const volatile f32* scales=&WWHD_GAME_DATA(0x101BAB14,f32);
    for(u32 i=0;i<10;i++) {
        R_MO2(s8,actor+0x321C+i)=joints[i];
        R_MO2(f32,actor+0x3228+4*i)=scales[i];
    }
    R_MO2(s8,actor+0x3A9)=5;
    (void)randomizer_mo2_execute(actor);
    (void)randomizer_mo2_execute(actor);
    return 4;
}
