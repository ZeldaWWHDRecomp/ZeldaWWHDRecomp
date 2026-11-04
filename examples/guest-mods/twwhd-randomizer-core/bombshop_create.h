/* CC0 daNpc_Bms1_c::_create, public wwhd source a874e1a9.
 * MIT randomizer pearl checks02209F88/02209FCC are constant true. */
#pragma once
WWHD_GAME_FUNC(0x025D4ED0,void,randomizer_bms_actor_construct,(u32 actor));
WWHD_GAME_FUNC(0x025E7820,void,randomizer_bms_btp_construct,(u32 btp));
WWHD_GAME_FUNC(0x024F0474,void,randomizer_bms_acch_construct,(u32 acch));
WWHD_GAME_FUNC(0x024EFE94,void,randomizer_bms_circle_construct,(u32 circle));
WWHD_GAME_FUNC(0x0200BD2C,void,randomizer_bms_cc_status_construct,(u32 status));
WWHD_GAME_FUNC(0x02515DA0,void,randomizer_bms_dcc_status_construct,(u32 status));
WWHD_GAME_FUNC(0x02515FB8,void,randomizer_bms_cc_cylinder_construct,(u32 cylinder));
WWHD_GAME_FUNC(0x02018590,void,randomizer_bms_geometric_cylinder_construct,(u32 cylinder));
WWHD_GAME_FUNC(0x0259DAA0,void,randomizer_bms_joint_construct,(u32 joint));
WWHD_GAME_FUNC(0x0259F740,void,randomizer_bms_cut_construct,(u32 cut));
WWHD_GAME_FUNC(0x025885C4,void,randomizer_bms_stick_params,(u32 stick,s32 a,s32 b,s32 c,s32 d,f32 rate,f32 threshold,s32 g,s32 h));
WWHD_GAME_FUNC(0x025885E8,void,randomizer_bms_stick_init,(u32 stick));
WWHD_GAME_FUNC(0x025BBDF0,void,randomizer_bms_cam_construct,(u32 cam));
WWHD_GAME_FUNC(0x025BC760,void,randomizer_bms_items_construct,(u32 items));
WWHD_GAME_FUNC(0x025B8B94,s32,randomizer_bms_event_query,(u32 events,u32 bit));
WWHD_GAME_FUNC(0x02520460,s32,randomizer_bms_resource_load,(u32 phase,u32 archive));
WWHD_GAME_FUNC(0x025D63E8,s32,randomizer_bms_solid_heap,(u32 actor,u32 callback,u32 size));
WWHD_GAME_FUNC(0x025F0A10,s32,randomizer_bms_hio_child,(u32 name,u32 hio));
WWHD_REPLACE(0x02209DB4,s32,randomizer_bombshop_create,(u32 actor)) {
    if(!(R_BMS(u32,actor+0x2E4)&8)) {
        if(actor) {
            randomizer_bms_actor_construct(actor);
            R_BMS(s16,actor+0x3AC)=0;
            const u32 zero=WWHD_GAME_DATA(0x10017DBC,u32);
            R_BMS(s16,actor+0x3C4)=0;
            R_BMS(u32,actor+0x3C0)=zero;
            R_BMS(s16,actor+0x3C6)=0;
            R_BMS(s16,actor+0x3C8)=0;
            R_BMS(u32,actor+0xB4)=0x10017D54;
            R_BMS(s16,actor+0x3AE)=0;
            R_BMS(s16,actor+0x3B0)=0;
            R_BMS(u32,actor+0x3BC)=zero;
            randomizer_bms_btp_construct(actor+0x44C);
            randomizer_bms_acch_construct(actor+0x4D8);
            R_BMS(u32,actor+0x4E8)=0x10017D24;
            R_BMS(u32,actor+0x4EC)=0x10017D44;
            R_BMS(u32,actor+0x4F8)=0x10017D34;
            R_BMS(u8,actor+0x4F0)=1;
            randomizer_bms_circle_construct(actor+0x69C);
            randomizer_bms_cc_status_construct(actor+0x6DC);
            randomizer_bms_dcc_status_construct(actor+0x6F8);
            R_BMS(u32,actor+0x6F4)=0x1004AE88;
            R_BMS(u32,actor+0x6F8)=0x1004AEC0;
            randomizer_bms_cc_cylinder_construct(actor+0x718);
            R_BMS(u32,actor+0x82C)=0x100015A8;
            R_BMS(u32,actor+0x828)=0x10017D14;
            randomizer_bms_geometric_cylinder_construct(actor+0x830);
            R_BMS(u32,actor+0x754)=0x1004B108;
            R_BMS(u32,actor+0x844)=0x1004B150;
            R_BMS(u32,actor+0x82C)=0x1004B160;
            randomizer_bms_joint_construct(actor+0x850);
            randomizer_bms_cut_construct(actor+0x884);
            R_BMS(u32,actor+0x914)=0x10050788;
            randomizer_bms_stick_params(actor+0x8F0,15,15,0,0,
                WWHD_GAME_DATA(0x10017ED0,f32),WWHD_GAME_DATA(0x10017DCC,f32),0,0x2000);
            randomizer_bms_stick_init(actor+0x8F0);
            randomizer_bms_cam_construct(actor+0x968);
            randomizer_bms_items_construct(actor+0x9B8);
        }
        R_BMS(u32,actor+0x2E4)|=8;
    }
    const s8 type=(s8)(R_BMS(u32,actor+0xB0)>>24);
    R_BMS(s8,actor+0xA06)=type;
    /* Keep the native event read and its callback order even though both
     * patched branches simplify to the type-only decision. */
    (void)randomizer_bms_event_query(randomizer_daily_save()+0x644,0xA02);
    if(type!=0) {
        R_BMS(u8,actor+0xA0D)=1;
        return 5;
    }
    R_BMS(u8,actor+0xA0D)=0;
    const s32 phase=randomizer_bms_resource_load(actor+0x43C,0x101BD3A0);
    if(phase==4) {
        R_BMS(s8,actor+0xA06)=R_BMS(s8,actor+0xA06)!=0;
        if(!randomizer_bms_solid_heap(actor,0x02209434,0x18000))return 5;
        const u32 model=R_BMS(u32,R_BMS(u32,actor+0x444)+0x90);
        R_BMS(u32,actor+0x348)=model?model+0xC8:0;
        if(WWHD_GAME_DATA(0x10466654,s32)<0) {
            const s32 child=randomizer_bms_hio_child(0x10017EE4,0x10466650);
            WWHD_GAME_DATA(0x10466650,s8)=(s8)child;
        }
        WWHD_GAME_DATA(0x10466654,u32)+=1;
        if(!randomizer_bombshop_init(actor))return 5;
    }
    return phase;
}
