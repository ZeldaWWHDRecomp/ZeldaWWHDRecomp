/* Complete CC0 daObjGryw00 switch-wait and level-move consumers, a874e1a9.
 * MIT misc_rando_features: fivefold pond animation/water speeds. */
#pragma once
#ifndef R_POND
#define R_POND(t,address) (*(volatile t*)(address))
#endif
WWHD_GAME_FUNC(0x025BA0C0,s32,randomizer_pond_switch,(u32 info,s32 index,s32 room));
WWHD_GAME_FUNC(0x0234CEE8,void,randomizer_pond_particles,(u32 actor));
WWHD_GAME_FUNC(0x027F2BF8,s32,randomizer_pond_frame,(u32 control,f32 frame));
WWHD_REPLACE(0x0234D024,void,randomizer_pond_wait,(u32 actor)) {
    const s32 sw=R_POND(s32,actor+0x51C);
    if(sw==255)return;
    if(randomizer_pond_switch(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x20,
                             sw,R_POND(s8,actor+0x2FE))!=1)return;
    R_POND(f32,actor+0x53C)=WWHD_GAME_DATA(0x10029A28,f32);
    R_POND(f32,actor+0x540)=-0.5263158f;
    const f32 speed=WWHD_GAME_DATA(0x10029A38,f32);
    R_POND(f32,actor+0x490)=speed;
    R_POND(f32,actor+0x41C)=speed;
    R_POND(f32,actor+0x530)=speed;
    R_POND(f32,actor+0x534)=0.5263158f;
    randomizer_pond_particles(actor);
    R_POND(s32,actor+0x528)=0;
    R_POND(s32,actor+0x548)=1;
    R_POND(s16,actor+0x520)=0;
    R_POND(s32,actor+0x54C)=62;
    R_POND(u32,actor+0x524)=0x0234D0F0;
    R_POND(s16,actor+0x522)=-1;
}
WWHD_REPLACE(0x0234D14C,void,randomizer_pond_level_wait,(u32 actor)) {
    const f32 frame=WWHD_GAME_DATA(0x10029A24,f32);
    if(!randomizer_pond_frame(actor+0x41C,frame))return;
    const f32 maximum=WWHD_GAME_DATA(0x10029A20,f32);
    R_POND(s16,actor+0x522)=-1;
    const f32 zero=WWHD_GAME_DATA(0x100299D8,f32);
    R_POND(f32,actor+0x530)=maximum;
    R_POND(f32,actor+0x490)=zero;
    R_POND(s16,actor+0x520)=0;
    R_POND(f32,actor+0x534)=8.388704f;
    R_POND(u32,actor+0x524)=0x0234D1EC;
    R_POND(f32,actor+0x494)=frame;
}
