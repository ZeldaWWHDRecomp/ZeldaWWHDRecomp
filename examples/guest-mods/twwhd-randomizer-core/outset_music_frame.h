/* Complete public CC0 a874e1a9 JAIZelBasic_6.cpp zeldaGFrameWork.
 * MIT randomizer misc_rando_features.asm replaces only the queued-Outset
 * predicate; all native timers, ambience and BGM dispatch remain source-owned. */
#pragma once
enum {
    FRAME_dIsleRoom_WindfallIsland_e = 11,
    FRAME_dIsleRoom_DragonRoostIsland_e = 13,
    FRAME_dIsleRoom_ForestHaven_e = 41,
    FRAME_dIsleRoom_OutsetIsland_e = 44,
    FRAME_JA_SE_ATM_WIND_1 = 0x105A,
    FRAME_JA_SE_ATM_WIND_I_LINK = 0x1060,
    FRAME_JA_SE_ATM_WIND_MJT = 0x1064,
    FRAME_JA_SE_ATM_WIND_KINDAN = 0x1065,
    FRAME_JA_SE_ATM_BWD_WIND = 0x1075,
    FRAME_JA_SE_ATM_JB_WAVE = 0x1087,
    FRAME_JA_SE_ATM_WIND_R_DUN_MAE = 0x10A5,
    FRAME_JA_SE_ATM_D24_WIND = 0x10A6,
    FRAME_JA_SE_ATM_PRT_SHIP_INNER = 0x7019,
    FRAME_JA_SE_ATM_PRT_SHIP_WAVE = 0x701A,
    FRAME_JA_SE_ATM_FILE_SEL_SEA = 0x703B,
    FRAME_JA_SE_ATM_VS_GANON_WFALL = 0x706C,
    FRAME_JA_BGM_ITEM_GET = 0x80000002,
    FRAME_JA_BGM_D_RYU_MT = 0x80000006,
    FRAME_JA_BGM_I_MAJU = 0x80000007,
    FRAME_JA_BGM_DIE_LINK = 0x8000000A,
    FRAME_JA_BGM_I_MAJU_JAIL = 0x8000000F,
    FRAME_JA_BGM_BK_FLY_DOWN = 0x80000013,
    FRAME_JA_BGM_I_LINK_FOREST = 0x80000017,
    FRAME_JA_BGM_MBOSS_S = 0x8000001A,
    FRAME_JA_BGM_SELECT = 0x8000001E,
    FRAME_JA_BGM_GET_HEART = 0x80000024,
    FRAME_JA_BGM_ITEM_GET_S = 0x80000025,
    FRAME_JA_BGM_GET_SONG = 0x80000027,
    FRAME_JA_BGM_SEA = 0x8000002E,
    FRAME_JA_BGM_TAKT_CHUYA = 0x80000030,
    FRAME_JA_BGM_TAKT_SHIPPU = 0x80000032,
    FRAME_JA_BGM_SEA_STORM = 0x8000003C,
    FRAME_JA_BGM_I_WOOD_INNER = 0x8000003F,
    FRAME_JA_BGM_BAACHAN = 0x80000044,
    FRAME_JA_BGM_DEKU_PINCH = 0x80000045,
    FRAME_JA_BGM_DIOCTA_2 = 0x80000046,
    FRAME_JA_BGM_P_GANON_2 = 0x80000047,
    FRAME_JA_BGM_GET_PEARL = 0x8000004F,
    FRAME_JA_BGM_ISLAND_LINK_3 = 0x80000055,
    FRAME_JA_BGM_BGN_GET_BOX = 0x8000005D,
    FRAME_JA_BGM_TRIFORCE_DEMO = 0x80000061,
};
WWHD_GAME_FUNC(0x020212EC,void,frame_processLevObjSE,(u32 self));
WWHD_GAME_FUNC(0x02021534,void,frame_cbPracticeProcess,(u32 self));
WWHD_GAME_FUNC(0x02021DC4,void,frame_processDemoFanfareMute,(u32 self));
WWHD_GAME_FUNC(0x02021E30,s32,frame_checkBgmPlaying,(u32 self));
WWHD_GAME_FUNC(0x02021F28,void,frame_bgmStop,(u32 self,u32, u32));
WWHD_GAME_FUNC(0x0202204C,void,frame_bgmStart,(u32 self,u32, u32, u32));
WWHD_GAME_FUNC(0x0202148C,s32,frame_checkSubBgmPlaying,(u32 self));
WWHD_GAME_FUNC(0x0201DE00,void,frame_subBgmStart,(u32 self,u32));
WWHD_GAME_FUNC(0x0201D9C8,void,frame_subBgmStopInner,(u32 self));
WWHD_GAME_FUNC(0x0201E5CC,s32,frame_isDemo,(u32 self));
WWHD_GAME_FUNC(0x020217A4,s32,frame_checkSeqIDDemoPlaying,(u32 self,u32));
WWHD_GAME_FUNC(0x020233C8,s32,frame_checkPlayingStreamBgmFlag,(u32 self,u32));
WWHD_GAME_FUNC(0x02023410,u32,frame_checkSeaBgmID,(u32 self));
WWHD_GAME_FUNC(0x020234A4,void,frame_sceneBgmStart,(u32 self));
WWHD_GAME_FUNC(0x02024334,void,frame_processHeartGaugeSound,(u32 self));
WWHD_GAME_FUNC(0x020239C8,void,frame_processTime,(u32 self));
WWHD_GAME_FUNC(0x02023A94,void,frame_changeSeaBgm,(u32 self));
WWHD_GAME_FUNC(0x02023E0C,void,frame_bgmBattleGFrame,(u32 self));
WWHD_GAME_FUNC(0x02024018,void,frame_mbossBgmMuteProcess,(u32 self));
WWHD_GAME_FUNC(0x02024190,void,frame_enemyNearByGFrame,(u32 self));
WWHD_GAME_FUNC(0x02024278,void,frame_stSkyCloistersProcess,(u32 self));
WWHD_GAME_FUNC(0x0201DB08,s32,frame_checkStreamPlaying,(u32 self,u32));
WWHD_GAME_FUNC(0x02802848,void,frame_startSoundVec,(u32 self,u32, u32, u32, u32, u32, u32));
WWHD_GAME_FUNC(0x0201EBA0,u32,frame_seStart,(u32 self,u32, u32, u32, s32, f32, f32, f32, f32, u32));
WWHD_GAME_FUNC(0x0280B68C,void,frame_volume,(u32 sound,f32 volume,u32 fade,u32 track));
WWHD_GAME_FUNC(0x028F4384,f64,frame_sqrt,(f64 value));
static s32 frame_event_running(void){return R_GRANDMA(u8,wwhd_dComIfG_getGameInfo()+0x5292)!=0;}
WWHD_GAME_FUNC(0x02023474,s32,frame_daytime,(u32 self));
static u8 frame_decrement(u8 value){return value?value-1:0;}
static u32 frame_camera(u32 self){return R_GRANDMA(u32,R_GRANDMA(u32,self));}
static f32 frame_mul(f32 a,f32 b){
#if defined(__powerpc__)
 f32 r;__asm__("fmuls %0,%1,%2":"=f"(r):"f"(a),"f"(b));return r;
#else
 return (f32)((f64)a*(f64)b);
#endif
}
static f32 frame_madd(f32 a,f32 b,f32 c){
#if defined(__powerpc__)
 f32 r;__asm__("fmadds %0,%1,%2,%3":"=f"(r):"f"(a),"f"(b),"f"(c));return r;
#else
 return __builtin_fmaf(a,b,c);
#endif
}
static f32 frame_main_volume(u32 self){
 f32 v=frame_mul(R_GRANDMA(f32,self+0x90),R_GRANDMA(f32,self+0x94));
 const u32 offsets[]={0x98,0x9C,0xA0,0xA4,0xA8,0xAC,0xBC};
 for(u32 i=0;i<7;i++)v=frame_mul(v,R_GRANDMA(f32,self+offsets[i]));
 return v;
}
static void frame_scene_se(u32 self,u32 id,f32 volume){frame_seStart(self,id,0,0,0,1.0f,volume,-1.0f,-1.0f,0);}
WWHD_REPLACE(0x0202389C,void,randomizer_start_island_bgm,(u32 self)){
 if(!frame_daytime(self))return;
 const u32 room=R_GRANDMA(u8,self+0x2A8);
 if(room==44&&grandma_outset_bgm()==1){frame_bgmStart(self,0x80000055u,0,1);return;}
 frame_bgmStart(self,grandma_expand_bgm(self,R_GRANDMA(u16,GRANDMA_ISLAND_TABLE+room*4u)),0,1);
}
WWHD_REPLACE(0x02024478,void,randomizer_music_frame,(u32 self)) {
    /* frame counter, timers */
    if (R_GRANDMA(u8,self+0x29D) != 0) {
        u32 c = R_GRANDMA(u32,self+0x38);
        R_GRANDMA(u32,self+0x38) = c < 0xFFFFFFFF ? c + 1 : 0xFFFFFFFF;
    } else {
        R_GRANDMA(u32,self+0x38) = 0;
    }
    frame_processLevObjSE(self);
    frame_cbPracticeProcess(self);
    u8 timerNext = frame_decrement(R_GRANDMA(u8,self+0x26A));
    R_GRANDMA(u32,self+0x1FA4) = 0;
    R_GRANDMA(u8,self+0x269) = 0;
    R_GRANDMA(u8,self+0x26C) = 0;
    R_GRANDMA(u8,self+0x26B) = 0;
    R_GRANDMA(u8,self+0x26A) = timerNext;
    R_GRANDMA(u8,self+0x272) = frame_decrement(R_GRANDMA(u8,self+0x272));
    R_GRANDMA(u8,self+0x273) = frame_decrement(R_GRANDMA(u8,self+0x273));
    R_GRANDMA(u8,self+0x277) = frame_decrement(R_GRANDMA(u8,self+0x277));
    R_GRANDMA(u8,self+0x26D) = frame_decrement(R_GRANDMA(u8,self+0x26D));
    R_GRANDMA(u8,self+0x26E) = frame_decrement(R_GRANDMA(u8,self+0x26E));
    R_GRANDMA(u8,self+0x26F) = frame_decrement(R_GRANDMA(u8,self+0x26F));
    R_GRANDMA(u8,self+0x270) = frame_decrement(R_GRANDMA(u8,self+0x270));
    R_GRANDMA(u32,self+0x1F08) = 0;
    R_GRANDMA(u8,self+0x20AD) = 0;
    R_GRANDMA(u8,self+0x20AC) = 0;
    if (frame_event_running())
        R_GRANDMA(u8,self+0x71) = 5;
    else
        R_GRANDMA(u8,self+0x71) = frame_decrement(R_GRANDMA(u8,self+0x71));
    R_GRANDMA(u8,self+0xDE) = frame_decrement(R_GRANDMA(u8,self+0xDE));
    frame_processDemoFanfareMute(self);
    if (R_GRANDMA(u32,self+0x88) == FRAME_JA_BGM_I_MAJU_JAIL && frame_checkBgmPlaying(self) == 0) {
        frame_bgmStop(self,0, 0);
        frame_bgmStart(self,FRAME_JA_BGM_I_MAJU, 0, 0);
    }
    /* restore the sub bgm after it was interrupted */
    u32 subBgm = R_GRANDMA(u32,self+0x84);
    int restore;
    if (subBgm >= FRAME_JA_BGM_TAKT_CHUYA)
        restore = subBgm == FRAME_JA_BGM_TRIFORCE_DEMO || subBgm == FRAME_JA_BGM_BGN_GET_BOX ||
                  subBgm == FRAME_JA_BGM_GET_PEARL || subBgm <= FRAME_JA_BGM_TAKT_SHIPPU;
    else
        restore = subBgm == FRAME_JA_BGM_GET_SONG ||
                  (subBgm <= FRAME_JA_BGM_ITEM_GET_S &&
                   (subBgm >= FRAME_JA_BGM_GET_HEART || subBgm == FRAME_JA_BGM_ITEM_GET));
    if (restore && frame_checkSubBgmPlaying(self) == 0) {
        f32 b4 = R_GRANDMA(f32,self+0xB4);
        R_GRANDMA(u8,self+0x276) = 0;
        if (b4 != 0.0f && R_GRANDMA(u8,self+0xCB) != 0) {
            u32 c6 = R_GRANDMA(u8,self+0xC6);
            if (c6 == 1)
                frame_subBgmStart(self,FRAME_JA_BGM_MBOSS_S);
            else if (c6 == 2)
                frame_subBgmStart(self,FRAME_JA_BGM_DIOCTA_2);
            else if (c6 == 3)
                frame_subBgmStart(self,FRAME_JA_BGM_P_GANON_2);
            else {
                R_GRANDMA(s32,self+0xC0) = -2;
                frame_subBgmStopInner(self);
            }
            R_GRANDMA(u8,self+0xCB) = 0;
        }
    }
    u32 mainBgm = R_GRANDMA(u32,self+0x88);
    if (mainBgm == FRAME_JA_BGM_DEKU_PINCH) {
        if (frame_checkBgmPlaying(self) == 0)
            frame_bgmStart(self,FRAME_JA_BGM_I_WOOD_INNER, 0, 1);
    } else if (mainBgm == FRAME_JA_BGM_BK_FLY_DOWN) {
        if (frame_checkBgmPlaying(self) == 0)
            frame_bgmStart(self,FRAME_JA_BGM_I_LINK_FOREST, 0, 1);
    }
    // Sub-BGM fade-out countdown.
    s32 c0 = R_GRANDMA(s32,self+0xC0);
    if (c0 != -1) {
        if (c0 > 0) {
            R_GRANDMA(s32,self+0xC0) = c0 - 1;
        } else {
            frame_subBgmStopInner(self);
            R_GRANDMA(s32,self+0xC0) = -1;
        }
    }
    if (R_GRANDMA(u32,self+0x88) == FRAME_JA_BGM_D_RYU_MT) {
        if (R_GRANDMA(u8,self+0x75) == 1) {
            const u32 m = R_GRANDMA(u32,self+0x78);
            R_GRANDMA(f32,self+0xA0) = 0.0f;
            if (m != 0) {
                f32 v = frame_mul(R_GRANDMA(f32,self+0x90), R_GRANDMA(f32,self+0x94));
                v = frame_mul(v, R_GRANDMA(f32,self+0x98));
                v = frame_mul(v, R_GRANDMA(f32,self+0x9C));
                v = frame_mul(v, 0.0f);
                v = frame_mul(v, R_GRANDMA(f32,self+0xA4));
                v = frame_mul(v, R_GRANDMA(f32,self+0xA8));
                v = frame_mul(v, R_GRANDMA(f32,self+0xAC));
                v = frame_mul(v, R_GRANDMA(f32,self+0xBC));
                frame_volume(m,v,0x1E,0);
            }
            R_GRANDMA(u8,self+0x75) = 0;
        } else if (R_GRANDMA(f32,self+0xA0) == 0.0f) {
            const u32 m = R_GRANDMA(u32,self+0x78);
            R_GRANDMA(f32,self+0xA0) = 1.0f;
            if (m != 0) {
                f32 v = frame_mul(R_GRANDMA(f32,self+0x90), R_GRANDMA(f32,self+0x94));
                v = frame_mul(v, R_GRANDMA(f32,self+0x98));
                v = frame_mul(v, R_GRANDMA(f32,self+0x9C));
                v = frame_mul(v, R_GRANDMA(f32,self+0xA4));
                v = frame_mul(v, R_GRANDMA(f32,self+0xA8));
                v = frame_mul(v, R_GRANDMA(f32,self+0xAC));
                v = frame_mul(v, R_GRANDMA(f32,self+0xBC));
                frame_volume(m,v,0x1E,0);
            }
        }
    }
    if (frame_isDemo(self) == 1 && frame_checkSeqIDDemoPlaying(self,FRAME_JA_BGM_BAACHAN) != 0)
        frame_bgmStart(self,FRAME_JA_BGM_BAACHAN, 0, 0);

    /* scene ambience and the volume of the main bgm by the camera position */
    s32 scene = R_GRANDMA(s32,self+0x294);
    int skipSceneVolume = 0;
    if (scene == 0x21) {
        skipSceneVolume = 1;
        if (R_GRANDMA(u8,self+0x29D) == 1) {
            u32 cp = frame_camera(self);
            if (cp != 0) {
                f32 y = R_GRANDMA(f32,cp+4);
                if (!(y < 2400.0f)) {
                    f32 v;
                    int go = 1;
                    if (y < 3200.0f) {
                        v = (y - 2400.0f) / 800.0f;
                        if (v == 0.0f)
                            go = 0;
                    } else {
                        v = 1.0f;
                    }
                    if (go) {
                        frame_startSoundVec(self,FRAME_JA_SE_ATM_WIND_I_LINK, (self+0x1FA8), 0, 0, 0, 4);
                        if (R_GRANDMA(u32,self+0x1FA8) != 0)
                            frame_volume(R_GRANDMA(u32,self+0x1FA8),v,0,0);
                        scene = R_GRANDMA(s32,self+0x294);
                        skipSceneVolume = 0;
                    }
                }
            }
        }
    }
    if (!skipSceneVolume) {
        if (scene == 0x12 || scene == 0x55 || scene == 0x13) {
            f32 y = 0.0f;
            f32 windVolume = 0.0f;
            u32 room;
            int skipIslandVolume = 0;
            int roomLoaded = 0;
            if (R_GRANDMA(u8,self+0x29D) == 1) {
                u32 cp = frame_camera(self);
                if (cp != 0)
                    y = R_GRANDMA(f32,cp+4);
                if (cp != 0 && !(y < 2400.0f)) {
                    if (!(y < 3200.0f)) {
                        room = R_GRANDMA(u8,self+0x2A8);
                        windVolume = 1.0f;
                    } else {
                        room = R_GRANDMA(u8,self+0x2A8);
                        windVolume = (y - 2400.0f) / 800.0f;
                    }
                } else {
                    room = R_GRANDMA(u8,self+0x2A8);
                    windVolume = 0.0f;
                }
                if (room == FRAME_dIsleRoom_WindfallIsland_e && frame_isDemo(self) == 1)
                    skipIslandVolume = 1;
                if (!skipIslandVolume) {
                    if (windVolume == 0.0f) {
                        roomLoaded = 1;
                    } else if (frame_checkPlayingStreamBgmFlag(self,0xC0000036) == 0) {
                        frame_startSoundVec(self,FRAME_JA_SE_ATM_WIND_I_LINK, (self+0x1FA8), 0, 0, 0, 4);
                        if (R_GRANDMA(u32,self+0x1FA8) != 0)
                            frame_volume(R_GRANDMA(u32,self+0x1FA8),windVolume,0,0);
                    }
                }
            }
            if (!skipIslandVolume) {
                if (!roomLoaded)
                    room = R_GRANDMA(u8,self+0x2A8);
                int setVol = 0;
                if (room == FRAME_dIsleRoom_OutsetIsland_e) {
                    if (grandma_check_event(self,1) == 1) {
                        f32 v;
                        if (R_GRANDMA(u32,self+0x88) == FRAME_JA_BGM_DIE_LINK || y < 1800.0f)
                            v = 1.0f;
                        else if (!(y < 3000.0f))
                            v = 0.0f;
                        else
                            v = 1.0f - (y - 1800.0f) / 1200.0f;
                        const u32 m = R_GRANDMA(u32,self+0x78);
                        R_GRANDMA(f32,self+0xBC) = v;
                        setVol = m != 0;
                    }
                } else if (room == FRAME_dIsleRoom_DragonRoostIsland_e) {
                    f32 v;
                    if (R_GRANDMA(u32,self+0x88) == FRAME_JA_BGM_DIE_LINK || R_GRANDMA(u8,self+0x70) == 2 || y < 1800.0f)
                        v = 1.0f;
                    else if (!(y < 3000.0f))
                        v = R_GRANDMA(f32,(u32)&WWHD_GAME_DATA(0x10003910,u8)); /* 0.3 */
                    else
                        v = frame_madd(1.0f - (y - 1800.0f) / 1200.0f, 0.7f, 0.3f);
                    const u32 m = R_GRANDMA(u32,self+0x78);
                    R_GRANDMA(f32,self+0xBC) = v;
                    setVol = m != 0;
                } else if (room == FRAME_dIsleRoom_ForestHaven_e) {
                    if (R_GRANDMA(u8,self+0x70) != 3) {
                        if (!frame_event_running()) {
                            u32 cp = frame_camera(self);
                            f32 dz = R_GRANDMA(f32,cp+8) - 198000.0f;
                            f32 dx = R_GRANDMA(f32,cp+0) - 195000.0f;
                            f64 d = frame_sqrt(frame_madd(dx, dx, frame_mul(dz,dz)));
                            f32 v = 1.0f;
                            if (d > 8000.0f) {
                            } else if (d > 5000.0f) {
                                v = (f32)(d - 5000.0) / 3000.0f;
                            } else {
                                v = 0.0f;
                            }
                            u32 n = R_GRANDMA(u32,self+0x88);
                            u32 b275 = R_GRANDMA(u8,self+0x275);
                            if (n == FRAME_JA_BGM_DIE_LINK)
                                v = 1.0f;
                            if (b275 == 0)
                                R_GRANDMA(f32,self+0xA0) = v;
                            setVol = R_GRANDMA(u32,self+0x78) != 0;
                        }
                    }
                }
                if (setVol)
                    frame_volume(R_GRANDMA(u32,self+0x78),frame_main_volume(self),0,0);
                scene = R_GRANDMA(s32,self+0x294);
            }
        }
        if (scene == 0x19) {
            u32 cp = frame_camera(self);
            u32 n = R_GRANDMA(u32,self+0x88);
            f32 y = cp != 0 ? R_GRANDMA(f32,cp+4) : 0.0f;
            f32 v;
            if (n == FRAME_JA_BGM_DIE_LINK || y < 2500.0f)
                v = 1.0f;
            else if (y < 5000.0f)
                v = 1.0f - (y - 2500.0f) / 2500.0f;
            else
                v = 0.0f;
            const u32 m = R_GRANDMA(u32,self+0x78);
            R_GRANDMA(f32,self+0xBC) = v;
            if (m != 0) {
                f32 t = frame_mul(R_GRANDMA(f32,self+0x90), R_GRANDMA(f32,self+0x94));
                t = frame_mul(t, R_GRANDMA(f32,self+0x98));
                t = frame_mul(t, R_GRANDMA(f32,self+0x9C));
                t = frame_mul(t, R_GRANDMA(f32,self+0xA0));
                t = frame_mul(t, R_GRANDMA(f32,self+0xA4));
                t = frame_mul(t, R_GRANDMA(f32,self+0xA8));
                t = frame_mul(t, R_GRANDMA(f32,self+0xAC));
                t = frame_mul(t, v);
                frame_volume(m,t, 0,0);
            }
            scene = R_GRANDMA(s32,self+0x294);
        }
        if (scene == 0x35) {
            u32 cp = frame_camera(self);
            f32 z = R_GRANDMA(f32,cp+8),x = R_GRANDMA(f32,cp+0);
            f64 d = frame_sqrt(frame_madd(x, x, frame_mul(z,z)));
            f32 v = 1.0f;
            if (d > 8500.0f) {
            } else if (d > 6500.0f) {
                v = (f32)(d - 6500.0) / 2000.0f;
            } else {
                v = 0.0f;
            }
            u32 n = R_GRANDMA(u32,self+0x88);
            const u32 m = R_GRANDMA(u32,self+0x78);
            if (n == FRAME_JA_BGM_DIE_LINK)
                v = 1.0f;
            R_GRANDMA(f32,self+0xA0) = v;
            if (m != 0) {
                f32 t = frame_mul(R_GRANDMA(f32,self+0x90), R_GRANDMA(f32,self+0x94));
                t = frame_mul(t, R_GRANDMA(f32,self+0x98));
                t = frame_mul(t, R_GRANDMA(f32,self+0x9C));
                t = frame_mul(t, v);
                t = frame_mul(t, R_GRANDMA(f32,self+0xA4));
                t = frame_mul(t, R_GRANDMA(f32,self+0xA8));
                t = frame_mul(t, R_GRANDMA(f32,self+0xAC));
                t = frame_mul(t, R_GRANDMA(f32,self+0xBC));
                frame_volume(m,t, 0,0);
            }
        }
    }
    if (R_GRANDMA(u32,self+0x88) == FRAME_JA_BGM_SELECT) {
        u32 s = R_GRANDMA(s32,self+0x294);
        if (s < 0x4E || (s > 0x52 && s != 0x5D))
            frame_scene_se(self,FRAME_JA_SE_ATM_FILE_SEL_SEA,1.0f);
    }
    // Start queued BGM once nothing plays.
    u32 cc = R_GRANDMA(u8,self+0xCC);
    if (cc != 0 && frame_checkBgmPlaying(self) == 0) {
        if (cc == 1) {
            if (R_GRANDMA(u32,self+0x78) == 0 ||
                (R_GRANDMA(u32,self+0x88) != FRAME_JA_BGM_SEA && R_GRANDMA(u32,self+0x88) != FRAME_JA_BGM_SEA_STORM))
                frame_bgmStart(self,frame_checkSeaBgmID(self), 0x5A, 0);
            R_GRANDMA(u8,self+0x28E) = 0;
            R_GRANDMA(u8,self+0xCC) = 0;
        } else if (cc == 2) {
            frame_sceneBgmStart(self);
            R_GRANDMA(u8,self+0x28E) = 0;
            R_GRANDMA(u8,self+0xCC) = 0;
        } else {
            if (cc == 3) {
                u32 room = R_GRANDMA(u8,self+0x2A8);
                if (room == FRAME_dIsleRoom_OutsetIsland_e && grandma_outset_bgm() == 1)
                    frame_bgmStart(self,FRAME_JA_BGM_ISLAND_LINK_3, 0, 0);
                else
                    frame_bgmStart(self,grandma_expand_bgm(self,R_GRANDMA(u16,GRANDMA_ISLAND_TABLE+room*4u)), 0, 0);
            }
            R_GRANDMA(u8,self+0xCC) = 0;
        }
    }
    frame_processTime(self);
    frame_changeSeaBgm(self);
    frame_bgmBattleGFrame(self);
    frame_mbossBgmMuteProcess(self);
    frame_enemyNearByGFrame(self);
    frame_stSkyCloistersProcess(self);
    /* scene ambience loops */
    if (R_GRANDMA(u8,self+0x29D) == 1) {
        u32 s = R_GRANDMA(s32,self+0x294);
        switch (s) {
        case 7:
            frame_scene_se(self,FRAME_JA_SE_ATM_WIND_KINDAN, 1.0f);
            break;
        case 0xC:
        case 0x54:
        case 0x58:
            frame_scene_se(self,FRAME_JA_SE_ATM_PRT_SHIP_INNER, 1.0f);
            break;
        case 0x11:
            frame_scene_se(self,FRAME_JA_SE_ATM_PRT_SHIP_WAVE, 1.0f);
            break;
        case 0x18:
            frame_scene_se(self,FRAME_JA_SE_ATM_BWD_WIND, 1.0f);
            break;
        case 0x21:
            frame_scene_se(self,FRAME_JA_SE_ATM_WIND_R_DUN_MAE, 1.0f);
            break;
        case 0x23:
            if (frame_checkSubBgmPlaying(self) == 1)
                frame_scene_se(self,FRAME_JA_SE_ATM_WIND_1, 0.3f);
            else
                frame_scene_se(self,FRAME_JA_SE_ATM_WIND_1, R_GRANDMA(f32,(u32)&WWHD_GAME_DATA(0x100039BC,u8))); /* 0.9 */
            break;
        case 0x29:
            frame_scene_se(self,FRAME_JA_SE_ATM_JB_WAVE, 1.0f);
            break;
        case 0x5A:
            frame_scene_se(self,FRAME_JA_SE_ATM_D24_WIND, 1.0f);
            break;
        case 0x5C: {
            u32 r = R_GRANDMA(u8,self+0x2A9);
            if (r != 8 && r != 9 && r != 0xA && r != 0xB)
                frame_scene_se(self,FRAME_JA_SE_ATM_VS_GANON_WFALL, 1.0f);
            break;
        }
        case 9:
        case 0x40: {
            if (frame_checkStreamPlaying(self,0xC0000004) != 0)
                break;
            u32 cp = frame_camera(self);
            f32 y = cp != 0 ? R_GRANDMA(f32,cp+4) : 0.0f;
            if (y < 9000.0f)
                break;
            f32 v;
            if (y < 11000.0f) {
                v = (y - 9000.0f) / 2000.0f;
                if (v == 0.0f)
                    break;
            } else {
                v = 1.0f;
            }
            frame_startSoundVec(self,FRAME_JA_SE_ATM_WIND_MJT, (self+0x1FA8), 0, 0, 0, 4);
            if (R_GRANDMA(u32,self+0x1FA8) != 0)
                frame_volume(R_GRANDMA(u32,self+0x1FA8),v,0,0);
            break;
        }
        }
    }
    frame_processHeartGaugeSound(self);
    if (R_GRANDMA(u32,self+0x20D0) == 0)
        R_GRANDMA(u16,self+0x20D4) = 0xFFFF; // HD: only the first halfword is reset.
}
