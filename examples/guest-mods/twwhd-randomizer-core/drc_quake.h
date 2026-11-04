/* Public CC0 daObjQuake_c::_create, d_a_obj_quake.cpp/.h.
 * MIT randomizer policy: use actual DRC boss completion instead of Din's Pearl.
 */
#pragma once
WWHD_GAME_FUNC(0x025D4ED0,void,randomizer_quake_ctor,(u32 actor));
WWHD_GAME_ORIGINAL(0x02520A84,s32,randomizer_quake_boss,(u32 stage));
WWHD_GAME_FUNC(0x02386B20,u8,randomizer_quake_type,(u32 actor));
WWHD_GAME_FUNC(0x025DA884,void,randomizer_quake_draw_queue,(u32 tag));
WWHD_GAME_FUNC(0x025F0A10,s32,randomizer_quake_hio_child,(u32 name,u32 hio));
#ifndef RANDOMIZER_QUAKE_STAGE_INFO
#define RANDOMIZER_QUAKE_STAGE_INFO(stage) \
    ((u32(*)(u32))(*(volatile u32*)(*(volatile u32*)(stage)+0x15C)))((stage))
#endif
WWHD_REPLACE(0x02386B4C,s32,randomizer_quake_create,(u32 actor)) {
    if(!(*(volatile u32*)(actor+0x2E4)&8)) {
        randomizer_quake_ctor(actor);
        *(volatile u32*)(actor+0xB4)=0x1002EBDC;
        *(volatile u32*)(actor+0x2E4)|=8;
    }
    if(randomizer_quake_boss(3))return 3;
    if(randomizer_quake_type(actor)>=3)return 5;
    randomizer_quake_draw_queue(actor+0xDC);
    *(volatile u8*)(actor+0x3BC)=randomizer_quake_type(actor);
    *(volatile u8*)(actor+0x3BD)=0;
    const u32 stage=wwhd_dComIfG_getGameInfo()+0x5150;
    const u32 info=RANDOMIZER_QUAKE_STAGE_INFO(stage);
    *(volatile f32*)(actor+0x3AC)=(f32)*(volatile u8*)(info+0xF)*30.0f;
    const u8 type=*(volatile u8*)(actor+0x3BC);
    if(type==0 || type==2) {
        *(volatile f32*)(actor+0x3B4)=4.0f;
        *(volatile f32*)(actor+0x3B8)=6.0f;
    } else if(type==1) {
        *(volatile f32*)(actor+0x3B4)=0.0f;
        *(volatile f32*)(actor+0x3B8)=6.0f;
    }
    if((s8)WWHD_GAME_DATA(0x1046BD08,u8)<0)
        WWHD_GAME_DATA(0x1046BD08,u8)=(u8)randomizer_quake_hio_child(
            (u32)&WWHD_GAME_DATA(0x1002EC08,u8),(u32)&WWHD_GAME_DATA(0x1046BD08,u8));
    WWHD_GAME_DATA(0x1046BD0C,unsigned int)+=1;
    return 4;
}
