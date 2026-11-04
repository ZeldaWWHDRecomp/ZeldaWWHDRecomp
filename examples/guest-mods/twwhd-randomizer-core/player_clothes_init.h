/* Complete public CC0 a874e1a9 playerInit (02410BE8), d_a_player_main_04b.cpp.
 * The existing entrance_spawn owner calls this source body inside its original
 * nested actor/resource scope. MIT randomizer replaces only the late clothing
 * predicate; native initialization and all callback seams remain in order. */
#pragma once
#ifndef R_LINK_INIT
#define R_LINK_INIT(t,a) (*(volatile t*)(a))
#endif
#ifndef LINK_INIT_DATA
#define LINK_INIT_DATA(a) ((u32)&WWHD_GAME_DATA(a,u8))
#endif
WWHD_GAME_FUNC(0x025D63E8,s32,link_init_heap,(u32 self,u32 callback,u32 size));
WWHD_GAME_FUNC(0x0273AA24,void,link_init_assert,(u32 file,u32 line,u32 message));
WWHD_GAME_FUNC(0x027F3F94,u32,link_init_joint_tree,(u32 data));
WWHD_GAME_FUNC(0x02444F48,void,link_init_terminate,(u32 safe));
WWHD_GAME_FUNC(0x024F06B4,void,link_init_acch,(u32 self,u32 pos,u32 old,u32 actor,s32 count,u32 circles,u32 speed,u32 angle,u32 shape));
WWHD_GAME_FUNC(0x024EFF44,void,link_init_wall,(u32 self,f32 height,f32 radius));
WWHD_GAME_FUNC(0x02515F14,void,link_init_status,(u32 self,u32 weight,u32 type,u32 actor));
WWHD_GAME_FUNC(0x02516518,void,link_init_cylinder,(u32 self,u32 source));
WWHD_GAME_FUNC(0x025164C0,void,link_init_capsule,(u32 self,u32 source));
WWHD_GAME_FUNC(0x0251677C,void,link_init_sphere,(u32 self,u32 source));
WWHD_GAME_FUNC(0x02410888,void,link_init_anime_heap,(u32 self,u32 heap,u32 type));
WWHD_GAME_FUNC(0x025E3630,u32,link_init_solid_heap,(u32 size,u32 align));
WWHD_GAME_FUNC(0x025E37D8,void,link_init_restore_heap,(void));
WWHD_GAME_FUNC(0x023DC63C,void,link_init_clear_keep,(u32 keep));
WWHD_GAME_FUNC(0x023E07C0,void,link_init_reset_sound,(u32 self));
WWHD_GAME_FUNC(0x026066C4,u32,link_init_resource,(u32 system,u32 safe,u32 index));
WWHD_GAME_FUNC(0x02543534,u32,link_init_start_demo,(u32 event,u32 demo));
WWHD_GAME_FUNC(0x023DCA08,void,link_init_end_damage,(u32 self));
WWHD_GAME_FUNC(0x0252D6E8,void,link_init_mirror,(u32 mirror,u32 value));
WWHD_GAME_FUNC(0x025564B4,void,link_init_light,(u32 light));
WWHD_GAME_FUNC(0x027ED1F0,u32,link_init_name,(u32 table,u32 index));
WWHD_GAME_FUNC(0x028EFFD0,void,link_init_array,(u32 array,u32 count,u32 stride,u32 constructor));
WWHD_GAME_FUNC(0x0272B8B8,void,link_init_copy_texture_list,(u32 list,u32 headers));
#ifndef LINK_INIT_ASSURE
#define LINK_INIT_ASSURE(s) (((void(*)(u32))R_LINK_INIT(u32,(s)[1]+0x14))((u32)(s)))
#endif
#define link_init_assertion(line,text) link_init_assert(LINK_INIT_DATA(0x10035D20),(line),LINK_INIT_DATA(text))
static s32 link_init_equal(u32 literal,u32 text){
    u32 a[2]={literal,LINK_INIT_DATA(0x10034B24)},b[2]={text,LINK_INIT_DATA(0x10034B24)};
    link_init_terminate((u32)a);LINK_INIT_ASSURE(a);u32 first=a[0];LINK_INIT_ASSURE(b);
    u32 second=b[0];if(first==second)return 1;first=a[0];second=b[0];
    for(u32 n=0;n<0x40001;n++,first++,second++){
        const u8 x=R_LINK_INIT(u8,first),y=R_LINK_INIT(u8,second);
        if(x!=y)return 0;if(!x)return 1;
    }return 0;
}
static u32 link_init_material_name(u32 material){
    const u32 name=R_LINK_INIT(u32,material),offset=R_LINK_INIT(u32,name+4);
    return offset?name+4+offset:0;
}
static s32 link_init_material_is_data(u32 material,u32 a,u32 b,u32 c,u32 d){
    return link_init_equal(a,link_init_material_name(material))||
           link_init_equal(b,link_init_material_name(material))||
           link_init_equal(c,link_init_material_name(material))||
           link_init_equal(d,link_init_material_name(material));
}
#define link_init_material_is(m,a,b,c,d) link_init_material_is_data((m),LINK_INIT_DATA(a),LINK_INIT_DATA(b),LINK_INIT_DATA(c),LINK_INIT_DATA(d))
static u32 link_init_joint(u32 data,u32 joint){
    const u32 count=R_LINK_INIT(u32,data+4),base=R_LINK_INIT(u32,data+8);
    return joint<count?base+joint*0x1C:base;
}
static void link_init_copy24(u32 dst,u32 src){for(u32 k=0;k<0x24;k+=4)R_LINK_INIT(u32,dst+k)=R_LINK_INIT(u32,src+k);}
static u32 link_init_texture_resource_data(u32 literal,u32 index){
    u32 arc[2]={literal,LINK_INIT_DATA(0x10034B24)};
    return link_init_resource(WWHD_GAME_DATA(0x101F4F28,u32),(u32)arc,index);
}
#define link_init_texture_resource(literal,index) link_init_texture_resource_data(LINK_INIT_DATA(literal),(index))
static void link_init_tail(u32 self,u32 demo,u32 extra){
    const u32 event=link_init_start_demo(wwhd_dComIfG_getGameInfo()+0x52E8,demo);
    const u32 flags=R_LINK_INIT(u32,self+0x3B8);
    R_LINK_INIT(u32,self+0x69E0)=event;R_LINK_INIT(u32,self+0x3B8)=flags|extra|0x100;
    R_LINK_INIT(f32,self+0x434)=1.0f;R_LINK_INIT(u32,self+0xCDC)&=~2u;
    link_init_end_damage(self);link_init_mirror(self+0xF8C,0);
    const u32 light=self+0x68B0;
    R_LINK_INIT(s16,light+0xC)=600;R_LINK_INIT(s16,light+0xE)=400;R_LINK_INIT(s16,light+0x10)=120;
    R_LINK_INIT(f32,light+0x18)=250;
    for(u32 k=0;k<12;k+=4)R_LINK_INIT(u32,light+k)=R_LINK_INIT(u32,self+0x314+k);
    R_LINK_INIT(f32,light+0x14)=0;link_init_light(light);
    R_LINK_INIT(s16,self+0x697E)=900;R_LINK_INIT(s16,self+0x6926)=R_LINK_INIT(s16,self+0x32A);R_LINK_INIT(s32,self+0x3B4)=0xAD;
}
static u32 link_init_slw(u32 value,u32 shift){shift&=63;return shift>=32?0:value<<shift;}
static u32 link_init_srw(u32 value,u32 shift){shift&=63;return shift>=32?0:value>>shift;}
static void randomizer_player_init_source(u32 self){
    const u32 items=self+0xD90;
    if(!link_init_heap(self,0x023D6184,0))link_init_assertion(0x5F3B,0x10035CF0);
    {
        const u32 model=R_LINK_INIT(u32,self+0x448);
        R_LINK_INIT(u32,self+0x348)=model?model+0xC8:0;
        R_LINK_INIT(u32,model+0xB8)=self;
        R_LINK_INIT(u32,R_LINK_INIT(u32,items+0x4970)+0xB8)=self;
    }
    for(u16 j=0;j<R_LINK_INIT(u16,link_init_joint_tree(R_LINK_INIT(u32,R_LINK_INIT(u32,items+0x4970)+0xAC))+8);j=(u16)(j+1)){
        const u32 node=link_init_joint(R_LINK_INIT(u32,R_LINK_INIT(u32,items+0x4970)+0xAC),j);
        R_LINK_INIT(u32,node+8)=0x023D6980;
    }
    {const u8 joints[]={2,14,5,9,13,29,15,1};for(u32 k=0;k<8;k++)R_LINK_INIT(u32,link_init_joint(R_LINK_INIT(u32,self+0x444),joints[k])+8)=0x023D80F0;}
    for(u32 k=0;k<2;k++){
        const u32 calc=R_LINK_INIT(u32,self+0x57F0+k*4);
        R_LINK_INIT(u32,calc+0xA8)=self;
        R_LINK_INIT(u32,R_LINK_INIT(u32,self+0x57F0+k*4)+0xB0)=0x023D7194;
        R_LINK_INIT(u32,R_LINK_INIT(u32,self+0x57F0+k*4)+0xB4)=0x023D78E0;
    }
    s32 on=0,none=0,blend=0;
    u32 material=R_LINK_INIT(u32,link_init_joint(R_LINK_INIT(u32,self+0x444),0x13)+0x10);
    for(u32 pass=0;pass<2;pass++){
        while(material){
            if(link_init_material_is(material,0x10035DF0,0x10035DFC,0x10035D68,0x10035D74)){
                R_LINK_INIT(u32,self+0x62C+(u32)on*4)=R_LINK_INIT(u32,material+8);on++;
                if(on>4)link_init_assertion(0x5F96,0x10035E08);
            }else if(link_init_material_is(material,0x10035E18,0x10035E24,0x10035D80,0x10035D8C)){
                R_LINK_INIT(u32,self+0x61C+(u32)none*4)=R_LINK_INIT(u32,material+8);none++;
                if(none>4)link_init_assertion(0x5F9E,0x10035E30);
            }else if(link_init_material_is(material,0x10035D08,0x10035D10,0x10035CF4,0x10035CFC)){
                R_LINK_INIT(u32,self+0x60C+(u32)blend*4)=R_LINK_INIT(u32,material+8);blend++;
                if(blend>4)link_init_assertion(0x5FA6,0x10035D34);
            }
            material=R_LINK_INIT(u32,material+4);
        }
        material=R_LINK_INIT(u32,link_init_joint(R_LINK_INIT(u32,self+0x444),0x15)+0x10);
    }
    if(on!=4)link_init_assertion(0x5FAF,0x10035E44);
    if(none!=4)link_init_assertion(0x5FB0,0x10035E54);
    if(blend!=4)link_init_assertion(0x5FB1,0x10035D48);
    R_LINK_INIT(u32,self+0x63C)=R_LINK_INIT(u32,R_LINK_INIT(u32,link_init_joint(R_LINK_INIT(u32,self+0x444),8)+0x10)+8);
    R_LINK_INIT(u32,self+0x640)=R_LINK_INIT(u32,R_LINK_INIT(u32,link_init_joint(R_LINK_INIT(u32,self+0x444),12)+0x10)+8);
    {
        const u32 hand_data=R_LINK_INIT(u32,R_LINK_INIT(u32,self+0xCF4)+0xAC);
        for(u32 j=1;j<=10;j++){
            const u32 node=link_init_joint(hand_data,j),shape=R_LINK_INIT(u32,R_LINK_INIT(u32,node+0x10)+8);
            R_LINK_INIT(u8,shape+4)=0;
            const u32 block=R_LINK_INIT(u32,R_LINK_INIT(u32,self+0xCF4)+0x2C);
            R_LINK_INIT(u16,block+4)|=0x10;
            const u32 matrix=R_LINK_INIT(u32,block+0x10)+j*0x30;
            for(u32 k=0;k<0x30;k+=4)R_LINK_INIT(u32,matrix+k)=0;
        }
    }
    {
        const u32 buffer=R_LINK_INIT(u32,self+0x5848+8);
        R_LINK_INIT(u32,self+0x5858+8)=buffer+0x2400;
        R_LINK_INIT(u32,self+0x5868+8)=buffer+0x4800;
        for(u32 i=1;i<=2;i++)R_LINK_INIT(u32,self+0x5868+i*0x10+8)=R_LINK_INIT(u32,self+0x5868+8)+i*0x2400;
    }
    link_init_acch(self+0x80C,self+0x314,self+0x300,self,3,self+0x74C,self+0x33C,self+0x320,self+0x328);
    R_LINK_INIT(u32,self+0x834)=((R_LINK_INIT(u32,self+0x834)&~0x400u)|0x2000u)&~8u;
    R_LINK_INIT(f32,self+0x3C4)=17;R_LINK_INIT(f32,self+0x378)=-175;R_LINK_INIT(f32,self+0x374)=-2.5f;
    R_LINK_INIT(f32,self+0x8D4)=500;R_LINK_INIT(f32,self+0x8CC)=125;
    link_init_wall(self+0x74C,30.1f,35);link_init_wall(self+0x78C,89.9f,35);link_init_wall(self+0x7CC,125,35);
    R_LINK_INIT(f32,self+0x3C8)=125;R_LINK_INIT(u16,self+0x69A2)=0x100;
    R_LINK_INIT(f32,self+0x3E0)=0;R_LINK_INIT(u16,self+0x69B0)=0x100;
    R_LINK_INIT(f32,self+0x3DC)=0;R_LINK_INIT(f32,self+0x3D8)=0;
    R_LINK_INIT(s32,self+0x69BC)=R_LINK_INIT(s8,wwhd_dComIfG_getGameInfo()+0x5B30);
    {
        const u32 attention=wwhd_dComIfG_getGameInfo()+0x5804,status=self+0x7620;
        R_LINK_INIT(u32,self+0x6894)=attention;
        link_init_status(status,0x78,0xFF,self);link_init_cylinder(self+0x765C,LINK_INIT_DATA(0x101CECDC));
        const s16 body=R_LINK_INIT(s16,self+0x3D2);
        R_LINK_INIT(u32,self+0x765C+0xD8)=self+0x6996;
        const u32 flags=R_LINK_INIT(u32,self+0x765C+0x94);
        const s16 shape=R_LINK_INIT(s16,self+0x32A);
        R_LINK_INIT(u32,self+0x765C+0x94)=flags|8;
        R_LINK_INIT(u32,self+0x765C+0x44)=status;R_LINK_INIT(s16,self+0x6996)=(s16)((u16)shape+(u16)body);
        link_init_cylinder(self+0x778C,LINK_INIT_DATA(0x101CED20));R_LINK_INIT(u32,self+0x778C+0x44)=status;
        link_init_cylinder(self+0x79EC,LINK_INIT_DATA(0x101CED20));R_LINK_INIT(u32,self+0x79EC+0x44)=status;R_LINK_INIT(u32,self+0x79EC+0x28)=0x800000;
        for(u32 i=0;i<3;i++){link_init_capsule(self+0x7B1C+i*0x138,LINK_INIT_DATA(0x101CEDA8));R_LINK_INIT(u32,self+0x7B1C+i*0x138+0x44)=status;}
        link_init_cylinder(self+0x78BC,LINK_INIT_DATA(0x101CED64));R_LINK_INIT(u32,self+0x78BC+0x44)=status;
        link_init_capsule(self+0x7EC4,LINK_INIT_DATA(0x101CEDF4));R_LINK_INIT(u32,self+0x7EC4+0x44)=status;R_LINK_INIT(f32,self+0x7EC4+0x134)=70;
        link_init_sphere(self+0x7FFC,LINK_INIT_DATA(0x101CEB50));R_LINK_INIT(u32,self+0x7FFC+0x44)=status;
        link_init_capsule(self+0x8128,LINK_INIT_DATA(0x101CEDF4));R_LINK_INIT(u32,self+0x8128+0x10)=0x800000;
        R_LINK_INIT(u32,self+0x8128+0x44)=status;R_LINK_INIT(f32,self+0x8128+0x134)=20;
    }
    for(u32 i=0;i<2;i++){
        const u32 heap=self+0x5848+i*0x10;link_init_anime_heap(self,heap+0xC,0);
        R_LINK_INIT(s16,heap)=-1;R_LINK_INIT(s16,heap+2)=-1;R_LINK_INIT(s16,heap+4)=-1;
    }
    for(u32 i=0;i<3;i++){
        const u32 heap=self+0x5868+i*0x10;link_init_anime_heap(self,heap+0xC,0);
        R_LINK_INIT(s16,heap)=-1;R_LINK_INIT(s16,heap+2)=-1;R_LINK_INIT(s16,heap+4)=-1;
    }
    link_init_anime_heap(self,self+0x65D0+0xC,1);
    R_LINK_INIT(s16,self+0x65D6)=-1;R_LINK_INIT(s16,self+0x65D4)=-1;R_LINK_INIT(s16,self+0x65D0)=-1;R_LINK_INIT(s16,self+0x65D2)=-1;
    link_init_anime_heap(self,self+0x65E0+0xC,2);
    R_LINK_INIT(s16,self+0x65E2)=-1;R_LINK_INIT(s16,self+0x65E0)=-1;
    R_LINK_INIT(s16,self+0x6940)=-1;R_LINK_INIT(u32,self+0x6938)=LINK_INIT_DATA(0x10035D04);
    R_LINK_INIT(s16,self+0x65E4)=-1;R_LINK_INIT(s16,self+0x65E6)=-1;
    for(u32 i=0;i<2;i++){
        u32 heap=link_init_solid_heap(0x1E600,0x20);R_LINK_INIT(u32,items+0x36A8+i*4)=heap;
        if(!heap){if(i)link_init_assertion(0x6064,0x10035DC4);else link_init_assertion(0x605A,0x10035D98);heap=R_LINK_INIT(u32,items+0x36A8+i*4);}
        R_LINK_INIT(u32,heap+0x10)=(i?LINK_INIT_DATA(0x10035DDC):LINK_INIT_DATA(0x10035DB0));link_init_restore_heap();
    }
    link_init_anime_heap(self,items+0x3A78,3);
    link_init_clear_keep(self+0x6590);link_init_clear_keep(self+0x6598);link_init_clear_keep(self+0x65A0);link_init_clear_keep(self+0x65A8);
    R_LINK_INIT(s32,self+0x6A8C)=-1;R_LINK_INIT(s16,self+0x324)=0;R_LINK_INIT(s16,self+0x32C)=0;
    R_LINK_INIT(s32,self+0x6A88)=-1;R_LINK_INIT(s16,self+0x6976)=8;
    R_LINK_INIT(s32,self+0x6A80)=-1;R_LINK_INIT(s32,self+0x6A84)=-1;R_LINK_INIT(f32,self+0x6AAC)=1;
    link_init_reset_sound(self);
    {
        const u32 texture=link_init_texture_resource(0x10035D18,0x71);
        if(!texture)link_init_assertion(0x6087,0x10035E68);
        R_LINK_INIT(u32,self+0x58E8+0x4C)=texture;
    }
    {
        const u32 image=link_init_texture_resource(0x101CEB48,0x72);
        if(!image)link_init_assertion(0x608D,0x10035E78);
        const u32 offset=R_LINK_INIT(u32,image+0x1C);
        R_LINK_INIT(u32,self+0x58E8+0x44)=image;R_LINK_INIT(u32,self+0x58E8+0x48)=image+offset;
    }
    {
        const u32 start=(R_LINK_INIT(u32,self+0xB0)>>12)&15;
        const u32 parameters=R_LINK_INIT(u32,self+0xB0);
        const u8 last=R_LINK_INIT(u8,WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32)+0x1177);
        if(start==14)link_init_tail(self,0xD4,0);
        else if(start==12)link_init_tail(self,0xD0,0);
        else if(start==6)link_init_tail(self,0xCF,0);
        else if(start==10)link_init_tail(self,0xD2,0x08000000);
        else if(start==11)link_init_tail(self,0xD3,0);
        else if(start==9||last==6)link_init_tail(self,0xCB,0);
        else if(start==2&&(last==4||last==5))link_init_tail(self,0xC9,0);
        else if(start==15)link_init_tail(self,0xD5,0);
        else link_init_tail(self,parameters>>24,0);
    }
    const u32 casual=link_init_texture_resource(0x101CEB48,0x71);
    R_LINK_INIT(u32,self+0x454)=self+0x458;
    const u32 texture=R_LINK_INIT(u32,R_LINK_INIT(u32,self+0x444)+0x30);
    if(!texture)link_init_assertion(0x60DA,0x10035E88);
    const u32 names=R_LINK_INIT(u32,R_LINK_INIT(u32,self+0x444)+0x34);
    if(!names)link_init_assertion(0x60DC,0x10035E98);
    for(u16 i=0;i<R_LINK_INIT(u16,texture);i=(u16)(i+1)){
        const u32 name=link_init_name(names,i);if(!name)continue;
        u32 k=0;u8 first,second;
        do{first=R_LINK_INIT(u8,name+k);second=R_LINK_INIT(u8,LINK_INIT_DATA(0x10035D5C)+k);k++;}while(first==second&&first);
        if(first!=second)continue;
        const u32 resource=R_LINK_INIT(u32,texture+4)+(u32)i*0x24;
        {
            s32 count=R_LINK_INIT(s32,self+0x458);
            if(count<R_LINK_INIT(s32,self+0x45C)){
                R_LINK_INIT(u32,R_LINK_INIT(u32,self+0x460)+(u32)count*4)=resource;
                count=R_LINK_INIT(s32,self+0x458)+1;R_LINK_INIT(s32,self+0x458)=count;
            }
            const u32 last=(u32)count-1,list=R_LINK_INIT(u32,self+0x454),number=R_LINK_INIT(u32,list);
            const u32 dst=self+0x48C+(last<10?last*0x24:0);
            const u32 src=last<number?R_LINK_INIT(u32,R_LINK_INIT(u32,list+8)+last*4):0;
            link_init_copy24(dst,src);
        }
        link_init_copy24(R_LINK_INIT(u32,texture+4)+(u32)i*0x24,casual);
        {
            u32 resource=R_LINK_INIT(u32,texture+4)+(u32)i*0x24;
            R_LINK_INIT(u32,resource+0x1C)=R_LINK_INIT(u32,resource+0x1C)+casual-resource;
            resource=R_LINK_INIT(u32,texture+4)+(u32)i*0x24;
            R_LINK_INIT(u32,resource+0xC)=R_LINK_INIT(u32,resource+0xC)+casual-resource;
            resource=R_LINK_INIT(u32,texture+4)+(u32)i*0x24;
            R_LINK_INIT(u32,resource+0x20)=R_LINK_INIT(u32,casual+0x20);
        }
        {
            const u32 hi=R_LINK_INIT(u32,texture+0x18),lo=R_LINK_INIT(u32,texture+0x1C);
            if(i<0x40){
                const u32 shift=i,h=link_init_slw(hi,shift)|link_init_srw(lo,32-shift)|link_init_slw(lo,shift+32);
                R_LINK_INIT(u32,texture+8)|=h;R_LINK_INIT(u32,texture+0xC)|=link_init_slw(lo,shift);
            }else{
                const u32 shift=i-0x40,word=R_LINK_INIT(u32,texture+0x10),h=link_init_slw(hi,shift)|link_init_srw(lo,32-shift)|link_init_slw(lo,shift+32);
                const u32 low=R_LINK_INIT(u32,texture+0x14)|link_init_slw(lo,shift);
                R_LINK_INIT(u32,texture+0x14)=low;R_LINK_INIT(u32,texture+0x10)=word|h;
            }
        }
    }
    /* The skipped original event/Second-Quest predicates must not execute. */
    (void)WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32); /* retained allocation load */
    if(randomizer_in_casual_clothes())R_LINK_INIT(u32,self+0x3BC)|=8;
    if(!(R_LINK_INIT(u32,self+0x3BC)&8)){
        u32 tmp[0x168/4];const u32 t=(u32)tmp,list=R_LINK_INIT(u32,self+0x454);
        link_init_array(t,10,0x24,0x02444280);
        for(u32 k=0;k<10;k++){
            const u32 src=k<R_LINK_INIT(u32,list)?R_LINK_INIT(u32,R_LINK_INIT(u32,list+8)+k*4):0;
            link_init_copy24(t+k*0x24,src);
        }
        link_init_copy_texture_list(R_LINK_INIT(u32,self+0x454),self+0x48C);
        for(u32 k=0;k<10;k++)link_init_copy24(self+0x48C+k*0x24,t+k*0x24);
    }
    R_LINK_INIT(u8,self+0x8282)=0;R_LINK_INIT(u8,self+0x8265)=0;R_LINK_INIT(u16,self+0x8280)=0;
    R_LINK_INIT(u8,self+0x827C)=0;R_LINK_INIT(u32,self+0x8268)=0;R_LINK_INIT(u8,self+0x8264)=0;
    const u32 info=wwhd_dComIfG_getGameInfo();R_LINK_INIT(u32,info+0x5CDC)&=~0x100000u;
}
#define RANDOMIZER_PLAYER_INIT_BODY(actor) randomizer_player_init_source(actor)
