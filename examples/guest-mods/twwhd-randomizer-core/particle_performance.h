/* Complete CC0 a874e1a9 d_particle_resources.cpp createCommon consumer.
 * MIT performance.asm changes only maxParticles to 1500: allocation size,
 * emitter/group limits, failure continuation and all callbacks stay native.
 * USA OSReport is an actual dispatchable import, not a native original entry. */
#pragma once
#ifndef WWHD_RANDOMIZER_PARTICLE_PERFORMANCE
#error "Regenerate prepared seed data: missing validated particle policy"
#elif WWHD_RANDOMIZER_PARTICLE_PERFORMANCE != 0 && WWHD_RANDOMIZER_PARTICLE_PERFORMANCE != 1
#error "Invalid validated particle policy"
#elif WWHD_RANDOMIZER_PARTICLE_PERFORMANCE
#ifndef R_PARTICLE
#define R_PARTICLE(t,a) (*(volatile t*)(a))
#endif
#ifndef PARTICLE_DATA
#define PARTICLE_DATA(a) ((u32)&WWHD_GAME_DATA(a,u8))
#endif
WWHD_GAME_FUNC(0x02756170,u32,particle_context,(u32 manager,u32 context));
WWHD_GAME_FUNC(0x026124B0,u32,particle_load_bank,(u32 resources,u32 archive,u32 resource,u32 flags));
WWHD_GAME_FUNC(0x027E2DC0,u32,particle_bank,(void));
WWHD_GAME_FUNC(0x0273B050,u32,particle_new_heap,(u32 size,u32 heap,u32 align));
WWHD_GAME_FUNC(0x0273AD10,u32,particle_new,(u32 size));
WWHD_GAME_FUNC(0x02827000,u32,particle_resource_ctor,(u32 self,u32 data,u32 heap,u32 bank));
WWHD_GAME_FUNC(0x027EC1FC,u32,particle_heap_size,(u32 heap));
WWHD_GAME_FUNC(0xC0009EE8,void,particle_report,(u32 format,...));
WWHD_GAME_FUNC(0x028010F8,void,particle_render_init,(u32 self,u32 texture,u32 heap));
WWHD_GAME_FUNC(0x02820DE4,u32,particle_emitter_ctor,(u32 self,u32 resources,u32 particles,u32 emitters,u32 groups,u32 heap,u32 render));
WWHD_GAME_FUNC(0x027DFA24,u32,particle_find_texture,(u32 table,u32 name));
WWHD_GAME_FUNC(0x02822304,void,particle_register_texture,(u32 manager,u32 texture,u32 id));
WWHD_GAME_FUNC(0x026066C4,u32,particle_get_res,(u32 controller,u32 name,u32 index));
WWHD_GAME_FUNC(0x025A47E0,u32,particle_model_ctor,(u32 self,u32 first,u32 second,u32 heap));
WWHD_GAME_FUNC(0x02826FC4,u32,particle_resource_get,(u32 manager,u32 id));
WWHD_GAME_FUNC(0x025A7674,s32,particle_new_simple,(u32 self,u32 id,u32 group));
WWHD_GAME_FUNC(0x025A7724,void,particle_defaults,(u32 self));
WWHD_GAME_FUNC(0x0273AA24,void,particle_assert,(u32 file,u32 line,u32 message));
static void randomizer_particle_name(u32 *name,u32 text){name[0]=text;name[1]=PARTICLE_DATA(0x10051A34);}
static void randomizer_particle_report(u32 self,u32 format){
    const u32 heap=R_PARTICLE(u32,self+0x128);
    const u32 size=particle_heap_size(heap);
    particle_report(format,size);
}
WWHD_REPLACE(0x025A7838,void,randomizer_particle_common,(u32 self,u32 data)){
    u32 old_mode=1,heap=R_PARTICLE(u32,self+0x128);
    if(heap)old_mode=particle_context(R_PARTICLE(u32,PARTICLE_DATA(0x101F8B4C)),heap);
    u32 first_name[2],second_name[2],resource_a[2],resource_b[2];
    randomizer_particle_name(first_name,PARTICLE_DATA(0x10051D78));
    randomizer_particle_name(second_name,PARTICLE_DATA(0x10051D84));
    (void)particle_load_bank(R_PARTICLE(u32,PARTICLE_DATA(0x101F4F7C)),(u32)first_name,(u32)second_name,0);
    u32 bank=particle_bank();R_PARTICLE(u32,self+0x415C)=bank;
    randomizer_particle_report(self,PARTICLE_DATA(0x10051D94));
    heap=R_PARTICLE(u32,self+0x128);
    u32 resources=particle_new_heap(12,heap,4);
    if(resources){bank=R_PARTICLE(u32,self+0x415C);heap=R_PARTICLE(u32,self+0x128);resources=particle_resource_ctor(resources,data,heap,bank);}
    R_PARTICLE(u32,self+0x12C)=resources;
    if(!resources)particle_assert(PARTICLE_DATA(0x10051DA4),0x711,PARTICLE_DATA(0x10051DB4));
    randomizer_particle_report(self,PARTICLE_DATA(0x10051DCC));
    heap=R_PARTICLE(u32,self+0x128);
    u32 render=particle_new_heap(16,heap,4);
    if(render){
        R_PARTICLE(u32,render)=0;R_PARTICLE(u32,render+4)=0;
        u32 list=render+8;if(!list)list=particle_new(8);
        if(list){R_PARTICLE(u32,list+4)=0;R_PARTICLE(u32,list)=0;}
    }
    R_PARTICLE(u32,self+0x4158)=render;
    const u32 system=R_PARTICLE(u32,PARTICLE_DATA(0x101F8A94));
    const u32 count=R_PARTICLE(u32,system+16);
    u32 texture=0;if(count>1)texture=R_PARTICLE(u32,system+20)+0x5C;
    heap=R_PARTICLE(u32,self+0x128);particle_render_init(render,texture,heap);
    heap=R_PARTICLE(u32,self+0x128);
    u32 manager=particle_new_heap(0x580BB8,heap,4);
    if(manager){resources=R_PARTICLE(u32,self+0x12C);const u32 control=R_PARTICLE(u32,self+0x4158);heap=R_PARTICLE(u32,self+0x128);manager=particle_emitter_ctor(manager,resources,1500,150,200,heap,control);}
    R_PARTICLE(u32,PARTICLE_DATA(0x1047B2D4))=manager;
    if(!manager)particle_assert(PARTICLE_DATA(0x10051DA4),0x726,PARTICLE_DATA(0x10051DDC));
    randomizer_particle_report(self,PARTICLE_DATA(0x10051DF0));
    randomizer_particle_name(second_name,PARTICLE_DATA(0x10051D84));
    randomizer_particle_name(first_name,PARTICLE_DATA(0x10051D78));
    if(particle_load_bank(R_PARTICLE(u32,PARTICLE_DATA(0x101F4F7C)),(u32)first_name,(u32)second_name,0)){
        const u32 res=particle_bank(),offset=R_PARTICLE(u32,res+0x4C);
        const u32 table=particle_find_texture(offset?res+0x4C+offset:0,PARTICLE_DATA(0x10051E00));
        if(table){
            const u32 relative=R_PARTICLE(u32,table),size=R_PARTICLE(u32,table+4);
            u32 cursor=relative?table+relative:0,n=size>>3;
            while(n--){const u32 id=R_PARTICLE(u32,cursor),tex=R_PARTICLE(u32,cursor+4);cursor+=8;
                if(tex)particle_register_texture(R_PARTICLE(u32,PARTICLE_DATA(0x1047B2D4)),tex,(u16)id);
            }
        }
    }
    heap=R_PARTICLE(u32,self+0x128);
    u32 model=particle_new_heap(16,heap,4);
    if(model){
        randomizer_particle_name(resource_a,PARTICLE_DATA(0x10051D70));
        u32 controller=R_PARTICLE(u32,PARTICLE_DATA(0x101F4F28));
        const u32 first=particle_get_res(controller,(u32)resource_a,0x31);
        randomizer_particle_name(resource_b,PARTICLE_DATA(0x10051D70));
        controller=R_PARTICLE(u32,PARTICLE_DATA(0x101F4F28));
        const u32 second=particle_get_res(controller,(u32)resource_b,0x30);
        heap=R_PARTICLE(u32,self+0x128);model=particle_model_ctor(model,first,second,heap);
    }
    R_PARTICLE(u32,self+0x130)=model;
    if(!model)particle_assert(PARTICLE_DATA(0x10051DA4),0x74F,PARTICLE_DATA(0x10051E14));
    randomizer_particle_report(self,PARTICLE_DATA(0x10051E2C));
    const u32 ids=PARTICLE_DATA(0x101EA50C);
    for(u32 i=0;i<8;i++){
        const u32 r=R_PARTICLE(u32,self+0x12C);const u16 id=R_PARTICLE(u16,ids+i*2);
        if(particle_resource_get(R_PARTICLE(u32,r+4),id))
            (void)particle_new_simple(self,id,id&0x4000?4:0);
    }
    randomizer_particle_report(self,PARTICLE_DATA(0x10051E3C));
    R_PARTICLE(u8,self+0x147)=R_PARTICLE(u8,self+0x146);
    particle_defaults(self);
    if(old_mode!=1)(void)particle_context(R_PARTICLE(u32,PARTICLE_DATA(0x101F8B4C)),old_mode);
}
#endif
