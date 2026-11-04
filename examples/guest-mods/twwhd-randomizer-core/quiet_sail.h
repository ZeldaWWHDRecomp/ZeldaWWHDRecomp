/* Complete CC0 d_a_ship.cpp::procSteerMove, pin a874e1a9. The MIT
 * randomizer quiet option omits only the three Swift Sail transition sounds.
 * Runtime traversal settings compose at their original semantic boundaries. */
#pragma once
#ifndef WWHD_RANDOMIZER_QUIET_SWIFT_SAIL
#error "Regenerate prepared seed data: missing quiet Swift Sail policy"
#elif WWHD_RANDOMIZER_QUIET_SWIFT_SAIL != 0 && WWHD_RANDOMIZER_QUIET_SWIFT_SAIL != 1
#error "Invalid quiet Swift Sail policy"
#elif WWHD_RANDOMIZER_QUIET_SWIFT_SAIL
#define QS_B(a,o) (*(volatile u8*)((a)+(o)))
#define QS_H(a,o) (*(volatile u16*)((a)+(o)))
#define QS_S(a,o) (*(volatile s16*)((a)+(o)))
#define QS_W(a,o) (*(volatile unsigned int*)((a)+(o)))
#define QS_F(a,o) (*(volatile f32*)((a)+(o)))
#ifndef QS_PTR
#define QS_PTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
WWHD_GAME_FUNC(0x0247DD90,s32,qs_next,(u32 actor,s32 mode));
WWHD_GAME_FUNC(0x0200F378,void,qs_angle,(u32 out,s16 target,s16 rate,s16 maximum,s16 minimum));
WWHD_GAME_FUNC(0x0247D4B0,void,qs_move_angle,(u32 actor,s16 angle));
WWHD_GAME_FUNC(0x0247D398,void,qs_sail_angle,(u32 actor));
WWHD_GAME_FUNC(0x025200D4,u32,qs_play,(void));
WWHD_GAME_FUNC(0x0257E490,void,qs_wind_set,(s16 x,s16 y));
WWHD_GAME_FUNC(0x0257DAA8,u32,qs_wind_vector,(void));
WWHD_GAME_FUNC(0x0257DB04,u32,qs_wind_power,(void));
WWHD_GAME_FUNC(0x020195B0,s16,qs_atan,(f32 x,f32 z));
WWHD_GAME_FUNC(0x0247E0D0,s32,qs_obstacle,(u32 actor));
WWHD_GAME_FUNC(0x027F2BF8,s32,qs_frame,(u32 ctrl,f32 frame));
WWHD_GAME_FUNC(0x02474F4C,void,qs_first_decrement,(u32 actor,f32 speed));
WWHD_GAME_FUNC(0x0247D65C,void,qs_decrement,(u32 actor,f32 speed));
WWHD_GAME_FUNC(0x0200ECD4,void,qs_accelerate,(u32 value,f32 target,f32 ratio,f32 maximum,f32 minimum));
WWHD_GAME_FUNC(0x02007D4C,s32,qs_jump_trigger,(u32 pad));
WWHD_GAME_FUNC(0x024751E0,void,qs_control_angle,(u32 actor,s16 angle));
WWHD_GAME_FUNC(0x0247BB24,void,qs_part,(u32 actor,s32 part));
#ifndef QS_FNMSUB
static f32 qs_fnmsub(f32 a,f32 b,f32 c){
#pragma clang fp contract(fast)
    return -(a*b-c);
}
#else
#define qs_fnmsub QS_FNMSUB
#endif
static unsigned int qs_bits(f32 value){union{f32 f;unsigned int w;}v;v.f=value;return v.w;}
static s32 qs_truncate(f32 value){
    if(value!=value||value<=-2147483648.0f)return (-2147483647-1);
    if(value>=2147483648.0f)return 2147483647;
    return (s32)value;
}
static f32 qs_trig(u16 angle,unsigned offset){
    return QS_F((u32)&WWHD_GAME_DATA(0x104A44F8,u8),((u32)angle>>3)*8+offset);
}
WWHD_REPLACE(0x0247E854,s32,randomizer_quiet_sail,(u32 self)){
    if(qs_next(self,1))return 1;
    const u16 stick_angle=QS_H(self,0x68C);
    const f32 stick=QS_F(self,0x6D0);
    const f32 sine=qs_trig(stick_angle,0);
    const f32 turn_product=8192.0f*stick;
    const f32 turn=-(turn_product*sine);
    qs_angle(self+0x64E,(s16)qs_truncate(turn),4,700,256);
    if(!(QS_W(self,0x644)&1))qs_move_angle(self,QS_S(self,0x64E));
    qs_sail_angle(self);
    if(QS_H(self,0x67A)==11 && !(QS_F(QS_PTR(self,0x3B4),0x9C)<9.0f) && QS_B(self,0x637)==1)
        QS_W(self,0x644)|=0x200;
    if(QS_W(self,0x644)&1){
        qs_control_angle(self,0);QS_H(self,0x656)=0;
        goto finish;
    }
    unsigned raised=0;
    f32 maximum=100.0f;
    const u32 play=qs_play();
    if(QS_B(play,0x5D2C)){
        qs_wind_set(0,(s16)((0x5000-(s32)QS_S(self,0x32A))&~0x1FFF));
        if(QS_B(self,0x63D)==0){
            QS_B(self,0x63D)=1;
            /* quiet: no seStart(28A6) or SE_SAIL_POWER_UP */
            const u32 grid=QS_PTR(self,0x6FC);if(grid)QS_H(grid,0x2B8C)=15;
            QS_W(self,0x644)|=0x80000000u;raised=1;
        }
    }else{
        wwhd_ship_normal_sail_wind(self);
        const unsigned active=QS_B(self,0x63D);
        maximum=55.0f;
        if(active==1){
            QS_B(self,0x63D)=0;
            /* quiet: no seStart(28A7) */
            const u32 grid=QS_PTR(self,0x6FC);if(grid)QS_H(grid,0x2B8C)=15;
            QS_W(self,0x644)|=0x80000000u;
        }
    }
    const u32 vector=qs_wind_vector();
    const u32 power=qs_wind_power();
    const f32 strength=QS_F(power,0);
    f32 wind=strength+strength;
    const f32 z=QS_F(vector,8),x=QS_F(vector,0);
    const s16 angle=qs_atan(x,z);
    const s16 relative=(s16)(angle-QS_S(self,0x32A));
    const s32 difference=relative<0?-(s32)relative:relative;
    f32 factor;
    if(difference<0x4000)factor=qs_fnmsub((f32)difference,1.0f/65536.0f,1.0f);
    else if(difference<=0x6000)factor=qs_fnmsub((f32)(difference-0x4000),5.493164e-05f,0.75f);
    else{
        factor=qs_fnmsub((f32)(difference-0x6000),0.00032967035f,0.3f);
        factor=factor>=0.0f?factor:0.0f;
    }
    if(wind>1.0f)wind=1.0f;
    const f32 directional=factor*maximum;
    wind=directional*wind;
    const f32 stock=wind;
    const f32 written=wwhd_ship_sail_target(stock);
    const unsigned scaled=written!=stock;
    wind=written;
    f32 stick_rate=20.0f;
    s32 should_decrement=0;
    f32 decrement_target=0;
    if(qs_obstacle(self)){
        const f32 speed=QS_F(self,0x370);
        const f32 stored=QS_F(self,0x6C4);
        wind=30.0f;
        decrement_target=stored;should_decrement=stored<speed;
    }else if(raised || qs_frame(QS_PTR(self,0x3B4)+0x98,9.0f)){
        const f32 boosted=wind*1.2f;
        if(QS_F(self,0x370)<boosted){
            const u32 tornado=QS_PTR(self,0x704);
            QS_F(self,0x370)=boosted;
            if(tornado || QS_PTR(self,0x70C)){
                const f32 speed=QS_F(self,0x370);
                QS_F(self,0x6C4)=10000.0f;stick_rate=20.0f;
                decrement_target=10000.0f;should_decrement=10000.0f<speed;
            }else{
                const f32 speed=QS_F(self,0x370);
                QS_F(self,0x6C4)=wind;stick_rate=20.0f;
                decrement_target=wind;should_decrement=wind<speed;
            }
        }else{
            const f32 speed=QS_F(self,0x370);
            const f32 stored=QS_F(self,0x6C4);stick_rate=20.0f;
            decrement_target=stored;should_decrement=stored<speed;
        }
    }else{
        const f32 speed=QS_F(self,0x370);
        const f32 stored=QS_F(self,0x6C4);stick_rate=20.0f;
        decrement_target=stored;should_decrement=stored<speed;
    }
    if(should_decrement){
        qs_first_decrement(self,decrement_target);
    }else if(!(QS_W(self,0x644)&0x200) || !(wind>0.0f)){
        const f32 stick_now=QS_F(self,0x6D0);
        const f32 cosine=qs_trig(QS_H(self,0x68C),4);
        const f32 rate=stick_rate*stick_now;
        qs_decrement(self,rate*cosine);
    }else{
        qs_accelerate(self+0x370,wind,0.1f,2.0f,0.1f);
    }
    if(QS_B(qs_play(),0x5292))goto finish;
    if(scaled && qs_bits(wind)==qs_bits(written))wind=stock;
    {
        const u32 link=QS_PTR(qs_play(),0x5B34);
        if(QS_PTR(qs_play(),0x5B2C)!=link)goto finish;
    }
    if(QS_H(self,0x67A)!=11)goto finish;
    if(QS_F(QS_PTR(self,0x3B4),0x9C)<9.0f)goto finish;
    if(QS_W(self,0x644)&1)goto finish;
    if(!(QS_F(self,0x6E0)>30.000002f))goto finish;
    if(QS_PTR(self,0x704) || QS_PTR(self,0x70C))goto finish;
    if(wind<stick_rate)goto finish;
    QS_W(self,0x644)|=0x4000;
    if(qs_jump_trigger(0)){
        f32 vertical=wind;
        QS_W(self,0x644)|=0x41;
        if(vertical<15.0f)vertical=15.0f;
        else if(vertical>40.0f)vertical=40.0f;
        const s16 heading=QS_S(self,0x32A),pitch=QS_S(self,0x654);
        const unsigned part=QS_B(self,0x637);
        const f32 lift=vertical-stick_rate;
        QS_S(self,0x322)=heading;QS_F(self,0x340)=vertical;
        const f32 jump=lift*0.008f;
        QS_H(self,0x66E)=0;QS_S(self,0x654)=pitch>>1;QS_F(self,0x6C8)=jump;
        if(part==1)return 1;
        goto change_part;
    }
finish:
    if(QS_B(self,0x637)==1)return 1;
change_part:
    {
        const u32 body=QS_PTR(self,0x3B4);
        if(QS_F(body,0x98)<0.01f)qs_part(self,1);
    }
    return 1;
}
#endif
