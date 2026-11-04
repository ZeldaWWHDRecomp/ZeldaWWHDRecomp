/* Complete CC0 a874e1a9 People createInit022BF510. Only the ignored
 * Kamo chart query changes to6910; all original initialization is retained. */
#pragma once
WWHD_GAME_FUNC(0x022BEB50,u32,rwc_rail,(u32 actor));
WWHD_GAME_FUNC(0x0259E6D0,s32,rwc_path_info,(u32 path,s32 index,s32 room,u32 reverse));
WWHD_GAME_FUNC(0x0259E778,void,rwc_path_point,(u32 path,u32 point,u32 index));
WWHD_GAME_FUNC(0x0259EB60,s32,rwc_path_next,(u32 path));
WWHD_GAME_FUNC(0x022BEEDC,void,rwc_wait_animation,(u32 actor));
WWHD_GAME_FUNC(0x02543F10,s32,rwc_event_index_native,(u32 manager,u32 name,s32 map));
WWHD_GAME_FUNC(0x0259F814,void,rwc_staff,(u32 cut,u32 name,u32 actor));
WWHD_GAME_FUNC(0x025D674C,void,rwc_cull,(u32 actor,f32 x0,f32 y0,f32 z0,f32 x1,f32 y1,f32 z1));
WWHD_GAME_FUNC(0x0259E08C,void,rwc_joints,(u32 joint,s32 a,s32 b,s32 c,s32 d,s32 e,s32 f,s32 g,s32 h,s32 i));
WWHD_GAME_FUNC(0x027F4D5C,void,rwc_model_calculate,(u32 model));
WWHD_GAME_FUNC(0x02515F14,void,rwc_status,(u32 status,s32 weight,s32 damage,u32 actor));
WWHD_GAME_FUNC(0x02516518,void,rwc_cylinder,(u32 cylinder,u32 source));
static s16 rwc_event_index(u32 name){return(s16)rwc_event_index_native(randomizer_people_game()+0x52C4,name,0xFF);}
WWHD_REPLACE(0x022BF510,s32,randomizer_people_create_init,(u32 actor)) {
    int temp = 0xFF;
    u8 pathIndex = rwc_rail(actor);
    if (pathIndex != 0xFF) {
        rwc_path_info(actor+0x870,pathIndex,(s8)RWQ8(actor,0x326),1);
        if (RWE_POINTER(actor,0x870) == 0) {
            return 5;
        }
        RWQ32(actor,0x2E0) = RWQ32(actor,0x2E0) & ~0x80; /* fopAcM_OffStatus(this, 0x80) */
        unsigned int point[3];
        rwc_path_point(actor+0x870,(u32)point,RWQ8(actor,0x875));
        RWQ32(actor,0x300)=point[0];RWQ32(actor,0x304)=point[1];RWQ32(actor,0x308)=point[2];
        RWQ32(actor,0x314)=point[0];RWQ32(actor,0x318)=point[1];RWQ32(actor,0x31C)=point[2];
        rwc_path_next(actor+0x870);
        RWS16(actor,0x8E2) = 1;
        temp = 0xFE;
        RWQ8(actor,0x915) = 1;
    }
    RWE_F32(actor,0x374) = -9.0f;
    rwc_wait_animation(actor);
    for (int i = 0; i < 4; i++) {
        RWS16(actor,0x8DA+2*(i)) = -1;
    }
    switch (RWQ8(actor,0x905)) {
    case 0:
        RWS16(actor,0x8DA+2*(0)) = rwc_event_index(0x100204F4); /* "UO1_GET_ITEM" */
        break;
    case 3:
    case 4:
        RWS16(actor,0x8DA+2*(0)) = rwc_event_index(0x10020504); /* "UB1_TALK" */
        RWS16(actor,0x8DA+2*(1)) = rwc_event_index(0x100204DC); /* "UB1_TALK_XY" */
        RWS16(actor,0x8DA+2*(1)) = rwc_event_index(0x10020510); /* "UB1_TALK_PHOTO" */
        {
            s16 idx = rwc_event_index(0x100204C4); /* "UB1_TALK_PHOTO_GET_ITEM" */
            RWQ8(actor,0x910) = 0;
            RWS16(actor,0x8DA+2*(3)) = idx;
        }
        RWS16(actor,0xFC)=RWS16(actor,0x8DA);
        break;
    case 6:
        RWS16(actor,0x8DA+2*(0)) = rwc_event_index(0x10020520); /* "UB4_GET_ITEM" */
        break;
    case 8:
        RWS16(actor,0x8DA+2*(0)) = rwc_event_index(0x10020530); /* "UW2_GET_ITEM" */
        break;
    case 9:
        RWS16(actor,0x8DA+2*(0)) = rwc_event_index(0x10020540); /* "UM1_GET_ITEM" */
        RWS16(actor,0x8DA+2*(1)) = rwc_event_index(0x10020550); /* "UM1_TALK" */
        break;
    case 11:
        RWS16(actor,0x8DA+2*(0)) = rwc_event_index(0x1002055C); /* "UM3_TELESCOPE_TALK" */
        RWS16(actor,0x8DA+2*(1)) = rwc_event_index(0x10020570); /* "UM3_GET_ITEM" */
        if (RWQ8(actor,0x90F)) {
            RWQ32(actor,0x8CC) = RWQ32(actor,0x8CC) | 0x40000000;
        }
        break;
    case 14:
        RWS16(actor,0x8DA+2*(0)) = rwc_event_index(0x10020580); /* "SA3_GET_ITEM" */
        break;
    case 16: {
        RWS16(actor,0x8DA+2*(0)) = rwc_event_index(0x100204E8); /* "SA5_TALK_XY" */
        RWS16(actor,0x8DA+2*(1)) = rwc_event_index(0x10020590); /* "SA5_GET_ITEM" */
        u8 reg = randomizer_people_tmp_reg(0xFD07 /* UNK_FD07 */);
        for (int i = 0; i < 3; i++) {
            if (reg & (1 << i)) {
                u32 pig = rws_search(0x100204C0,0xF00, RWQ32((u32)&WWHD_GAME_DATA(0x1002014C,u8),4*i));
                if (pig != 0) {
                    /* kb_class::taura_pos_set(current.pos) */
                    unsigned int y=RWQ32(actor,0x318),x=RWQ32(actor,0x314),z=RWQ32(actor,0x31C);
                    RWQ32(pig,0x798)=x;
                    RWQ32(pig,0x79C)=y;
                    RWQ8(pig,0x750) = 1;
                    RWQ32(pig,0x7A0)=z;
                    RWQ32(pig,0x314)=RWQ32(actor,0x314);RWQ32(pig,0x318)=RWQ32(actor,0x318);RWQ32(pig,0x31C)=RWQ32(actor,0x31C);
                }
            }
        }
    }
        /* fall through */
    case 15:
        if (RWQ8(actor,0x90F)) {
            RWQ32(actor,0x8CC) = RWQ32(actor,0x8CC) | 0x40000000;
        }
        break;
    case 17:
        /* HD: an UG1 placed at x = -1984 is moved to (-2002, 284, -200328) */
        if ((s32)rwe_truncate(RWE_F32(actor,0x2EC)) == -0x7C0) {
            RWE_F32(actor,0x318) = 284.0f;
            RWE_F32(actor,0x31C) = -200328.0f;
            RWE_F32(actor,0x304) = 284.0f;
            RWE_F32(actor,0x308) = -200328.0f;
            RWE_F32(actor,0x314) = -2002.0f;
            RWE_F32(actor,0x300) = -2002.0f;
            RWE_F32(actor,0x2EC) = -2002.0f;
            RWE_F32(actor,0x2F0) = 284.0f;
            RWE_F32(actor,0x2F4) = -200328.0f;
        }
        /* fall through */
    case 18:
        if (rwp_people_argument(actor) == 0) {
            temp = 0x50;
            RWS16(actor,0x8E2) = 1;
            RWQ32(actor,0x8CC) = 0x1000;
        }
        RWS16(actor,0x8DA+2*(0)) = rwc_event_index(0x100205A0); /* "UG1_TALK" */
        RWQ8(actor,0x915) = 1;
        break;
    default:
        break;
    }
    RWQ32(actor,0x100)=0x022BF300;
    RWQ32(actor,0x104)=0x022BF21C;
    RWQ32(actor,0x108)=0x022BF39C;
    rwc_staff(actor+0x3E0,RWE_POINTER((u32)&WWHD_GAME_DATA(0x101C3E7C,u8),4*RWQ8(actor,0x905)),actor);
    RWS16(actor,0x8F8) = 0;
    RWQ8(actor,0x8FC) = 0;
    RWQ8(actor,0x8FD) = 0;
    RWQ8(actor,0x8FE) = 0;
    u32 model=RWE_POINTER(RWE_POINTER(actor,0x44C),0x90);
    RWQ32(actor,0x348)=model?(unsigned int)(model+0xC8):0; /* fopAcM_SetMtx */
    rwc_cull(actor, -70.0f, 0.0f, -70.0f, 70.0f, 200.0f, 70.0f);
    u32 data=RWE_POINTER(actor,0x8A0);
    RWE_F32(actor,0x364)=RWE_F32(data,0x30)/WWHD_GAME_DATA(0x1048D04C,f32);
    u32 dist=(u32)&WWHD_GAME_DATA(0x101C3E28,u8)+2*RWQ8(actor,0x905)+RWQ8(actor,0x90F);
    RWQ8(actor,0x389)=RWQ8(dist,0); /* attention_info.distances[fopAc_Attn_TYPE_TALK_e] */
    RWQ32(actor,0x39C) = 0x0100000A; /* RWQ32(actor,0x39C) */
    RWQ8(actor,0x38B)=RWQ8(dist,0); /* attention_info.distances[fopAc_Attn_TYPE_SPEAK_e] */
    u32 dat=RWE_POINTER(actor,0x8A0);
    rwc_joints(actor+0x3AC,(s16)RWS16(dat,0x04), (s16)RWS16(dat,0x06), (s16)RWS16(dat,0x0C), (s16)RWS16(dat,0x0E), (s16)RWS16(dat,0x00), (s16)RWS16(dat,0x02),
                   (s16)RWS16(dat,0x08), (s16)RWS16(dat,0x0A), (s16)RWS16(dat,0x10));
    if (RWQ8(actor,0x905) == 11 && !RWQ8(actor,0x90F)) {
        (void)rwq_event(0x6910);
    }
    RWQ8(actor,0x911) = RWQ8(RWE_POINTER(actor,0x8A0),0x5A);
    RWQ8(actor,0x912) = RWQ8(RWE_POINTER(actor,0x8A0),0x5C);
    RWQ8(actor,0x913) = RWQ8(RWE_POINTER(actor,0x8A0),0x5D);
    RWQ32(actor,0x8BC)=RWQ32(RWE_POINTER(actor,0x8A0),0x28);
    RWS16(actor,0x8EA) = RWS16(RWE_POINTER(actor,0x8A0),0x34);
    RWS16(actor,0x8EC) = RWS16(RWE_POINTER(actor,0x8A0),0x36);
    RWQ32(actor,0x8C0)=RWQ32(RWE_POINTER(actor,0x8A0),0x3C);
    if (RWQ8(actor,0x905) != 11) {
        rwe_correct(actor+0x450,randomizer_people_game()+0x12A0);
        union {unsigned int word;f32 value;} ground;
        ground.word=RWQ32(actor,0x4E4);
        if(ground.value!=-1000000000.0f) {
            RWQ32(actor,0x2F0)=ground.word;
            RWQ32(actor,0x318)=ground.word;
        }
    }
    rwe_matrix(actor);
    rwc_model_calculate(RWE_POINTER(RWE_POINTER(actor,0x44C),0x90));
    rwc_status(actor+0x654,temp,0xFF,actor);
    rwc_cylinder(actor+0x690,(u32)&WWHD_GAME_DATA(0x101EA190,u8));
    RWQ32(actor,0x6D4)=(unsigned int)(actor+0x654);
    unsigned int center[3];
    center[0]=RWQ32(actor,0x314);
    center[1]=RWQ32(actor,0x318);
    center[2]=RWQ32(actor,0x31C);
    rwe_collision(actor,actor+0x690,(u32)center,RWE_F32(actor,0x8C0), RWE_F32(RWE_POINTER(actor,0x8A0),0x40));
    return 4;
}
