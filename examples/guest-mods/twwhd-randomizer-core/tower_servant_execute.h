/* CC0 public d_a_npc_os_a.cpp::execute (a874e1a9), with the MIT
 * make_game_nonlinear.asm inactive-room change at 022AC2B8 only. */
#pragma once
#define OS_E(t,a,o) (*(volatile t*)((u32)(a)+(o)))
WWHD_GAME_FUNC(0x024451B4,s32,Os_initialRoomCheckHD,(u32 actor));
WWHD_GAME_FUNC(0x0244586C,void,Os_initialRestartHD,(u32 actor, s32 restart, s32 flag));
WWHD_GAME_FUNC(0x024EFF3C,void,Os_setWallRadiusHD,(u32 circle, f32 radius));
WWHD_GAME_FUNC(0x025D9F38,u32,Os_searchPedestalHD,(u32 name, s32 room, s32 flag));
WWHD_GAME_FUNC(0x025DD868,s32,Os_isCreatingHD,(u32 processId));
WWHD_GAME_FUNC(0x024452A0,s32,Os_moveStopCheckHD,(u32 actor));
WWHD_GAME_FUNC(0x02008254,s32,Os_polygonSafeHD,(u32 background, u32 polygon));
WWHD_GAME_FUNC(0x024EEABC,s32,Os_isMovingBackgroundHD,(u32 background, u32 polygon));
WWHD_GAME_FUNC(0x024EF968,void,Os_correctMovingBackgroundHD,(u32 background, u32 polygon, s32 flag, u32 pos, s32 angle, s32 extra));
WWHD_GAME_FUNC(0x02445950,s32,Os_positionMoveCheckHD,(u32 actor, u32 name));
WWHD_GAME_FUNC(0x025D6800,void,Os_movePositionHD,(u32 actor, u32 status));
WWHD_GAME_FUNC(0x024EF130,s32,Os_groundRoomHD,(u32 background, u32 polygon));
WWHD_GAME_FUNC(0x024EEEB8,u32,Os_groundColorHD,(u32 background, u32 polygon));
WWHD_GAME_FUNC(0x024EF0BC,s32,Os_groundCodeHD,(u32 background, u32 polygon));
WWHD_GAME_FUNC(0x020084C8,u32,Os_trianglePlaneHD,(u32 background, u32 backgroundIndex, u32 polygonIndex));
WWHD_GAME_FUNC(0x023FD4E4,void,Os_restartLinkHD,(u32 player, s32 mode, s32 room, f32 speed, s32 flag));
WWHD_GAME_FUNC(0x0259E08C,void,os_exec_joint,(u32 joint,s32 backx,s32 backy,s32 minbackx,s32 minbacky,s32 headx,s32 heady,s32 minheadx,s32 minheady,s32 step));
WWHD_GAME_FUNC(0x022AC030,void,os_exec_event_check,(u32 actor));
WWHD_GAME_FUNC(0x025200D4,u32,os_exec_info,(void));
WWHD_GAME_FUNC(0x024F08A8,void,os_exec_crr,(u32 acch,u32 background));
WWHD_GAME_FUNC(0x025D537C,void,os_exec_stage_layer,(u32 actor));
WWHD_GAME_FUNC(0x022AB1C4,void,os_exec_check_player,(u32 actor));
WWHD_GAME_FUNC(0x022A9EAC,s32,os_exec_finish,(u32 actor));
WWHD_GAME_FUNC(0x022AB28C,void,os_exec_end_beam,(u32 actor));
WWHD_GAME_FUNC(0x022AA42C,s8,os_exec_restart_number,(u32 actor));
WWHD_GAME_FUNC(0x022A9E0C,s32,os_exec_wakeup,(u32 actor));
WWHD_GAME_FUNC(0x022AA778,void,os_exec_set_npc,(u32 actor,u32 pmf,u32 parameter));
WWHD_GAME_FUNC(0x022AB6DC,s32,os_exec_event_proc,(u32 actor));
WWHD_GAME_FUNC(0x022AB2BC,void,os_exec_collision,(u32 actor));
WWHD_GAME_FUNC(0x022AB488,void,os_exec_animation,(u32 actor));
WWHD_GAME_FUNC(0x022ABBF8,s32,os_exec_return_link,(u32 actor));
WWHD_GAME_FUNC(0x022ABC68,void,os_exec_carry,(u32 actor));
WWHD_GAME_FUNC(0x022ABCB0,void,os_exec_order_check,(u32 actor));
WWHD_GAME_FUNC(0x022ABE84,s32,os_exec_goal,(u32 actor));
WWHD_GAME_FUNC(0x022AC160,void,os_exec_event_order,(u32 actor));
WWHD_GAME_FUNC(0x022AA464,void,os_exec_matrix,(u32 actor));
WWHD_GAME_FUNC(0x022ABB18,void,os_exec_player_action,(u32 actor,u32 parameter));
WWHD_GAME_FUNC(0x022ABD30,void,os_exec_npc_action,(u32 actor,u32 parameter));
WWHD_GAME_FUNC(0x022ABF7C,void,os_exec_make_beam,(u32 actor,s32 parameter));
WWHD_GAME_FUNC(0x02520540,s32,dComIfGp_getReverb,(s32 room));
WWHD_GAME_FUNC(0x025E1A40,void,mDoAud_seStart,(u32 sound,u32 position,u32 parameter,s32 reverb));
WWHD_GAME_FUNC(0x025A847C,u32,dPa_control_set,(u32 particle,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 setup,u32 primary,u32 environment,u32 scale2d));

