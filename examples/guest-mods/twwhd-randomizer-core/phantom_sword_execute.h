/* Complete public CC0 daBoko_c::execute, decomp a874e1a9.
 * MIT flexible_item_locations: keep the maze sword after event3A08. */
#pragma once
#ifndef R_BOKO
#define R_BOKO(t,a) (*(volatile t*)(a))
#endif
#ifndef RANDOMIZER_BOKO_PROC
#define RANDOMIZER_BOKO_PROC(fn,self) ((void(*)(u32))(fn))(self)
#endif
#ifndef RANDOMIZER_BOKO_STAGE_CALL
#define RANDOMIZER_BOKO_STAGE_CALL(fn,self) ((u32(*)(u32))(fn))(self)
#endif
WWHD_GAME_FUNC(0x025162A4,s32,randomizer_boko_hit,(u32 sphere));
WWHD_GAME_FUNC(0x02516300,u32,randomizer_boko_hit_object,(u32 sphere));
WWHD_GAME_FUNC(0x027F4D5C,void,randomizer_boko_model,(u32 model));
WWHD_GAME_FUNC(0x020C0E08,void,randomizer_boko_top,(u32 actor,s32 mode));
WWHD_GAME_FUNC(0x02520540,s32,randomizer_boko_reverb,(s32 room));
WWHD_GAME_FUNC(0x025E1A40,void,randomizer_boko_sound,(u32 id,u32 pos,u32 flags,s32 reverb));
WWHD_GAME_FUNC(0x025D9F70,s32,randomizer_boko_water,(u32 pos,u32 height));
WWHD_GAME_FUNC(0x025200D4,u32,randomizer_boko_game,(void));
WWHD_GAME_FUNC(0x025A847C,void,randomizer_boko_particle,(u32 particle,u32 group,u32 id,u32 pos,u32 angle,u32 scale,u32 alpha,u32 callback,s32 room,u32 color,u32 environment,u32 extra));
WWHD_GAME_FUNC(0x020C0EF8,void,randomizer_boko_flame,(u32 actor));
WWHD_GAME_FUNC(0x025A5AC8,void,randomizer_boko_end_flame,(u32 callback));
WWHD_GAME_FUNC(0x02516264,void,randomizer_boko_reset_hit,(u32 sphere));
WWHD_GAME_FUNC(0x02018D40,void,randomizer_boko_center,(u32 sphere,u32 pos));
WWHD_GAME_FUNC(0x0200E240,void,randomizer_boko_collision,(u32 cc,u32 sphere));
WWHD_GAME_FUNC(0x02516C14,void,randomizer_boko_mass,(u32 manager,u32 sphere,u32 mass));
WWHD_GAME_FUNC(0x020C1684,void,randomizer_boko_chain,(u32 actor));
WWHD_GAME_FUNC(0x025B8B94,s32,randomizer_boko_event,(u32 event,u32 flag));
WWHD_GAME_FUNC(0x025E742C,void,randomizer_boko_animation,(u32 animation));
WWHD_GAME_FUNC(0x025D57E0,void,randomizer_boko_delete,(u32 actor));
static u32 randomizer_boko_stage_info(void) {
    const u32 dt=randomizer_boko_game()+0x5150;
    return RANDOMIZER_BOKO_STAGE_CALL(R_BOKO(u32,R_BOKO(u32,dt)+0x15C),dt);
}
static s32 randomizer_boko_sword_disappears(void) {
    const s32 event=randomizer_boko_event(WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x644,0x3A08);
    if(!event)return event;
    const u32 stage=randomizer_boko_game()+0x5134;
    static const u8 maze[]="GanonJ";
    for(u32 i=0;i<sizeof(maze);i++) {
        const u8 current=R_BOKO(u8,stage+i),expected=maze[i];
        if(current!=expected)return (s32)expected-(s32)current;
        if(!current)return 0;
    }
    return 0;
}
WWHD_REPLACE(0x020C183C,s32,randomizer_phantom_sword_execute,(u32 actor)) {
    const s16 delay=R_BOKO(s16,actor+0x446);
    if(delay) {R_BOKO(s16,actor+0x446)=(s16)(delay-1);return 1;}
    const s16 slot=R_BOKO(s16,actor+0xA2E);
    if(slot) {
        const u32 self=actor+R_BOKO(s16,actor+0xA2C);
        const u32 target=slot<0?R_BOKO(u32,actor+0xA30):
            R_BOKO(u32,R_BOKO(u32,self+R_BOKO(s16,actor+0xA32))+(u32)slot*8+4);
        RANDOMIZER_BOKO_PROC(target,self);
    }
    const u32 y=R_BOKO(u32,actor+0x318),z=R_BOKO(u32,actor+0x31C),x=R_BOKO(u32,actor+0x314);
    R_BOKO(u32,actor+0x380)=y;R_BOKO(u32,actor+0x398)=z;
    R_BOKO(u32,actor+0x37C)=x;R_BOKO(u32,actor+0x384)=z;
    R_BOKO(u32,actor+0x390)=x;R_BOKO(u32,actor+0x394)=y;
    s32 extinguish=0;
    if(randomizer_boko_hit(actor+0x700)) {
        const u32 hit=randomizer_boko_hit_object(actor+0x700);
        const s16 timer=R_BOKO(s16,actor+0x43C);
        if(hit && timer>=0) {
            const u32 flags=R_BOKO(u32,hit+0x10);
            if(timer>0) {
                if(flags&0x100) {R_BOKO(s16,actor+0x43C)=1;extinguish=1;}
            } else if(flags&0x20200) {
                const s32 reverb=randomizer_boko_reverb(R_BOKO(s8,actor+0x326));
                randomizer_boko_sound(0x6902,actor+0x44C,0,reverb);
                R_BOKO(u8,actor+0x2DB)=21;R_BOKO(s16,actor+0x43C)=900;
            }
        }
    }
    randomizer_boko_model(R_BOKO(u32,actor+0x3B4));
    randomizer_boko_top(actor,0);
    const f32 zero=WWHD_GAME_DATA(0x1000A4B8,f32);
    if(R_BOKO(s16,actor+0x43C)) {
        if(R_BOKO(u8,actor+0x430)!=2)R_BOKO(s16,actor+0x43C)--;
        s32 wet=extinguish;
        if(!wet) {
            f32 height;
            if(randomizer_boko_water(actor+0x44C,(u32)&height))wet=!(height<R_BOKO(f32,actor+0x450));
        }
        const s32 stopped=wet || !R_BOKO(s16,actor+0x43C);
        if(wet) {
            if(R_BOKO(s16,actor+0x43C)) {
                const u32 game=randomizer_boko_game();
                randomizer_boko_particle(R_BOKO(u32,game+0x5AB0),0,0x35A,actor+0x44C,0,0,255,0,-1,0,0,0);
            }
            R_BOKO(f32,actor+0x964)=zero;R_BOKO(u8,actor+0x2DB)=14;R_BOKO(s16,actor+0x43C)=0;
        } else if(R_BOKO(s16,actor+0x43C))randomizer_boko_flame(actor);
        else {R_BOKO(u8,actor+0x2DB)=14;R_BOKO(f32,actor+0x964)=zero;}
        if(stopped && R_BOKO(u32,actor+0x4B0)) {
            const s32 reverb=randomizer_boko_reverb(R_BOKO(s8,actor+0x326));
            randomizer_boko_sound(0x6904,actor+0x44C,0,reverb);
            randomizer_boko_end_flame(actor+0x4AC);
        }
        const u32 top_y=R_BOKO(u32,actor+0x450),top_z=R_BOKO(u32,actor+0x454),top_x=R_BOKO(u32,actor+0x44C);
        R_BOKO(u32,actor+0x484)=top_z;R_BOKO(u32,actor+0x47C)=top_x;
        f32 factor=WWHD_GAME_DATA(0x1000A48C,f32);
        R_BOKO(u32,actor+0x480)=top_y;
        if(randomizer_boko_stage_info()) {
            const u32 kind=(R_BOKO(u32,randomizer_boko_stage_info()+12)>>16)&7;
            if(kind==1 || kind==4)factor=WWHD_GAME_DATA(0x1000A4F0,f32);
        }
        R_BOKO(f32,actor+0x490)=(WWHD_GAME_DATA(0x1000A4F4,f32)*factor)*R_BOKO(f32,actor+0x964);
    } else {randomizer_boko_reset_hit(actor+0x700);R_BOKO(f32,actor+0x490)=zero;}
    if(R_BOKO(u32,actor+0xB0)==0) {
        const u32 flags=R_BOKO(u32,actor+0x700);
        R_BOKO(u32,actor+0x700)=R_BOKO(s16,actor+0x43C)>0?flags|1:flags&~1u;
        randomizer_boko_center(actor+0x818,actor+0x44C);
        randomizer_boko_collision(randomizer_boko_game()+0x26A4,actor+0x700);
        const u8 mode=R_BOKO(u8,actor+0x430);
        randomizer_boko_mass(randomizer_boko_game()+0x4EF8,actor+0x700,mode==3?1:4);
    }
    if(R_BOKO(u32,actor+0xA28))randomizer_boko_chain(actor);
    if(R_BOKO(u32,actor+0xB0)==5 && randomizer_boko_sword_disappears()) {
        randomizer_boko_animation(actor+0x3B8);
        if((R_BOKO(u8,actor+0x3C7)&1) || R_BOKO(f32,actor+0x3B8)==zero)randomizer_boko_delete(actor);
    }
    return 1;
}
