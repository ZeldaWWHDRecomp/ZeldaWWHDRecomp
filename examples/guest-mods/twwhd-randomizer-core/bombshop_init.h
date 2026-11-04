/* CC0 daNpc_Bms1_c::CreateInit, public wwhd source a874e1a9.
 * MIT randomizer: all four pearl queries in this consumer return true.
 * No saved pearl bit or event flag is changed. */
#pragma once
#ifndef R_BMS
#define R_BMS(t,address) (*(volatile t*)(address))
#endif
#ifndef RANDOMIZER_BMS_CALL
#define RANDOMIZER_BMS_CALL(target,self) ((void(*)(u32,u32))(target))((self),0)
#endif
WWHD_GAME_FUNC(0x02515F14,void,randomizer_bms_status_init,(u32 status,u32 weight,u32 damage,u32 actor));
WWHD_GAME_FUNC(0x02516518,void,randomizer_bms_cylinder_set,(u32 cylinder,u32 source));
WWHD_GAME_FUNC(0x025BCF84,void,randomizer_bms_item_list,(u32 items));
WWHD_GAME_FUNC(0x025BC810,void,randomizer_bms_items_create,(u32 items,s32 count,s32 room));
WWHD_GAME_FUNC(0x0259F7D4,void,randomizer_bms_event_actor,(u32 cut,u32 name,u32 actor));
WWHD_GAME_FUNC(0x025E55A0,void,randomizer_bms_morf_calc,(u32 morf));
WWHD_GAME_FUNC(0x02209438,void,randomizer_bms_set_mtx,(u32 actor));
static void randomizer_bms_wait_action(u32 actor) {
    const s16 index=R_BMS(s16,actor+0x962);
    if(index==-1 && R_BMS(s16,actor+0x960)==0 && R_BMS(u32,actor+0x964)==0x0220BDCC)return;
    if(index!=0) {
        const u32 self=actor+(s16)R_BMS(s16,actor+0x960);
        R_BMS(s8,actor+0xA08)=-1;
        if(index<0)RANDOMIZER_BMS_CALL(R_BMS(u32,actor+0x964),self);
        else {
            const u32 table=R_BMS(u32,self+R_BMS(s16,actor+0x966));
            RANDOMIZER_BMS_CALL(R_BMS(u32,table+(u32)index*8+4),self);
        }
    }
    R_BMS(s8,actor+0xA08)=0;
    R_BMS(s16,actor+0x960)=0;
    R_BMS(s16,actor+0x962)=-1;
    R_BMS(u32,actor+0x964)=0x0220BDCC;
    RANDOMIZER_BMS_CALL(R_BMS(u32,actor+0x964),actor);
}
WWHD_REPLACE(0x022098D0,s32,randomizer_bombshop_init,(u32 actor)) {
    /* Native reads angle Y/Z before X and stores Y/Z before X. */
    const u16 z=R_BMS(u16,actor+0x324),y=R_BMS(u16,actor+0x322);
    const s8 type=R_BMS(s8,actor+0xA06);
    R_BMS(u16,actor+0x938)=y;
    R_BMS(u16,actor+0x93A)=z;
    const u16 x=R_BMS(u16,actor+0x320);
    R_BMS(f32,actor+0x374)=WWHD_GAME_DATA(0x10017EC4,f32);
    R_BMS(u16,actor+0x936)=x;
    R_BMS(u8,actor+0x38B)=0xAD;
    R_BMS(u32,actor+0x39C)=10;
    R_BMS(u8,actor+0x389)=0xAD;
    if(type==0 || type==1)randomizer_bms_wait_action(actor);
    const u32 pz=R_BMS(u32,actor+0x31C),py=R_BMS(u32,actor+0x318),px=R_BMS(u32,actor+0x314);
    R_BMS(u32,actor+0x930)=pz;
    R_BMS(u32,actor+0x92C)=py;
    R_BMS(u32,actor+0x928)=px;
    randomizer_bms_status_init(actor+0x6DC,255,255,actor);
    randomizer_bms_cylinder_set(actor+0x718,0x101BD2D4);
    R_BMS(u8,actor+0xA0A)=0;
    R_BMS(u32,actor+0x958)=0;
    R_BMS(u8,actor+0x95C)=255;
    R_BMS(u32,actor+0x75C)=actor+0x6DC;
    R_BMS(u8,actor+0x918)=0;
    if(R_BMS(s8,actor+0xA06)!=1) {
        const f32 scale=WWHD_GAME_DATA(0x10017EC8,f32);
        R_BMS(f32,actor+0x3CC)=scale;
        R_BMS(f32,actor+0x3D4)=scale;
        R_BMS(f32,actor+0x3D8)=scale;
        R_BMS(f32,actor+0x3D0)=scale;
        R_BMS(f32,actor+0x3DC)=scale;
        R_BMS(f32,actor+0x3E8)=WWHD_GAME_DATA(0x10017DD4,f32);
        R_BMS(f32,actor+0x3E4)=WWHD_GAME_DATA(0x10017DD4,f32);
        R_BMS(f32,actor+0x3E0)=scale;
    } else {
        const f32 left_x=WWHD_GAME_DATA(0x10017ECC,f32),left_y=WWHD_GAME_DATA(0x10017ED0,f32);
        const f32 right_x=WWHD_GAME_DATA(0x10017ED4,f32),right_y=WWHD_GAME_DATA(0x10017ED8,f32);
        R_BMS(f32,actor+0x3D0)=left_y;
        R_BMS(f32,actor+0x3CC)=left_x;
        R_BMS(f32,actor+0x3D8)=right_x;
        R_BMS(f32,actor+0x3D4)=left_y;
        R_BMS(f32,actor+0x3DC)=right_y;
        R_BMS(f32,actor+0x3E8)=WWHD_GAME_DATA(0x10017DD4,f32);
        R_BMS(f32,actor+0x3E4)=WWHD_GAME_DATA(0x10017DD4,f32);
        R_BMS(f32,actor+0x3E0)=right_y;
    }
    R_BMS(u32,actor+0x41C)=WWHD_GAME_DATA(0x101E9C38,u32);
    R_BMS(u32,actor+0x420)=WWHD_GAME_DATA(0x101E9C3C,u32);
    R_BMS(u32,actor+0x424)=WWHD_GAME_DATA(0x101E9C40,u32);
    R_BMS(u32,actor+0x428)=WWHD_GAME_DATA(0x101E9C44,u32);
    R_BMS(u32,actor+0x42C)=WWHD_GAME_DATA(0x101E9C38,u32);
    R_BMS(u32,actor+0x430)=WWHD_GAME_DATA(0x101E9C3C,u32);
    R_BMS(u32,actor+0x434)=WWHD_GAME_DATA(0x101E9C40,u32);
    R_BMS(u32,actor+0x438)=WWHD_GAME_DATA(0x101E9C44,u32);
    R_BMS(s16,actor+0x9FA)=2;
    R_BMS(s16,actor+0x96A)=0;
    R_BMS(u32,actor+0x96C)=0;
    R_BMS(s16,actor+0x968)=0;
    R_BMS(s16,actor+0x9B6)=2;
    randomizer_bms_item_list(actor+0x9B8);
    randomizer_bms_items_create(actor+0x9B8,3,R_BMS(s8,actor+0x326));
    const u32 height=R_BMS(u32,actor+0x318);
    R_BMS(u8,actor+0x95D)=0;
    R_BMS(u32,actor+0x948)=height;
    randomizer_bms_event_actor(actor+0x884,0x10017EDC,actor);
    const u32 morf=R_BMS(u32,actor+0x444);
    R_BMS(u32,actor+0x8EC)=actor+0x850;
    randomizer_bms_morf_calc(morf);
    randomizer_bms_set_mtx(actor);
    return 1;
}
