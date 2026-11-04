/* Complete public CC0 d_a_npc_mk/ho/tc consumers (a874e1a9).
 * MIT remove_cutscenes: retain manual birthday/pendant/reward paths, remove only
 * Ivan's automatic birthday order and Tingle's post-rescue menu flag store. */
#pragma once
WWHD_GAME_FUNC(0x025200D4,u32,randomizer_gate_info,(void));
WWHD_GAME_FUNC(0x025B8B94,s32,randomizer_gate_event_query,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x025B8B68,void,randomizer_gate_event_on,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x025B8B7C,void,randomizer_gate_event_off,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x025B8BB0,u8,randomizer_gate_event_reg,(u32 event,u32 reg));
WWHD_GAME_FUNC(0x02299358,u8,randomizer_gate_ivan_talk,(u32 actor));
WWHD_GAME_FUNC(0x0229706C,void,randomizer_gate_ivan_animation,(u32 actor));
WWHD_GAME_FUNC(0x022985D0,s32,randomizer_gate_ivan_demo_check,(u32 actor));
WWHD_GAME_FUNC(0x022987B0,u8,randomizer_gate_ivan_demo,(u32 actor));
WWHD_GAME_FUNC(0x02297990,u8,randomizer_gate_ivan_next_visit,(u32 actor));
WWHD_GAME_FUNC(0x02297D78,void,randomizer_gate_ivan_visit_init,(u32 actor,u8 mode));
WWHD_GAME_FUNC(0x02298058,void,randomizer_gate_ivan_visit,(u32 actor));
WWHD_GAME_FUNC(0x022994F0,void,randomizer_gate_ivan_event,(u32 actor));
WWHD_GAME_FUNC(0x0200F5C8,s32,randomizer_gate_chase,(u32 speed,f32 target,f32 step));
WWHD_GAME_FUNC(0x025D6894,s16,randomizer_gate_actor_angle,(u32 actor,u32 target));
WWHD_GAME_FUNC(0x0223F774,void,randomizer_gate_marie_animation,(u32 actor));
WWHD_GAME_FUNC(0x02556BC0,s32,randomizer_gate_night_stop,(void));
WWHD_GAME_FUNC(0x0254457C,s32,randomizer_gate_tingle_end,(u32 manager,u32 name));
WWHD_GAME_FUNC(0x025D57E0,s32,randomizer_gate_actor_delete,(u32 actor));
WWHD_GAME_FUNC(0x02555D0C,u32,randomizer_gate_environment,(void));
static u32 randomizer_gate_save(void){return WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);}
static s32 randomizer_gate_event(u32 flag){return randomizer_gate_event_query(randomizer_gate_save()+0x644,flag);}
static s32 randomizer_gate_temporary(u32 flag){return randomizer_gate_event_query(randomizer_gate_save()+0x1178,flag);}
static u8 randomizer_gate_register(u32 reg){return randomizer_gate_event_reg(randomizer_gate_save()+0x644,reg);}
WWHD_REPLACE(0x022995C4,s32,randomizer_ivan_visit,(u32 actor)) {
    f32 target=0.0f;
    if(*(volatile u16*)(actor+0x7FC)&1) {
        *(volatile u8*)(actor+0x836)=*(volatile u8*)(actor+0x835);
        *(volatile u8*)(actor+0x835)=randomizer_gate_ivan_talk(actor);
        randomizer_gate_ivan_animation(actor);
        *(volatile f32*)(actor+0x370)=0.0f;
        *(volatile u8*)(actor+0x818)=1;
        if(randomizer_gate_temporary(0x0580)) {
            randomizer_gate_event_off(randomizer_gate_save()+0x1178,0x0580);
            *(volatile u32*)(actor+0x2E0)&=~0x4000u;
        }
    } else if(randomizer_gate_ivan_demo_check(actor)) {
        *(volatile u8*)(actor+0x836)=*(volatile u8*)(actor+0x835);
        *(volatile u8*)(actor+0x835)=10;
        randomizer_gate_ivan_animation(actor);
        *(volatile f32*)(actor+0x370)=0.0f;
        *(volatile u8*)(actor+0x818)=1;
        (void)randomizer_gate_ivan_demo(actor);
    } else {
        const u8 mode=randomizer_gate_ivan_next_visit(actor);
        if(mode!=*(volatile u8*)(actor+0x818))randomizer_gate_ivan_visit_init(actor,mode);
        randomizer_gate_ivan_visit(actor);
        if(*(volatile s8*)(actor+0x829)==4)target=2.5f;
        else if(*(volatile s8*)(actor+0x829)==5)
            target=*(volatile u8*)(actor+0x818)==5?9.0f:12.0f;
        const f32 step=target-*(volatile f32*)(actor+0x370)>=0.0f?1.1f:2.8f;
        (void)randomizer_gate_chase(actor+0x370,target,step);
        *(volatile u8*)(actor+0x834)|=1;
        if(*(volatile u8*)(actor+0x818)==4 || *(volatile u8*)(actor+0x818)==5) {
            const u32 link=*(volatile u32*)(randomizer_gate_info()+0x5B34);
            s16 angle=randomizer_gate_actor_angle(link,actor);
            angle=(s16)(angle-*(volatile s16*)(link+0x32A));
            /* Native NEG followed by EXTSH deliberately keeps -32768 negative. */
            if(angle<0)angle=(s16)-(s32)angle;
            if(angle>0x1800)*(volatile u8*)(actor+0x834)&=(u8)~1u;
        }
        if(*(volatile u16*)(actor+0x81C))*(volatile u8*)(actor+0x834)&=(u8)~1u;
        /* Upstream jump follows this real query; manual visitSetEvent stays live. */
        (void)randomizer_gate_event(0x1F80);
        if(*(volatile u8*)(actor+0x834)&1)randomizer_gate_ivan_event(actor);
    }
    return 1;
}
WWHD_REPLACE(0x0223FFB4,u32,randomizer_marie_get_message,(u32 actor)) {
    switch(*(volatile s8*)(actor+0x81B)) {
    case 1:
        if(randomizer_gate_night_stop())return 0x2722;
        if(!randomizer_gate_event(0x1E01)) {
            randomizer_gate_event_on(randomizer_gate_save()+0x644,0x1E01);return 0x2711;
        }
        return (randomizer_gate_register(0xC0FF)==0?0u:1u)+0x2712;
    case 3:
        if(*(volatile u8*)(randomizer_gate_info()+0x52B1)!=0x1F || !randomizer_gate_event(0x1F80))return 0x2739;
        if(randomizer_gate_register(0xC0FF)==0)return 0x273A;
        if(!randomizer_gate_event(0x1C08))return 0x2749;
        if(!randomizer_gate_event(0x1C04))return randomizer_gate_temporary(0x0104)?0x274A:0x274C;
        if(randomizer_gate_register(0xC0FF)>=0x63)return 0x275E;
        return 0x2754;
    case 4:return *(volatile u32*)(actor+0x808);
    default:return 0;
    }
}
WWHD_REPLACE(0x022405D4,s32,randomizer_marie_wait,(u32 actor)) {
    if(*(volatile u16*)(actor+0x800)&0x10) {
        *(volatile u8*)(actor+0x81C)=*(volatile u8*)(actor+0x81B);
        *(volatile u8*)(actor+0x81B)=2;
        randomizer_gate_marie_animation(actor);
        *(volatile u8*)(actor+0x806)=0xFF;
    } else if(*(volatile u16*)(actor+0x800)&1) {
        *(volatile u8*)(actor+0x81C)=*(volatile u8*)(actor+0x81B);
        *(volatile u8*)(actor+0x81B)=1;
        randomizer_gate_marie_animation(actor);
    } else {
        *(volatile u8*)(actor+0x81A)|=1;
        if(randomizer_gate_event(0x1F80) && !randomizer_gate_night_stop())
            *(volatile u8*)(actor+0x81A)|=4;
    }
    return *(volatile f32*)(*(volatile u32*)(actor+0x3B4)+0xB0)<1.0f;
}
WWHD_REPLACE(0x022ECBB4,void,randomizer_tingle_rescue_complete,(u32 actor)) {
    if(randomizer_gate_tingle_end(randomizer_gate_info()+0x52C4,0x10022C5C)) {
        randomizer_gate_event_on(randomizer_gate_save()+0x644,0x0B80);
        const u32 info=randomizer_gate_info();
        *(volatile u16*)(info+0x52B8)|=8;
        (void)randomizer_gate_actor_delete(actor);
        /* Retain the native pointer read, omit only the post-rescue menu store. */
        (void)WWHD_GAME_DATA(0x101F8344,u32);
    }
    if(*(volatile u8*)(randomizer_gate_environment()+0x10A4)!=0xFF) {
        /* Native ADDI wraps before the signed timer comparison. */
        const u32 next=*(volatile u32*)(actor+0x7F4)+1u;
        *(volatile u32*)(actor+0x7F4)=next;
        if((s32)next>0xCB)*(volatile u8*)(randomizer_gate_environment()+0x10A4)=0xFF;
    }
}
