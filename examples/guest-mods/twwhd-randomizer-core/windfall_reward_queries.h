/* Complete CC0 a874e1a9 People map-ownership consumers. MIT randomizer
 * replaces only chart-ownership tests with persistent one-time reward flags. */
#pragma once
WWHD_GAME_FUNC(0x022BED70,u32,rwq_animation,(u32 actor,u32 table,s32 flag));
WWHD_GAME_FUNC(0x022BEFF4,s32,rwq_photo,(u32 actor,u32 item));
WWHD_GAME_FUNC(0x0273AA24,void,rwq_assert,(u32 file,s32 line,u32 condition));
#ifndef RWQ_PTR
#define RWQ_PTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
#define RWQ8(a,o) (*(volatile u8*)((a)+(o)))
#define RWQ32(a,o) (*(volatile unsigned int*)((a)+(o)))
static s32 rwq_event(u32 flag) {
    return randomizer_people_event_query(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,flag);
}
WWHD_REPLACE(0x022BF304,s32,randomizer_people_pompie_reward_done,(u32 actor)) {
    (void)actor;
    return rwq_event(0x6904)!=0;
}
WWHD_REPLACE(0x022BEEDC,void,randomizer_people_wait_animation,(u32 actor)) {
    u8 npc=RWQ8(actor,0x905);
    if(npc==5&&RWQ8(actor,0x8FD)) {
        (void)rwq_animation(actor,0x101C31B4,0);
        return;
    }
    if(npc==11) {
        const u8 night=RWQ8(actor,0x90F);
        if(night==0) {
            if(rwq_event(0x6910)) {
                (void)rwq_animation(actor,0x101C31D8,1);
                return;
            }
            npc=RWQ8(actor,0x905);
        } else {
            (void)rwq_animation(actor,RWQ_PTR((u32)&WWHD_GAME_DATA(0x101C4044,u8),4*((unsigned int)npc*2+night)),1);
            return;
        }
    }
    if(npc==8) {
        if(RWQ8(actor,0x907)==1) {
            (void)rwq_animation(actor,0x101C318C,1);
            return;
        }
    } else if(npc>=19) {
        rwq_assert(0x1002047C,0x24CB,0x10020448);
        npc=RWQ8(actor,0x905);
    }
    const u8 night=RWQ8(actor,0x90F);
    (void)rwq_animation(actor,RWQ_PTR((u32)&WWHD_GAME_DATA(0x101C4044,u8),4*((unsigned int)npc*2+night)),1);
}
WWHD_REPLACE(0x022BF05C,s16,randomizer_people_xy_check,(u32 actor,s32 button)) {
    const u32 play=randomizer_people_game();
    const u8 npc=RWQ8(actor,0x905);
    const u8 item=RWQ8(play+(unsigned int)button,0x5BBB);
    switch(npc) {
    case 2:case 10:return rwq_photo(actor,item)!=0;
    case 3:case 4:
        if(rwq_event(0x2102)&&rwq_photo(actor,item))return 1;
        return item==0x1F;
    case 5:return item==0x1F;
    case 6:
        if(!(RWQ32(actor,0x8CC)&0x40)&&rwq_photo(actor,item))return 1;
        return item==0x1F;
    case 11:
        if(!RWQ8(actor,0x90F)&&rwq_photo(actor,item)&&rwq_event(0x2310)&&!rwq_event(0x6910))return 1;
        return 0;
    case 16:
        if(rwq_photo(actor,item))return 0;
        if(!RWQ8(actor,0x90F)&&rwq_event(0x2440)&&!randomizer_people_event_query(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x1178,0x0280))return 1;
        return 0;
    default:return 0;
    }
}
