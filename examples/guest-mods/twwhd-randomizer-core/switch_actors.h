/* Source port of TWWHD Randomizer 1.2.0 switch_op.asm and
 * switch_dungeon_flag.asm (MIT; LICENSE-TWWHD-Randomizer.txt).
 * Native actor ABI and save helpers cross-checked against public CC0 wwhd_src.
 * Keep the assembly's numeric state transitions, including its timer quirks;
 * C preserves the actor pointer across helper calls instead of volatile PPC r3. */
#pragma once
#define RANDOMIZER_SW_BYTE(a,o) (*(volatile u8*)((a)+(o)))
#define RANDOMIZER_SW_HALF(a,o) (*(volatile u16*)((a)+(o)))
#define RANDOMIZER_SW_SIGNED(a,o) (*(volatile s16*)((a)+(o)))
#ifndef RANDOMIZER_SW_WORD
#define RANDOMIZER_SW_WORD(a,o) (*(volatile u32*)((a)+(o)))
#endif

static u32 randomizer_swop_is_condition(u32 actor) {
    u32 set=0,unset=0;
    const u32 first=RANDOMIZER_SW_BYTE(actor,0x3B0),count=RANDOMIZER_SW_BYTE(actor,0x3B1);
    for(u32 i=0;i<count;++i) {
        if(wwhd_dSv_info_c_isSwitch(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x20,
            (first+i)&0xFF,RANDOMIZER_SW_BYTE(actor,0x326)))++set;
        else ++unset;
    }
    switch(RANDOMIZER_SW_BYTE(actor,0x3AD)) {
        case 0:return unset==0; // AND, including the empty set.
        case 1:return unset>0;
        case 2:return set>0;
        case 3:return set==0;
        case 4:return set==1; // exactly one, not odd parity.
        case 5:return set!=1;
        default:return 0;
    }
}
static u32 randomizer_swop_create(u32 actor) {
    if(!(RANDOMIZER_SW_WORD(actor,0x2E4)&8)) {
        wwhd_fopAc_ac_c_ct(actor);RANDOMIZER_SW_WORD(actor,0x2E4)|=8;
    }
    const u32 params=RANDOMIZER_SW_WORD(actor,0xB0);
    RANDOMIZER_SW_BYTE(actor,0x3AD)=params&15;
    RANDOMIZER_SW_BYTE(actor,0x3AE)=(params>>4)&1;
    RANDOMIZER_SW_BYTE(actor,0x3AF)=params>>8;
    RANDOMIZER_SW_BYTE(actor,0x3B0)=params>>16;
    RANDOMIZER_SW_BYTE(actor,0x3B1)=params>>24;
    RANDOMIZER_SW_BYTE(actor,0x3B2)=RANDOMIZER_SW_HALF(actor,0x2F8);
    RANDOMIZER_SW_BYTE(actor,0x3B6)=RANDOMIZER_SW_HALF(actor,0x2FC);
    RANDOMIZER_SW_HALF(actor,0x3B4)=(u16)wwhd_event_index(wwhd_dComIfG_getGameInfo()+0x52C4,
        0,RANDOMIZER_SW_BYTE(actor,0x3B2));
    RANDOMIZER_SW_BYTE(actor,0x3B7)=RANDOMIZER_SW_BYTE(actor,0x3B6);
    const u32 output=RANDOMIZER_SW_BYTE(actor,0x3AF);
    RANDOMIZER_SW_BYTE(actor,0x3AC)=output==0xFF?5:
        wwhd_dSv_info_c_isSwitch(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x20,
            output,RANDOMIZER_SW_BYTE(actor,0x326))?
            (RANDOMIZER_SW_BYTE(actor,0x3AE)?4:5):0;
    return 4;
}
static u32 randomizer_swop_is_delete(u32 actor){(void)actor;return 1;}
static u32 randomizer_swop_delete(u32 actor){(void)actor;return 1;}
static u32 randomizer_swop_draw(u32 actor){(void)actor;return 1;}
static void randomizer_swop_event_end(u32 actor) {
    if(!wwhd_end_check(wwhd_dComIfG_getGameInfo()+0x52C4,RANDOMIZER_SW_SIGNED(actor,0x3B4)))return;
    RANDOMIZER_SW_BYTE(actor,0x3AC)=RANDOMIZER_SW_BYTE(actor,0x3AE)?4:5;
    RANDOMIZER_SW_HALF(wwhd_dComIfG_getGameInfo(),0x52B8)|=8;
}
static void randomizer_swop_condition_met(u32 actor) {
    if(!randomizer_swop_is_condition(actor))return;
    if(RANDOMIZER_SW_BYTE(actor,0x3B6)) {RANDOMIZER_SW_BYTE(actor,0x3AC)=1;return;}
    if(RANDOMIZER_SW_SIGNED(actor,0x3B4)!=-1) {RANDOMIZER_SW_BYTE(actor,0x3AC)=2;return;}
    wwhd_dSv_info_c_onSwitch(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x20,
        RANDOMIZER_SW_BYTE(actor,0x3AF),RANDOMIZER_SW_BYTE(actor,0x326));
    // Both upstream branches write 4 (despite the non-continuous comment saying Deactivated).
    RANDOMIZER_SW_BYTE(actor,0x3AC)=4;
}
static void randomizer_swop_timer(u32 actor) {
    if(RANDOMIZER_SW_BYTE(actor,0x3AE)&&!randomizer_swop_is_condition(actor)) {
        RANDOMIZER_SW_BYTE(actor,0x3B7)=RANDOMIZER_SW_BYTE(actor,0x3B6);
        RANDOMIZER_SW_BYTE(actor,0x3AC)=0;return;
    }
    if(RANDOMIZER_SW_BYTE(actor,0x3B7)) {--RANDOMIZER_SW_BYTE(actor,0x3B7);return;}
    if(RANDOMIZER_SW_SIGNED(actor,0x3B4)!=-1) {RANDOMIZER_SW_BYTE(actor,0x3AC)=2;return;}
    wwhd_dSv_info_c_onSwitch(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x20,
        RANDOMIZER_SW_BYTE(actor,0x3AF),RANDOMIZER_SW_BYTE(actor,0x326));
    // Literal upstream behavior: continuous -> 5; otherwise remain Timer.
    if(RANDOMIZER_SW_BYTE(actor,0x3AE))RANDOMIZER_SW_BYTE(actor,0x3AC)=5;
}
static void randomizer_swop_event_start(u32 actor) {
    if(RANDOMIZER_SW_HALF(actor,0xF8)==2) {
        RANDOMIZER_SW_BYTE(actor,0x3AC)=3;
        wwhd_dSv_info_c_onSwitch(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x20,
            RANDOMIZER_SW_BYTE(actor,0x3AF),RANDOMIZER_SW_BYTE(actor,0x326));return;
    }
    if(RANDOMIZER_SW_BYTE(actor,0x3AE)&&!randomizer_swop_is_condition(actor)) {
        RANDOMIZER_SW_BYTE(actor,0x3AC)=0;return;
    }
    wwhd_fopAcM_orderOtherEventId(actor,RANDOMIZER_SW_HALF(actor,0x3B4),
        RANDOMIZER_SW_BYTE(actor,0x3B2),0xFFFF,0,1);
}
static void randomizer_swop_condition_unmet(u32 actor) {
    if(randomizer_swop_is_condition(actor))return;
    RANDOMIZER_SW_BYTE(actor,0x3AC)=0;
    wwhd_dSv_info_c_offSwitch(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x20,
        RANDOMIZER_SW_BYTE(actor,0x3AF),RANDOMIZER_SW_BYTE(actor,0x326));
}
static u32 randomizer_swop_execute(u32 actor) {
    switch(RANDOMIZER_SW_BYTE(actor,0x3AC)) {
        case 0:randomizer_swop_condition_met(actor);break;
        case 1:randomizer_swop_timer(actor);break;
        case 2:randomizer_swop_event_start(actor);break;
        case 3:randomizer_swop_event_end(actor);break;
        case 4:randomizer_swop_condition_unmet(actor);break;
    }
    return 1;
}
static u32 randomizer_dngsw_create(u32 actor) {
    if(!(RANDOMIZER_SW_WORD(actor,0x2E4)&8)) {
        wwhd_fopAc_ac_c_ct(actor);RANDOMIZER_SW_WORD(actor,0x2E4)|=8;
    }
    return wwhd_dSv_info_c_isSwitch(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x20,
        RANDOMIZER_SW_WORD(actor,0xB0)&0xFF,RANDOMIZER_SW_BYTE(actor,0x326))?3:4;
}
static u32 randomizer_dngsw_is_delete(u32 actor){(void)actor;return 1;}
static u32 randomizer_dngsw_delete(u32 actor){(void)actor;return 1;}
static u32 randomizer_dngsw_draw(u32 actor){(void)actor;return 1;}
static u32 randomizer_dngsw_conditions(u32 actor) {
    const u32 params=RANDOMIZER_SW_WORD(actor,0xB0),stages=(params>>8)&0xFFFF,condition=(params>>24)&7;
    for(u32 stage=0;stage<16;++stage)if((stages&(1u<<stage))&&
        !wwhd_dSv_memBit_c_isDungeonItem(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x3A0+stage*0x24,condition))return 0;
    return 1;
}
static u32 randomizer_dngsw_execute(u32 actor) {
    if(randomizer_dngsw_conditions(actor)) {
        wwhd_dSv_info_c_onSwitch(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x20,
            RANDOMIZER_SW_WORD(actor,0xB0)&0xFF,RANDOMIZER_SW_BYTE(actor,0x326));
        wwhd_fopAcM_delete_actor(actor);
    }
    return 1;
}

