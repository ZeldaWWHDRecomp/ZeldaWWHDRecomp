/* Shieldless hammer defense: fix_vanilla_bugs.asm (MIT).
 * Complete targeted consumers adapted from public CC0 ZeldaWWHDDecomp/wwhd
 * a874e1a9 d_a_player_main_03.cpp; all other input/animation branches retained.
 * Saved equipment is never forged. Non-target players delegate to native bodies. */
#pragma once
#ifndef R_SW
#define R_SW(type,address) (*(volatile type*)(address))
#endif
#ifndef R_SHIELD_GUARD
#define R_SHIELD_GUARD(self) (((s32(*)(u32))R_SW(u32,R_SW(u32,(self)+0xB4)+0x3C))(self))
#endif
WWHD_GAME_FUNC(0x024EDFCC,u8,shield_lockon_truth,(u32 attention));
static s32 shield_attention_lock(u32 attention) {
    return shield_lockon_truth(attention) || (R_SW(u32,attention+0x20)&0x20000000);
}
static s32 shield_abs(s32 value) {
    const u32 mask=(u32)(value>>31);
    return (s32)(((u32)value^mask)-mask);
}
static void shield_set_r_status(u8 status) {
    R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB5)=status;
}
static u8 shield_can_defend(u32 self) {
    return R_SW(u16,self+0x69B0)==0x33 ||
        R_SW(u8,WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x2F)!=255;
}
WWHD_GAME_FUNC(0x023D6A18,s32,shield_checkBowAnime,(u32 self));
WWHD_GAME_FUNC(0x023D6A5C,s32,shield_checkRopeAnime,(u32 self));
WWHD_GAME_FUNC(0x023D6ABC,s32,shield_checkUpperReadyThrowAnime,(u32 self));
WWHD_GAME_FUNC(0x023D794C,s32,shield_checkEquipAnime,(u32 self));
WWHD_GAME_FUNC(0x023D85E4,s32,shield_checkPhotoBoxItem,(u32 self, int item));
WWHD_GAME_FUNC(0x023D8600,s32,shield_checkBowItem,(u32 self, int item));
WWHD_GAME_FUNC(0x023DC6A4,s32,shield_resetActAnimeUpper,(u32 self, int upperIdx, f32 i_morf));
WWHD_GAME_FUNC(0x023DC7AC,void,shield_deleteEquipItem,(u32 self, s32 param_1));
WWHD_GAME_FUNC(0x023E2358,s32,shield_procScope_init,(u32 self, int param_1));
WWHD_GAME_FUNC(0x023E32AC,s32,shield_changeWaitProc,(u32 self));
WWHD_GAME_FUNC(0x023E4E18,int,shield_getDirectionFromShapeAngle,(u32 self));
WWHD_GAME_FUNC(0x023E5400,s32,shield_changeSpecialBattle,(u32 self));
WWHD_GAME_FUNC(0x023E6380,s32,shield_procCutTurn_init,(u32 self, s32 param_0));
WWHD_GAME_FUNC(0x023E73FC,int,shield_changeCutProc,(u32 self));
WWHD_GAME_FUNC(0x023E79DC,s32,shield_procFanSwing_init,(u32 self));
WWHD_GAME_FUNC(0x023E8984,s32,shield_checkNextBowMode,(u32 self));
WWHD_GAME_FUNC(0x023E8BD4,s32,shield_checkNextHookshotMode,(u32 self));
WWHD_GAME_FUNC(0x023E8F2C,s32,shield_checkNextBoomerangMode,(u32 self));
WWHD_GAME_FUNC(0x023E91E4,s32,shield_checkNextRopeMode,(u32 self));
WWHD_GAME_FUNC(0x023E92A0,s32,shield_procTactWait_init,(u32 self, int r30));
WWHD_GAME_FUNC(0x023EA1AC,s32,shield_checkNextActionGrab,(u32 self));
WWHD_GAME_FUNC(0x023EA680,void,shield_setDoStatusBasic,(u32 self));
WWHD_GAME_FUNC(0x023EAA80,int,shield_getReadyItem,(u32 self));
WWHD_GAME_FUNC(0x023EAB40,s32,shield_itemTrigger,(u32 self));
WWHD_GAME_FUNC(0x023EAD5C,s32,shield_checkNextActionHookshotReady,(u32 self));
WWHD_GAME_FUNC(0x023EB208,s32,shield_checkNextActionBowReady,(u32 self));
WWHD_GAME_FUNC(0x023EB510,s32,shield_checkNextActionBoomerangReady,(u32 self));
WWHD_GAME_FUNC(0x023EC084,s32,shield_checkNextActionRopeReady,(u32 self));
WWHD_GAME_FUNC(0x023EC498,void,shield_setDoStatus,(u32 self));
WWHD_GAME_FUNC(0x023EC724,int,shield_orderTalk,(u32 self));
WWHD_GAME_FUNC(0x023ECA94,s32,shield_procShipReady_init,(u32 self));
WWHD_GAME_FUNC(0x023ECF74,s32,shield_procPushPullWait_init,(u32 self, int param_0));
WWHD_GAME_FUNC(0x023ED0F0,s32,shield_procGrabReady_init,(u32 self));
WWHD_GAME_FUNC(0x023ED5A4,s32,shield_procJumpCut_init,(u32 self, int param_0));
WWHD_GAME_FUNC(0x023ED788,s32,shield_procWHideReady_init,(u32 self, u32 param_0, u32 param_1));
WWHD_GAME_FUNC(0x023ED970,s32,shield_procCutTurnCharge_init,(u32 self));
WWHD_GAME_FUNC(0x023ED9E0,s32,shield_procWeaponNormalSwing_init,(u32 self));
WWHD_GAME_FUNC(0x023EDB6C,s32,shield_procWeaponSideSwing_init,(u32 self));
WWHD_GAME_FUNC(0x023EDC14,s32,shield_procWeaponFrontSwingReady_init,(u32 self));
WWHD_GAME_FUNC(0x023EDD14,s32,shield_procWeaponThrow_init,(u32 self));
WWHD_GAME_FUNC(0x023EDD9C,s32,shield_procBottleSwing_init,(u32 self, int param_0));
WWHD_GAME_FUNC(0x023EDF70,s32,shield_procHammerSideSwing_init,(u32 self));
WWHD_GAME_FUNC(0x023EE068,s32,shield_procHammerFrontSwingReady_init,(u32 self));
WWHD_GAME_FUNC(0x023EE208,s32,shield_procCall_init,(u32 self));
WWHD_GAME_FUNC(0x023EE2B8,s32,shield_procCrouch_init,(u32 self));
WWHD_GAME_FUNC(0x023EE328,s32,shield_checkGuardAccept,(u32 self));
WWHD_GAME_FUNC(0x023EE510,s32,shield_procCrouchDefense_init,(u32 self));
WWHD_GAME_FUNC(0x023EE5B0,s32,shield_procSideStep_init,(u32 self, int dir));
WWHD_GAME_FUNC(0x023EE710,s32,shield_procBackJump_init,(u32 self));
WWHD_GAME_FUNC(0x023EE86C,s32,shield_procFrontRoll_init,(u32 self, f32 morf));
WWHD_GAME_FUNC(0x023EEA30,s32,shield_procSubjectivity_init,(u32 self, int crouch));
WWHD_GAME_FUNC(0x023EFB68,s32,shield_checkItemChangeFromButton,(u32 self));
WWHD_GAME_FUNC(0x024EBAE8,u32,shield_convCatchPId,(u32 object,u32 id));
WWHD_GAME_FUNC(0x025D7640,void,shield_orderZHintEvent,(u32 self,u32 actor));
WWHD_GAME_FUNC(0x025D7710,void,shield_orderDoorEvent,(u32 self,u32 actor));
WWHD_GAME_FUNC(0x025D7C08,void,shield_orderTreasureEvent,(u32 self,u32 actor));
WWHD_GAME_FUNC(0x025E3F3C,void,shield_initOldFrameMorf,(u32 object,f32 morph,s32 start,s32 end));

