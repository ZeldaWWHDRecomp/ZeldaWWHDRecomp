/* Public CC0 daYkgr_c::_create, d_a_ykgr.cpp/.h (a874e1a9).
 * Pond heat haze follows Gohma; other stages and emitter lifecycle stay native. */
#pragma once
WWHD_GAME_FUNC(0x025D4ED0,void,randomizer_drc_heat_ctor,(u32 actor));
WWHD_GAME_FUNC(0x025AAF88,u32,randomizer_drc_heat_path,(s32 index,s32 room));
WWHD_GAME_ORIGINAL(0x02520A84,s32,randomizer_drc_heat_boss,(u32 stage));
WWHD_GAME_FUNC(0x025A847C,u32,randomizer_drc_heat_particle,(u32 control,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 setup,u32 primary,u32 environment,u32 scale2));
WWHD_GAME_FUNC(0x024EA58C,void,randomizer_drc_heat_param,(u32 callback,f32 value));
WWHD_GAME_FUNC(0x025D537C,void,randomizer_drc_heat_layer,(u32 actor));
#ifndef RANDOMIZER_DRC_HEAT_STAGE
static int randomizer_drc_heat_stage(void) {
    u32 a[2]={(u32)&WWHD_GAME_DATA(0x10043034,u8),(u32)&WWHD_GAME_DATA(0x10043040,u8)};
    u32 b[2]={wwhd_dComIfG_getGameInfo()+0x5134,(u32)&WWHD_GAME_DATA(0x10043040,u8)};
    ((void(*)(u32))(*(volatile u32*)(a[1]+0x14)))((u32)a);
    ((void(*)(u32))(*(volatile u32*)(a[1]+0x14)))((u32)a);
    const u32 captured_literal=a[0];
    ((void(*)(u32))(*(volatile u32*)(b[1]+0x14)))((u32)b);
    const u32 second=b[0];
    if(captured_literal==second)return 1;
    u32 first=a[0],next=second;
    for(u32 count=0x40001;count;count--) {
        const u8 ca=*(volatile u8*)first,cb=*(volatile u8*)next;
        if(ca!=cb)return 0;if(!ca)return 1;first++;next++;
    }
    return 0;
}
#define RANDOMIZER_DRC_HEAT_STAGE() randomizer_drc_heat_stage()
#endif
WWHD_REPLACE(0x024EA6A0,s32,randomizer_drc_heat_create,(u32 actor)) {
    u32 prm=*(volatile u32*)(actor+0xB0);
    const u8 type=(u8)((prm>>20)&15);
    if(!(*(volatile u32*)(actor+0x2E4)&8)) {
        if(actor) {
            randomizer_drc_heat_ctor(actor);
            *(volatile u32*)(actor+0xB4)=0x10043058;
        }
        prm=*(volatile u32*)(actor+0xB0);
        *(volatile u32*)(actor+0x2E4)|=8;
    }
    const u8 index=(u8)(prm>>8);
    u32 path=0;
    if(index!=255)path=randomizer_drc_heat_path(index,(s8)*(volatile u8*)(actor+0x326));
    WWHD_GAME_DATA(0x10475630,u32)=path;
    if(RANDOMIZER_DRC_HEAT_STAGE() && randomizer_drc_heat_boss(3))return 3;
    if(WWHD_GAME_DATA(0x10475628,u32)==0) {
        const u32 player=*(volatile u32*)(wwhd_dComIfG_getGameInfo()+0x5B2C);
        *(volatile f32*)(actor+0x314)=*(volatile f32*)(player+0x314);
        *(volatile f32*)(actor+0x318)=*(volatile f32*)(player+0x318);
        *(volatile f32*)(actor+0x31C)=*(volatile f32*)(player+0x31C);
        const u32 control=*(volatile u32*)(wwhd_dComIfG_getGameInfo()+0x5AB0);
        const u32 emitter=randomizer_drc_heat_particle(control,4,0xC06D,actor+0x314,0,0,255,0,-1,0,0,0);
        WWHD_GAME_DATA(0x10475628,u32)=emitter;
        if(!emitter)return 5;
        *(volatile u32*)(emitter+0x1E8)=(u32)&WWHD_GAME_DATA(0x1046EC88,u8);
        randomizer_drc_heat_param((u32)&WWHD_GAME_DATA(0x1046EC88,u8),-3.0f);
        randomizer_drc_heat_layer(actor);
        const u32 current=WWHD_GAME_DATA(0x10475628,u32);
        const u8 alpha=type==1?255:0;
        WWHD_GAME_DATA(0x10475653,u8)=alpha;
        *(volatile u8*)(current+0x247)=alpha;
        *(volatile f32*)(actor+0x3E8)=0.5f;
        *(volatile f32*)(actor+0x3EC)=0.0f;
        if(WWHD_GAME_DATA(0x10475628,u32))WWHD_GAME_DATA(0x10475652,u8)=type==1;
        return 4;
    }
    WWHD_GAME_DATA(0x10475652,u8)=type==1;
    return 3;
}
