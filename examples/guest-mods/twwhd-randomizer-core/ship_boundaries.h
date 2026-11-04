/* Polygon boundary control flow: public CC0 ZeldaWWHDDecomp/wwhd a874e1a9,
 * d_a_ship.cpp. First-three sea-path removal: MIT TWWHD Randomizer 1.2.0
 * make_game_nonlinear.asm, Copyright (c) 2024 SuperDude88. */
#pragma once
WWHD_GAME_FUNC(0x025AAF88,u32,randomizer_ship_room_path,(u32 path,s32 room));
WWHD_GAME_FUNC(0x025AB070,u32,randomizer_ship_next_path,(u32 path,s32 room));
WWHD_GAME_FUNC(0x025B7D90,s32,randomizer_ship_symbol,(u32 collect,u32 symbol));
WWHD_GAME_FUNC(0x028F4384,f64,randomizer_ship_sqrt,(f64 value));

/* Match the native single-precision fused multiply-add, without requiring a
 * libm import in freestanding guest modules. */
static f32 randomizer_ship_fmadds(f32 a,f32 b,f32 c) {
#pragma clang fp contract(fast)
    return a*b+c;
}

WWHD_REPLACE(0x02474A98,s32,randomizer_ship_out_of_range,(u32 actor)) {
    const u8 path_id=*(volatile u8*)(actor+0x634);
    if(path_id==255)return 0;
    s32 path_index=0,closest_index=0;
    u32 closest= (u32)&WWHD_GAME_DATA(0x101D03F4,u8);
    const u32 stage_data=wwhd_dComIfG_getGameInfo()+0x5150;
    const u32 vtable=*(volatile u32*)stage_data;
    const u32 stage=((u32(*)(u32))(*(volatile u32*)(vtable+0x15C)))(stage_data);
    const int sea=((*(volatile u32*)(stage+12)>>16)&7)==7;
    u32 path=randomizer_ship_room_path(path_id,-1);
    while(path) {
        u32 count=*(volatile u16*)path;
        u32 point=*(volatile u32*)(path+8);
        /* Retain undersized paths and their iterator advancement. The upstream
         * branches skip the three story boundaries only on the Great Sea. */
        if(count<3) {
            path=randomizer_ship_next_path(path,-1);path_index++;continue;
        }
        if(sea && path_index<3) {
            /* Upstream changes the branches after these native queries, not
             * the calls themselves; retain their order and side effects. */
            if(path_index==0)(void)wwhd_save_event_test(0x0902);
            else if(path_index==1)(void)randomizer_ship_symbol(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0xD4,2);
            else (void)wwhd_dComIfG_getGameInfo();
            path=randomizer_ship_next_path(path,-1);path_index++;continue;
        }
        /* Native reloads these after the ownership checks. Keep the distinction
         * from the point iterator captured above. */
        count=*(volatile u16*)path;
        const u32 points=*(volatile u32*)(path+8);
        f32 minimum=3.4028235e+38f;
        const f32 z=*(volatile f32*)(actor+0x31C),x=*(volatile f32*)(actor+0x314);
        for(s32 i=0;i<(s32)count;i++,point+=16) {
            const f32 dz=z-*(volatile f32*)(point+12),dx=x-*(volatile f32*)(point+4);
            const f32 distance=randomizer_ship_fmadds(dx,dx,dz*dz);
            if(minimum>distance){closest_index=i;minimum=distance;closest=point+4;}
        }
        const s32 last=(s32)count-1;
        const u32 next=points+(closest_index==last?4:(u32)closest_index*16+20);
        const u32 previous=points+(closest_index==0?(u32)last*16+4:(u32)closest_index*16-12);
        const s16 next_angle=wwhd_cM_atan2s_hd(
            *(volatile f32*)next-*(volatile f32*)closest,
            *(volatile f32*)(next+8)-*(volatile f32*)(closest+8));
        const s16 previous_angle=wwhd_cM_atan2s_hd(
            *(volatile f32*)closest-*(volatile f32*)previous,
            *(volatile f32*)(closest+8)-*(volatile f32*)(previous+8));
        const s16 current_angle=wwhd_cM_atan2s_hd(
            *(volatile f32*)(actor+0x314)-*(volatile f32*)closest,
            *(volatile f32*)(actor+0x31C)-*(volatile f32*)(closest+8));
        const s16 next_difference=(s16)(u16)((s32)current_angle-next_angle);
        const s16 previous_difference=(s16)(u16)((s32)wwhd_cM_atan2s_hd(*(volatile f32*)(actor+0x314)-*(volatile f32*)previous,*(volatile f32*)(actor+0x31C)-*(volatile f32*)(previous+8))-previous_angle);
        int outside=0;
        if((s16)(u16)((s32)next_angle-previous_angle)>=0) {
            if(next_difference<0){*(volatile s16*)(actor+0x674)=(s16)(u16)((s32)next_angle+0x4000);outside=1;}
            if(previous_difference<0){*(volatile s16*)(actor+0x674)=(s16)(u16)((s32)previous_angle+0x4000);outside=1;}
        } else if(next_difference<0 && previous_difference<0) {
            const f32 dnz=*(volatile f32*)(next+8)-*(volatile f32*)(actor+0x31C),dnx=*(volatile f32*)next-*(volatile f32*)(actor+0x314);
            const f64 next_distance=randomizer_ship_sqrt(randomizer_ship_fmadds(dnx,dnx,dnz*dnz));
            const f32 dpz=*(volatile f32*)(previous+8)-*(volatile f32*)(actor+0x31C),dpx=*(volatile f32*)previous-*(volatile f32*)(actor+0x314);
            const f64 previous_distance=randomizer_ship_sqrt(randomizer_ship_fmadds(dpx,dpx,dpz*dpz));
            *(volatile s16*)(actor+0x674)=(s16)(u16)((s32)(next_distance>previous_distance?previous_angle:next_angle)+0x4000);
            outside=1;
        }
        if(outside) {
            const u32 info=wwhd_dComIfG_getGameInfo();
            *(volatile u32*)(actor+0x648)=*(volatile u8*)(info+0x5CEA)==1?0x60A:(sea && path_index!=3?0x609:0x5DD);
            *(volatile u32*)(actor+0x644)|=0x00400000;
            return 1;
        }
        path=randomizer_ship_next_path(path,-1);path_index++;
    }
    return 0;
}
