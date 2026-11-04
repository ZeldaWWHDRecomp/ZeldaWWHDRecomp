/* Public CC0 a874e1a9 d_com_inf_game.cpp setGameStartStage.
 * MIT TWWHD Randomizer1.2.0 entrance_rando_features.asm025220C4 policy.
 * Built-in mod-stage handling remains outside this native-body replacement. */
#pragma once
WWHD_GAME_ORIGINAL(0x025217F8,void,randomizer_native_savewarp_stage,(void));
WWHD_GAME_FUNC(0x025B50DC,void,randomizer_savewarp_set,(u32 place,u32 name,s32 room,u32 point));
WWHD_GAME_FUNC(0x02525C34,void,randomizer_savewarp_assure,(u32 string));
WWHD_GAME_FUNC(0x02560728,void,randomizer_savewarp_time,(f32 time));
WWHD_GAME_FUNC(0x0252174C,u32,randomizer_savewarp_landing,(u32 room));
WWHD_GAME_FUNC(0x02521678,u32,randomizer_savewarp_mesh,(u32 position));
WWHD_GAME_FUNC(0x025215BC,u32,randomizer_savewarp_scene,(s32 number));
WWHD_GAME_FUNC(0x025C1BB8,u32,randomizer_savewarp_ocean_z,(u32 map));
WWHD_GAME_FUNC(0x025C1BA0,u32,randomizer_savewarp_ocean_x,(u32 map));
WWHD_GAME_FUNC(0x0273AA24,void,randomizer_savewarp_assert,(u32 file,s32 line,u32 condition));
#ifndef RANDOMIZER_SAVEWARP_VCALL
#define RANDOMIZER_SAVEWARP_VCALL(object,slot) \
    ((u32(*)(u32))(*(volatile u32*)(*(volatile u32*)(object)+(slot))))((object))