static s32 os_exec_pmf(u32 p,u32 function) {
 return OS_E(s16,p,2)==-1 && OS_E(s16,p,0)==0 && OS_E(u32,p,4)==function;
}
static void os_exec_copy_vector(u32 dst,u32 src) {
 OS_E(f32,dst,0)=OS_E(f32,src,0);
 OS_E(f32,dst,4)=OS_E(f32,src,4);
 OS_E(f32,dst,8)=OS_E(f32,src,8);
}
static f32 os_exec_sine(u32 angle){return OS_E(f32,0x104A44F8,(angle&0xFFF8));}
static f32 os_exec_cosine(u32 angle){return OS_E(f32,0x104A44FC,(angle&0xFFF8));}
static void os_setEmitterScale(u32 emitter,u32 scale) {
 f32 x=OS_E(f32,scale,0);OS_E(f32,emitter,0x220)=x;
 f32 y=OS_E(f32,scale,4);OS_E(f32,emitter,0x224)=y;
 f32 z=OS_E(f32,scale,8);OS_E(f32,emitter,0x238)=x;
 OS_E(f32,emitter,0x228)=z;OS_E(f32,emitter,0x23C)=y;OS_E(f32,emitter,0x240)=z;
}
static void os_exec_lighting(u32 actor) {
 OS_E(f32,0x10475460,0x0)=OS_E(f32,actor,0x110);
 OS_E(f32,0x10475460,0x4)=OS_E(f32,actor,0x114);
 OS_E(f32,0x10475460,0x8)=OS_E(f32,actor,0x118);
 OS_E(f32,0x10475460,0xC)=OS_E(f32,actor,0x11C);
 OS_E(f32,0x10475460,0x10)=OS_E(f32,actor,0x120);
 OS_E(f32,0x10475460,0x14)=OS_E(f32,actor,0x124);
 OS_E(u8,0x10475460,0x18)=OS_E(u8,actor,0x128);
 OS_E(u8,0x10475460,0x19)=OS_E(u8,actor,0x129);
 OS_E(u8,0x10475460,0x1A)=OS_E(u8,actor,0x12A);
 OS_E(u8,0x10475460,0x1B)=OS_E(u8,actor,0x12B);
 OS_E(u16,0x10475460,0x1C)=OS_E(u16,actor,0x12C);
 OS_E(u16,0x10475460,0x1E)=OS_E(u16,actor,0x12E);
 OS_E(u16,0x10475460,0x20)=OS_E(u16,actor,0x130);
 OS_E(u16,0x10475460,0x22)=OS_E(u16,actor,0x132);
 OS_E(f32,0x10475460,0x24)=OS_E(f32,actor,0x134);
 OS_E(f32,0x10475460,0x28)=OS_E(f32,actor,0x138);
 OS_E(f32,0x10475460,0x2C)=OS_E(f32,actor,0x13C);
 OS_E(f32,0x10475460,0x30)=OS_E(f32,actor,0x140);
 OS_E(f32,0x10475460,0x34)=OS_E(f32,actor,0x144);
 OS_E(f32,0x10475460,0x38)=OS_E(f32,actor,0x148);
 OS_E(f32,0x10475460,0x3C)=OS_E(f32,actor,0x14C);
 OS_E(f32,0x10475460,0x40)=OS_E(f32,actor,0x150);
 OS_E(u32,0x10475460,0x84)=OS_E(u32,actor,0x194);
 OS_E(u32,0x10475460,0x88)=OS_E(u32,actor,0x198);
 OS_E(u32,0x10475460,0x8C)=OS_E(u32,actor,0x19C);
 OS_E(u16,0x10475460,0x90)=OS_E(u16,actor,0x1A0);
 OS_E(u16,0x10475460,0x92)=OS_E(u16,actor,0x1A2);
 OS_E(u16,0x10475460,0x94)=OS_E(u16,actor,0x1A4);
 OS_E(u16,0x10475460,0x96)=OS_E(u16,actor,0x1A6);
 OS_E(u32,0x10475460,0x98)=OS_E(u32,actor,0x1A8);
 OS_E(u32,0x10475460,0x9C)=OS_E(u32,actor,0x1AC);
 OS_E(u16,0x10475460,0xA0)=OS_E(u16,actor,0x1B0);
 OS_E(u16,0x10475460,0xA2)=OS_E(u16,actor,0x1B2);
 OS_E(u16,0x10475460,0xA4)=OS_E(u16,actor,0x1B4);
 OS_E(u16,0x10475460,0xA6)=OS_E(u16,actor,0x1B6);
 OS_E(f32,0x10475460,0xA8)=OS_E(f32,actor,0x1B8);
 OS_E(f32,0x10475460,0xAC)=OS_E(f32,actor,0x1BC);
 OS_E(f32,0x10475460,0xB0)=OS_E(f32,actor,0x1C0);
 OS_E(u8,0x10475460,0xB4)=OS_E(u8,actor,0x1C4);
 OS_E(u8,0x10475460,0xB5)=OS_E(u8,actor,0x1C5);
 OS_E(u8,0x10475460,0xB6)=OS_E(u8,actor,0x1C6);
 OS_E(u8,0x10475460,0xB7)=OS_E(u8,actor,0x1C7);
 OS_E(u8,0x10475460,0xB8)=OS_E(u8,actor,0x1C8);
 OS_E(u8,0x10475460,0xB9)=OS_E(u8,actor,0x1C9);
 OS_E(u8,0x10475460,0xBA)=OS_E(u8,actor,0x1CA);
 OS_E(u8,0x10475460,0xBB)=OS_E(u8,actor,0x1CB);
 OS_E(u8,0x10475460,0xBC)=OS_E(u8,actor,0x1CC);
 OS_E(f32,0x10475460,0xC0)=OS_E(f32,actor,0x1D0);
 OS_E(f32,0x10475460,0xC4)=OS_E(f32,actor,0x1D4);
 OS_E(f32,0x10475460,0xC8)=OS_E(f32,actor,0x1D8);
 OS_E(f32,0x10475460,0xCC)=OS_E(f32,actor,0x1DC);
 OS_E(f32,0x10475460,0xD0)=OS_E(f32,actor,0x1E0);
 OS_E(f32,0x10475460,0xD4)=OS_E(f32,actor,0x1E4);
 OS_E(u8,0x10475460,0xD8)=OS_E(u8,actor,0x1E8);
 OS_E(u8,0x10475460,0xD9)=OS_E(u8,actor,0x1E9);
 OS_E(u8,0x10475460,0xDA)=OS_E(u8,actor,0x1EA);
 OS_E(u8,0x10475460,0xDB)=OS_E(u8,actor,0x1EB);
 OS_E(u16,0x10475460,0xDC)=OS_E(u16,actor,0x1EC);
 OS_E(u16,0x10475460,0xDE)=OS_E(u16,actor,0x1EE);
 OS_E(u16,0x10475460,0xE0)=OS_E(u16,actor,0x1F0);
 OS_E(u16,0x10475460,0xE2)=OS_E(u16,actor,0x1F2);
 OS_E(f32,0x10475460,0xE4)=OS_E(f32,actor,0x1F4);
 OS_E(f32,0x10475460,0xE8)=OS_E(f32,actor,0x1F8);
 OS_E(f32,0x10475460,0xEC)=OS_E(f32,actor,0x1FC);
 OS_E(f32,0x10475460,0xF0)=OS_E(f32,actor,0x200);
 OS_E(f32,0x10475460,0xF4)=OS_E(f32,actor,0x204);
 OS_E(f32,0x10475460,0xF8)=OS_E(f32,actor,0x208);
 OS_E(f32,0x10475460,0xFC)=OS_E(f32,actor,0x20C);
 OS_E(f32,0x10475460,0x100)=OS_E(f32,actor,0x210);
 OS_E(f32,0x10475460,0x144)=OS_E(f32,actor,0x254);
 OS_E(f32,0x10475460,0x148)=OS_E(f32,actor,0x258);
 OS_E(f32,0x10475460,0x14C)=OS_E(f32,actor,0x25C);
 OS_E(f32,0x10475460,0x150)=OS_E(f32,actor,0x260);
 OS_E(f32,0x10475460,0x154)=OS_E(f32,actor,0x264);
 OS_E(f32,0x10475460,0x158)=OS_E(f32,actor,0x268);
 OS_E(u8,0x10475460,0x15C)=OS_E(u8,actor,0x26C);
 OS_E(u8,0x10475460,0x15D)=OS_E(u8,actor,0x26D);
 OS_E(u8,0x10475460,0x15E)=OS_E(u8,actor,0x26E);
 OS_E(u8,0x10475460,0x15F)=OS_E(u8,actor,0x26F);
 OS_E(u16,0x10475460,0x160)=OS_E(u16,actor,0x270);
 OS_E(u16,0x10475460,0x162)=OS_E(u16,actor,0x272);
 OS_E(u16,0x10475460,0x164)=OS_E(u16,actor,0x274);
 OS_E(u16,0x10475460,0x166)=OS_E(u16,actor,0x276);
 OS_E(f32,0x10475460,0x168)=OS_E(f32,actor,0x278);
 OS_E(f32,0x10475460,0x16C)=OS_E(f32,actor,0x27C);
 OS_E(f32,0x10475460,0x170)=OS_E(f32,actor,0x280);
 OS_E(f32,0x10475460,0x174)=OS_E(f32,actor,0x284);
 OS_E(f32,0x10475460,0x178)=OS_E(f32,actor,0x288);
 OS_E(f32,0x10475460,0x17C)=OS_E(f32,actor,0x28C);
 OS_E(f32,0x10475460,0x180)=OS_E(f32,actor,0x290);
 OS_E(f32,0x10475460,0x184)=OS_E(f32,actor,0x294);
}
WWHD_REPLACE(0x022AC1DC,s32,randomizer_servant_execute,(u32 actor)) {
    /* static JGeometry::TVec3<f32> splash_scale(0.6f, ...), ripple_scale(1.0f, ...) */
    if ((u32)OS_E(u32,0x10467F00,0) == 0) {
        OS_E(u32,0x10467F00,0) = 1;
        OS_E(f32,0x10467F00,0x8) = 0.6f;
        OS_E(f32,0x10467F00,0x10) = 0.6f;
        OS_E(f32,0x10467F00,0xC) = 0.6f;
    }
    if ((u32)OS_E(u32,0x10467F00,4) == 0) {
        OS_E(u32,0x10467F00,4) = 1;
        OS_E(f32,0x10467F00,0x14) = 1.0f;
        OS_E(f32,0x10467F00,0x1C) = 1.0f;
        OS_E(f32,0x10467F00,0x18) = 1.0f;
    }
    OS_E(u32,actor,0x8F4) = OS_E(u32,actor,0x8F4) & ~0x10u;
    OS_E(u32,actor,0x2E0) = OS_E(u32,actor,0x2E0) & ~0x20;
    os_exec_check_player(actor);
    if (!os_exec_finish(actor)) {
        /* Upstream 022AC2B8: unsigned room bytes, before the native initial-room check. */
        if(OS_E(u8,actor,0x326)!=WWHD_GAME_DATA(0x1047E6C8,u8))
            OS_E(u8,actor,0x2FE)=0xFF;
        if (!Os_initialRoomCheckHD(actor)) {
            OS_E(s8,actor,0x326) = (s8)Os_groundRoomHD((os_exec_info()+0x12A0), actor+0x524); /* HD: GetRoomId(m_gnd) */
            if (OS_E(u32,os_exec_info(),0x5B38) == actor) {
                (OS_E(u32,os_exec_info(),0x5B38)=0);
            }
            os_exec_end_beam(actor);
            return 1;
        }
        Os_initialRestartHD(actor, (s32)os_exec_restart_number(actor), 1);
        if (os_exec_wakeup(actor)) {
            OS_E(u32,actor,0x3BC) = OS_E(u32,actor,0x3BC) & ~0x40u;
        } else {
            OS_E(u32,actor,0x3BC) = OS_E(u32,actor,0x3BC) | 0x40u;
        }
    }
    os_exec_stage_layer(actor);

    f32 r15 = 15.0f;
    if (os_exec_pmf(actor+0x8E8, 0x022AECD4)) {
        Os_setWallRadiusHD(actor+0x690, r15);
        Os_setWallRadiusHD(actor+0x6D0, r15);
    } else {
        Os_setWallRadiusHD(actor+0x690, 40.0f);
        Os_setWallRadiusHD(actor+0x6D0, 40.0f);
    }

    u32 ped = Os_searchPedestalHD(OS_E(u32,0x101C28F0,4*(s8)OS_E(s8,actor,0x2DD)), 0xFF, 1);
    if (ped != 0) {
        u32 id = ped != 0 ? (u32)OS_E(u32,ped,0x04) : 0xFFFFFFFFu;
        if (Os_isCreatingHD(id)) {
            ped = 0;
        }
    }
    OS_E(u32,actor,0x614) = ped;

    if (ped != 0) {
        if (!(OS_E(u32,actor,0x8F4) & 1) /* !isFinish() */) {
            if (os_exec_finish(actor)) {
                u32 p = OS_E(u32,actor,0x614);
                OS_E(f32,actor,0x2EC) = OS_E(f32,p,0x314);
                OS_E(f32,actor,0x2F0) = OS_E(f32,p,0x318) + 240.0f;
                OS_E(f32,actor,0x2F4) = OS_E(f32,p,0x31C);
            }
        } else if (!(OS_E(u32,actor,0x8F4) & 2) /* !isSetHomePos() */) {
            u32 p = OS_E(u32,actor,0x614);
            OS_E(f32,actor,0x2EC) = OS_E(f32,ped,0x314);
            OS_E(f32,actor,0x2F0) = OS_E(f32,p,0x318) + 240.0f;
            OS_E(f32,actor,0x2F4) = OS_E(f32,p,0x31C);
            s8 pedRoom = OS_E(s8,p,0x2FE);
            if (OS_E(s8,actor,0x2FE) != pedRoom) {
                OS_E(s8,actor,0x2FE) = pedRoom;
            }
            if (OS_E(s8,actor,0x326) != pedRoom) {
                OS_E(s8,actor,0x326) = pedRoom;
            }
            os_exec_crr(actor+0x43C,os_exec_info()+0x12A0);
            OS_E(u32,actor,0x8F4) = OS_E(u32,actor,0x8F4) | 0x10;
            if (OS_E(f32,actor,0x4D0) != -1000000000.0f) {
                OS_E(u8,actor,0x1C9) = (u8)Os_groundRoomHD((os_exec_info()+0x12A0), actor+0x524); /* OS_E(u8,actor,0x1C9) */
                u8 color = (u8)Os_groundColorHD((os_exec_info()+0x12A0), actor+0x524);
                u32 f = OS_E(u32,actor,0x8F4);
                OS_E(u8,actor,0x1CA) = color; /* OS_E(u8,actor,0x1CA) */
                OS_E(u32,actor,0x8F4) = f | 2;
            }
        }
    }

    if (os_exec_finish(actor)) {
        u32 f = OS_E(u32,actor,0x8F4);
        s8 roomNo = OS_E(s8,actor,0x326);
        if (f & 1) {
            os_exec_copy_vector(actor+0x314,actor+0x2EC);
        }
        os_exec_info();
        u8 st = OS_E(u8,0x1047E8E8,0x22C*(s8)roomNo);
        if (roomNo < 0 || !(st & 0x10) || OS_E(u32,actor,0x614) == 0) {
            os_exec_end_beam(actor);
            return 1;
        }
    } else {
        if (Os_moveStopCheckHD(actor)) {
            u32 fn[2];
            fn[0]=WWHD_GAME_DATA(0x1001F260,u32);
            fn[1]=WWHD_GAME_DATA(0x1001F264,u32);
            os_exec_set_npc(actor,(u32)fn,0);
            OS_E(u8,actor,0x913) = 0;
            os_exec_end_beam(actor);
            return 1;
        }
        if (OS_E(u8,actor,0x918) && Os_polygonSafeHD((os_exec_info()+0x12A0), actor+0x96C) &&
            Os_isMovingBackgroundHD((os_exec_info()+0x12A0), actor+0x96C)) {
            Os_correctMovingBackgroundHD((os_exec_info()+0x12A0), actor+0x96C, 1, actor+0x300, 0, 0);
        }
    }

    OS_E(u32,actor,0x2E0) = (OS_E(u32,actor,0x2E0) & ~0x3Fu) | 0x2E;
    {
        os_exec_joint(actor+0x87C,
            OS_E(s16,0x10467E44,0x36),OS_E(s16,0x10467E44,0x3A),
            OS_E(s16,0x10467E44,0x3E),OS_E(s16,0x10467E44,0x42),
            OS_E(s16,0x10467E44,0x34),OS_E(s16,0x10467E44,0x38),
            OS_E(s16,0x10467E44,0x3C),OS_E(s16,0x10467E44,0x40),
            OS_E(s16,0x10467E44,0x44));
    }

    if (!(OS_E(u32,actor,0x8F4) & 1) /* !isFinish() */) {
        if (!(OS_E(u32,actor,0x2E0) & 0x2000) && (OS_E(u32,actor,0x8F4) & 8) &&
            Os_positionMoveCheckHD(actor, OS_E(u32,0x101C28E4,4*(s8)OS_E(s8,actor,0x2DD)))) {
            u16 ang = OS_E(u16,actor,0x322);
            f32 sy = OS_E(f32,actor,0x340);
            if (OS_E(f32,actor,0x378) < sy) {
                sy = sy - OS_E(f32,actor,0x374);
                OS_E(f32,actor,0x340) = sy;
                if (sy < OS_E(f32,actor,0x378)) {
                    OS_E(f32,actor,0x340) = OS_E(f32,actor,0x378);
                }
            } else if (OS_E(f32,actor,0x378) > sy) {
                sy = sy + OS_E(f32,actor,0x374);
                OS_E(f32,actor,0x340) = sy;
                if (sy > OS_E(f32,actor,0x378)) {
                    OS_E(f32,actor,0x340) = OS_E(f32,actor,0x378);
                }
            }
            f32 sf = OS_E(f32,actor,0x370);
            OS_E(f32,actor,0x33C) = sf * os_exec_sine(ang);
            OS_E(f32,actor,0x344) = sf * os_exec_cosine(ang);
            Os_movePositionHD(actor, actor+0x710);
        }
        os_exec_crr(actor+0x43C,os_exec_info()+0x12A0);
        OS_E(u32,actor,0x8F4) = OS_E(u32,actor,0x8F4) | 0x10;

        f32 gndY = OS_E(f32,actor,0x4D0);
        if ((os_exec_pmf(actor+0x8E0, 0x022AFF94) ||
             os_exec_pmf(actor+0x8E8, 0x022AF8D4)) &&
            !(OS_E(u32,actor,0x464) & 0x20) /* !ChkGroundHit() */) {
            f32 delta = gndY - OS_E(f32,actor,0x318);
            if (delta < 0.0f && !(delta < -30.1f)) {
                OS_E(f32,actor,0x340) = 0.0f;
                OS_E(f32,actor,0x318) = gndY;
                OS_E(u32,actor,0x464) = OS_E(u32,actor,0x464) | 0x20;
            }
            gndY = OS_E(f32,actor,0x4D0);
        }

        if (gndY != -1000000000.0f) {
            u32 gnd = actor+0x524;
            u32 pla = Os_trianglePlaneHD((os_exec_info()+0x12A0), (u32)OS_E(u16,gnd,2), (u32)OS_E(u16,gnd,0));
            if (pla) {
                os_exec_copy_vector(actor+0x960,pla); /* plane->mNormal */
            }
            s8 roomNo = (s8)Os_groundRoomHD((os_exec_info()+0x12A0), gnd);
            OS_E(s8,actor,0x326) = roomNo;
            OS_E(u8,actor,0x1C9) = roomNo; /* OS_E(u8,actor,0x1C9) */
            u8 color = (u8)Os_groundColorHD((os_exec_info()+0x12A0), gnd);
            OS_E(u32,actor,0x970) = (u32)OS_E(u32,gnd,4); /* field_0x7FC.SetPolyInfo(m_gnd) */
            OS_E(u16,actor,0x96C) = OS_E(u16,gnd,0);
            OS_E(u16,actor,0x96E) = OS_E(u16,gnd,2);
            OS_E(u8,actor,0x1CA) = color;
            OS_E(u8,actor,0x732) = roomNo; /* mStts.SetRoomId(roomNo) */
            OS_E(u32,actor,0x974) = (u32)OS_E(u32,gnd,8);
            if (roomNo != 7) {
                OS_E(u32,actor,0x614) = 0;
            }
        }

        OS_E(u8,actor,0x918) = (OS_E(u32,actor,0x464) >> 5) & 1;
        if (!(OS_E(u32,actor,0x2E0) & 0x2000)) {
            if (OS_E(f32,actor,0x4D0) == -1000000000.0f || Os_groundCodeHD((os_exec_info()+0x12A0), actor+0x524) == 4) {
                u8 c = OS_E(u8,actor,0x604);
                if (c < 30) {
                    OS_E(u8,actor,0x604) = c + 1;
                } else {
                    s8 room = OS_E(s8,actor,0x2FE);
                    os_exec_info();
                    if (!(OS_E(u8,0x1047E8E8,0x22C*(s8)room) & 0x10)) {
                        // HD: preserve the GHS word-copy order, including packed angle/room words.
                        u16 ax=OS_E(u16,actor,0x2F8),ay=OS_E(u16,actor,0x2FA);
                        OS_E(u16,actor,0x328)=ax;
                        OS_E(u32,actor,0x31C)=OS_E(u32,actor,0x2F4);
                        OS_E(u32,actor,0x320)=OS_E(u32,actor,0x2F8);
                        OS_E(u32,actor,0x324)=OS_E(u32,actor,0x2FC);
                        OS_E(f32,actor,0x370)=0.0f;
                        OS_E(u8,actor,0x604)=0;
                        OS_E(u16,actor,0x32A)=ay;
                        OS_E(u32,actor,0x318)=OS_E(u32,actor,0x2F0);
                        OS_E(u32,actor,0x314)=OS_E(u32,actor,0x2EC);
                        OS_E(u16,actor,0x32C)=OS_E(u16,actor,0x2FC);
                    } else {

                        Os_restartLinkHD(OS_E(u32,os_exec_info(),0x5B34), 5, 0xC9, -1.0f, 0);
                    }
                }
            } else {
                OS_E(u8,actor,0x604) = 0;
            }
            if (OS_E(u32,actor,0x464) & 0x1000) {
                Os_restartLinkHD(OS_E(u32,os_exec_info(),0x5B34), 5, 0xC9, -1.0f, 0);
                if (!(OS_E(u32,actor,0x8F4) & 4) /* !isWaterHit() */) {
                    OS_E(u32,actor,0x8F4) = OS_E(u32,actor,0x8F4) | 4;
                    u32 splash = dPa_control_set(OS_E(u32,os_exec_info(),0x5AB0), 0, 0x40, actor+0x314,
                                        0, 0, 0xFF, 0, -1, 0, 0, 0);
                    if (splash) {
                        OS_E(f32,splash,0x34)=r15;
                        os_setEmitterScale(splash, 0x10467F08);
                    }

                    u32 ripple = dPa_control_set(
                        OS_E(u32,os_exec_info(),0x5AB0), 5, 0x3D, actor+0x314, 0, 0,
                        0xFF, 0x1047B2E4, -1, 0, 0, 0);
                    if (ripple) {
                        os_setEmitterScale(ripple, 0x10467F14);
                    }
                }
            }
        }
    } else {
        os_exec_crr(actor+0x43C,os_exec_info()+0x12A0);
        f32 gh = OS_E(f32,actor,0x4D0);
        OS_E(u32,actor,0x8F4) = OS_E(u32,actor,0x8F4) | 0x10;
        if (gh != -1000000000.0f) {
            OS_E(u8,actor,0x1C9) = (u8)Os_groundRoomHD((os_exec_info()+0x12A0), actor+0x524);
            OS_E(u8,actor,0x1CA) = (u8)Os_groundColorHD((os_exec_info()+0x12A0), actor+0x524);
        }
    }

    if (!(OS_E(u32,actor,0x2E0) & 0x2000)) {
        os_exec_collision(actor);
    }
    if (Os_positionMoveCheckHD(actor, OS_E(u32,0x101C28E4,4*(s8)OS_E(s8,actor,0x2DD)))) {
        os_exec_animation(actor);
    }

    if (!os_exec_event_proc(actor)) {
        if (!os_exec_finish(actor)) {
            if (OS_E(u32,os_exec_info(),0x5B2C) == actor) {
                u32 st = OS_E(u32,actor,0x2E0);
                u32 flg = OS_E(u32,actor,0x600);
                OS_E(u32,actor,0x2E0) = (st & ~0x3Fu) | 0x33;
                if (flg & 1) {
                    OS_E(s8,actor,0x915) = 0;
                } else {
                    os_exec_player_action(actor,0);
                    if (os_exec_return_link(actor)) {
                        OS_E(u32,actor,0x600) = OS_E(u32,actor,0x600) | 1;
                    }
                }
            } else {
                if (os_exec_wakeup(actor)) {
                    OS_E(u32,actor,0x2E0) = (OS_E(u32,actor,0x2E0) & ~0x3Fu) | 0x2E;
                }
                os_exec_carry(actor);
                os_exec_order_check(actor);
                os_exec_npc_action(actor,0);
                if (!os_exec_pmf(actor+0x8E8, 0x022AEF70)) {
                    OS_E(u16,actor,0x322) = OS_E(u16,actor,0x32A);
                }
            }
            os_exec_goal(actor);
        }
        if (OS_E(s32,actor,0x8FC) == 2 || OS_E(s32,actor,0x8FC) == 4) {
            os_exec_make_beam(actor,0);
        } else {
            os_exec_end_beam(actor);
        }
        os_exec_event_check(actor);
    }

    os_exec_event_order(actor);
    os_exec_matrix(actor);
    if (OS_E(u32,actor,0x8B4) != 0 || OS_E(u32,actor,0x8BC) != 0) {
        s32 reverb = dComIfGp_getReverb(OS_E(s8,actor,0x326));
        mDoAud_seStart(0x7046, actor+0x314, 0, reverb);
    }
    os_exec_lighting(actor);
    return 1;
}


#undef OS_E
