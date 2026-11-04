/* Full public CC0 a874e1a9 d_a_bgn.cpp J3DModelPacketS_draw (02082ED0).
 * MIT misc_rando_features.asm substitutes only the visible-ear outfit query. */
#pragma once
#ifndef R_CLOTHES
#define R_CLOTHES(t,a) (*(volatile t*)(a))
#endif
#ifndef CLOTHES_DATA
#define CLOTHES_DATA(a) ((u32)&WWHD_GAME_DATA(a,u8))
#endif
WWHD_GAME_FUNC(0x02750250,void,clothes_state_construct,(u32 state));
WWHD_GAME_FUNC(0x02750370,void,clothes_state_apply,(u32 state));
WWHD_GAME_FUNC(0x02082E58,void,clothes_material,(u32 packet,u32 state));
WWHD_GAME_FUNC(0x027F3F94,u32,clothes_joint_tree,(u32 data));
WWHD_GAME_FUNC(0x0208AEF4,void,clothes_assure_direct,(u32 safe));
#ifndef CLOTHES_ASSURE
#define CLOTHES_ASSURE(s) (((void(*)(u32))R_CLOTHES(u32,(s)[1]+0x14))((u32)(s)))
#endif
#ifndef CLOTHES_DRAW
#define CLOTHES_DRAW(packet,offset,info) (((void(*)(u32,u32))R_CLOTHES(u32,R_CLOTHES(u32,(packet)+(offset))+0x2C))((packet),(info)))
#endif
static s32 clothes_name_equal(u32 literal,u32 name){
    u32 a[2]={literal,CLOTHES_DATA(0x10008A1C)},b[2]={name,CLOTHES_DATA(0x10008A1C)};
    clothes_assure_direct((u32)a);CLOTHES_ASSURE(a);
    u32 p=a[0];CLOTHES_ASSURE(b);
    u32 q=b[0];if(p==q)return 1;
    /* Native identity captures the literal before the third callback; the
     * fallback reloads it afterward (02083094..020830B4). */
    p=a[0];q=b[0];
    for(u32 n=0x40001;n;n--,p++,q++){
        const u8 first=R_CLOTHES(u8,p),second=R_CLOTHES(u8,q);
        if(first!=second)return 0;if(!first)return 1;
    }
    return 0;
}
static u32 clothes_mesh_name(u32 mesh){
    const u32 header=R_CLOTHES(u32,mesh);
    const s32 offset=R_CLOTHES(s32,header+4);
    return offset?header+4+(u32)offset:0;
}
static u32 clothes_material_packet(u32 model,u32 mesh){
    const u32 shape=R_CLOTHES(u32,mesh+8),data=R_CLOTHES(u32,shape);
    const u32 index=R_CLOTHES(u16,data+0xC),count=R_CLOTHES(u32,model+0x130);
    u32 indices=R_CLOTHES(u32,model+0x134);if(index<count)indices+=index*2;
    const u32 material_index=R_CLOTHES(u16,indices);
    const u32 material_count=R_CLOTHES(u32,model+0x128);
    u32 packet=R_CLOTHES(u32,model+0x12C);if(material_index<material_count)packet+=material_index*0xAC;
    return packet;
}
WWHD_REPLACE(0x02082ED0,void,randomizer_reflection_draw,(u32 packet,u32 context)){
    if(R_CLOTHES(u32,context+0xC)!=2)return;
    u32 state[0x11C/4];clothes_state_construct((u32)state);
    clothes_material(packet,(u32)state);clothes_state_apply((u32)state);
    if(R_CLOTHES(u32,packet+0xA8)){
        u32 info[13];const u32 target=R_CLOTHES(u32,context);
        const u32 helper=R_CLOTHES(u32,packet+0xA8);
        const u32 model=R_CLOTHES(u32,packet+0x98);
        for(u32 i=0;i<13;i++)info[i]=0;
        info[5]=helper;info[3]=2;info[0]=target;info[1]=2;
        if(model){
            const u32 data=R_CLOTHES(u32,model+0xAC);
            for(u16 j=0;j<R_CLOTHES(u16,clothes_joint_tree(data)+8);j=(u16)(j+1)){
                u32 node=R_CLOTHES(u32,data+8);
                if(j<R_CLOTHES(u32,data+4))node+=(u32)j*0x1C;
                for(u32 mesh=R_CLOTHES(u32,node+0x10);mesh;mesh=R_CLOTHES(u32,mesh+4)){
                    const u32 shape=R_CLOTHES(u32,mesh+8);
                    if(R_CLOTHES(u8,shape+4)){
                        const u32 material=clothes_material_packet(R_CLOTHES(u32,packet+0x98),mesh);
                        s32 skip=0;
                        if(R_CLOTHES(u8,packet+0xAD)){
                            /* Upstream replaces the field load at02083028;
                             * its preceding allocation-pointer load remains. */
                            (void)WWHD_GAME_DATA(WWHD_ADDR_save_info_pointer,u32);
                            if(randomizer_in_casual_clothes()){
                            if(clothes_name_equal(CLOTHES_DATA(0x10008CB4),clothes_mesh_name(mesh))||
                               clothes_name_equal(CLOTHES_DATA(0x10008CBC),clothes_mesh_name(mesh)))skip=1;
                            }
                        }
                        if(!skip)CLOTHES_DRAW(material,0xC,(u32)info);
                    }else if(R_CLOTHES(u8,packet+0xAD)){
                        if(clothes_name_equal(CLOTHES_DATA(0x10008CC4),clothes_mesh_name(mesh))||
                           clothes_name_equal(CLOTHES_DATA(0x10008CCC),clothes_mesh_name(mesh))||
                           clothes_name_equal(CLOTHES_DATA(0x10008CA4),clothes_mesh_name(mesh))||
                           clothes_name_equal(CLOTHES_DATA(0x10008CAC),clothes_mesh_name(mesh))){
                            const u32 material=clothes_material_packet(R_CLOTHES(u32,packet+0x98),mesh);
                            CLOTHES_DRAW(material,0xC,(u32)info);
                        }
                    }
                }
            }
        }
        const u32 material=R_CLOTHES(u32,packet+0x9C);
        if(material)CLOTHES_DRAW(material,0xC,(u32)info);
        const u32 line=R_CLOTHES(u32,packet+0xA0);
        if(line)CLOTHES_DRAW(line,0x130,(u32)info);
    }
    clothes_state_apply(CLOTHES_DATA(0x104B474C));
}
