/* Complete CC0 a874e1a9 daNpc_Bs1_c::getMsg022157C8.
 * MIT fix_vanilla_bugs refuses blue Chu jelly at the original dispatch seam. */
#pragma once
#ifndef RBB
#define RBB(t,a) (*(volatile t*)(a))
#endif
WWHD_GAME_FUNC(0x025200D4,u32,rbb_game,(void));
WWHD_GAME_FUNC(0x02550FF4,s32,rbb_spoils,(u32 item));
WWHD_GAME_FUNC(0x025BCF44,s32,rbb_sold_out,(u32 stock));
WWHD_GAME_FUNC(0x02520C0C,s32,rbb_owned,(u32 item));
WWHD_GAME_FUNC(0x0221572C,s32,rbb_sells_bombs,(u32 actor));
WWHD_GAME_FUNC(0x025B8BB0,u8,rbb_register_native,(u32 events,u32 key));
WWHD_GAME_FUNC(0x025B8B94,s32,rbb_event_native,(u32 events,u32 flag));
WWHD_GAME_FUNC(0x025B8B68,void,rbb_on_native,(u32 events,u32 flag));
WWHD_GAME_FUNC(0x0273AA24,void,rbb_assert,(u32 file,s32 line,u32 condition));
static u32 rbb_events(void){return WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644;}
static u8 rbb_register(u32 key){return rbb_register_native(rbb_events(),key);}
static s32 rbb_event(u32 flag){return rbb_event_native(rbb_events(),flag);}
static void rbb_on(u32 flag){rbb_on_native(rbb_events(),flag);}
static s32 rbb_talk_xy(void){return (unsigned int)(RBB(u8,rbb_game()+0x52B0)-1u)<=3;}
WWHD_REPLACE(0x022157C8,u32,randomizer_beedle_get_message,(u32 actor)) {
    u32 msgNo = RBB(u32,actor+0x8B8);
    if (msgNo != 0) {
        RBB(u32,actor+0x8B8) = 0;
        return msgNo;
    }
    if (rbb_talk_xy()) {
        const u32 play = rbb_game();
        s8 type = RBB(s8,actor+0x99E);
        u8 itemNo = (u8)RBB(u8,play+0x52B1); /* dComIfGp_event_getPreItemNo() */
        if (type == 0) {
            if (rbb_spoils(itemNo)) {
                RBB(u32,actor+0x9AC) = itemNo;
                if (itemNo == 0x1F) {
                    msgNo = 0xF94; /* dItemNo_JOY_PENDANT_e */
                } else if (itemNo == 0x4B) {
                    msgNo = 0xF75;
                } else if ((itemNo >= 0x45 && itemNo <= 0x47) || (itemNo >= 0x49 && itemNo <= 0x4B)) {
                    /* skull necklace, boko baba seed, golden feather, red / green / blue jelly: switch table
                     */
                    msgNo = (u16)RBB(u16,(u32)&WWHD_GAME_DATA(0x100185D4,u8)+(itemNo-0x45)*2);
                } else if (rbb_register(0x7F0F) < 10) {
                    msgNo = 0xF9E;
                } else {
                    msgNo = 0xFD4;
                }
            } else if (itemNo == 0x9D /* dItemNo_COMPLIMENTARY_ID_e */) {
                msgNo = 0xF6F;
            } else if (itemNo == 0x9E /* dItemNo_FILL_UP_COUPON_e */) {
                msgNo = 0xF73;
            } else {
                msgNo = 0xF75;
            }
        } else if (itemNo == 0x9D || itemNo == 0x9E) {
            msgNo = 0x2F56;
        } else if (rbb_spoils(itemNo)) {
            msgNo = 0x2F79;
        } else {
            msgNo = 0x2F57;
        }
    } else if (RBB(s8,actor+0x99E) == 0) {
        u8 points = rbb_register(0x86FF);
        if (rbb_sold_out(actor+0x940)) {
            msgNo = 0xF3D;
        } else if (rbb_owned(0x31 /* dItemNo_BOMB_BAG_e */) && !rbb_event(0x1F20) &&
                   rbb_sells_bombs(actor)) {
            rbb_on(0x1F20);
            RBB(s8,actor+0x9A3) = 1;
            msgNo = 0xF55;
        } else if (RBB(s8,actor+0x9A3) != 0) {
            msgNo = 0xF58;
        } else if (points >= 60) {
            if (RBB(s8,actor+0x9A2) != 0) {
                msgNo = 0xF5D;
            } else {
                msgNo = 0xF5C;
                RBB(s8,actor+0x9A2) = 1;
            }
        } else if (points != 0) {
            if (RBB(s8,actor+0x9A2) != 0) {
                msgNo = 0xF5B;
            } else {
                msgNo = 0xF5A;
                RBB(s8,actor+0x9A2) = 1;
            }
        } else if (RBB(s8,actor+0x9A2) != 0) {
            msgNo = 0xF41;
        } else {
            RBB(s8,actor+0x9A2) = 1;
            msgNo = 0xF3D;
        }
    } else if (rbb_sold_out(actor+0x940)) {
        msgNo = 0x2F62;
    } else if (rbb_owned(0x31) && !rbb_event(0x1F20) && rbb_sells_bombs(actor)) {
        rbb_on(0x1F20);
        RBB(s8,actor+0x9A3) = 1;
        msgNo = 0x2F64;
    } else if (RBB(s8,actor+0x9A3) != 0) {
        msgNo = 0x2F67;
    } else if (rbb_event(0x1F08)) {
        if (rbb_event(0x2040)) {
            if (RBB(s8,actor+0x9A4) == 1) {
                msgNo = 0x2F60;
            } else {
                msgNo = 0x2F61;
            }
        } else {
            rbb_on(0x2040);
            msgNo = 0x2F5F;
            RBB(s8,actor+0x9A4) = 1;
        }
    } else {
        switch (rbb_register(0xBB07)) {
        case 0:
            if (RBB(s8,actor+0x9A2) != 0 || rbb_event(0x1F10)) {
                msgNo = 0x2F46;
                break;
            }
            rbb_on(0x1F10);
            msgNo = 0x2F45;
            break;
        case 1:
            msgNo = RBB(s8,actor+0x9A2) != 0 ? 0x2F46 : 0x2F58;
            break;
        case 2:
            msgNo = RBB(s8,actor+0x9A2) != 0 ? 0x2F46 : 0x2F59;
            break;
        case 3:
            msgNo = RBB(s8,actor+0x9A2) != 0 ? 0x2F46 : 0x2F5A;
            break;
        case 4:
            msgNo = RBB(s8,actor+0x9A2) != 0 ? 0x2F46 : 0x2F5B;
            break;
        case 5:
            msgNo = RBB(s8,actor+0x9A2) != 0 ? 0x2F46 : 0x2F5C;
            break;
        case 6:
            msgNo = RBB(s8,actor+0x9A2) != 0 ? 0x2F69 : 0x2F5D;
            break;
        case 7:
            msgNo = RBB(s8,actor+0x9A2) != 0 ? 0x2F6A : 0x2F5E;
            break;
        default:
            /* HD: an assertion (line 0x5D5) instead of an uninitialised message number */
            rbb_assert(0x100185E4,0x5D5,0x100185D0);
            msgNo = 0;
            break;
        }
        if (rbb_event(0x1F10)) {
            rbb_on(0x1F10);
        }
        RBB(s8,actor+0x9A2) = 1;
    }
    return msgNo;
}
