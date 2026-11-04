/* Complete CC0 a874e1a9 People/Lenzo phase_1 consumers. Preserve the actual
 * out-of-line constructors and parameter helpers; replace only chart queries. */
#pragma once
WWHD_GAME_FUNC(0x022BE7CC,u32,rwp_people_ctor,(u32 actor));
WWHD_GAME_FUNC(0x022BE784,u32,rwp_people_argument,(u32 actor));
WWHD_GAME_FUNC(0x022CC418,u32,rwp_photo_ctor,(u32 actor));
WWHD_GAME_FUNC(0x022CC3EC,u32,rwp_photo_argument,(u32 actor));
WWHD_GAME_FUNC(0x02554288,s32,rwp_day,(void));
#ifndef RWP_PTR
#define RWP_PTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
#ifndef RWP_LITERAL
#define RWP_LITERAL(a) (a)
#endif
typedef struct {volatile u32 text,vtable;} rwp_string;
static void rwp_terminate(rwp_string* string) {
    const u32 callback=RWP_PTR(string->vtable,0x14);
    ((void(*)(u32))callback)((u32)string);
}
static s32 rwp_stage_ocmera(void) {
    rwp_string name={RWP_LITERAL(0x100203E0),0x10020114};
    const u32 play=randomizer_people_game();
    rwp_string stage={play+0x5134,0x10020114};
    rwp_terminate(&name);rwp_terminate(&name);
    const u32 captured=name.text;
    rwp_terminate(&stage);
    const u32 text=stage.text;
    if(captured==text)return 1;
    for(unsigned int i=0;i<0x40001;i++) {
        const u8 a=*(volatile u8*)(captured+i),b=*(volatile u8*)(text+i);
        if(a!=b)return 0;
        if(!a)return 1;
    }
    return 0;
}
WWHD_REPLACE(0x022BE8C0,s32,randomizer_people_first_phase,(u32 actor)) {
    if(!(RWQ32(actor,0x2E4)&8)) {
        if(actor)(void)rwp_people_ctor(actor);
        RWQ32(actor,0x2E4)|=8;
    }
    const u32 argument=rwp_people_argument(actor);
    switch(RWQ8(actor,0x905)) {
    case 6:
        if(rwp_stage_ocmera()) {
            if(rwq_event(0x6904)||argument!=(u32)(s32)*(volatile s16*)(randomizer_people_game()+0x513C))return 3;
            RWQ8(actor,0x904)=1;
            RWQ32(actor,0x8CC)|=0x40;
            return 2;
        }
        break;
    case 8:case 10:
        if(argument!=0xFF&&((argument<<1)&0xFE)!=(randomizer_people_reg(0xB907)&6))return 3;
        break;
    case 17:case 18: {
        const s32 day=rwp_day();
        if(argument==0&&(day==5||day==6))return 3;
        if(argument==1&&day!=5&&day!=6)return 3;
        break;
    }
    default:break;
    }
    RWQ8(actor,0x904)=1;
    return 2;
}
WWHD_REPLACE(0x022CC4F8,s32,randomizer_photo_first_phase,(u32 actor)) {
    if(!(RWQ32(actor,0x2E4)&8)) {
        if(actor)(void)rwp_photo_ctor(actor);
        RWQ32(actor,0x2E4)|=8;
    }
    const u32 argument=rwp_photo_argument(actor);
    if(argument!=0xFF) {
        if(argument==0) {
            if(!rwq_event(0x6904)&&argument!=(u32)(s32)*(volatile s16*)(randomizer_people_game()+0x513C))return 3;
        } else {
            if(rwq_event(0x6904)||argument!=(u32)(s32)*(volatile s16*)(randomizer_people_game()+0x513C))return 3;
            RWQ8(actor,0xB35)=4;
        }
    }
    RWQ8(actor,0xB36)=1;
    return 2;
}