static s32 shield_check_buttons_body(u32 self) {

    if (R_SW(u32,self+0x3C0) & 0x80) {
        u16 eq = R_SW(u16,self+0x69B0);
        if (eq == 0x103) {
            shield_resetActAnimeUpper(self,2 , -1.0f);
            return shield_changeCutProc(self);
        }
        if (shield_checkPhotoBoxItem(self,eq) || eq == 0x20) {
            return shield_procScope_init(self,(u32)eq);
        }
        if (eq == 0x34) {
            return shield_procFanSwing_init(self);
        }
        if (shield_checkBowItem(self,eq)) {
            return shield_checkNextBowMode(self);
        }
        if (eq == 0x2F) {
            return shield_checkNextHookshotMode(self);
        }
        if (eq == 0x2D) {
            return shield_checkNextBoomerangMode(self);
        }
        if (eq == 0x25) {
            return shield_checkNextRopeMode(self);
        }
        if (eq == 0x22) {
            return shield_procTactWait_init(self,-1);
        }
    }
    u16 upper = R_SW(u16,self+0x5888);
    if (upper == 0x95 || upper == 0x96 ) {
        if (shield_checkNextActionGrab(self)) {
            return 1;
        }
        shield_setDoStatusBasic(self);
    } else if (upper == 0xA7 ) {
        if (shield_checkNextActionHookshotReady(self)) {
            return 1;
        }
        shield_setDoStatusBasic(self);
    } else if (shield_checkBowAnime(self)) {
        if (shield_checkNextActionBowReady(self)) {
            return 1;
        }
        shield_setDoStatusBasic(self);
    } else if (upper == 0x34 ) {
        return 1;
    } else if (upper == 0x35 ) {
        if (shield_checkNextActionBoomerangReady(self)) {
            return 1;
        }
        shield_setDoStatusBasic(self);
    } else if (shield_checkRopeAnime(self)) {
        if (shield_checkNextActionRopeReady(self)) {
            return 1;
        }
        shield_setDoStatusBasic(self);
    } else {
        shield_setDoStatus(self);

        if (shield_orderTalk(self)) {
            return 1;
        }
        if (R_SW(u8,self+0x690D) & 1 ) {
            u8 doStatus = R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB7);
            u32 play = wwhd_dComIfG_getGameInfo();
            if (doStatus == 0x31 ) {

                u32 zhint = shield_convCatchPId(play+0x5928,
                                                        R_SW(u32,play+0x5930));
                shield_orderZHintEvent(self, zhint);
                return 1;
            }
            if (R_SW(u8,play+0x5BB7) == 0x1C ) {
                return shield_procShipReady_init(self);
            }
            if (R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB7) == 0x11) {
                return shield_procPushPullWait_init(self,1);
            }

            if (R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB7) == 0xB ) {
                u32 entry = R_SW(u32,self+0x6898);
                u32 actor = R_SW(u32,self+0x65B8);
                if (R_SW(s32,entry+8)  == 5) {
                    shield_orderDoorEvent(self, actor);
                    shield_changeWaitProc(self);
                } else {
                    shield_orderTreasureEvent(self, actor);
                }
                return 1;
            }
            if (R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB7) == 4  ||
                R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB7) == 0x1B ) {
                return shield_procGrabReady_init(self);
            }
            if (R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB7) == 0x51 ) {
                return shield_procJumpCut_init(self,0);
            }
            if (R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB7) == 0x10 ) {
                return shield_procWHideReady_init(self,0, self+0x732C);
            }
        }
    }
    if (!shield_checkEquipAnime(self) && !R_SHIELD_GUARD(self)) {
        if (!(WWHD_GAME_DATA(0x101CEF16,u16)==1)) {
            u16 eq = R_SW(u16,self+0x69B0);
            if (eq == 0x103) {
                if (R_SW(u32,self+0x3B8) & 0x4) {
                    return shield_procCutTurnCharge_init(self);
                }
                if (R_SW(u8,self+0x690D) & 2 ) {
                    if (shield_abs(R_SW(s32,self+0x69C8)) > 0xF800) {
                        return shield_procCutTurn_init(self,1);
                    }
                    return shield_changeCutProc(self);
                }
                if (R_SW(u8,self+0x68E1) != 0) {
                    return shield_changeCutProc(self);
                }
            } else if (eq == 0x101) {
                if (R_SW(u32,self+0x3B8) & 4) {
                    return shield_procCutTurnCharge_init(self);
                }

                if (R_SW(u8,self+0x690D) & 1) {
                    u32 weapon = R_SW(u32,self+0x6594);
                    if (weapon != 0) {
                        if (shield_abs(R_SW(s32,self+0x69C8)) > 0xF800) {
                            return shield_procCutTurn_init(self,1);
                        }
                        u32 prm = R_SW(u32,weapon+0xB0);
                        if (prm == 0 || prm == 1) {
                            return shield_procWeaponNormalSwing_init(self);
                        } else if (prm == 4) {
                            return shield_procWeaponSideSwing_init(self);
                        } else {
                            return shield_procWeaponFrontSwingReady_init(self);
                        }
                    }
                    if (R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB7) == 0xE ) {
                        return shield_procWeaponThrow_init(self);
                    }
                }
                if ((R_SW(u8,self+0x690D) & 2) && R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB6) == 0xE) {
                    return shield_procWeaponThrow_init(self);
                }
            }
            u32 ready;
            if (wwhd_item_ready_trigger(self,(u32)shield_itemTrigger(self)) && (ready = shield_getReadyItem(self), R_SW(u16,self+0x69B0) == ready)) {
                eq = R_SW(u16,self+0x69B0);
                if (eq == 0x2F) {
                    if (R_SW(u16,self+0x5888) != 0xA7) {
                        wwhd_item_action_dispatch(self,0x2F);
                        return shield_checkNextHookshotMode(self);
                    }
                } else if (eq == 0x25) {
                    if (!shield_checkRopeAnime(self)) {
                        wwhd_item_action_dispatch(self,0x25);
                        return shield_checkNextRopeMode(self);
                    }
                } else {
                    if (eq == 0x2D) {
                        wwhd_item_action_dispatch(self,0x2D);
                        return shield_checkNextBoomerangMode(self);
                    }
                    if (shield_checkBowItem(self,eq)) {
                        return shield_checkNextBowMode(self);
                    }
                    if (eq == 0x34) {
                        return shield_procFanSwing_init(self);
                    }
                    if (eq == 0x50) {
                        return shield_procBottleSwing_init(self,0);
                    }
                    if (eq == 0x33) {
                        int dir = shield_getDirectionFromShapeAngle(self);
                        if (R_SW(f32,self+0x6A08) > 0.05f &&
                            (dir == 2  || dir == 3 )) {
                            return shield_procHammerSideSwing_init(self);
                        }
                        return shield_procHammerFrontSwingReady_init(self);
                    }
                    if (shield_checkPhotoBoxItem(self,eq) || eq == 0x20) {
                        return shield_procScope_init(self,(u32)eq);
                    }
                    if (eq == 0x22) {
                        return shield_procTactWait_init(self,-1);
                    }
                }
            }
        }
        if (shield_changeSpecialBattle(self)) {
            return 1;
        }
    }
    if (R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB5) == 0  && !shield_checkUpperReadyThrowAnime(self) &&
        !shield_checkEquipAnime(self)) {
        u16 up = R_SW(u16,self+0x5888);
        if (up != 0x95 && up != 0x96 ) {
            if (R_SW(u32,self+0x3BC) & 0x2) {
                shield_set_r_status(0x24 );
                if (R_SW(u8,self+0x690D) & 0x40 ) {
                    return shield_procCall_init(self);
                }
            } else if (R_SW(u16,self+0x69B0) == 0x101) {
                shield_set_r_status(9 );
                if (R_SW(u8,self+0x690D) & 0x40) {
                    shield_deleteEquipItem(self,0);
                    shield_initOldFrameMorf((u32)R_SW(u32,self+0x65CC), 5.0f, 0, 0x2A);
                    return 1;
                }
            } else if ((!shield_can_defend(self)) ||
                       R_SW(u16,self+0x69B0) == 0x100) {
                if (R_SW(u32,self+0x6A70) & 1) {
                    shield_set_r_status(0xF );
                    if (R_SW(u8,self+0x690E) & 0x40 ) {
                        return shield_procCrouch_init(self);
                    }
                }
            } else {
                if (shield_checkGuardAccept(self)) {
                    shield_set_r_status(0x36 );
                    if (R_SW(u8,self+0x690E) & 0x40) {
                        return shield_procCrouchDefense_init(self);
                    }
                }
            }
        }
    }
    if (R_SW(u8,self+0x690D) & 1) {
        if (R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB7) == 0x12 ) {
            int dir = shield_getDirectionFromShapeAngle(self);
            if (dir == 2 || dir == 3) {
                return shield_procSideStep_init(self,dir);
            }
            if (dir == 1 ) {
                return shield_procBackJump_init(self);
            }
        } else if (R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB7) == 0xC ) {
            if (!shield_attention_lock(R_SW(u32,self+0x6894)) && R_SW(f32,self+0x6A08) > 0.05f) {
                R_SW(s16,self+0x32A) = R_SW(s16,self+0x6930);
            }
            return shield_procFrontRoll_init(self,0.0f);
        }
    }
    if (R_SW(u32,wwhd_dComIfG_getGameInfo()+0x5B2C) ==
            self
        && !(R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5292)!=0) && !(R_SW(f32,self+0x3CC) < 0.0f) ) {
        R_SW(u32,self+0x3C0) = R_SW(u32,self+0x3C0) | 0x4000000;
        s32 camIdx = R_SW(s32,self+0x69BC);
        if (R_SW(u32,wwhd_dComIfG_getGameInfo()+0x5B00+camIdx*0x34) & 0x1000) {
            return shield_procSubjectivity_init(self,0);
        }
    }
    return shield_checkItemChangeFromButton(self);
}

