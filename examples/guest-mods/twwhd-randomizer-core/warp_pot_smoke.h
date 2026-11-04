/* Complete CC0 a874e1a9 d_a_obj_warpt.cpp::createInit023BA4E0.
 * MIT misc_rando_features.asm023BA514/34 selects red/purple smoke for
 * inter-dungeon event indices2/5. Seed preparation validates the fixed colors
 * and unchanged default tables; unsupported color variants do not fall back. */
#pragma once
#define RWP8(a,o) (*(volatile u8*)((a)+(o)))
#define RWP32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define RWPF(a,o) (*(volatile f32*)((a)+(o)))
#ifndef RWPPTR
#define RWPPTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
WWHD_GAME_FUNC(0x023B985C,u8,rwp_has_lid,(u32 actor));
WWHD_GAME_FUNC(0x023B9CB0,void,rwp_mode,(u32 actor,s32 operation,s32 mode));
WWHD_GAME_FUNC(0x023BA134,void,rwp_initialize_collision,(u32 actor));
WWHD_GAME_FUNC(0x023BA1BC,void,rwp_set_collision,(u32 actor));
WWHD_GAME_FUNC(0x023BA250,void,rwp_set_matrix,(u32 actor));
WWHD_GAME_FUNC(0x024EEA6C,void,rwp_register,(u32 background,u32 collision,u32 actor));
WWHD_GAME_FUNC(0x024EFF44,void,rwp_wall,(u32 circle,f32 height,f32 radius));
WWHD_GAME_FUNC(0x024F06B4,void,rwp_acch,(u32 acch,u32 current,u32 old,u32 actor,s32 count,u32 circle,u32 speed,u32 angle,u32 shape));
WWHD_GAME_FUNC(0x025881A4,s32,rwp_player_in_circle,(u32 position,f32 radius,f32 height));
WWHD_GAME_FUNC(0x025D674C,void,rwp_cull_box,(u32 actor,f32 minx,f32 miny,f32 minz,f32 maxx,f32 maxy,f32 maxz));
WWHD_GAME_FUNC(0x025D6870,void,rwp_move,(u32 actor,u32 collision));
WWHD_REPLACE(0x023BA4E0,void,randomizer_warp_pot_create_init,(u32 actor)) {
    const unsigned int primary_index=RWP32(actor,0x3D0);
    const unsigned int primary_event=RWP32(actor,0x3D4);
    const u32 primary=(u32)&WWHD_GAME_DATA(0x100337D4,u8)+4u*primary_index;
    RWP32(actor,0x9E8)=(primary_event==2||primary_event==5)?0xE5101B80u:RWP32(primary,0);
    const unsigned int environment_index=RWP32(actor,0x3D0);
    const unsigned int environment_event=RWP32(actor,0x3D4);
    const u32 environment=(u32)&WWHD_GAME_DATA(0x100337E8,u8)+4u*environment_index;
    RWP32(actor,0x9EC)=(environment_event==2||environment_event==5)?0x3C379D80u:RWP32(environment,0);
    const u32 model=RWPPTR(actor,0x3EC);
    RWPPTR(actor,0x348)=model?model+0xC8:0;
    rwp_cull_box(actor,-80.0f,-0.0f,-80.0f,80.0f,160.0f,80.0f);
    const u32 lid=RWPPTR(actor,0x45C);
    if(lid){
        RWP32(lid,0xA8)=0x024EE658;
        const u32 play=wwhd_dComIfG_getGameInfo();
        rwp_register(play+0x12A0,RWPPTR(actor,0x45C),actor);
    }
    RWP32(RWPPTR(actor,0x3F4),0xA8)=0x024EE658;
    const u32 play=wwhd_dComIfG_getGameInfo();
    rwp_register(play+0x12A0,RWPPTR(actor,0x3F4),actor);
    const u8 has_lid=rwp_has_lid(actor);
    rwp_mode(actor,0,has_lid?1:0);
    RWP32(RWPPTR(actor,0x428),0xB0)=0x023B9E88;
    rwp_initialize_collision(actor);
    rwp_set_collision(actor);
    rwp_wall(actor+0x96C,30.0f,50.0f);
    rwp_acch(actor+0x7A8,actor+0x314,actor+0x300,actor,1,actor+0x96C,actor+0x33C,0,0);
    RWP32(actor,0x7D0)=RWP32(actor,0x7D0)|0x40C;
    RWPF(actor,0x374)=-6.5f;
    rwp_move(actor,0);
    /* Preserve native raw position bits and interleaved capture/store order. */
    _Alignas(4) unsigned int position[3];
    const unsigned int x=RWP32(actor,0x314),y=RWP32(actor,0x318);
    position[0]=x;
    const unsigned int z=RWP32(actor,0x31C);
    position[1]=y;
    position[2]=z;
    const s32 inside=rwp_player_in_circle((u32)position,50.0f,100.0f);
    RWP8(actor,0x9BC)=inside?30:5;
    rwp_set_matrix(actor);
}
