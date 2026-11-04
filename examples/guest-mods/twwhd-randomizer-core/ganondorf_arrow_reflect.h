/* Complete CC0 daArrow_c::ShieldReflect, public a874e1a9.
 * MIT fix_vanilla_bugs: do not start reflected-shot demo above25 health. */
#pragma once
#ifndef R_ARROW
#define R_ARROW(t,a) (*(volatile t*)(a))
#endif
WWHD_GAME_FUNC(0x025D5578,s32,randomizer_arrow_find,(s32 name,u32 output));
WWHD_GAME_FUNC(0x024EDFCC,s32,randomizer_arrow_lock,(u32 attention));
WWHD_GAME_FUNC(0x024EC8D0,u32,randomizer_arrow_target,(u32 attention,s32 index));
WWHD_GAME_FUNC(0x0200F974,s32,randomizer_arrow_angle,(u32 origin,u32 target));
WWHD_GAME_FUNC(0x025A847C,u32,randomizer_arrow_particle,(u32 control,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 room,u32 primary,u32 environment,u32 scale2d));
WWHD_GAME_FUNC(0x020195B0,s32,randomizer_arrow_atan,(f64 y,f64 x));
/* Match the native fmuls second-operand rounding even for a wider F1 value. */
static f32 randomizer_arrow_multiply(f64 a,f64 b) {
    union {f64 value;unsigned long long bits;} rounded={.value=b};
    rounded.bits=(rounded.bits&0xFFFFFFFFF8000000ull)+(rounded.bits&0x8000000ull);
    return (f32)(a*rounded.value);
}
WWHD_REPLACE(0x02053B18,void,randomizer_ganondorf_arrow_reflect,(u32 actor)) {
    const f64 velocity=randomizer_hyrule_sqrt(randomizer_hyrule_square_magnitude((const f32*)(actor+0x33C)));
    const u32 player=R_ARROW(u32,wwhd_dComIfG_getGameInfo()+0x5B34);
    const s16 yaw=(s16)(R_ARROW(s16,player+0x32A)+R_ARROW(s16,player+0x3D2));
    s16 pitch=R_ARROW(s16,player+0x3D0);
    unsigned int ganondorf;
    if(randomizer_arrow_find(0xF6,(u32)&ganondorf) && ganondorf) {
        const u32 attention=wwhd_dComIfG_getGameInfo()+0x5804;
        if(randomizer_arrow_lock(attention) && randomizer_arrow_target(attention,0)==ganondorf) {
            /* Upstream loads x before testing the unsigned health byte. */
            union {unsigned int words[3];f32 values[3];} chest;
            chest.words[0]=R_ARROW(u32,ganondorf+0x314);
            if(R_ARROW(u8,ganondorf+0x3A1)<=25) {
                chest.words[1]=R_ARROW(u32,ganondorf+0x318);
                chest.words[2]=R_ARROW(u32,ganondorf+0x31C);
                chest.values[1]=WWHD_GAME_DATA(0x1047BA90,f32)+WWHD_GAME_DATA(0x1000744C,f32);
                const s32 angle=randomizer_arrow_angle(player+0x314,(u32)chest.values);
                R_ARROW(u32,ganondorf+0xB0)=0x23;
                pitch=(s16)-angle;
                R_ARROW(s16,actor+0x724)=(s16)(WWHD_GAME_DATA(0x1047B68E,s16)+15);
                const u32 particle=R_ARROW(u32,wwhd_dComIfG_getGameInfo()+0x5AB0);
                R_ARROW(u32,actor+0x728)=randomizer_arrow_particle(particle,0,0x3EE,player+0x314,0,0,255,0,-1,0,0,0);
            }
        }
    }
    const u32 yi=(u16)yaw&0xFFF8u,xi=(u16)pitch&0xFFF8u;
    const f32 sy=R_ARROW(f32,0x104A44F8+yi),cx=R_ARROW(f32,0x104A44FC+xi);
    const f32 sx=R_ARROW(f32,0x104A44F8+xi),cy=R_ARROW(f32,0x104A44FC+yi);
    const f32 vx=randomizer_arrow_multiply(randomizer_arrow_multiply(sy,cx),velocity);
    const f32 vy=randomizer_arrow_multiply(-sx,velocity);
    const f32 vz=randomizer_arrow_multiply(randomizer_arrow_multiply(cy,cx),velocity);
    R_ARROW(f32,actor+0x33C)=vx;R_ARROW(f32,actor+0x340)=vy;
    union {unsigned int words[3];f32 values[3];} horizontal;
    horizontal.words[1]=WWHD_GAME_DATA(0x100073D0,u32);
    horizontal.values[0]=vx;horizontal.values[2]=vz;
    R_ARROW(f32,actor+0x344)=vz;
    const f64 xz=randomizer_hyrule_sqrt(randomizer_hyrule_square_magnitude(horizontal.values));
    const s32 angle_x=randomizer_arrow_atan(-R_ARROW(f32,actor+0x340),-xz);
    const f32 final_x=R_ARROW(f32,actor+0x33C),final_z=R_ARROW(f32,actor+0x344);
    R_ARROW(s16,actor+0x328)=(s16)angle_x;
    R_ARROW(s16,actor+0x32A)=(s16)randomizer_arrow_atan(-final_x,-final_z);
    R_ARROW(s16,actor+0x32C)=0;
}