WWHD_GAME_FUNC(0x023DCF8C,void,shield_free_grab,(u32 self));
WWHD_GAME_FUNC(0x023DE7E8,s32,shield_set_upper,(u32 self,u32 animation,s32 part,f32 rate,f32 start,s32 end,f32 morph));
WWHD_GAME_FUNC(0x0200F5C8,s32,shield_chase,(f32* value,f32 target,f32 step));
WWHD_GAME_FUNC(0x027F3F94,u32,shield_joint_tree,(u32 model_data));
WWHD_GAME_FUNC(0x027E0174,s32,shield_joint_count,(u32 resource,s32 argument));
static void shield_set_joint_ratio(u32 blend,u32 index,f32 value) {
    const u32 model=R_SW(u32,blend+0x80);
    const u32 resource=shield_joint_tree(R_SW(u32,model+0xAC));
    const s32 count=shield_joint_count(resource,0);
    for(u32 i=0;count>0 && i<(u32)count;i++) {
        // Native code reloads mutable blend-data/weights for every store.
        const u32 data=R_SW(u32,blend+0x7C);
        const u32 weights=R_SW(u32,data+0xC+index*0x10);
        R_SW(f32,weights+i*4)=value;
    }
}
static void shield_guard_body(u32 self) {
    const u32 blend=R_SW(u32,self+0x57F4);
    const u32 data=R_SW(u32,blend+0x7C);
    f32 ratio=R_SW(f32,R_SW(u32,data+0x2C));
    s32 proc=R_SW(s32,self+0x65F0);
    u8 defend;
    if(proc==0x6D || proc==0xD) defend=1;
    else if(shield_checkEquipAnime(self) || R_SW(u16,self+0x5888)==0x95 || R_SW(u16,self+0x5888)==0x96) defend=0;
    else {
        defend=0;
        if(shield_attention_lock(R_SW(u32,self+0x6894))) {
            const u16 item=R_SW(u16,self+0x69B0);
            if(item!=0x101 && shield_can_defend(self)) {
                if(shield_checkGuardAccept(self) &&
                   (R_SW(u32,self+0x6590)==0xFFFFFFFF || R_SW(u32,self+0x6594))) defend=1;
            }
        }
        proc=R_SW(s32,self+0x65F0);
    }
    if(defend) {
        shield_set_r_status(0x36);
        proc=R_SW(s32,self+0x65F0);
    }
    const u16 animation=R_SW(u16,self+0x69B0)==0x33?0x1B:0x16;
    if(proc!=0x6D && proc!=0xD) {
        if((R_SW(u8,self+0x690E)&0x40) && R_SW(u8,wwhd_dComIfG_getGameInfo()+0x5BB5)==0x36) {
            shield_free_grab(self);
            if(R_SW(u16,self+0x5888)!=animation) {
                shield_resetActAnimeUpper(self,2,-1.0f);
                shield_set_upper(self,animation,2,1.25f,0.0f,-1,2.4f);
            } else if(ratio<1.0f) {
                shield_chase(&ratio,1.0f,0.41666666f);
                shield_set_joint_ratio(R_SW(u32,self+0x57F4),2,ratio);
            }
            const u16 under=R_SW(u16,self+0x5848);
            if(under==0x22 || under==0x24) {
                const u32 frame=R_SW(u32,self+0x5818+0x20+4);
                R_SW(f32,frame)=R_SW(f32,self+0x5898+4);
            }
        } else if(R_SW(u16,self+0x5888)==0x16 || R_SW(u16,self+0x5888)==0x1B) {
            shield_chase(&ratio,0.0f,0.41666666f);
            if(!(ratio>0.0f)) shield_resetActAnimeUpper(self,2,2.4f);
            else shield_set_joint_ratio(R_SW(u32,self+0x57F4),2,ratio);
        }
    }
    if(R_SHIELD_GUARD(self)) R_SW(u32,self+0x765C+0x94)|=1;
    else R_SW(u32,self+0x765C+0x94)&=~1u;
}
WWHD_GAME_ORIGINAL(0x023EFF20,s32,shield_native_buttons,(u32 self));
WWHD_GAME_ORIGINAL(0x023FB8A0,void,shield_native_guard,(u32 self));
static u8 shieldless_hammer_target(u32 self) {
    return R_SW(u16,self+0x69B0)==0x33 &&
        R_SW(u8,WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x2F)==255;
}
WWHD_REPLACE(0x023EFF20,s32,shieldless_hammer_buttons,(u32 self)) {
    if(!shieldless_hammer_target(self)) return shield_native_buttons(self);
    return shield_check_buttons_body(self);
}
WWHD_REPLACE(0x023FB8A0,void,shieldless_hammer_guard,(u32 self)) {
    if(!shieldless_hammer_target(self)) {shield_native_guard(self);return;}
    shield_guard_body(self);
}
