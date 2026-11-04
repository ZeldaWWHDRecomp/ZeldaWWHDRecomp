/* Complete public CC0 a874e1a9 d_a_player_main_07.cpp::procTactWait.
 * classic_features.asm removes only the falling-cancel conditional branch at
 * 0243A28C. Keep the condition's actual ground-field load and all callbacks.
 * Staged consumer: the complete classic-family decoder/owner selects it. */
#pragma once
#ifndef WWHD_RANDOMIZER_CLASSIC_MODE
#error "Regenerate prepared seed data: missing classic mode policy"
#elif WWHD_RANDOMIZER_CLASSIC_MODE != 0 && WWHD_RANDOMIZER_CLASSIC_MODE != 1
#error "Invalid classic mode policy"
#elif WWHD_RANDOMIZER_CLASSIC_MODE
#ifndef CTW_B
#define CTW_B(a,o) (*(volatile u8*)((a)+(o)))
#define CTW_H(a,o) (*(volatile u16*)((a)+(o)))
#define CTW_S(a,o) (*(volatile s16*)((a)+(o)))
#define CTW_W(a,o) (*(volatile unsigned int*)((a)+(o)))
#define CTW_I(a,o) (*(volatile s32*)((a)+(o)))
#define CTW_F(a,o) (*(volatile f32*)((a)+(o)))
#endif
#ifndef CTWPTR
#define CTWPTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
WWHD_GAME_FUNC(0x023D83D8,void,ctw_body_off,(u32 self));
WWHD_GAME_FUNC(0x023E2DC4,void,ctw_ship_position,(u32 self));
WWHD_GAME_FUNC(0x025E1F14,void,ctw_ambient,(void));
WWHD_GAME_FUNC(0x025200D4,u32,ctw_play,(void));
WWHD_GAME_FUNC(0x02543280,void,ctw_cut,(u32 manager,s32 staff));
WWHD_GAME_FUNC(0x02439D54,s32,ctw_play_init,(u32 self,s32 song,s32 warp,s32 no_message));
WWHD_GAME_FUNC(0x02439D24,s32,ctw_cancel_trigger,(u32 self));
WWHD_GAME_FUNC(0x023DC6A4,s32,ctw_reset_upper,(u32 self,s32 part,f32 morph));
WWHD_GAME_FUNC(0x025E1988,void,ctw_sound,(u32 sound));
WWHD_GAME_FUNC(0x024F8044,u32,ctw_camera,(void));
WWHD_GAME_FUNC(0x0253E860,void,ctw_camera_end,(u32 camera,u32 actor));
WWHD_GAME_FUNC(0x023F2048,void,ctw_end_demo,(u32 self));
WWHD_GAME_FUNC(0x025E1E94,void,ctw_reset,(void));
WWHD_GAME_FUNC(0x025E162C,s32,ctw_direction,(s32 hand,s32 previous));
WWHD_GAME_FUNC(0x026185A0,s32,ctw_cursor,(u32 pad));
WWHD_GAME_FUNC(0x025E1EF0,void,ctw_volume,(f32 volume));
WWHD_GAME_FUNC(0x02439C14,u16,ctw_right_animation,(u32 self,s32 direction));
WWHD_GAME_FUNC(0x02439C58,u16,ctw_left_animation,(u32 self,s32 direction));
WWHD_GAME_FUNC(0x023DE7E8,s32,ctw_set_upper,(u32 self,u32 animation,s32 part,f32 rate,f32 start,s32 end,f32 morph));
WWHD_GAME_FUNC(0x025E1EE0,void,ctw_set_beat,(s32 beat));
WWHD_GAME_FUNC(0x025E3F3C,void,ctw_old_frame,(u32 old,f32 morph,s32 first,s32 last));
WWHD_GAME_FUNC(0x025E1F48,void,ctw_arm_sound,(s32 direction));
WWHD_GAME_FUNC(0x025E1EFC,s32,ctw_beat_word,(void));
WWHD_GAME_FUNC(0x025E1F08,f64,ctw_beat_frames,(void));
WWHD_GAME_FUNC(0x025E1F20,void,ctw_metronome,(s32 beat,s32 direction));
WWHD_GAME_FUNC(0x027DF40C,void,ctw_update_functor,(u32 object));
WWHD_GAME_FUNC(0x025E1F34,s32,ctw_judge,(s32 beat,s32 direction));
WWHD_GAME_FUNC(0x025B7B10,s32,ctw_is_tact,(u32 item,u8 song));
static void ctw_cut_end(u32 self){const s32 staff=CTW_I(self,0x69DC);ctw_cut(ctw_play()+0x52C4,staff);}
static void ctw_bstatus(u8 status){CTW_B(ctw_play(),0x5BB6)=status;}
#ifndef CTW_COSINE_TABLE
#define CTW_COSINE_TABLE ((u32)&WWHD_GAME_DATA(0x104A44FC,u8))
#endif
static f32 ctw_stick_volume(u32 self){
    const u16 angle=CTW_H(self,0x6924);
    const f32 distance=CTW_F(self,0x6A08);
    const f32 cosine=CTW_F(CTW_COSINE_TABLE,((u32)angle>>3)*8);
    return distance*cosine;
}
static void ctw_clear_ratio(u32 ratio){
    s32 i=0;
    if(i<CTW_I(ratio,8))do{
        CTW_F(CTWPTR(ratio,12),4*(unsigned int)i)=0.0f;
        ++i;
    }while(i<CTW_I(ratio,8));
}
#ifndef CTW_FRAME_CALL
#define CTW_FRAME_CALL(fn,arg,frame,p2,p3) (((f32(*)(u32,f32,f32,f32))(fn))(arg,frame,p2,p3))
#endif
static void ctw_frame(u32 self,f32 frame){
    const u32 animation=CTWPTR(self,0x4908);
    CTW_F(self,0x48FC)=frame;CTW_F(animation,0)=frame;
    const u32 functor=CTWPTR(self,0x4918);
    const u32 target=CTWPTR(functor,0x10);
    const f32 p2=CTW_F(functor,4),p3=CTW_F(functor,8);
    const u32 argument=CTWPTR(functor,0x14);
    const f32 result=CTW_FRAME_CALL(target,argument,frame,p2,p3);
    CTW_F(functor,0)=result;
    ctw_update_functor(self+0x4918);
}
static unsigned int ctw_truncate(f32 value){
    if(value!=value||value<=-2147483648.0f)return 0x80000000u;
    if(value>=2147483648.0f)return 0x7FFFFFFFu;
    return (unsigned int)(s32)value;
}
static unsigned int ctw_divide(unsigned int a,unsigned int b){return b?a/b:0;}
WWHD_REPLACE(0x0243A094,s32,randomizer_classic_tact_wait,(u32 self)) {
    if (CTW_W(self,0x2E4) & 4) { /* HD */
        ctw_body_off(self);
    }
    ctw_ship_position(self);
    ctw_ambient();
    s32 p6 = CTW_I(self,0x69C0);
    if (p6 == -4) {
        ctw_cut_end(self);
        return 1;
    }
    s16 p1 = CTW_S(self,0x6918);
    if (p1 > 0) {
        p1 = (s16)(p1 - 1);
        CTW_S(self,0x6918) = p1;
        if (CTW_I(self,0x69C0) == -5) {
            f32 t = CTW_F(self,0x69F8) + 1.0f;
            p1 = CTW_S(self,0x6918);
            CTW_F(self,0x69F8) = t;
        }
        if (p1 != 0) {
            return 1;
        }
        p6 = CTW_I(self,0x69C0);
        if (p6 == -5 || p6 == 6 || p6 == 7) {
            ctw_cut_end(self);
            CTW_B(ctw_play(),0x5BD1) = 0; /* dComIfGp_setMetronomeOff() */
            return 1;
        }
        if (p6 == -1 || p6 == -3 || p6 >= 0) {
            ctw_play_init(self,CTW_I(self,0x69C4), p6 == -1, p6 >= 0);
        }
        return 1;
    }
    if (p1 < -1) { /* HD: a short count-up before the wait reacts */
        p1 = (s16)(p1 + 1);
        CTW_S(self,0x6918) = p1;
        p6 = CTW_I(self,0x69C0);
    }
    if ((u32)p6 == 1 || ((u32)p6 >= 5 && (u32)p6 <= 7)) {
        f32 t = CTW_F(self,0x6A04);
        if (!(t < 0.0f)) {
            CTW_F(self,0x6A04) = t - 1.0f;
            p6 = CTW_I(self,0x69C0);
        }
        if (p6 == 5) {
            ctw_bstatus(7); /* HD: the B status (GameCube: A) */
        }
        p1 = CTW_S(self,0x6918);
    } else if (p6 != -5) {
        ctw_bstatus(7);
        p1 = CTW_S(self,0x6918);
    }
    s32 cancel = ctw_cancel_trigger(self) != 0;
    if (!cancel && p1 < -1) {
        // Preserve the native condition load; classic removes only its branch.
        const unsigned int ground = CTW_W(self,0x834);
        (void)(ground & 0x20);
    }
    if (p1 == 0 || (cancel && CTW_B(ctw_play(),0x5BB6) == 7) || !(CTW_F(self,0x6A04) > 0.0f)) {
        if (p1 != 0 && CTW_S(self,0x6918) != 0) {
            ctw_reset_upper(self,2,-1.0f);
            ctw_reset_upper(self,1,-1.0f);
            ctw_sound(0x870);
        }
        CTW_B(ctw_play(),0x5BD1) = 0; /* dComIfGp_setMetronomeOff() */
        if (cancel && CTW_I(self,0x69C0) == 5) {
            CTW_F(self,0x6A04) = -1000.0f;
        }
        if (CTW_I(self,0x69C0) == -1) {
            {const u32 play=ctw_play();CTW_H(play,0x52B8)|=8;}
            {const u32 camera=ctw_camera();ctw_camera_end(camera,CTW_W(self,4));}
            ctw_end_demo(self);
        } else {
            ctw_cut_end(self);
        }
        ctw_reset();
        return 1;
    }
    s32 right, left;
    s32 count;
    u8 tactOn = CTW_B(CTWPTR(WWHD_GAME_DATA(0x101F8344,u32),0x1DC),0x16B); /* HD */
    if (tactOn != 0 && CTW_I(self,0x69C0) != -5) {
        right = ctw_direction(1, (s32)CTW_S(self,0x691A));
        left = ctw_direction(0, (s32)CTW_S(self,0x691C));
        if (left == 0) { /* HD: the GamePad input */
            left = ctw_cursor(WWHD_GAME_DATA(0x101F5088,u32));
        }
        count = CTW_W(self,0x6A7C) != 0;
        ctw_volume(ctw_stick_volume(self));
    } else {
        right = CTW_S(self,0x691A);
        left = CTW_S(self,0x691C);
        count = tactOn != 0;
    }
    if (count) { /* HD: GameCube counts every frame */
        CTW_F(self,0x69F8) = CTW_F(self,0x69F8) + 1.0f;
    }
    if ((u32)right != (u32)(s32)CTW_S(self,0x691A)) {
        f32 w = CTW_F(self,0x69FC);
        if (!(w > 0.0f) || right != 0) {
            ctw_set_upper(self,(u32)ctw_right_animation(self,right),1, 0.08f, 0.0f, -1, 4.0f);
            if (right == 0 || right == 2 || right == 4) {
                CTW_F(self,0x58C8)=0.8f;
                CTW_W(self,0x58CC)=CTW_W(self,0x589C);
            }
            s16 r = CTW_S(self,0x691A);
            if (r == 4 || r == 2 || right == 4 || right == 2) {
                ctw_reset();
                ctw_set_beat(right);
                CTW_S(self,0x6920) = 0;
                CTW_F(self,0x69F8) = 0.0f;
                ctw_old_frame(CTWPTR(self,0x65CC), 5.0f, 0, 0x2A);
                CTW_W(self,0x6A7C) = 0;
                CTW_S(self,0x6916) = -1;
            }
            CTW_S(self,0x691A) = (s16)right;
            ctw_clear_ratio(self+0x5828); /* mAnmRatioUpper[UPPER_MOVE1_e].setRatio(0.0f) */
            if (CTW_S(self,0x691A) != 0) {
                CTW_F(self,0x69FC) = 2.0f;
            }
        } else {
            CTW_F(self,0x69FC) = w - 1.0f;
        }
    } else if (CTW_S(self,0x691A) != 0) {
        CTW_F(self,0x69FC) = 2.0f;
    }
    s32 beatSet = 0;
    f64 beat;
    f64 cnt; /* first input retains native unrounded F29 */
    if ((u32)left != (u32)(s32)CTW_S(self,0x691C)) {
        f32 w = CTW_F(self,0x6A00);
        if (!(w > 0.0f) || left != 0) {
            ctw_set_upper(self,(u32)ctw_left_animation(self,left),2, 1.0f, 0.0f, -1,
                             4.0f); /* HD: HIO folded */
            CTW_S(self,0x691C) = (s16)left;
            ctw_clear_ratio(self+0x5838); /* mAnmRatioUpper[UPPER_MOVE2_e].setRatio(0.0f) */
            ctw_arm_sound(left);
            if (CTW_S(self,0x691C) != 0) {
                CTW_F(self,0x6A00) = 2.0f;
                if (CTW_S(self,0x6920) == 0) { /* HD: the first input starts the beat */
                    CTW_W(self,0x6A7C) = ctw_beat_word();
                    f64 bf = ctw_beat_frames();
                    cnt = bf;
                    CTW_F(self,0x69F8) = (f32)cnt;
                    beat = ctw_beat_frames();
                    beatSet = 1;
                }
            }
        } else {
            cnt = CTW_F(self,0x69F8);
            CTW_F(self,0x6A00) = w - 1.0f;
            beat = ctw_beat_frames();
            beatSet = 1;
        }
    } else if (CTW_S(self,0x691C) != 0) {
        CTW_F(self,0x6A00) = 2.0f;
    }
    if (!beatSet) {
        cnt = CTW_F(self,0x69F8);
        beat = ctw_beat_frames();
    }
    s16 p0;
    unsigned beatCounted = 0;
    if (!((f64)cnt < beat)) {
        ctw_metronome((s32)CTW_S(self,0x6920), (s32)CTW_S(self,0x691C));
        /* mpEquipItemBrk->setFrame(getFrameMax() - 0.001f) (HD brkAnm at +0x48F8, its end frame at +0xA) */
        ctw_frame(self,(f32)CTW_S(self,0x4902)-0.001f);
        CTW_S(self,0x6916) = 3;
        f64 bf = ctw_beat_frames();
        f32 nc = (f32)((f64)(f32)CTW_F(self,0x69F8) - bf);
        u32 m24 = CTW_W(self,0x6A7C);
        CTW_F(self,0x69F8) = nc;
        if (m24 != 0) { /* HD: a countdown of beats (GameCube toggles 0/1) */
            p0 = CTW_S(self,0x6916);
            CTW_W(self,0x6A7C) = m24 - 1;
            beatCounted = 1;
        }
    } else {
        ctw_frame(self,0.0f); /* mpEquipItemBrk->setFrame(0.0f) */
    }
    if (!beatCounted) {
        p0 = CTW_S(self,0x6916);
    }
    if (p0 >= 0) {
        if (p0 > 0) {
            p0 = (s16)(p0 - 1);
            CTW_S(self,0x6916) = p0;
        }
        if (p0 == 0) {
            s32 judge = ctw_judge((s32)CTW_S(self,0x6920), (s32)CTW_S(self,0x691C));
            CTW_I(self,0x69C4) = judge;
            s16 p5 = (s16)(CTW_S(self,0x6920) + 1);
            CTW_S(self,0x6920) = p5;
            s32 nbeat = ctw_beat_word();
            u32 rf = CTW_W(self,0x3C0);
            s32 song = CTW_I(self,0x69C0);
            if (!(p5 < nbeat)) {
                CTW_S(self,0x6920) = 0;
            }
            CTW_W(self,0x3C0)=rf|0x01000000;
            CTW_S(self,0x6916) = -1;
            s32 match = 0;
            if (song >= 0) {
                match = (u32)song == (u32)CTW_I(self,0x69C4);
            } else if (song == -1 || song == -3) {
                s32 j = CTW_I(self,0x69C4);
                match = j >= 0 && j != 6 && j != 7 && ctw_is_tact(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0xD4,(u8)j);
            }
            if (match) {
                CTW_S(self,0x6918) = 0x1E;
                ctw_sound(0x896);
                CTW_F(self,0x69F8) = 0.0f;
            } else {
                CTW_I(self,0x69C4) = -1;
                if (CTW_I(self,0x69C0) == -5) {
                    s16 m4 = (s16)(CTW_S(self,0x691E) + 1);
                    CTW_S(self,0x691E) = m4;
                    if (m4 == 6) {
                        CTW_S(self,0x6918) = 0xF;
                    }
                } else if (CTW_I(self,0x69C0) == -2) {
                    s16 m4 = (s16)(CTW_S(self,0x691E) + 1);
                    CTW_S(self,0x691E) = m4;
                    if (m4 == 5) {
                        CTW_F(self,0x69F8) = 0.0f;
                        CTW_S(self,0x6918) = 0x1E;
                    }
                }
            }
        }
    }
    CTW_F(self,0x58D8)=1.0f; /* HD: HIO folded */
    if (CTW_S(self,0x6918) >= 0) {
        return 1;
    }
    /* the arm swing frame follows the global frame counter in beats */
    f32 two = (f32)(ctw_beat_frames() * 2.0);
    unsigned int period;
    if (two < 2147483648.0f) {
        period = (u32)ctw_truncate(two);
    } else {
        period = (u32)ctw_truncate(two - 2147483648.0f) + 0x80000000u;
    }
    unsigned int frm = WWHD_GAME_DATA(0x101FF560,unsigned int);
    f32 phase = (f32)(frm - ctw_divide(frm, period) * period);
    const f64 lastBeat=ctw_beat_frames();
    const f32 denominator=(f32)(lastBeat+lastBeat);
    f32 r=phase/denominator;
    CTW_F(self,0x589C)=r*(f32)CTW_S(self,0x58A2);
    CTW_F(self,0x58CC)=r*(f32)CTW_S(self,0x58D2);
    CTW_W(CTWPTR(self,0x57FC),0)=CTW_W(self,0x589C);
    const u32 upper=CTWPTR(self,0x582C);
    if(upper)CTW_W(upper,0)=CTW_W(self,0x58CC);
    return 1;
}

#endif
