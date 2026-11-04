/* Full public CC0 a874e1a9 d_s_play_7.cpp dScnPly_Delete (025B0630).
 * MIT TWWHD Randomizer 1.2.0 fix_vanilla_bugs.asm inserts sub-BGM stop
 * immediately before the final play getter/initialization, after resource removal. */
#pragma once
#ifndef R_UNLOAD
#define R_UNLOAD(t,a) (*(volatile t*)(a))
#endif
#ifndef UNLOAD_DATA
#define UNLOAD_DATA(a) ((u32)&WWHD_GAME_DATA(a,u8))
#endif
WWHD_GAME_FUNC(0x020389F0,u32,unload_heap,(u32 x));
WWHD_GAME_FUNC(0x020386CC,void,unload_release_heap,(u32 x));
WWHD_GAME_FUNC(0x0203899C,void,unload_release_resources,(u32 x));
WWHD_GAME_FUNC(0x024EDB20,void,unload_attention,(u32 x,u32 mode));
WWHD_GAME_FUNC(0x02518794,u32,unload_collision,(u32 x));
WWHD_GAME_FUNC(0x0251FD14,void,unload_initialize,(u32 x));
WWHD_GAME_FUNC(0x025204C8,void,unload_resource,(u32 phase,u32 name));
WWHD_GAME_FUNC(0x02522C60,void,unload_recollection,(void));
WWHD_GAME_FUNC(0x02524C50,void,unload_magma,(u32 x));
WWHD_GAME_FUNC(0x02524D3C,void,unload_grass,(u32 x));
WWHD_GAME_FUNC(0x02524E3C,void,unload_tree,(u32 x));
WWHD_GAME_FUNC(0x02524F3C,void,unload_wood,(u32 x));
WWHD_GAME_FUNC(0x0252503C,void,unload_flower,(u32 x));
WWHD_GAME_FUNC(0x02528B48,u32,unload_demo,(u32 x));
WWHD_GAME_FUNC(0x0253EA70,void,unload_event,(u32 x));
WWHD_GAME_FUNC(0x02591588,void,unload_environment,(u32 x));
WWHD_GAME_FUNC(0x025A809C,u32,unload_particle,(u32 x));
WWHD_GAME_FUNC(0x025B05E4,void,unload_ocean,(void));
WWHD_GAME_FUNC(0x025BEF30,u32,unload_snapshot,(u32 x));
WWHD_GAME_FUNC(0x025C3370,void,unload_stage,(u32 x));
WWHD_GAME_FUNC(0x025CC110,void,unload_vibration,(u32 x,u32 mode));
WWHD_GAME_FUNC(0x025DB8F4,void,unload_message_heap,(u32 x));
WWHD_GAME_FUNC(0x025DD630,void,unload_base,(u32 x,u32 mode));
WWHD_GAME_FUNC(0x025E1928,void,unload_stop_bgm,(void));
WWHD_GAME_FUNC(0x025F0A18,void,unload_hio,(s32 x));
WWHD_GAME_FUNC(0x026FBC4C,void,unload_ending_resources,(u32 x));
WWHD_GAME_FUNC(0x02707FA0,u32,unload_finalize_sea,(u32 x));
WWHD_GAME_FUNC(0x02708270,void,unload_destroy_sea,(u32 x));
WWHD_GAME_FUNC(0x02715C10,u32,unload_finalize_game,(u32 x));
WWHD_GAME_FUNC(0x02717B70,void,unload_destroy_game,(u32 x));
WWHD_GAME_FUNC(0x0271DCC8,u32,unload_finalize_ending,(u32 x));
WWHD_GAME_FUNC(0x0271E6B8,void,unload_destroy_ending,(u32 x));
WWHD_GAME_FUNC(0x0271F090,u32,unload_finalize_title,(u32 x));
WWHD_GAME_FUNC(0x0271F3D8,void,unload_destroy_title,(u32 x));
WWHD_GAME_FUNC(0x0276B21C,void,unload_scene,(u32 x,u32 mode));
WWHD_GAME_FUNC(0x02792010,void,unload_wind,(u32 x,u32 mode));
#ifndef UNLOAD_ASSURE
#define UNLOAD_ASSURE(a) (((void(*)(u32))R_UNLOAD(u32,(a)[1]+0x14))((u32)(a)))
#endif
#ifndef UNLOAD_BACKGROUND_DESTROY
#define UNLOAD_BACKGROUND_DESTROY(info) (((void(*)(u32))R_UNLOAD(u32,R_UNLOAD(u32,(info)+0x26A0)+0x1C))((info)+0x12A0))
#endif
static int unload_stage_is(u32 literal) {
    u32 a[2]={literal,UNLOAD_DATA(0x10052EEC)};
    const u32 info=wwhd_dComIfG_getGameInfo();
    u32 b[2]={info+0x5134,UNLOAD_DATA(0x10052EEC)};
    UNLOAD_ASSURE(a);
    UNLOAD_ASSURE(a);
    u32 first=a[0];
    UNLOAD_ASSURE(b);
    u32 second=b[0];
    if(first==second)return 1;
    /* Native captures before stage assurance for pointer identity, but reloads
     * both strings for fallback (025B0828..025B0848). */
    first=a[0];second=b[0];
    for(u32 n=0;n<0x40001u;n++,first++,second++) {
        const u8 c=R_UNLOAD(u8,first);
        if(c!=R_UNLOAD(u8,second))return 0;
        if(!c)return 1;
    }
    return 0;
}
WWHD_REPLACE(0x025B0630,s32,randomizer_stage_delete,(u32 self)) {
    const u32 sys=R_UNLOAD(u32,UNLOAD_DATA(0x1018F47C));
    if(sys) {
        if(R_UNLOAD(u32,sys+0x18)==1)return 0;
        unload_release_resources(sys);
        const u32 q=R_UNLOAD(u32,UNLOAD_DATA(0x1018F47C));
        const u32 state=R_UNLOAD(u32,R_UNLOAD(u32,q+0x1C)+0x80);
        if(state==3||state==4)unload_release_heap(unload_heap(q));
    }
    unload_attention(wwhd_dComIfG_getGameInfo()+0x5804,2);
    unload_vibration(wwhd_dComIfG_getGameInfo()+0x599C,2);
    u32 info=wwhd_dComIfG_getGameInfo();
    UNLOAD_BACKGROUND_DESTROY(info);
    u32 r=unload_collision(wwhd_dComIfG_getGameInfo()+0x26A4);
    r=unload_snapshot(r);unload_stage(r);
    unload_event(wwhd_dComIfG_getGameInfo()+0x51D0);
    r=unload_particle(R_UNLOAD(u32,wwhd_dComIfG_getGameInfo()+0x5AB0));
    r=unload_demo(r);unload_environment(r);
    unload_message_heap(R_UNLOAD(u32,wwhd_dComIfG_getGameInfo()+0x5C24));
    if(unload_stage_is(UNLOAD_DATA(0x10053228))||unload_stage_is(UNLOAD_DATA(0x10053230))||
       unload_stage_is(UNLOAD_DATA(0x10053238))||unload_stage_is(UNLOAD_DATA(0x10053240)))unload_recollection();
    unload_magma(wwhd_dComIfG_getGameInfo()+0x12A0);
    unload_grass(wwhd_dComIfG_getGameInfo()+0x12A0);
    unload_tree(wwhd_dComIfG_getGameInfo()+0x12A0);
    unload_wood(wwhd_dComIfG_getGameInfo()+0x12A0);
    unload_flower(wwhd_dComIfG_getGameInfo()+0x12A0);
    info=wwhd_dComIfG_getGameInfo();
    R_UNLOAD(s32,info+0x5B4C)=0;R_UNLOAD(u8,info+0x5BB0)=0;
    const s32 dark=(s8)R_UNLOAD(u8,UNLOAD_DATA(0x1047B560));
    R_UNLOAD(u32,UNLOAD_DATA(0x1047C89C)+0x10)=0xFFFFFFFFu;
    R_UNLOAD(u8,UNLOAD_DATA(0x1047C89C)+5)=0;
    unload_hio(dark);unload_ocean();
    R_UNLOAD(u8,wwhd_dComIfG_getGameInfo()+0x5AC9)=0;
    const s32 preload=(s8)R_UNLOAD(u8,UNLOAD_DATA(0x101EACB4));
    if(preload>=0) {
        const u32 entry=UNLOAD_DATA(0x100530D8)+(u32)preload*12;
        const u32 names=R_UNLOAD(u32,entry+4);u32 num=R_UNLOAD(u8,entry+8);
        if(names&&R_UNLOAD(u32,names)&&num) {
            u32 p=names,off=0;
            do {unload_resource(UNLOAD_DATA(0x1047B448)+off,R_UNLOAD(u32,p));p+=4;off+=8;}while(--num);
        }
    }
    unload_stop_bgm();
    unload_initialize(wwhd_dComIfG_getGameInfo()+0x12A0);
    const u32 renderer=R_UNLOAD(u32,UNLOAD_DATA(0x101F95D0));
    R_UNLOAD(u32,UNLOAD_DATA(0x104B4708))=0;
    const u32 count=R_UNLOAD(u32,renderer+0x1020);
    u32 views=R_UNLOAD(u32,renderer+0x1024);if(count>1)views+=4;
    const u32 view=R_UNLOAD(u32,views);
    R_UNLOAD(u32,view+0x6BCC)=0;R_UNLOAD(u32,view+0x6BD0)=0;
    const u32 mode=R_UNLOAD(u32,self+0xB64);
    if(mode==0)unload_destroy_title(unload_finalize_title(R_UNLOAD(u32,UNLOAD_DATA(0x101F83FC))));
    else if(mode==1) {
        const u32 game=unload_finalize_game(R_UNLOAD(u32,UNLOAD_DATA(0x101F8344)));
        if(!R_UNLOAD(u8,UNLOAD_DATA(0x101F8350)))unload_destroy_game(game);
    } else if(mode==2) {
        unload_destroy_ending(unload_finalize_ending(R_UNLOAD(u32,UNLOAD_DATA(0x101F83D0))));
        unload_ending_resources(R_UNLOAD(u32,UNLOAD_DATA(0x101F7274)));
    } else if(mode==3)unload_destroy_sea(unload_finalize_sea(R_UNLOAD(u32,UNLOAD_DATA(0x101F8268))));
    unload_wind(self+0x5D4,2);unload_scene(self+0x1D4,2);unload_base(self,2);
    return 1;
}
