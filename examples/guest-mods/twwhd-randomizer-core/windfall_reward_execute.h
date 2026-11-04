/* Complete CC0 a874e1a9 People _execute022C305C; MIT upstream's Kamo
 * chart query becomes6910, including ordinary event/animation/physics paths. */
#pragma once
#define RWE_F32(a,o) (*(volatile f32*)((a)+(o)))
#ifndef RWE_POINTER
#define RWE_POINTER(a,o) (*(volatile u32*)((a)+(o)))
#endif
#ifndef RWE_CALL_MOVE
#define RWE_CALL_MOVE(target,actor) ((void(*)(u32))(target))((actor))
#endif
WWHD_GAME_FUNC(0x022BFFE4,void,rwe_warp,(u32 actor));
WWHD_GAME_FUNC(0x022C0084,void,rwe_attention,(u32 actor));
WWHD_GAME_FUNC(0x022C045C,void,rwe_check_order,(u32 actor));
WWHD_GAME_FUNC(0x022C2958,void,rwe_event_move,(u32 actor));
WWHD_GAME_FUNC(0x022C2A04,void,rwe_event_order,(u32 actor));
WWHD_GAME_FUNC(0x022C2CC0,void,rwe_texture,(u32 actor));
WWHD_GAME_FUNC(0x022C2D6C,void,rwe_animation,(u32 actor));
WWHD_GAME_FUNC(0x022C2E50,void,rwe_look,(u32 actor));
WWHD_GAME_FUNC(0x022BF3A0,void,rwe_matrix,(u32 actor));
WWHD_GAME_FUNC(0x0200F5C8,s32,rwe_chase,(u32 value,f32 target,f32 step));
WWHD_GAME_FUNC(0x025D6870,void,rwe_move,(u32 actor,u32 vector));
WWHD_GAME_FUNC(0x024F08A8,void,rwe_correct,(u32 acch,u32 background));
WWHD_GAME_FUNC(0x022BF480,void,rwe_collision,(u32 actor,u32 cylinder,u32 center,f32 radius,f32 height));
WWHD_GAME_FUNC(0x025F1884,void,rwe_rotate,(u32 matrix,s32 angle));
WWHD_GAME_FUNC(0x028E8F64,void,rwe_transform,(u32 matrix,u32 source,u32 target));
WWHD_GAME_FUNC(0x028E8D88,void,rwe_add,(u32 a,u32 b,u32 target));
static unsigned int rwe_truncate(f32 value){if(value!=value)return 0x80000000u;if(value>=2147483648.0f)return 0x7FFFFFFFu;if(value<=-2147483648.0f)return 0x80000000u;return(unsigned int)(s32)value;}
static void rwe_call_move(u32 actor) {
    const u32 entry=(u32)&WWHD_GAME_DATA(0x101C4BE4,u8)+RWQ8(actor,0x903)*8;
    const s32 slot=(s16)RWS16(entry,2),adjust=(s16)RWS16(entry,0);
    const u32 object=actor+adjust;
    u32 target;
    if(slot<0)target=RWE_POINTER(entry,4);
    else{const s32 offset=(s16)RWS16(entry,6);const u32 table=RWE_POINTER(object,offset);target=RWE_POINTER(table,slot*8+4);}
    RWE_CALL_MOVE(target,object);
}
WWHD_REPLACE(0x022C305C,s32,randomizer_people_execute,(u32 actor)) {
    // HD: reset the photo object here.
    RWQ8(actor,0x91A) = 0xFF;
    rwe_warp(actor);

    switch (RWQ8(actor,0x905)) {
    case 17:
    case 18:
        if (rwp_people_argument(actor) == 0) {
            unsigned int diff[3];
            randomizer_people_subtract(actor+0x314,(u32)diff,actor+0x2EC);
            f32 mag=(f32)(randomizer_people_sqrt(randomizer_people_square((u32)diff))-180.0);
            f32 f3 = 20.0f;

            if (mag < 0.0f) {
                mag = 0.0f;
            } else if (mag > f3) {
                mag = f3;
            }
            /* mStts.SetWeight(): the weight byte */
            RWQ8(actor,0x668)=(u8)rwe_truncate((50.0f*mag/f3)+80.0f);
        }
    }

    rwe_attention(actor);
    rwe_check_order(actor);
    if (!RWQ8(randomizer_people_game(),0x5292) || (RWS16(actor,0xF8) == 1 && RWQ8(actor,0x910) != 0)) {
        rwe_call_move(actor);
    } else {
        rwe_event_move(actor);
    }

    rwe_event_order(actor);
    if (RWS16(actor,0xF8) != 1) {
        RWQ8(actor,0x900) = 0;
        if (!RWQ8(actor,0x90F)) {
            switch (RWQ8(actor,0x905)) {
            case 3:
                if (RWQ8(actor,0x909) & 0x3) {
                    RWQ8(actor,0x909) = RWQ8(actor,0x909) & ~0x3;
                    u8 rand = rws_random(actor,8);
                    if (rand == RWQ8(actor,0x908)) {
                        rand = 0;
                    }
                    rwq_animation(actor,RWE_POINTER((u32)&WWHD_GAME_DATA(0x101C3460,u8),4*(u8)rand),1);
                    RWQ8(actor,0x908) = rand;
                }
                break;
            case 4:
                if (RWQ8(actor,0x909) & 0x3) {
                    RWQ8(actor,0x909) = RWQ8(actor,0x909) & ~0x3;
                    u8 rand = rws_random(actor,8);
                    if (rand == RWQ8(actor,0x908)) {
                        rand = 0;
                    }
                    rwq_animation(actor,RWE_POINTER((u32)&WWHD_GAME_DATA(0x101C3480,u8),4*(u8)rand),1);
                    RWQ8(actor,0x908) = rand;
                }
                break;
            case 11:
                if (!rwq_event(0x6910) && (RWQ8(actor,0x909) & 0x3)) {
                    RWQ8(actor,0x909) = RWQ8(actor,0x909) & ~0x3;
                    u8 rand = rws_random(actor,8);
                    if (rand == RWQ8(actor,0x908)) {
                        rand = 0;
                    }
                    rwq_animation(actor,RWE_POINTER((u32)&WWHD_GAME_DATA(0x101C34A0,u8),4*(u8)rand),1);
                    RWQ8(actor,0x908) = rand;
                }
                break;
            }
        } else {
            switch (RWQ8(actor,0x905)) {
            case 14:
                if (RWQ8(actor,0x909) & 0x3) {
                    RWQ8(actor,0x909) = RWQ8(actor,0x909) & ~0x3;
                    u8 rand;
                    if (RWQ8(actor,0x908) == 3) {
                        rand = 0;
                        RWE_F32(actor,0x8B8) = 15.0f;
                    } else {
                        rand = rws_random(actor,4);
                    }
                    rwq_animation(actor,RWE_POINTER((u32)&WWHD_GAME_DATA(0x101C34C0,u8),4*(u8)rand),1);
                    RWQ8(actor,0x908) = rand;
                    if (RWQ8(actor,0x907) == 3) {
                        RWQ8(actor,0x912) = 0;
                    } else {
                        RWQ8(actor,0x912) = 1;
                    }
                }
                break;
            case 16:
                if (RWQ8(actor,0x909) & 0x3) {
                    RWQ8(actor,0x909) = RWQ8(actor,0x909) & ~0x3;
                    u8 rand;
                    if (RWQ8(actor,0x908) == 3) {
                        rand = 0;
                        RWE_F32(actor,0x8B8) = 15.0f;
                    } else {
                        rand = rws_random(actor,4);
                    }
                    rwq_animation(actor,RWE_POINTER((u32)&WWHD_GAME_DATA(0x101C34D0,u8),4*(u8)rand),1);
                    RWQ8(actor,0x908) = rand;
                    if (RWQ8(actor,0x907) == 3) {
                        RWQ8(actor,0x912) = 0;
                    } else {
                        RWQ8(actor,0x912) = 1;
                    }
                }
                break;
            }
        }
    }

    rwe_texture(actor);
    rwe_animation(actor);
    if (RWQ8(actor,0x915) != 0) {
        if (RWQ8(actor,0x907) == 2) {
            rwe_chase(actor+0x370,RWE_F32(actor,0x8B4),0.1f);
            f32 speed = RWE_F32(actor,0x370) * RWE_F32(RWE_POINTER(actor,0x8A0),0x44);
            u32 morf=RWE_POINTER(actor,0x44C);
            if (speed < 0.5f) {
                speed = 0.5f;
            }
            RWE_F32(morf,0x98)=speed;
        } else {
            RWE_F32(actor,0x370) = 0.0f;
        }
        rwe_move(actor,actor+0x654); /* mStts.GetCCMoveP() */
        rwe_correct(actor+0x450,randomizer_people_game()+0x12A0);
    }

    {
        u32 dat=RWE_POINTER(actor,0x8A0);
        unsigned int pos[3];
        pos[0]=RWQ32(actor,0x314);
        pos[1]=RWQ32(actor,0x318);
        pos[2]=RWQ32(actor,0x31C);
        rwe_collision(actor,actor+0x690,(u32)pos,RWE_F32(actor,0x8C0),RWE_F32(dat,0x40));
    }
    unsigned int temp[3];
    u32 dat=RWE_POINTER(actor,0x8A0);
    temp[0]=RWQ32(dat,0x18);temp[1]=RWQ32(dat,0x1C);temp[2]=RWQ32(dat,0x20);
    const u32 matrix=(u32)&WWHD_GAME_DATA(0x1048D0CC,u8);
    rwe_rotate(matrix,(s16)RWS16(actor,0x322));
    rwe_transform(matrix,(u32)temp,(u32)temp);
    rwe_add((u32)temp,actor+0x314,(u32)temp); /* temp += current.pos */
    RWQ32(actor,0x390)=temp[0];RWQ32(actor,0x394)=temp[1];RWQ32(actor,0x398)=temp[2]; /* attention_info.position = temp */
    const u32 eye_data=RWE_POINTER(actor,0x8A0);
    const f32 eye_offset=RWE_F32(eye_data,0x24),eye_base=RWE_F32(actor,0x318);
    const unsigned int eye_x=RWQ32(actor,0x314);
    const f32 eye_y=eye_base+eye_offset;
    const unsigned int eye_z=RWQ32(actor,0x31C);
    RWQ32(actor,0x37C)=eye_x;RWE_F32(actor,0x380)=eye_y;RWQ32(actor,0x384)=eye_z;
    rwe_look(actor);
    rwe_matrix(actor);
    return 0;
}
