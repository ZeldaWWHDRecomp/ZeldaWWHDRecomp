#pragma once
static void randomizer_flower_move(u32 a){
    RF16(a,0x3B4)=(u16)(RF16(a,0x3B4)+1);
    const u32 mode=(u32)(s32)RFS16(a,0x3B6);
    if(mode<1){
        u32 mask=0;
        for(u32 i=0;i<5;i++){
            if(RF8(a,0x448+i)==2){RF8(a,0x448+i)=0;RFF(a,0x454)=0.0f;}
            if(RF8(a,0x448+i)){
                if(RF8(a,0x448+i)==1){RFF(a,0x454)=1.0f;RF8(a,0x448+i)=5;}
                mask|=RF8((u32)&WWHD_GAME_DATA(0x101B868C,u8),i);
            }
        }
        const s16 yaw=RFS16((u32)&WWHD_GAME_DATA(0x101B86B4,u8),mask*2);
        const s16 pitch=RFS16((u32)&WWHD_GAME_DATA(0x101B86F4,u8),mask*2);
        const s16 offset=RFS16((u32)&WWHD_GAME_DATA(0x101B8734,u8),mask*2);
        rf_calc(a+0x3EC,rf_reg_f(8,4)-(f32)offset,0.1f,50.0f*RFF(a,0x454));
        if(mask==31){
            /* Upstream retains the real query and removes only its branch. */
            (void)rf_inventory(0x2D);
            RF8(a,0x3A1)=0;RF16(a,0x3B6)=1;RF16(a,0x494)=1;
        }
        const s16 speed=(s16)(u16)rf_truncate(10000.0f*RFF(a,0x454));
        rf_angle(a+0x450,pitch,4,speed);
        if(pitch)rf_angle(a+0x44E,yaw,2,speed);
        rf_calc(a+0x454,1.0f,1.0f,rf_reg_f(8,14)+0.001f);
        rf_angle(a+0x320,0,10,0x200);rf_angle(a+0x324,0,10,0x200);
        s16 timer=RFS16(a,0x3B4);
        RF16(a,0x3E4)=(u16)rf_truncate(rf_sine((s32)timer*900)*RFF(a,0x3D8));
        RF16(a,0x3E8)=(u16)rf_truncate(rf_sine((s32)timer*700)*RFF(a,0x3E0));
        rf_calc(a+0x3D8,rf_reg_f(14,9)+300.0f,1.0f,rf_reg_f(14,3)+20.0f);
        rf_calc(a+0x3E0,rf_reg_f(14,9)+300.0f,1.0f,rf_reg_f(14,3)+20.0f);
        timer=RFS16(a,0x3B4);
        RFF(a,0x3C0)=rf_sine((s32)timer*0x2EE)*RFF(a,0x3CC);
        RFF(a,0x3C8)=rf_sine((s32)timer*900)*RFF(a,0x3D4);
        rf_zero(a+0x3CC,1.0f,0.25f);rf_zero(a+0x3D4,1.0f,0.25f);
        rf_add_angles(a+0x320,a+0x3E4,a+0x328);
        randomizer_flower_vector sum;
        rf_vector_add(a+0x2EC,&sum,a+0x3C0);
        RF32(a,0x314)=RF32((u32)&sum,0);
        const f32 vertical=RFF(a,0x3EC);
        RFF(a,0x318)=sum.y+vertical;
        RF32(a,0x31C)=RF32((u32)&sum,8);
        rf_zero(a+0x3C4,0.05f,rf_reg_f(0,7)+2.0f);
    }else if(mode==1){
        rf_angle(a+0x328,0,10,0x800);rf_angle(a+0x32C,0,10,0x800);
        rf_angle(a+0x450,0,4,0x200);
        const f32 vy=RFF(a,0x340);
        RFF(a,0x318)=RFF(a,0x318)+vy;
        RFF(a,0x340)=vy-(rf_reg_f(12,14)+3.0f);
        if(RFS16(a,0x496)==(s16)(u16)(rf_reg_s(0,4)+0x1B)){
            if(RF8(a,0x3BE)!=255)rf_switch_on(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x20,RF8(a,0x3BE),RFS8(a,0x326));
            rf_sound_current(a,0x6A0E);rf_shock(0,2,4);
        }
        if(RFS16(a,0x496)==(s16)(u16)(rf_reg_s(0,5)+0x40)){
            randomizer_flower_vector splash;
            rf_raw_vector((u32)&splash,a+0x314);splash.y=1170.0f;
            rf_particle(0x82A4,(u32)&splash);rf_particle(0x82A5,(u32)&splash);
            rf_particle(0x82A6,a+0x314);rf_particle(0x82A7,(u32)&splash);
        }
        RFF(a,0x490)=rf_reg_f(0,9)+1380.0f;
        if(!(RFF(a,0x318)>RFF(a,0x490))){
            RFF(a,0x318)=RFF(a,0x490);
            if(RFF(a,0x340)<-20.0f){rf_shock(6,2,7);rf_sound_current(a,0x6951);RFF(a,0x4B8)=rf_reg_f(12,10)+90.0f;}
            RFF(a,0x340)=0.0f;rf_angle(a+0x450,0,2,0x2000);
        }
    }
}