typedef struct {
    s32 layer;u16 list; s16 list_priority; s16 id;u16 pad;
    const void* base;u32 size,extra,parameters;const void* actor_base;
    s16 priority;u16 pad2;const void* methods;u32 status;u8 group,cull,pad3[2];
} randomizer_switch_profile;
static const void* const randomizer_swop_methods[]={randomizer_swop_create,randomizer_swop_delete,
    randomizer_swop_execute,randomizer_swop_is_delete,randomizer_swop_draw};
static const void* const randomizer_dngsw_methods[]={randomizer_dngsw_create,randomizer_dngsw_delete,
    randomizer_dngsw_execute,randomizer_dngsw_is_delete,randomizer_dngsw_draw};
static const randomizer_switch_profile randomizer_swop_profile={-3,7,-3,0x1E4,0,0,0x3B8,0,0,0,0x9F,0,randomizer_swop_methods,0,0,0,{0,0}};
static const randomizer_switch_profile randomizer_dngsw_profile={-3,7,-3,0x1E5,0,0,0x3AC,0,0,0,0x134,0,randomizer_dngsw_methods,0x44000,0,6,{0,0}};
static const wwhd_native_actor_profile_v1 randomizer_swop_registration={sizeof(wwhd_native_actor_profile_v1),1,&randomizer_swop_profile,"SwOp",-1,0};
static const wwhd_native_actor_profile_v1 randomizer_dngsw_registration={sizeof(wwhd_native_actor_profile_v1),1,&randomizer_dngsw_profile,"DngSw",-1,0};
#ifndef RANDOMIZER_SWITCH_ACTORS_HOST_TEST
_Static_assert(sizeof(randomizer_switch_profile)==0x30,"native actor profile layout");
_Static_assert(sizeof(wwhd_native_actor_profile_v1)==28,"profile descriptor ABI");
#endif
WWHD_HOOK(0x028EA120,randomizer_register_switch_actors,(void)) {
    const s32 sw=wwhd_native_actor_profile_register(&randomizer_swop_registration);
    const s32 dungeon=wwhd_native_actor_profile_register(&randomizer_dngsw_registration);
    wwhd_log_int("coremod SwOp profile",sw);wwhd_log_int("coremod DngSw profile",dungeon);
}