#endif
#ifndef RANDOMIZER_SAVEWARP_STRING_CALL
#define RANDOMIZER_SAVEWARP_STRING_CALL(target,string) ((void(*)(u32))(target))((string))
#endif
static u32 randomizer_savewarp_stage_info(void) {
    const u32 stage=wwhd_dComIfG_getGameInfo()+0x5150;
    return RANDOMIZER_SAVEWARP_VCALL(stage,0x15C);
}
static int randomizer_savewarp_equal(u32 left,u32 right,int direct) {
    u32 a[2]={left,(u32)&WWHD_GAME_DATA(0x1004B8C0,u8)};
    u32 b[2]={right,(u32)&WWHD_GAME_DATA(0x1004B8C0,u8)};
    if(direct)randomizer_savewarp_assure((u32)a);
    else RANDOMIZER_SAVEWARP_STRING_CALL(*(volatile u32*)(a[1]+0x14),(u32)a);
    RANDOMIZER_SAVEWARP_STRING_CALL(*(volatile u32*)(a[1]+0x14),(u32)a);
    const u32 third=*(volatile u32*)(b[1]+0x14),captured=a[0];
    RANDOMIZER_SAVEWARP_STRING_CALL(third,(u32)b);
    u32 second=b[0];
    if(captured==second)return 1;
    u32 first=a[0];
    for(u32 count=0x40001;count;count--) {
        const u8 c1=*(volatile u8*)first,c2=*(volatile u8*)second;
        if(c1!=c2)return 0;if(!c1)return 1;first++;second++;
    }
    return 0;
}
static void randomizer_savewarp_copy(u8* to,u32 from,u32 count) {
    u32 n=0;for(;n<count;n++){const u8 c=*(volatile u8*)(from+n);to[n]=c;if(!c)break;}
    for(;n<count;n++)to[n]=0;
}
WWHD_REPLACE(0x025217F8,void,randomizer_savewarp_stage,(void)) {
    if(!init_entrances_shuffled){randomizer_native_savewarp_stage();return;}
    u8 name[8];u32 entry=(u32)&WWHD_GAME_DATA(0x101D5E18,u8);
    for(u32 i=0;i<5;i++,entry+=14) {
        if(*(volatile u8*)entry==1 && wwhd_dSv_event_c_isEventBit(
            WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,*(volatile u16*)(entry+2)))break;
    }
    if(!randomizer_savewarp_equal(entry+4,(u32)&WWHD_GAME_DATA(0x1004BA6C,u8),1)) {
        randomizer_savewarp_copy(name,entry+4,8);
        const s32 room=(s8)*(volatile u8*)(entry+12);
        const u32 point=*(volatile u8*)(entry+13);
        randomizer_savewarp_set(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x50,(u32)name,room,point);return;
    }
    const u32 type=(*(volatile u32*)(randomizer_savewarp_stage_info()+12)>>16)&7;
    const u32 stage_no=(*(volatile u8*)(randomizer_savewarp_stage_info()+9)>>1)&127;
    const u32 sea=(u32)&WWHD_GAME_DATA(0x1004BA60,u8);
    if(randomizer_savewarp_equal((u32)&WWHD_GAME_DATA(0x1004BA64,u8),wwhd_dComIfG_getGameInfo()+0x5134,0)) {
        randomizer_savewarp_copy(name,sea,8);
        const s32 room=(s8)wwhd_dSv_event_c_getEventReg(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0xC3FF);
        const u32 point=wwhd_dSv_event_c_getEventReg(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x85FF);
        randomizer_savewarp_time(120.0f);
        randomizer_savewarp_set(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x50,(u32)name,room,point);return;
    }
    if(type==7) {
        const u32 player=*(volatile u32*)(wwhd_dComIfG_getGameInfo()+0x5B34);
        const s32 room=(s8)*(volatile u8*)(player+0x326);
        const u32 point=*(volatile u8*)(player+0x69E7);
        if(room>=0 && point!=255 && randomizer_savewarp_landing((u32)room)) {
            randomizer_savewarp_copy(name,wwhd_dComIfG_getGameInfo()+0x5134,7);name[7]=0;
            randomizer_savewarp_set(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x50,(u32)name,room,point);return;
        }
        const u32 ship=*(volatile u32*)(wwhd_dComIfG_getGameInfo()+0x5B3C);
        const u32 scene=randomizer_savewarp_mesh(ship?ship+0x314:player+0x314);
        randomizer_savewarp_copy(name,scene,8);
        const s32 scene_room=(s8)*(volatile u8*)(scene+9);const u32 scene_point=*(volatile u8*)(scene+8);
        randomizer_savewarp_set(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x50,(u32)name,scene_room,scene_point);return;
    }
    if(type==1 || type==3 || type==6 || type==8 || stage_no==9) {
        const u32 scene=randomizer_savewarp_scene(0);randomizer_savewarp_copy(name,scene,8);
        const s32 room=(s8)*(volatile u8*)(scene+9);const u32 point=*(volatile u8*)(scene+8);
        randomizer_savewarp_set(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x50,(u32)name,room,point);return;
    }
    if(stage_no==10) {
        u32 position[3];const u32 game=wwhd_dComIfG_getGameInfo();
        position[0]=*(volatile u32*)(game+0x5D18);position[1]=*(volatile u32*)(game+0x5D1C);position[2]=*(volatile u32*)(game+0x5D20);
        const u32 scene=randomizer_savewarp_mesh((u32)position);randomizer_savewarp_copy(name,scene,8);
        const s32 room=(s8)*(volatile u8*)(scene+9);const u32 point=*(volatile u8*)(scene+8);
        randomizer_savewarp_set(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x50,(u32)name,room,point);return;
    }
    if(stage_no-11<=2) {
        randomizer_savewarp_copy(name,sea,8);const u32 stage=wwhd_dComIfG_getGameInfo()+0x5150;
        const u32 map=RANDOMIZER_SAVEWARP_VCALL(stage,0x74);
        if(!map)randomizer_savewarp_assert((u32)&WWHD_GAME_DATA(0x1004BA70,u8),0xA78,(u32)&WWHD_GAME_DATA(0x1004BA84,u8));
        const u32 z=randomizer_savewarp_ocean_z(map);const u32 term=z*7+21;
        const u32 x=randomizer_savewarp_ocean_x(map);const s32 native_room=(s8)(term+x+3+1);
        (void)native_room; /* Coordinates/callbacks remain native although the room argument changes. */
        const u32 place=WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x50;
        const u32 graphics=WWHD_GAME_DATA(0x101F8344,u32),ocean=*(volatile u32*)(graphics+0x218);
        const s32 room=(s32)*(volatile u8*)(ocean+0x3E)+1;
        randomizer_savewarp_set(place,(u32)name,room,0);return;
    }
    randomizer_savewarp_copy(name,sea,8);
    randomizer_savewarp_set(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x50,(u32)name,11,0);
}
