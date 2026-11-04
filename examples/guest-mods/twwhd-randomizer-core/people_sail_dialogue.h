/* Complete CC0 public d_a_npc_people_msg.cpp::getMsg (a874e1a9).
 * MIT make_game_nonlinear.asm: Kamo, Kane and Candy retain the normal-sail
 * query, then take their ordinary dialogue regardless of its result.
 * Native message tables remain live game data, not copied package assets. */
#pragma once
#define RPEOPLE(t,a,o) (*(volatile t*)((u32)(a)+(o)))
WWHD_GAME_FUNC(0x025200D4,u32,randomizer_people_game,(void));
WWHD_GAME_FUNC(0x02520C0C,s32,randomizer_people_item,(u32 item));
WWHD_GAME_FUNC(0x025B8B94,s32,randomizer_people_event_query,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x025B8B68,void,randomizer_people_on,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x025B8B7C,void,randomizer_people_off,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x025B8BB0,u8,randomizer_people_reg_query,(u32 event,u32 reg));
WWHD_GAME_FUNC(0x025B8AF4,void,randomizer_people_reg_write,(u32 event,u32 reg,u8 value));
WWHD_GAME_FUNC(0x022C6910,s32,randomizer_people_color,(u32 actor));
WWHD_GAME_FUNC(0x022C693C,s32,randomizer_people_all,(u32 actor));
WWHD_GAME_FUNC(0x022C6978,s32,randomizer_people_one,(u32 actor));
WWHD_GAME_FUNC(0x022C60D0,s32,randomizer_people_wind,(u32 actor));
WWHD_GAME_FUNC(0x022C69A0,s32,randomizer_people_daiza,(u32 actor));
WWHD_GAME_FUNC(0x022C6BDC,s16,randomizer_people_pig_timer,(u32 actor));
WWHD_GAME_FUNC(0x022C6C00,s32,randomizer_people_pig_ok,(u32 actor));
WWHD_GAME_FUNC(0x0259D624,void,randomizer_people_distance_angle,(u32 a,u32 b,u32 distance,u32 angle));
WWHD_GAME_FUNC(0x0201ADE0,void,randomizer_people_subtract,(u32 a,u32 result,u32 b));
WWHD_GAME_FUNC(0x028E8DD0,f64,randomizer_people_square,(u32 vector));
WWHD_GAME_FUNC(0x028F4384,f64,randomizer_people_sqrt,(f64 value));
WWHD_GAME_FUNC(0x0273AA24,void,randomizer_people_assert,(u32 file,s32 line,u32 condition));
static u32 randomizer_people_save(void){return WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);}
static s32 randomizer_people_event(u32 flag){return randomizer_people_event_query(randomizer_people_save()+0x644,flag);}
static s32 randomizer_people_temporary(u32 flag){return randomizer_people_event_query(randomizer_people_save()+0x1178,flag);}
static void randomizer_people_event_on(u32 flag){randomizer_people_on(randomizer_people_save()+0x644,flag);}
static void randomizer_people_event_off(u32 flag){randomizer_people_off(randomizer_people_save()+0x644,flag);}
static void randomizer_people_temporary_on(u32 flag){randomizer_people_on(randomizer_people_save()+0x1178,flag);}
static u8 randomizer_people_reg(u32 reg){return randomizer_people_reg_query(randomizer_people_save()+0x644,reg);}
static u8 randomizer_people_tmp_reg(u32 reg){return randomizer_people_reg_query(randomizer_people_save()+0x1178,reg);}
static void randomizer_people_reg_set(u32 reg,u8 value){randomizer_people_reg_write(randomizer_people_save()+0x644,reg,value);}
static void randomizer_people_tmp_reg_set(u32 reg,u8 value){randomizer_people_reg_write(randomizer_people_save()+0x1178,reg,value);}
static u8 randomizer_people_picture(void){return RPEOPLE(u8,randomizer_people_game(),0x5BE6);}
static u8 randomizer_people_picture_detail(void){return RPEOPLE(u8,randomizer_people_game(),0x5BE7);}
static u8 randomizer_people_pre_item(void){return RPEOPLE(u8,randomizer_people_game(),0x52B1);}
static u16 randomizer_people_rupees(void){return RPEOPLE(u16,randomizer_people_save(),0x24);}
static s32 randomizer_people_endless_night(void){return randomizer_people_event(0xA02)&&!randomizer_people_item(0x69);}
static f64 randomizer_people_home_distance(u32 actor){
 union { f32 values[3]; unsigned int words[3]; } temporary;
 unsigned int difference[3];randomizer_people_subtract(actor+0x314,(u32)temporary.values,actor+0x2EC);
 difference[0]=temporary.words[0];difference[2]=temporary.words[2];difference[1]=0;
 /* Native 022C82E0/83A0: square→sqrt→compare f1, with no FRSP. */
 return randomizer_people_sqrt(randomizer_people_square((u32)difference));
}
WWHD_REPLACE(0x022C6C68,u32,randomizer_people_message,(u32 actor)) {
    u32 msgNo = 0;
    RPEOPLE(u32,actor,0x8A8) = 0;

    /* HD: dEvt_control_c::chkPhoto is play + 0x52B2 (the play object is fetched twice) */
    u8 photo = RPEOPLE(u8,randomizer_people_game(),0x52B2);
    u32 play = randomizer_people_game();
    if (photo) {
        switch (RPEOPLE(u8,actor,0x905)) {
        case 2:
            if (randomizer_people_picture() == 1) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3A2C;
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2F1C;
            }
            break;
        case 6:
            if (randomizer_people_picture() != 5 && randomizer_people_picture() != 6 &&
                randomizer_people_picture() != 0x6D) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2F6C;
            } else if (!randomizer_people_color(actor)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2F74;
            } else if (!randomizer_people_event(0x6908)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C36A8;
                randomizer_people_event_on(0x2504);
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2F7C;
            }
            break;
        case 10:
            if (randomizer_people_picture() != 0x67) { /* HD: 0x67 (GameCube 4) */
                RPEOPLE(u32,actor,0x8A8) = 0x101C3758;
            } else if (randomizer_people_picture_detail() != 0) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3B38;
            } else if (!randomizer_people_color(actor)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3768;
            } else if (!randomizer_people_event(0x2220)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3778;
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3B44;
            }
            break;
        case 11:
            if (randomizer_people_picture() != 7) {
                if (randomizer_people_picture() == 8) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C301C;
                } else if (randomizer_people_picture() == 9) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3014;
                } else {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C300C;
                }
            } else if (!randomizer_people_color(actor)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3024;
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3B80;
            }
            break;
        case 16:
            RPEOPLE(u32,actor,0x8A8) = 0x101C30CC;
            break;
        }
    } else if ((u32)((u8)RPEOPLE(u8,play,0x52B0) - 1) <= 3) { /* dComIfGp_event_chkTalkXY() */
        u32 itemNo = randomizer_people_pre_item();
        switch (RPEOPLE(u8,actor,0x905)) {
        case 3:
        case 4:
            msgNo = 0x2C9D;
            break;
        case 5:
            msgNo = 0x2CF7;
            break;
        case 6:
            msgNo = 0x2D63;
            break;
        case 16:
            if (itemNo != 0x45) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C30CC;
            } else if (!randomizer_people_event(0x2620)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3978;
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3D00;
            }
            break;
        }
    } else {
        switch (RPEOPLE(u8,actor,0x905)) {
        case 0:
            if (!randomizer_people_item(0x30)) {
                if (!randomizer_people_event(0x2501)) {
                    randomizer_people_event_on(0x2501);
                    RPEOPLE(u32,actor,0x8A8) = 0x101C39F4;
                } else {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C2ECC;
                }
            } else if (!randomizer_people_event(0x1B40)) {
                randomizer_people_event_on(0x1B40);
                RPEOPLE(u32,actor,0x8A8) = 0x101C34E0;
            } else if (randomizer_people_all(actor)) {
                if (!randomizer_people_event(0x1B10)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3A00;
                    randomizer_people_event_on(0x1B10);
                } else {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C2ED4;
                }
            } else if (randomizer_people_one(actor)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3A14;
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3A20;
            }
            break;
        case 1:
            if (RPEOPLE(u8,actor,0x90E) & 0x1) {
                RPEOPLE(u8,actor,0x90E) = RPEOPLE(u8,actor,0x90E) & ~0x1;
                RPEOPLE(u32,actor,0x8A8) = 0x101C2EE4;
            } else if (randomizer_people_event(0x2D01)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2EEC;
            } else if (!randomizer_people_event(0x1B04)) {
                randomizer_people_event_on(0x1B04);
                RPEOPLE(u32,actor,0x8A8) = 0x101C34F0;
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2EDC;
            }
            break;
        case 2:
            if (RPEOPLE(u8,actor,0x907) == 3 || (RPEOPLE(u32,actor,0x8CC) & 0x8)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2F0C;
            } else if (RPEOPLE(u8,actor,0x907) == 4) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2F14;
            } else if (!randomizer_people_event(0x1E08)) {
                randomizer_people_event_on(0x1E08);
                RPEOPLE(u32,actor,0x8A8) = 0x101C3500;
            } else if (!(RPEOPLE(u32,actor,0x8CC) & 0x2)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2EF4;
            } else if (RPEOPLE(u32,actor,0x8CC) & 0x4) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2F04;
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2EFC;
            }
            break;
        case 5:
            if (!randomizer_people_event(0x1D20)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3688;
                randomizer_people_event_on(0x1D20);
            } else {
                u32 pShip = RPEOPLE(u32,randomizer_people_game(),0x5B3C);
                f32 temp=9999.9f;
                if (pShip != 0) {
                    f32 a[3],b[3];
                    f32 z=RPEOPLE(f32,actor,0x31C),x=RPEOPLE(f32,actor,0x314),y=RPEOPLE(f32,actor,0x318);
                    a[2]=z;a[0]=x;a[1]=y;
                    b[0]=RPEOPLE(f32,pShip,0x314);b[1]=RPEOPLE(f32,pShip,0x318);b[2]=RPEOPLE(f32,pShip,0x31C);
                    randomizer_people_distance_angle((u32)a,(u32)b,(u32)&temp,0);
                    if (!(temp > 500.0f)) {
                        RPEOPLE(u32,actor,0x8A8) = 0x101C2F4C;
                        break;
                    }
                }
                if (randomizer_people_item(0x22)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3698;
                } else {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3A6C;
                }
            }
            break;
        case 6:
            if (RPEOPLE(u32,actor,0x8CC) & 0x40) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3A84;
            } else if (!randomizer_people_event(0x1D10)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2F54;
                randomizer_people_event_on(0x1D10);
            } else if (randomizer_people_event(0x2504)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2F5C;
            } else if (!randomizer_people_item(0x23) && !randomizer_people_item(0x26)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2F64;
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3A78;
            }
            break;
        case 7:
            if (randomizer_people_endless_night()) {
                msgNo = 0x2DC9;
            } else if (!RPEOPLE(u8,actor,0x90F)) {
                if (!randomizer_people_event(0x1E20)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3A90;
                    randomizer_people_event_on(0x1E20);
                } else {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C2F84;
                }
            } else if (!randomizer_people_event(0x1E10)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3AB4;
                randomizer_people_event_on(0x1E10);
            } else if (randomizer_people_item(0x30)) {
                if (randomizer_people_item(0x2A)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3ACC;
                } else {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3AD8;
                }
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3AC0;
            }
            break;
        case 8:
            if (randomizer_people_reg(0xB907) < 2) {
                if (!randomizer_people_event(0x2101)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3AE4;
                    randomizer_people_event_on(0x2101);
                } else if (randomizer_people_reg(0xC407) < 6) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3AF0;
                } else if (randomizer_people_reg(0xC407) < 7) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3AFC;
                } else if (!randomizer_people_event(0x2240)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C36C8;
                } else if (!randomizer_people_event(0x2220)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3B08;
                } else {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C2FAC;
                    randomizer_people_reg_set(0xB907, 1);
                }
            } else if (randomizer_people_reg(0xB907) < 4) {
                if (!randomizer_people_event(0x2280)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C36D8;
                    randomizer_people_event_on(0x2280);
                    randomizer_people_reg_set(0xB907, 3);
                } else {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C2FB4;
                }
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2FBC;
            }
            break;
        case 9:
            if (randomizer_people_endless_night()) {
                msgNo = 0x2FBD;
            } else if (!randomizer_people_event(0x2104)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C36E8;
            } else if (!randomizer_people_event(0x2210)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3B14;
                randomizer_people_event_on(0x2210);
            } else if (!randomizer_people_event(0x1C02)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3718;
            } else if (!randomizer_people_event(0x1B20)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3728;
                randomizer_people_event_on(0x1B20);
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2FC4;
            }
            break;
        case 10:
            if (randomizer_people_reg(0xB907) < 2) {
                if (!randomizer_people_event(0x2208)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3738;
                    randomizer_people_event_on(0x2208);
                } else if (randomizer_people_reg(0xC407) < 7) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3748;
                } else if (randomizer_people_event(0x2220)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3B2C;
                    randomizer_people_reg_set(0xB907, 1);
                } else if (!randomizer_people_event(0x2240)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C2FCC;
                } else {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3B20;
                }
            } else if (randomizer_people_reg(0xB907) < 4) {
                if (!randomizer_people_event(0x2204)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C2FD4;
                    randomizer_people_event_on(0x2204);
                } else {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C2FDC;
                }
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C2FE4;
            }
            break;
        case 11:
            if (randomizer_people_endless_night()) {
                msgNo = 0x347B;
            } else if (!RPEOPLE(u8,actor,0x90F)) {
                (void)randomizer_people_item(0x78);
                if (!randomizer_people_event(0x2340)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3790;
                    randomizer_people_event_on(0x2340);
                } else if (!randomizer_people_event(0x2310)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3B50;
                } else if (randomizer_people_event(0x6910)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C2FFC;
                } else {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3B5C;
                }
            } else if (!randomizer_people_event(0x2320)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3B68;
                randomizer_people_event_on(0x2320);
            } else if (!randomizer_people_event(0x2308)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3B74;
            } else if (randomizer_people_event(0x6910) && randomizer_people_event(0x2280)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C37B0;
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3004;
            }
            break;
        case 12:
            if (randomizer_people_endless_night()) {
                msgNo = 0x2E82;
            } else if (!randomizer_people_event(0x2304)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3BB4;
                randomizer_people_event_on(0x2304);
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C303C;
            }
            break;
        case 13:
            if (!RPEOPLE(u8,actor,0x90F)) {
                if (!randomizer_people_event(0x2302)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3044;
                    randomizer_people_event_on(0x2302);
                } else if (randomizer_people_wind(actor) == 6) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C37C0;
                } else if (randomizer_people_wind(actor) == 2) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3BC8;
                } else {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3BD4;
                }
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C304C;
            }
            break;
        case 14:
            if (randomizer_people_endless_night()) {
                msgNo = 0x30E4;
            } else if (!RPEOPLE(u8,actor,0x90F)) {
                (void)randomizer_people_item(0x78);
                if (!randomizer_people_event(0x2301)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3BEC;
                    randomizer_people_event_on(0x2301);
                } else {
                    RPEOPLE(u32,actor,0x8A8) = RPEOPLE(u32,0x101C3850,randomizer_people_daiza(actor)*4);
                }
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3064;
            }
            break;
        case 15:
            if (randomizer_people_endless_night()) {
                msgNo = 0x321E;
            } else if (!RPEOPLE(u8,actor,0x90F)) {
                (void)randomizer_people_item(0x78);
                if (!randomizer_people_event(0x2480)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3C1C;
                    randomizer_people_event_on(0x2480);
                } else {
                    RPEOPLE(u32,actor,0x8A8) = RPEOPLE(u32,0x101C3920,randomizer_people_wind(actor)*4);
                }
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3074;
            }
            break;
        case 16:
            if (randomizer_people_endless_night()) {
                msgNo = 0x3299;
            } else if (!RPEOPLE(u8,actor,0x90F)) {
                if (RPEOPLE(u32,actor,0x8CC) & 0x80) {
                    RPEOPLE(u32,actor,0x8CC) = RPEOPLE(u32,actor,0x8CC) & ~0x80;
                    if (!randomizer_people_event(0x2680)) {
                        RPEOPLE(u32,actor,0x8A8) = 0x101C30BC;
                    } else if (randomizer_people_pig_timer(actor) == 0) {
                        RPEOPLE(u32,actor,0x8A8) = 0x101C30B4;
                    } else if (randomizer_people_tmp_reg(0xFE03) == 0) {
                        if (randomizer_people_pig_ok(actor)) {
                            RPEOPLE(u32,actor,0x8A8) = 0x101C3960;
                            randomizer_people_event_on(0x2A04);
                        } else if (!randomizer_people_temporary(0x0240)) {
                            u8 reg = randomizer_people_tmp_reg(0xFF03);
                            /* HD: the JUT_ASSERT's failure path skips the table read */
                            if (reg < 3) {
                                RPEOPLE(u32,actor,0x8A8) = RPEOPLE(u32,0x101C3CE8,reg*4);
                            } else {
                                // HD: the debug expression calls reg a_tempReg.
                                randomizer_people_assert(0x10020874,0x2113,0x10020888);
                            }
                            randomizer_people_temporary_on(0x0240);
                        } else {
                            RPEOPLE(u32,actor,0x8A8) = 0x101C3CF4;
                        }
                    } else {
                        u8 reg = randomizer_people_tmp_reg(0xFE03);
                        switch (reg) {
                        case 3:
                            RPEOPLE(u32,actor,0x8A8) = 0x101C30DC;
                            break;
                        case 2:
                            RPEOPLE(u32,actor,0x8A8) = 0x101C30E4;
                            break;
                        case 1:
                            RPEOPLE(u32,actor,0x8A8) = 0x101C3D3C;
                            randomizer_people_event_off(0x2680);
                            break;
                        }
                        reg -= 1;
                        randomizer_people_tmp_reg_set(0xFE03, reg);
                    }
                } else if (!randomizer_people_event(0x2440)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3C28;
                    randomizer_people_event_on(0x2440);
                } else if (randomizer_people_temporary(0x0280)) {
                    if (!randomizer_people_event(0x2680)) {
                        RPEOPLE(u32,actor,0x8A8) = 0x101C30BC;
                    } else if (randomizer_people_pig_timer(actor) == 0) {
                        RPEOPLE(u32,actor,0x8A8) = 0x101C30B4;
                    } else {
                        u8 reg = randomizer_people_tmp_reg(0xFF03);
                        if (reg < 4) {
                            RPEOPLE(u32,actor,0x8A8) = RPEOPLE(u32,0x101C3950,reg*4);
                        } else {
                            // HD: the debug expression calls reg a_tempReg.
                            randomizer_people_assert(0x10020874,0x2151,0x100208EC);
                        }
                    }
                } else if (randomizer_people_event(0x2680)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C308C;
                } else if ((u32)randomizer_people_rupees() > 80) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3094;
                } else if (!randomizer_people_event(0x2640)) {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3C58;
                } else {
                    RPEOPLE(u32,actor,0x8A8) = 0x101C3C64;
                }
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C30C4;
            }
            break;
        case 17:
            if (!(randomizer_people_home_distance(actor) < 200.0f)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C30FC;
            } else if (!randomizer_people_event(0x2880)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3D50;
                randomizer_people_event_on(0x2880);
            } else if (randomizer_people_event(0x2B04)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C30EC;
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C30F4;
            }
            break;
        case 18:
            if (!(randomizer_people_home_distance(actor) < 200.0f)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C310C;
            } else if (!randomizer_people_event(0x2B08)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3D5C;
                randomizer_people_event_on(0x2B08);
            } else if (randomizer_people_event(0x2B04)) {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3104;
            } else {
                RPEOPLE(u32,actor,0x8A8) = 0x101C3D68;
            }
            break;
        }
    }

    if (RPEOPLE(u32,actor,0x8A8)!=0) {
        msgNo = RPEOPLE(u32,RPEOPLE(u32,actor,0x8A8),0);
    }
    return msgNo;
}


#undef RPEOPLE
