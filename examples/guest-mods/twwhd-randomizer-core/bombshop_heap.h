/* CC0 complete daNpc_Bms1_c::CreateHeap; pearl query02208D34=true. */
#pragma once
WWHD_GAME_FUNC(0x026066C4,u32,randomizer_bms_object_res,(u32 controller,u32 safe_string,s32 index));
WWHD_GAME_FUNC(0x025E4F64,u32,randomizer_bms_morf_create,(u32 self,u32 model,u32 cb1,u32 cb2,u32 animation,s32 attr,f32 rate,s32 start,s32 end,s32 enabled,u32 sound,u32 flags,u32 model_flags));
WWHD_GAME_FUNC(0x027F68FC,u32,randomizer_bms_joint_names,(u32 data));
WWHD_GAME_FUNC(0x027DF9B0,s32,randomizer_bms_name_index,(u32 names,u32 name));
WWHD_GAME_FUNC(0x0273AA24,void,randomizer_bms_assert,(u32 file,u32 line,u32 condition));
WWHD_GAME_FUNC(0x025E38E0,u32,randomizer_bms_model_create,(u32 data,u32 flags,u32 model_flags));
WWHD_GAME_FUNC(0x02208BA8,s32,randomizer_bms_tex_init,(u32 actor,u32 modify));
WWHD_GAME_FUNC(0x02208CB4,u32,randomizer_bms_tex_set,(u32 actor,s32 index));
WWHD_GAME_FUNC(0x027F3F94,u32,randomizer_bms_joint_block,(u32 data));
WWHD_GAME_FUNC(0x024EFF44,void,randomizer_bms_wall,(u32 circle,f32 radius,f32 height));
WWHD_GAME_FUNC(0x024F06B4,void,randomizer_bms_acch_set,(u32 acch,u32 pos,u32 old,u32 actor,s32 enabled,u32 circle,u32 speed,u32 rotation,u32 shape));
WWHD_GAME_FUNC(0x025BBD7C,u32,randomizer_bms_cursor_create,(u32 model,u32 brk,f32 scale));
static u32 randomizer_bms_resource(s32 index) {
    u32 safe_string[2]={0x101BD3A0,0x10017CFC};
    return randomizer_bms_object_res(WWHD_GAME_DATA(0x101F4F28,u32),(u32)safe_string,index);
}
static void randomizer_bms_joint(u32 actor,u32 offset,u32 data,u32 name,u32 line,u32 condition) {
    const u32 header=randomizer_bms_joint_names(data);
    const u32 relative=R_BMS(u32,header+0x10);
    const s8 joint=(s8)randomizer_bms_name_index(relative?header+0x10+relative:0,name);
    R_BMS(s8,actor+offset)=joint;
    if(joint<0)randomizer_bms_assert(0x10017E58,line,condition);
}
static u16 randomizer_bms_joint_count(u32 data) {
    return R_BMS(u16,randomizer_bms_joint_block(data)+8);
}
static void randomizer_bms_callback(u32 data,u16 joint,u32 callback) {
    const u32 count=R_BMS(u32,data+4);
    u32 record=R_BMS(u32,data+8);
    if(joint<count)record+=(u32)joint*0x1C;
    R_BMS(u32,record+8)=callback;
}
WWHD_REPLACE(0x02208CDC,s32,randomizer_bombshop_heap,(u32 actor)) {
    const s8 type=R_BMS(s8,actor+0xA06);
    u32 data,animation;
    if(type==0) {
        data=randomizer_bms_resource(0x1D);
        animation=randomizer_bms_resource(0x14);
    } else {
        data=randomizer_bms_resource(0x1A);
        animation=randomizer_bms_resource(0x12);
        R_BMS(u8,actor+0xA02)=3;
    }
    const u32 morf=randomizer_bms_morf_create(0,data,0,0,animation,2,
        WWHD_GAME_DATA(0x10017DD4,f32),0,-1,1,0,0,0x11020203);
    if(morf)R_BMS(u32,actor+0x444)=morf;
    if(!morf || !R_BMS(u32,morf+0x90)) {
        R_BMS(u32,actor+0x444)=0;
        return 0;
    }
    randomizer_bms_joint(actor,0x848,data,0x10017E1C,0x842,0x10017E44);
    randomizer_bms_joint(actor,0x849,data,0x10017E6C,0x845,0x10017E2C);
    if(R_BMS(s8,actor+0xA06)==0)
        randomizer_bms_joint(actor,0x84C,data,0x10017E24,0x84A,0x10017E78);
    const u32 head=randomizer_bms_resource(0x23);
    const u32 head_model=randomizer_bms_model_create(head,0x80000,0x11020002);
    R_BMS(u32,actor+0x4C4)=head_model;
    if(!head_model)return 0;
    randomizer_bms_joint(actor,0x84A,head,0x10017E0C,0x86C,0x10017E8C);
    randomizer_bms_joint(actor,0x84B,head,0x10017E14,0x86F,0x10017EA4);
    R_BMS(u8,actor+0xA00)=1;
    if(!randomizer_bms_tex_init(actor,0))return 0;
    const s8 current_type=R_BMS(s8,actor+0xA06);
    if(current_type==0) {
        const s32 event=randomizer_bms_event_query(randomizer_daily_save()+0x644,0xA02);
        randomizer_bms_tex_set(actor,event?1:0);
    } else if(current_type==1)randomizer_bms_tex_set(actor,1);
    const u32 accessory=randomizer_bms_model_create(randomizer_bms_resource(0x1B),0,0x11020203);
    R_BMS(u32,actor+0x4C8)=accessory;
    if(!accessory)return 0;
    R_BMS(u32,actor+0x4CC)=randomizer_bms_model_create(randomizer_bms_resource(0x1C),0,0x11020203);
    if(R_BMS(s8,actor+0xA06)==0) {
        const s32 event=randomizer_bms_event_query(randomizer_daily_save()+0x644,0xA02);
        const u32 body=randomizer_bms_model_create(randomizer_bms_resource(event?0x1F:0x1E),0,0x11020203);
        R_BMS(u32,actor+0x4D0)=body;
        if(!body)return 0;
        const u32 shop=randomizer_bms_model_create(randomizer_bms_resource(0x20),0,0x11020203);
        R_BMS(u32,actor+0x4D4)=shop;
        if(!shop)return 0;
    } else R_BMS(u32,actor+0x4D0)=0;
    for(u16 i=0;i<randomizer_bms_joint_count(data);i=(u16)(i+1)) {
        if((s32)i==R_BMS(s8,actor+0x848) || (s32)i==R_BMS(s8,actor+0x849)) {
            const u32 model=R_BMS(u32,R_BMS(u32,actor+0x444)+0x90);
            randomizer_bms_callback(R_BMS(u32,model+0xAC),i,0x022083A4);
        }
    }
    R_BMS(u32,R_BMS(u32,R_BMS(u32,actor+0x444)+0x90)+0xB8)=actor;
    for(u16 i=0;i<randomizer_bms_joint_count(R_BMS(u32,R_BMS(u32,actor+0x4C4)+0xAC));i=(u16)(i+1)) {
        if((s32)i==R_BMS(s8,actor+0x84A) || (s32)i==R_BMS(s8,actor+0x84B))
            randomizer_bms_callback(R_BMS(u32,R_BMS(u32,actor+0x4C4)+0xAC),i,0x02208634);
    }
    R_BMS(u32,R_BMS(u32,actor+0x4C4)+0xB8)=actor;
    randomizer_bms_wall(actor+0x69C,WWHD_GAME_DATA(0x10017E08,f32),WWHD_GAME_DATA(0x10017DBC,f32));
    randomizer_bms_acch_set(actor+0x4D8,actor+0x314,actor+0x300,actor,1,actor+0x69C,actor+0x33C,0,0);
    const u32 brk=randomizer_bms_resource(0xB);
    const u32 cursor_model=randomizer_bms_resource(8);
    const u32 cursor=randomizer_bms_cursor_create(cursor_model,brk,WWHD_GAME_DATA(0x10466684,f32));
    R_BMS(u32,actor+0x9FC)=cursor;
    return cursor!=0;
}
