/* CC0 Moblin execution helpers, public d_a_mo2_exec.cpp a874e1a9.
 * Shared helpers retain the native Moblin TU storage and callback ordering. */
#pragma once
WWHD_GAME_FUNC(0x021C6368,void,randomizer_mo2_texture,(u32 actor,u32 index));
WWHD_GAME_FUNC(0x021C6578,void,randomizer_mo2_smoke,(u32 actor,f32 scale));
WWHD_GAME_FUNC(0x025A5F88,void,randomizer_mo2_smoke_end,(u32 callback));
WWHD_GAME_FUNC(0x0200FAD8,void,randomizer_mo2_translate,(f32 x,f32 y,f32 z,u32 concatenate));
WWHD_GAME_FUNC(0x0200FCD8,void,randomizer_mo2_transform,(u32 input,u32 output));
WWHD_GAME_FUNC(0x025F1C28,void,randomizer_mo2_rotate_y,(u32 matrix,s32 angle));
WWHD_GAME_FUNC(0x025F1884,void,randomizer_mo2_rotate_y_set,(u32 matrix,s32 angle));
WWHD_GAME_FUNC(0x020198D8,f64,randomizer_mo2_random,(f64 range));
WWHD_GAME_FUNC(0x02019918,f64,randomizer_mo2_random_centered,(f64 range));
WWHD_GAME_FUNC(0x020195B0,s32,randomizer_mo2_atan,(f32 x,f32 z));
static s32 randomizer_mo2_integer(f64 value) {
    if(value!=value || value<=-2147483648.0)return (s32)0x80000000u;
    if(value>=2147483648.0)return 0x7FFFFFFF;
    return (s32)value;
}
static u32 randomizer_mo2_matrix(void) {
    return WWHD_GAME_DATA(0x1018C7B0,u32);
}
static void randomizer_mo2_eye(u32 actor) {
    if(R_MO2(s16,actor+0x45C)!=0)R_MO2(s16,actor+0x45C)--;
    else {
        R_MO2(s16,actor+0x45C)=(s16)randomizer_mo2_integer((f32)(randomizer_mo2_random(50.0)+10.0));
        if(R_MO2(u8,actor+0x459)==0)randomizer_mo2_texture(actor,3);
    }
    if(R_MO2(u8,actor+0x459)!=0) {
        if(R_MO2(u8,actor+0x458)<R_MO2(u8,actor+0x45A))R_MO2(u8,actor+0x458)++;
        else R_MO2(u8,actor+0x459)=0;
    }
}
static void randomizer_mo2_ground_smoke(u32 actor) {
    if(R_MO2(s16,actor+0x898)==0)return;
    R_MO2(s16,actor+0x898)--;
    if(R_MO2(s16,actor+0x898)>=WWHD_GAME_DATA(0x10465500,s16)) {
        f32 point[3];
        point[0]=0.0f;
        point[1]=0.0f;
        R_MO2(s16,actor+0x890)=0;
        R_MO2(s16,actor+0x894)=0;
        randomizer_mo2_translate(R_MO2(f32,actor+0x314),R_MO2(f32,actor+0x318)+7.5f,R_MO2(f32,actor+0x31C),0);
        const u8 kind=R_MO2(u8,actor+0x89A);
        if(kind==0) {
            point[2]=-350.0f;
            randomizer_mo2_rotate_y(randomizer_mo2_matrix(),R_MO2(s16,actor+0x896));
            randomizer_mo2_transform((u32)point,actor+0x884);
            R_MO2(s16,actor+0x892)=R_MO2(s16,actor+0x896);
            randomizer_mo2_smoke(actor,6.0f);
            R_MO2(s16,actor+0x896)=(s16)(R_MO2(s16,actor+0x896)+WWHD_GAME_DATA(0x1047B696,s16)+2000);
        } else if(kind==1 || kind==3) {
            randomizer_mo2_rotate_y(randomizer_mo2_matrix(),R_MO2(s16,actor+0x322));
            randomizer_mo2_rotate_y(randomizer_mo2_matrix(),R_MO2(s16,actor+0x896));
            point[2]=kind==1?-55.0f:-37.5f;
            randomizer_mo2_transform((u32)point,actor+0x884);
            R_MO2(s16,actor+0x892)=R_MO2(s16,actor+0x896);
            randomizer_mo2_smoke(actor,kind==1?6.0f:2.0f);
            R_MO2(s16,actor+0x896)=(s16)(R_MO2(s16,actor+0x896)+0x1FA0);
        } else if(kind==2) {
            randomizer_mo2_translate(R_MO2(f32,actor+0x2D0C),R_MO2(f32,actor+0x2D10)+7.5f,R_MO2(f32,actor+0x2D14),0);
            randomizer_mo2_rotate_y(randomizer_mo2_matrix(),R_MO2(s16,actor+0x896));
            point[2]=-12.5f;
            randomizer_mo2_transform((u32)point,actor+0x884);
            R_MO2(s16,actor+0x892)=R_MO2(s16,actor+0x896);
            randomizer_mo2_smoke(actor,6.0f);
            R_MO2(s16,actor+0x896)=(s16)(R_MO2(s16,actor+0x896)+0x2000);
        } else if(kind==4) {
            const u32 src=actor+0x8BC+0x10C+(R_MO2(s32,actor+0x840)&1?14u:15u)*12;
            const f32 y=R_MO2(f32,src+4);
            R_MO2(f32,actor+0x888)=y;
            const u32 z=R_MO2(u32,src+8),x=R_MO2(u32,src);
            R_MO2(u32,actor+0x884)=x;
            R_MO2(u32,actor+0x88C)=z;
            R_MO2(f32,actor+0x888)=R_MO2(u8,actor+0x1002)?512.5f:y-12.5f;
            R_MO2(s16,actor+0x892)=(s16)randomizer_mo2_atan(R_MO2(f32,actor+0x33C),R_MO2(f32,actor+0x344));
            randomizer_mo2_smoke(actor,1.0f);
        }
    } else R_MO2(f32,actor+0x888)=R_MO2(f32,actor+0xFD4)+25000.0f;
    if(R_MO2(s16,actor+0x898)==0) {
        randomizer_mo2_smoke_end(actor+0x89C);
        R_MO2(u8,actor+0x89B)=0;
    }
}
WWHD_GAME_FUNC(0x028E90D4,void,randomizer_mo2_matrix_copy,(u32 source,u32 destination));
WWHD_GAME_FUNC(0x025D5834,u32,randomizer_mo2_spawn,(s32 name,u32 param,u32 position,s32 room,u32 angle,u32 scale,s32 subtype,u32 callback));
static void randomizer_mo2_throw_weapon(u32 actor) {
    if(R_MO2(s8,actor+0x10B1)!=0) {
        R_MO2(s16,actor+0x852)=WWHD_GAME_DATA(0x10465566,s16);
        const u32 model=R_MO2(u32,R_MO2(u32,actor+0x3E0)+0x90);
        const u32 block=R_MO2(u32,model+0x2C);
        R_MO2(u16,block+4)|=0x10;
        randomizer_mo2_matrix_copy(R_MO2(u32,block+0x10)+0x16*0x30,randomizer_mo2_matrix());
        f32 zero[3]={0.0f,0.0f,0.0f},position[3];
        randomizer_mo2_transform((u32)zero,(u32)position);
        R_MO2(u32,actor+0x2D60)=randomizer_mo2_spawn(0x1CF,4,(u32)position,R_MO2(s8,actor+0x326),0,0,-1,0);
        R_MO2(s8,actor+0x10B3)=1;
        R_MO2(s8,actor+0x10B2)=R_MO2(s8,actor+0x10B1);
        R_MO2(s8,actor+0x10B1)=0;
        R_MO2(s8,actor+0x10B0)=0;
    }
    if(R_MO2(s8,actor+0x10B3)!=0) {
        const u32 key=R_MO2(u32,actor+0x2D60);
        const u32 weapon=key==0xFFFFFFFFu?0:randomizer_mo2_judge(0x025E1234,(u32)&key);
        if(weapon && R_MO2(s8,actor+0x10B2)!=2) {
            R_MO2(s16,weapon+0x43A)=(s16)randomizer_mo2_integer(randomizer_mo2_random_centered(2000.0f));
            s16 angle=(s16)(R_MO2(s16,actor+0x32A)+0x8000);
            angle=(s16)(angle+(s16)randomizer_mo2_integer(randomizer_mo2_random_centered(8000.0f)));
            const f32 horizontal=(f32)(randomizer_mo2_random(10.0)+20.0);
            const f32 vertical=(f32)(randomizer_mo2_random(10.0)+20.0);
            R_MO2(s16,weapon+0x322)=angle;
            R_MO2(f32,weapon+0x340)=vertical;
            R_MO2(f32,weapon+0x370)=horizontal;
            R_MO2(s8,actor+0x10B3)=0;
        }
    }
}
WWHD_GAME_FUNC(0x024F8020,void,randomizer_mo2_camera_get,(void));
WWHD_GAME_FUNC(0x024F8044,u32,randomizer_mo2_camera_body,(void));
WWHD_GAME_FUNC(0x02520540,s32,randomizer_mo2_reverb,(s32 room));
WWHD_GAME_FUNC(0x025E1A40,void,randomizer_mo2_sound,(u32 id,u32 pos,u32 parameter,s32 reverb));
WWHD_GAME_FUNC(0x025CB374,void,randomizer_mo2_shock,(u32 vibration,s32 strength,s32 flags,u32 direction));
WWHD_GAME_FUNC(0x02515280,void,randomizer_mo2_camera_trim,(u32 camera,s32 size));
WWHD_GAME_FUNC(0x02514EE4,void,randomizer_mo2_camera_type,(u32 camera,u32 type,u32 actor));
WWHD_GAME_FUNC(0x0251528C,void,randomizer_mo2_camera_trim_type,(u32 camera,s32 type));
WWHD_GAME_FUNC(0x0259169C,void,randomizer_mo2_meter_show,(void));
WWHD_GAME_FUNC(0x025D7B24,void,randomizer_mo2_order_event,(u32 actor,u32 flag,u32 hind,u32 parameter));
WWHD_GAME_FUNC(0x02514F2C,void,randomizer_mo2_camera_stop,(u32 camera));
WWHD_GAME_FUNC(0x02514F38,void,randomizer_mo2_camera_start,(u32 camera));
WWHD_GAME_FUNC(0x0200ED84,void,randomizer_mo2_calculate,(u32 value,f32 target,f32 scale,f32 maximum));
WWHD_GAME_FUNC(0x02514F88,void,randomizer_mo2_camera_set,(u32 camera,u32 center,u32 eye,f32 fov,s32 angle));
static void randomizer_mo2_copy_vector(u32 to,u32 from) {
    for(u32 i=0;i<3;i++)R_MO2(u32,to+4*i)=R_MO2(u32,from+4*i);
}
static void randomizer_mo2_demo_camera(u32 actor) {
    (void)randomizer_mo2_game();
    randomizer_mo2_camera_get();
    const u32 camera=randomizer_mo2_camera_body();
    s32 set=1;
    switch((u8)R_MO2(s8,actor+0x2E21)) {
    case 0:set=0;break;
    case 1:case 2: {
        randomizer_mo2_sound(0x905,0,0,randomizer_mo2_reverb(R_MO2(s8,actor+0x326)));
        f32 direction[3]={0.0f,1.0f,0.0f};
        randomizer_mo2_shock(randomizer_mo2_game()+0x599C,WWHD_GAME_DATA(0x1047B68C,s16)+4,-0x21,(u32)direction);
        WWHD_GAME_DATA(0x101BA693,s8)=1;
        randomizer_mo2_camera_trim(camera,1);
        R_MO2(f32,actor+0x2E48)=55.0f;
        randomizer_mo2_copy_vector(actor+0x2E30,actor+0x314);
        R_MO2(s8,actor+0x2E21)=3;
        R_MO2(f32,actor+0x2E3C)=WWHD_GAME_DATA(0x1047B630,f32)+200.0f;
        R_MO2(f32,actor+0x2E40)=0.0f;
    }
    /* fall through */
    case 3:
        set=0;
        randomizer_mo2_camera_type(camera,0x10014EF4,actor);
        randomizer_mo2_camera_trim_type(camera,1);
        break;
    case 10:
        randomizer_mo2_camera_trim(camera,0);set=0;
        randomizer_mo2_meter_show();
        R_MO2(s8,actor+0x2E21)=0;
        WWHD_GAME_DATA(0x101BA693,s8)=0;
        break;
    case 0x32:
        if(R_MO2(u16,actor+0xF8)!=2) {
            randomizer_mo2_order_event(actor,2,0xFFFF,0);
            R_MO2(u16,actor+0xFA)|=2;
            set=0;break;
        }
        randomizer_mo2_camera_stop(camera);
        randomizer_mo2_camera_trim(camera,1);
        R_MO2(s8,actor+0x2E21)=0x33;
        {
            const u32 camera2=R_MO2(u32,randomizer_mo2_game()+0x5AF8);
            randomizer_mo2_copy_vector(actor+0x2E24,camera2+0xDC);
            randomizer_mo2_copy_vector(actor+0x2E30,camera2+0xE8);
        }
        R_MO2(f32,actor+0x2E48)=55.0f;
        R_MO2(s16,actor+0x2E22)=0;
        /* fall through */
    case 0x33:
        randomizer_mo2_calculate(actor+0x2E48,WWHD_GAME_DATA(0x1047B644,f32)+30.0f,0.2f,WWHD_GAME_DATA(0x1047B648,f32)+0.4f);
        if(R_MO2(s8,actor+0x2E20)==0) {
            randomizer_mo2_calculate(actor+0x2E30,R_MO2(f32,actor+0x314),0.1f,100.0f);
            randomizer_mo2_calculate(actor+0x2E34,R_MO2(f32,actor+0x318)+230.0f+WWHD_GAME_DATA(0x1047B640,f32),0.1f,100.0f);
            randomizer_mo2_calculate(actor+0x2E38,R_MO2(f32,actor+0x31C),0.1f,100.0f);
        }
        R_MO2(s16,actor+0x2E22)++;
        if(R_MO2(s16,actor+0x2E22)>150) {
            randomizer_mo2_camera_trim(camera,0);
            randomizer_mo2_camera_start(camera);
            randomizer_mo2_meter_show();
            R_MO2(u16,randomizer_mo2_game()+0x52B8)|=8;
            randomizer_mo2_delete(actor);
            return;
        }
        break;
    }
    if(set) {
        u32 center[3],eye[3];
        for(u32 i=0;i<3;i++)center[i]=R_MO2(u32,actor+0x2E30+4*i);
        for(u32 i=0;i<3;i++)eye[i]=R_MO2(u32,actor+0x2E24+4*i);
        randomizer_mo2_camera_set(camera,(u32)center,(u32)eye,R_MO2(f32,actor+0x2E48),0);
    }
}
