/* CC0 a874e1a9 Aryll heap/wait helpers and canonical WWHD ABI. */
#pragma once
#define RAO8(a,o) (*(volatile u8*)((a)+(o)))
#define RAOS8(a,o) (*(volatile s8*)((a)+(o)))
#define RAOU16(a,o) (*(volatile u16*)((a)+(o)))
#define RAO16(a,o) (*(volatile s16*)((a)+(o)))
#define RAO32(a,o) (*(volatile unsigned int*)((a)+(o)))
#ifndef RAOPTR
#define RAOPTR(a,o) (*(volatile u32*)((a)+(o)))
#endif
WWHD_GAME_FUNC(0x0227DFF0,u32,rao_material_constructor,(u32 animation));
WWHD_GAME_FUNC(0x0227E344,u32,rao_texture_animation,(u32 actor,s8 index,u32 reset));
WWHD_GAME_FUNC(0x025E4F64,u32,rao_morf_constructor,(u32 self,u32 model,u32 joint,u32 material,u32 animation,s32 loop,f32 speed,s32 start,s32 end,s32 play,u32 sound,u32 flags,u32 model_flags));
WWHD_GAME_FUNC(0x026067F4,u32,rao_resource_native,(u32 control,u32 name,s32 resource));
WWHD_GAME_FUNC(0x0273AA24,void,rao_assert,(u32 file,s32 line,u32 message));
WWHD_GAME_FUNC(0x0273AD10,u32,rao_allocate,(u32 size));
WWHD_GAME_FUNC(0x027DF9B0,s32,rao_name_index,(u32 table,u32 name));
WWHD_GAME_FUNC(0x027ED1F0,u32,rao_texture_name,(u32 table,u32 index));
WWHD_GAME_FUNC(0x027F68FC,u32,rao_joint_header,(u32 model_data));
WWHD_GAME_FUNC(0x022805B8,void,rao_set_state,(u32 actor,s32 state));
WWHD_GAME_FUNC(0x02282084,u8,rao_attention,(u32 actor));
WWHD_GAME_FUNC(0x02282290,void,rao_event_position,(u32 actor,u32 position,s32 index));
WWHD_GAME_FUNC(0x02282A60,s32,rao_wait1,(u32 actor));
WWHD_GAME_FUNC(0x02282B54,s32,rao_wait2,(u32 actor));
WWHD_GAME_FUNC(0x02282D94,s32,rao_wait3,(u32 actor));
WWHD_GAME_FUNC(0x02282EB0,s32,rao_wait4,(u32 actor));
WWHD_GAME_FUNC(0x02282F6C,s32,rao_talk1,(u32 actor));
WWHD_GAME_FUNC(0x025B8B94,s32,rao_event_query,(u32 event,u32 flag));
static unsigned int rao_shift_left(unsigned int value,unsigned int shift){shift&=63;return shift&32?0:value<<shift;}
static unsigned int rao_shift_right(unsigned int value,unsigned int shift){shift&=63;return shift&32?0:value>>shift;}
static u32 rao_resource(u32 name,s32 id){_Alignas(4) unsigned int descriptor[2];RAOPTR((u32)descriptor,0)=name;descriptor[1]=0x1001D0CC;const u32 control=WWHD_GAME_DATA(0x101F4F28,u32);return rao_resource_native(control,(u32)descriptor,id);}
static u32 rao_eye_material(u32 data,unsigned int index){
    const u32 name=(u32)&WWHD_GAME_DATA(0x10467980,u8)+index*8;
    const u32 vtable=RAOPTR(name,4);
    const u32 terminate=RAOPTR(vtable,0x14);
    const u32 header=RAOPTR(data,0); /* Captured before the native virtual callback. */
    ((void(*)(u32))terminate)(name);
    const unsigned int offset=RAO32(header,0x18);
    const s32 found=rao_name_index(offset?header+0x18+offset:0,RAOPTR(name,0));
    if(found<0)return 0;
    const unsigned int count=RAO32(data,0xC);
    const u32 base=RAOPTR(data,0x10);
    return (unsigned int)found<count?base+(unsigned int)found*0x39C:base;
}
static u32 rao_joint_names(u32 model){const u32 header=rao_joint_header(model);const unsigned int offset=RAO32(header,0x10);return offset?header+0x10+offset:0;}
static void rao_set_joint(u32 actor,unsigned int field,unsigned int callback){
    const u32 model=RAOPTR(RAOPTR(actor,0x44C),0x90);
    const u32 data=RAOPTR(model,0xAC);
    const unsigned int joint=(u16)(s16)RAOS8(actor,field);
    const unsigned int count=RAO32(data,4);
    const u32 base=RAOPTR(data,8);
    RAO32(joint<count?base+joint*0x1C:base,8)=callback;
}
