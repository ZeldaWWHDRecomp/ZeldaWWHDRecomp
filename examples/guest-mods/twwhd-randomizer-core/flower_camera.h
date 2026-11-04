#pragma once
static void randomizer_flower_camera(u32 a){
    const s32 camera_id=RFS8(wwhd_dComIfG_getGameInfo(),0x5B30);
    const u32 camera=RFP(wwhd_dComIfG_getGameInfo(),0x5AF8+0x34*camera_id)+0x248;
    rf_y_start(rf_matrix(),RFS16(a,0x32A));
    const u32 mode=(u32)(s32)RFS16(a,0x494);
    if(mode==1||mode==2){
        if(mode==1){
            if(RF16(a,0xF8)!=2){(void)rf_order_event(a,2,0xFFFFFFFFu,0);RF16(a,0xFA)|=2;return;}
            RF16(a,0x494)=(u16)(mode+1);
            rf_camera_stop(camera);rf_camera_trim(camera,2);
            rf_raw_vector(a+0x498,a+0x314);
            randomizer_flower_vector point={rf_reg_f(12,2)+500.0f,0,rf_reg_f(12,4)-1000.0f},offset;
            rf_position(&point,(u32)&offset);
            const f32 x=RFF(a,0x314);
            RFF(a,0x498)=x+offset.x;RFF(a,0x49C)=rf_reg_f(12,5)+1730.0f;
            const f32 z=RFF(a,0x31C);
            RFF(a,0x4A0)=z+offset.z;RFF(a,0x4A4)=x;
            const f32 y=RFF(a,0x318);
            RFF(a,0x4A8)=y;RFF(a,0x4AC)=z;
            RFF(a,0x4A8)=y+(rf_reg_f(12,6)-450.0f);
            RFF(a,0x4BC)=rf_reg_f(12,18)+65.0f;RF16(a,0x496)=0;
        }
        if(RFS16(a,0x496)<(s16)(u16)(rf_reg_s(12,3)+10))RFF(a,0x340)=0.0f;
        rf_calc(a+0x4A8,(RFF(a,0x318)-450.0f)+rf_reg_f(12,6),rf_reg_f(12,7)+0.5f,rf_reg_f(12,8)+200.0f);
        if(RFS16(a,0x496)>(s16)(u16)(rf_reg_s(12,4)+0x32))rf_calc(a+0x4BC,rf_reg_f(12,19)+80.0f,0.02f,rf_reg_f(12,20)+0.5f);
        if(RFS16(a,0x496)==100)rf_sound_global(0x806);
        if(RFS16(a,0x496)==0x96&&rf_reg_s(0,3)==0){
            RF16(a,0x494)=0;
            const u32 play=wwhd_dComIfG_getGameInfo();RF16(play,0x52B8)|=8;
            randomizer_flower_vector center,eye;
            rf_raw_vector((u32)&center,a+0x4A4);rf_raw_vector((u32)&eye,a+0x498);
            rf_camera_reset(camera,&center,&eye);rf_camera_start(camera);rf_camera_trim(camera,0);
        }
    }
    if(RFS16(a,0x494)!=0){
        const s16 count=RFS16(a,0x496);
        const f32 sine=rf_sine((s32)count*0x3300)*RFF(a,0x4B8);
        const f32 cosine=rf_cosine((s32)count*0x3000)*RFF(a,0x4B8);
        randomizer_flower_vector eye,center;
        eye.x=RFF(a,0x498)+sine;center.x=RFF(a,0x4A4)+sine;
        center.y=RFF(a,0x4A8)+cosine;eye.y=RFF(a,0x49C)+cosine;
        RF32((u32)&eye,8)=RF32(a,0x4A0);RF32((u32)&center,8)=RF32(a,0x4AC);
        const s16 bank=(s16)(u16)rf_truncate((rf_cosine((s32)RFS16(a,0x3B4)*0x1C00)*RFF(a,0x4B8))*8.0f);
        rf_camera_set(camera,&center,&eye,bank,RFF(a,0x4BC));
        rf_zero(a+0x4B8,1.0f,rf_reg_f(0,16)+5.0f);
        rf_report(0x19A,0x1AE,(u32)&WWHD_GAME_DATA(0x100134D4,u8),RFS16(a,0x496));
        RF16(a,0x496)=(u16)(RF16(a,0x496)+1);
    }
}
