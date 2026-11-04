/* Complete CC0 a874e1a9 People next_msgStatus022C6108. MIT upstream
 * Dampa6A80 check/write replaces only repeated chart ownership logic. */
#pragma once
#define RWS8(a,o) (*(volatile u8*)((a)+(o)))
#define RWS16(a,o) (*(volatile u16*)((a)+(o)))
#define RWS32(a,o) (*(volatile unsigned int*)((a)+(o)))
#ifndef RWS_POINTER
#define RWS_POINTER(a,o) RWS32(a,o)
#endif
WWHD_GAME_FUNC(0x025D9F38,u32,rws_search,(u32 name,u32 argument,u32 parameter));
WWHD_GAME_FUNC(0x022C1168,void,rws_set_message,(u32 actor,u32 message));
WWHD_GAME_FUNC(0x022BFF78,s32,rws_random,(u32 actor,s32 maximum));
WWHD_GAME_FUNC(0x02520FE4,u32,rws_item_count,(u32 item));
WWHD_GAME_FUNC(0x025B7D90,u32,rws_symbol,(u32 inventory,u32 symbol));
static void rws_reset_timer(u16 timer){RWS16(randomizer_people_game(),0x5BAA)=timer;}
static void rws_tmp_off(u32 flag){randomizer_people_off(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x1178,flag);}
WWHD_REPLACE(0x022C6108,u16,randomizer_people_next_status,(u32 actor,u32 pMsgNo)) {
    u16 status = 15;
    // HD: read the message manager once in place of GameCube mpCurrMsg.
    const unsigned int message=RWS32(pMsgNo,0);
    const u32 msg=WWHD_GAME_DATA(0x101F4B5C,u32);
    switch (message) {
    case 0x358B: {
        u32 other=rws_search(WWHD_GAME_DATA(0x101C3EC0,u32),0,0);
        if (other==0) { /* HD: JUT_ASSERT(pActor != NULL) */
            // HD: the assertion calls pActor act.
            rwq_assert(0x100207EC,0x1D43,0x10020800);
            return 16;
        }
        u32 play;
        if (RWS32(msg,0x948) == 0) {
            RWS32(other,0x7C0) = 0x358C;
        } else if (play = randomizer_people_game(), (s32)randomizer_people_rupees() < (s16)RWS16(play,0x5BA4)) { /* getRupee() < getMessageRupee() */
            RWS32(other,0x7C0) = 0x359B;
        } else {
            RWS32(other,0x7C0) = 0x358D;
            s16 rupee = RWS16(randomizer_people_game(),0x5BA4);
            u32 play=randomizer_people_game();
            RWS32(play,0x5B48) = RWS32(play,0x5B48) - rupee; /* setItemRupeeCount(-rupee) */
        }
        status = 16;
        break;
    }
    case 0x3024:
        randomizer_people_event_on(0x2220);
        status = 16;
        break;
    default:
        if (RWS_POINTER(actor,0x8AC) != 0) {
            RWS_POINTER(actor,0x8AC)=RWS_POINTER(actor,0x8AC)+8;
            if (RWS32(RWS_POINTER(actor,0x8AC),0) != 0) {
                rws_set_message(actor,RWS_POINTER(actor,0x8AC));
            } else {
                status = 16;
            }
        } else if (RWS_POINTER(actor,0x8A8) != 0) {
            u32 p=RWS_POINTER(actor,0x8A8)+4;
            RWS_POINTER(actor,0x8A8) = p;
            u32 v = RWS32(p,0);
            switch (v) {
            case 1:
                RWS_POINTER(actor,0x8A8) = 0;
                RWS16(actor,0x8F2) = RWS16(actor,0x8F2) | 0x1;
                status = 16;
                break;
            case 2:
                RWS_POINTER(actor,0x8A8) = 0;
                RWS16(actor,0x8F2) = RWS16(actor,0x8F2) | 0x2;
                status = 16;
                break;
            case 3:
                RWS_POINTER(actor,0x8A8) = 0;
                RWS16(actor,0x8F2) = RWS16(actor,0x8F2) | 0x4;
                status = 16;
                break;
            case 4:
                RWS_POINTER(actor,0x8A8) = 0;
                RWS16(actor,0x8F2) = RWS16(actor,0x8F2) | 0x8;
                status = 16;
                break;
            case 5:
                RWS16(actor,0x8F2) = RWS16(actor,0x8F2) | 0x10;
                status = 16;
                break;
            case 6: {
                p += 4;
                RWS_POINTER(actor,0x8A8) = p;
                u8 no = RWS8(p,3);
                RWS8(actor,0x906) = no;
                if (!randomizer_people_event(RWS16((u32)&WWHD_GAME_DATA(0x101C305C,u8),2*(no)))) {
                    p = RWS_POINTER(actor,0x8A8)+4;
                    RWS_POINTER(actor,0x8A8) = p;
                    RWS32(pMsgNo,0) = RWS32(p,0);
                    u8 no2 = RWS8(actor,0x906);
                    RWS32(actor,0x8D0) = RWS32((u32)&WWHD_GAME_DATA(0x101C3870,u8),4*(no2)); /* l_item_id_sa3 */
                    randomizer_people_event_on(RWS16((u32)&WWHD_GAME_DATA(0x101C305C,u8),2*(no2)));
                } else {
                    RWS_POINTER(actor,0x8A8) = 0;
                    status = 16;
                }
                break;
            }
            case 7:
                RWS16(actor,0x8F2) = RWS16(actor,0x8F2) | 0x20;
                status = 16;
                break;
            case 8: {
                p += 4;
                RWS_POINTER(actor,0x8A8) = p;
                u8 item = RWS32(p,0) == 0 ? (u8)0x29 : (u8)0x28;
                RWS_POINTER(actor,0x8A8) = p + 4;
                s32 got = randomizer_people_item(item);
                p = RWS_POINTER(actor,0x8A8);
                if (!got) {
                    p += 8;
                    RWS_POINTER(actor,0x8A8) = p;
                }
                RWS32(pMsgNo,0) = RWS32(p,0);
                break;
            }
            case 9:
                RWS32(actor,0x8D0) = 4;
                RWS16(actor,0x8F2) = RWS16(actor,0x8F2) | 0x40;
                status = 16;
                break;
            case 0xA:
                // HD: the reward is a bag upgrade only when not yet owned (item 0xFD).
                RWS32(actor,0x8D0) = 5;
                RWS16(actor,0x8F2) = RWS16(actor,0x8F2) | 0x40;
                if (!randomizer_people_event(0x6A80)) {
                    randomizer_people_event_on(0x6A80);
                    RWS32(actor,0x8D0) = init_reward_dampa;
                }
                status = 16;
                break;
            case 0:
                RWS_POINTER(actor,0x8A8) = 0;
                status = 16;
                break;
            case 0xB:
                if (RWS32(msg,0x948) == 0) {
                    if (randomizer_people_event(0x1808)) {
                        RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C2F94,u8));
                    } else if (rws_symbol(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0xD4,0)) {
                        RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C3A9C,u8));
                    } else if (randomizer_people_event(0x1E10)) {
                        RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C36B8,u8));
                    } else {
                        RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C3AA8,u8));
                    }
                } else {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C2F8C,u8));
                }
                RWS32(pMsgNo,0) = RWS32(RWS_POINTER(actor,0x8A8),0);
                break;
            case 0xC:
                if (RWS32(msg,0x948) == 0) {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C2F9C,u8));
                    randomizer_people_event_on(0x2240);
                } else {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C2FA4,u8));
                }
                RWS32(pMsgNo,0) = RWS32(RWS_POINTER(actor,0x8A8),0);
                break;
            case 0xD: {
                s32 dir = randomizer_people_wind(actor);
                RWS32(pMsgNo,0) = RWS32((u32)&WWHD_GAME_DATA(0x101C36F8,u8),4*(dir)); /* l_msg_um1_wind */
                break;
            }
            case 0xE:
                if (RWS32(msg,0x948) == 0) {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C37A0,u8));
                    randomizer_people_event_on(0x2310);
                } else {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C2FF4,u8));
                }
                RWS32(pMsgNo,0) = RWS32(RWS_POINTER(actor,0x8A8),0);
                break;
            case 0xF:
                if (RWS32(msg,0x948) == 0) {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C307C,u8));
                } else {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C3940,u8));
                }
                RWS32(pMsgNo,0) = RWS32(RWS_POINTER(actor,0x8A8),0);
                break;
            case 0x10:
                if (RWS32(msg,0x948) == 0) {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C30A4,u8));
                    randomizer_people_tmp_reg_set(0xFF03, (u8)rws_random(actor,3));
                    s32 b = randomizer_people_event(0x2A04);
                    rws_reset_timer(b?0x708:0xE10);
                    RWS32(actor,0x8CC) = RWS32(actor,0x8CC) | 0x100000;
                } else {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C309C,u8));
                }
                RWS32(pMsgNo,0) = RWS32(RWS_POINTER(actor,0x8A8),0);
                break;
            case 0x11:
                if (RWS32(msg,0x948) == 0) {
                    if (rws_item_count(0x45) < 3) {
                        RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C3D18,u8));
                    } else {
                        RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C3988,u8));
                        u32 play=randomizer_people_game();
                        RWS16(play,0x5B70) = RWS16(play,0x5B70) - 3; /* setItemBeastNumCount(SKULL_NECKLACE, -3) */
                        rws_reset_timer(0xE10);
                        RWS32(actor,0x8CC) = RWS32(actor,0x8CC) | 0x100000;
                    }
                } else {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C3D0C,u8));
                }
                status = 16;
                break;
            case 0x12: {
                u8 reg = randomizer_people_tmp_reg(0xFF03);
                if (reg >= 3) { /* HD: bounds assertion, the list is kept */
                    // HD: the assertion calls reg a_tempReg.
                    rwq_assert(0x100207EC,0x1E1B,0x1002080C);
                } else {
                    RWS_POINTER(actor,0x8A8) = RWS32((u32)&WWHD_GAME_DATA(0x101C3CAC,u8),4*(reg)); /* l_msg_sa5_explain[reg] */
                }
                if (RWS_POINTER(actor,0x8A8) != 0) { /* HD: null check */
                    RWS32(pMsgNo,0) = RWS32(RWS_POINTER(actor,0x8A8),0);
                }
                break;
            }
            case 0x13:
                RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C3D24,u8));
                RWS32(pMsgNo,0) = RWS32(RWS_POINTER(actor,0x8A8),0);
                break;
            case 0x14:
                if (RWS32(msg,0x948) == 0) {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C3CB8,u8));
                    randomizer_people_temporary_on(0x0280);
                    randomizer_people_event_on(0x2680);
                    randomizer_people_event_on(0x2640);
                    rws_tmp_off(0x0240);
                    randomizer_people_tmp_reg_set(0xFE03, 0);
                    randomizer_people_tmp_reg_set(0xFD07, 7);
                } else {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C30AC,u8));
                }
                RWS32(pMsgNo,0) = RWS32(RWS_POINTER(actor,0x8A8),0);
                break;
            case 0x15:
                if (RWS32(msg,0x948) == 0) {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C3D30,u8));
                    randomizer_people_temporary_on(0x0280);
                    randomizer_people_event_on(0x2680);
                    randomizer_people_event_on(0x2620);
                    randomizer_people_tmp_reg_set(0xFE03, 3);
                    randomizer_people_tmp_reg_set(0xFF03, 3);
                    randomizer_people_tmp_reg_set(0xFD07, 7);
                } else {
                    RWS_POINTER(actor,0x8A8) = ((u32)&WWHD_GAME_DATA(0x101C30D4,u8));
                }
                RWS32(pMsgNo,0) = RWS32(RWS_POINTER(actor,0x8A8),0);
                break;
            case 0x17:
                rws_tmp_off(0x0280);
                status = 16;
                break;
            default:
                RWS32(pMsgNo,0) = v;
                break;
            }
        } else {
            status = 16;
        }
    }
    return status;
}
