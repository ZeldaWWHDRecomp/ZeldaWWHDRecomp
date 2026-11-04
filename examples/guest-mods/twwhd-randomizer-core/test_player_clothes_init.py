"""Complete Link initialization with independent outfit, texture and spawn oracles."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

class PlayerClothesInit(unittest.TestCase):
    def test_complete_init(self):
        source=r'''

#define ID_heap 1
#define ID_assert 2
#define ID_joint_tree 3
#define ID_terminate 4
#define ID_acch 5
#define ID_wall 6
#define ID_status 7
#define ID_cylinder 8
#define ID_capsule 9
#define ID_sphere 10
#define ID_anime_heap 11
#define ID_solid_heap 12
#define ID_restore_heap 13
#define ID_clear_keep 14
#define ID_reset_sound 15
#define ID_resource 16
#define ID_start_demo 17
#define ID_end_damage 18
#define ID_mirror 19
#define ID_light 20
#define ID_name 21
#define ID_array 22
#define ID_copy_texture_list 23
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef uint16_t u16;typedef int16_t s16;typedef uint8_t u8;typedef int8_t s8;typedef int32_t s32;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_HOOK_RETURN(a,n,p) void n p
static _Alignas(16) u8 memory[0xC0000],save_before[0x2000],originals[70*0x24],actor_before[0x8400],info_before[0x7000],allowed[0x8400];
static const u32 self=0x10000,info=0x20000,save=0x30000,body_model=0x40000,body_data=0x41000,aura_model=0x42000,aura_data=0x43000,hands=0x44000,hand_data=0x45000;
static const u32 body_joints=0x50000,aura_joints=0x51000,hand_joints=0x52000,materials=0x54000,names=0x56000,shapes=0x58000,headers=0x5A000,aura_tree=0x5B000,hand_block=0x5C000,matrices=0x60000;
static const u32 texture=0x70000,texture_headers=0x71000,name_table=0x72000,texture_name=0x73000,casual=0x74000,image=0x75000,heap_base=0x76000;
static u32 save_pointer=0x30000,resource_system=0x1234,init_should_start_with_heros_clothes;
#define WWHD_ADDR_save_info_pointer 0x101F84DC
#define WWHD_GAME_DATA(a,t) (*(t*)((a)==WWHD_ADDR_save_info_pointer?&save_pointer:&resource_system))
static void *address(u32 a){return a<sizeof memory?memory+a:(void*)a;}
#define R_LINK_INIT_u32(a) (*(uint32_t*)address(a))
#define R_LINK_INIT_s32(a) (*(int32_t*)address(a))
#define R_LINK_INIT_u16(a) (*(uint16_t*)address(a))
#define R_LINK_INIT_s16(a) (*(int16_t*)address(a))
#define R_LINK_INIT_u8(a) (*(uint8_t*)address(a))
#define R_LINK_INIT_s8(a) (*(int8_t*)address(a))
#define R_LINK_INIT_f32(a) (*(float*)address(a))
#define R_LINK_INIT(t,a) R_LINK_INIT_##t(a)
#define LINK_INIT_DATA(a) (0xA0000u+((a)&0x1FFFFu))
#define R_SPAWN_U8(a,o) R_LINK_INIT(u8,(a)+(o))
#define R_SPAWN_U16(a,o) R_LINK_INIT(u16,(a)+(o))
#define R_SPAWN_U32(a,o) R_LINK_INIT(u32,(a)+(o))
static unsigned trace[1024],n,anime_heaps,solid_heaps,resources,arrays,restores,texture_count;
static u32 resource_indices[4],chosen_demo;
static void record(unsigned id){assert(n<1024);trace[n++]=id;}
static u32 wwhd_dComIfG_getGameInfo(void){record(100);return info;}
static void assure(u32 *safe){assert(safe[1]==LINK_INIT_DATA(0x10034B24));}
#define LINK_INIT_ASSURE(s) assure(s)
#include "casual_clothes.h"
#include "player_clothes_init.h"
#include "entrance_spawn.h"
s32 link_init_heap(u32 p0,u32 p1,u32 p2){record(1);(void)p0;(void)p1;(void)p2;assert(p0==self&&p1==0x023D6184&&p2==0);return 1;}
void link_init_assert(u32 p0,u32 p1,u32 p2){record(2);(void)p0;(void)p1;(void)p2;fprintf(stderr,"native assertion %lx\n",(unsigned long)p1);assert(!"unexpected native assertion");}
u32 link_init_joint_tree(u32 p0){record(3);(void)p0;assert(p0==aura_data);return aura_tree;}
void link_init_terminate(u32 p0){record(4);(void)p0;assert(((u32*)p0)[1]==LINK_INIT_DATA(0x10034B24));}
void link_init_acch(u32 p0,u32 p1,u32 p2,u32 p3,s32 p4,u32 p5,u32 p6,u32 p7,u32 p8){record(5);(void)p0;(void)p1;(void)p2;(void)p3;(void)p4;(void)p5;(void)p6;(void)p7;(void)p8;assert(p0==self+0x80C&&p1==self+0x314&&p2==self+0x300&&p3==self&&p4==3&&p5==self+0x74C&&p6==self+0x33C&&p7==self+0x320&&p8==self+0x328);}
void link_init_wall(u32 p0,f32 p1,f32 p2){record(6);(void)p0;(void)p1;(void)p2;assert(p0==self+0x74C||p0==self+0x78C||p0==self+0x7CC);assert(p2==35);}
void link_init_status(u32 p0,u32 p1,u32 p2,u32 p3){record(7);(void)p0;(void)p1;(void)p2;(void)p3;assert(p0==self+0x7620&&p1==0x78&&p2==255&&p3==self);}
void link_init_cylinder(u32 p0,u32 p1){record(8);(void)p0;(void)p1;}
void link_init_capsule(u32 p0,u32 p1){record(9);(void)p0;(void)p1;}
void link_init_sphere(u32 p0,u32 p1){record(10);(void)p0;(void)p1;}
void link_init_anime_heap(u32 p0,u32 p1,u32 p2){record(11);(void)p0;(void)p1;(void)p2;assert(p0==self&&p2<=3);anime_heaps++;}
u32 link_init_solid_heap(u32 p0,u32 p1){record(12);(void)p0;(void)p1;assert(p0==0x1E600&&p1==0x20);return heap_base+solid_heaps++*0x100;}
void link_init_restore_heap(void){record(13);}
void link_init_clear_keep(u32 p0){record(14);(void)p0;}
void link_init_reset_sound(u32 p0){record(15);(void)p0;randomizer_spawn_sound_ready(p0);}
u32 link_init_resource(u32 p0,u32 p1,u32 p2){record(16);(void)p0;(void)p1;(void)p2;assert(p0==resource_system);u32 *arc=(u32*)p1;assert(arc[1]==LINK_INIT_DATA(0x10034B24));resource_indices[resources++]=p2;randomizer_spawn_resource_ready(p0,p1,p2);return p2==0x72?image:casual;}
u32 link_init_start_demo(u32 p0,u32 p1){record(17);(void)p0;(void)p1;assert(p0==info+0x52E8);chosen_demo=p1;return 0x76543210;}
void link_init_end_damage(u32 p0){record(18);(void)p0;assert(p0==self);}
void link_init_mirror(u32 p0,u32 p1){record(19);(void)p0;(void)p1;assert(p0==self+0xF8C&&p1==0);}
void link_init_light(u32 p0){record(20);(void)p0;assert(p0==self+0x68B0);}
u32 link_init_name(u32 p0,u32 p1){record(21);(void)p0;(void)p1;assert(p0==name_table&&p1<texture_count);return texture_name;}
void link_init_array(u32 p0,u32 p1,u32 p2,u32 p3){record(22);(void)p0;(void)p1;(void)p2;(void)p3;assert(p1==10&&p2==0x24&&p3==0x02444280);arrays++;}
void link_init_copy_texture_list(u32 p0,u32 p1){record(23);(void)p0;(void)p1;assert(p0==self+0x458&&p1==self+0x48C);restores++;for(unsigned i=0;i<R_LINK_INIT(u32,p0);i++)link_init_copy24(R_LINK_INIT(u32,R_LINK_INIT(u32,p0+8)+i*4),p1+i*0x24);}
static void reset(unsigned count){
 memset(memory,0,sizeof memory);n=anime_heaps=solid_heaps=resources=arrays=restores=0;texture_count=count;
 R_LINK_INIT(u32,self+0x448)=body_model;R_LINK_INIT(u32,self+0x444)=body_data;R_LINK_INIT(u32,body_model+0xAC)=body_data;
 R_LINK_INIT(u32,self+0xD90+0x4970)=aura_model;R_LINK_INIT(u32,aura_model+0xAC)=aura_data;
 R_LINK_INIT(u32,self+0xCF4)=hands;R_LINK_INIT(u32,hands+0xAC)=hand_data;R_LINK_INIT(u32,hands+0x2C)=hand_block;R_LINK_INIT(u32,hand_block+0x10)=matrices;
 R_LINK_INIT(u32,body_data+4)=32;R_LINK_INIT(u32,body_data+8)=body_joints;
 R_LINK_INIT(u32,aura_data+4)=1;R_LINK_INIT(u32,aura_data+8)=aura_joints;R_LINK_INIT(u16,aura_tree+8)=1;
 R_LINK_INIT(u32,hand_data+4)=11;R_LINK_INIT(u32,hand_data+8)=hand_joints;
 R_LINK_INIT(u32,self+0x57F0)=0x78000;R_LINK_INIT(u32,self+0x57F4)=0x79000;
 const unsigned literals[]={0x10035DF0,0x10035DFC,0x10035D68,0x10035D74,0x10035E18,0x10035E24,0x10035D80,0x10035D8C,0x10035D08,0x10035D10,0x10035CF4,0x10035CFC};
 const char *words[]={"eyeLdamA","eyeRdamA","mayuLdamA","mayuRdamA","eyeLdamB","eyeRdamB","mayuLdamB","mayuRdamB","eyeL","eyeR","mayuL","mayuR"};
 for(unsigned i=0;i<12;i++){
  const u32 mat=materials+i*0x10,header=names+i*0x200;
  R_LINK_INIT(u32,mat)=header;R_LINK_INIT(u32,header+4)=0xFC;strcpy((char*)memory+header+0x100,words[i]);
  strcpy((char*)memory+LINK_INIT_DATA(literals[i]),words[i]);R_LINK_INIT(u32,mat+4)=i==11?0:mat+0x10;R_LINK_INIT(u32,mat+8)=shapes+i*8;R_LINK_INIT(u8,shapes+i*8+4)=1;
 }
 R_LINK_INIT(u32,body_joints+0x13*0x1C+0x10)=materials;
 R_LINK_INIT(u32,body_joints+8*0x1C+0x10)=materials;R_LINK_INIT(u32,body_joints+12*0x1C+0x10)=materials;
 for(unsigned j=1;j<=10;j++)R_LINK_INIT(u32,hand_joints+j*0x1C+0x10)=materials;
 R_LINK_INIT(u32,body_data+0x30)=texture;R_LINK_INIT(u32,body_data+0x34)=name_table;R_LINK_INIT(u16,texture)=count;R_LINK_INIT(u32,texture+4)=texture_headers;R_LINK_INIT(u32,texture+0x18)=0x80000000u;R_LINK_INIT(u32,texture+0x1C)=1;
 R_LINK_INIT(u32,self+0x45C)=10;R_LINK_INIT(u32,self+0x460)=headers;
 strcpy((char*)memory+LINK_INIT_DATA(0x10035D5C),"linktexS3TC");strcpy((char*)memory+texture_name,"linktexS3TC");
 strcpy((char*)memory+LINK_INIT_DATA(0x10035D18),"Always");strcpy((char*)memory+LINK_INIT_DATA(0x101CEB48),"Link");
 for(unsigned i=0;i<0x24;i++){memory[casual+i]=(u8)(0x71+i);for(unsigned j=0;j<70;j++)memory[texture_headers+j*0x24+i]=(u8)(j*37+i);}
 memcpy(originals,memory+texture_headers,sizeof originals);
 R_LINK_INIT(u32,image+0x1C)=0x98;
 R_LINK_INIT(u16,self+0x2F8)=1;
}
static void permit(unsigned offset,unsigned size){memset(allowed+offset,1,size);}
static void make_write_mask(void){
 const unsigned words[]={0x374,0x378,0x348,0x3B8,0x3BC,0x3B4,0x3C4,0x3C8,0x3D8,0x3DC,0x3E0,0x434,0x454,0x458,0x63C,0x640,0x834,0x8CC,0x8D4,0xCDC,0x5934,0x592C,0x5930,0x4438,0x443C,0x6938,0x6894,0x69BC,0x69E0,0x6A8C,0x6A88,0x6A80,0x6A84,0x6AAC,0x8268};
 const unsigned halves[]={0x324,0x32C,0x6940,0x69A2,0x69B0,0x6996,0x6976,0x697E,0x6926,0x8280};
 const unsigned bytes[]={0x8264,0x8265,0x827C,0x8282};
 for(unsigned i=0;i<sizeof words/sizeof *words;i++)permit(words[i],4);
 for(unsigned i=0;i<sizeof halves/sizeof *halves;i++)permit(halves[i],2);
 for(unsigned i=0;i<sizeof bytes/sizeof *bytes;i++)permit(bytes[i],1);
 permit(0x48C,0x168);permit(0x60C,0x30);permit(0x68B0,0x1C);
 for(unsigned i=0;i<2;i++){permit(0x5848+i*0x10,6);if(i)permit(0x5848+i*0x10+8,4);}
 for(unsigned i=0;i<3;i++){permit(0x5868+i*0x10,6);permit(0x5868+i*0x10+8,4);}
 permit(0x65D0,8);permit(0x65E0,8);
 const unsigned collisions[]={0x765C,0x778C,0x79EC,0x7B1C,0x7C54,0x7D8C,0x78BC,0x7EC4,0x7FFC,0x8128};
 for(unsigned i=0;i<10;i++)permit(collisions[i]+0x44,4);
 permit(0x765C+0xD8,4);permit(0x765C+0x94,4);permit(0x79EC+0x28,4);permit(0x7EC4+0x134,4);permit(0x8128+0x10,4);permit(0x8128+0x134,4);
}
int main(void){unsigned checks=0;make_write_mask();
 const unsigned counts[]={0,1,2,10,12,65,70};
 for(unsigned hero=0;hero<2;hero++)for(unsigned initial_casual=0;initial_casual<2;initial_casual++)for(unsigned mode=0;mode<16;mode++)for(unsigned last=0;last<8;last++)for(unsigned active=0;active<2;active++)for(unsigned c=0;c<7;c++){
  reset(counts[c]);R_LINK_INIT(u16,self+0x2F8)=active;init_should_start_with_heros_clothes=hero;R_LINK_INIT(u32,self+0xB0)=0x47000000u|(mode<<12);R_LINK_INIT(u8,save+0x1177)=last;
  R_LINK_INIT(u32,self+0x3BC)=0xA5A50000u|(initial_casual?8:0);R_LINK_INIT(u8,info+0x5B30)=0x80;
  memcpy(save_before,memory+save,sizeof save_before);randomizer_spawn_actor=0x5555;randomizer_spawn_resource_phase=7;
  memcpy(actor_before,memory+self,sizeof actor_before);memcpy(info_before,memory+info,sizeof info_before);
  randomizer_player_init(self);
  for(unsigned i=0;i<sizeof actor_before;i++)if(!allowed[i]&&actor_before[i]!=memory[self+i]){fprintf(stderr,"unexpected actor byte %x\n",i);assert(0);}
  assert(!memcmp(info_before,memory+info,sizeof info_before));
  assert(randomizer_spawn_actor==0x5555&&randomizer_spawn_resource_phase==7);
  // The late resource seam changes only lastMode to2 before its native read.
  unsigned effective=!active||last==2||last==3?last:2;save_before[0x1177]=effective;assert(!memcmp(save_before,memory+save,sizeof save_before));
  const u32 expected_demo=mode==14?0xD4:mode==12?0xD0:mode==6?0xCF:mode==10?0xD2:mode==11?0xD3:mode==9||effective==6?0xCB:mode==2&&(effective==4||effective==5)?0xC9:mode==15?0xD5:0x47;
  assert(chosen_demo==expected_demo&&R_LINK_INIT(u32,self+0x69E0)==0x76543210);
  assert(R_LINK_INIT(u32,self+0x3B8)==(0x100u|(mode==10?0x08000000u:0)));
  const unsigned casual_selected=!hero||initial_casual;assert(R_LINK_INIT(u32,self+0x3BC)==(0xA5A50000u|(casual_selected?8:0)));
  assert(restores==!casual_selected&&arrays==!casual_selected);
  assert(anime_heaps==8&&solid_heaps==2&&resources==3&&resource_indices[0]==0x71&&resource_indices[1]==0x72&&resource_indices[2]==0x71);
  assert(R_LINK_INIT(u32,self+0x5934)==casual&&R_LINK_INIT(u32,self+0x592C)==image&&R_LINK_INIT(u32,self+0x5930)==image+0x98);
  assert(R_LINK_INIT(s32,self+0x69BC)==-128&&R_LINK_INIT(s16,self+0x6976)==8);
  assert(R_LINK_INIT(u32,self+0x458)==(counts[c]<10?counts[c]:10));
  for(unsigned j=0;j<counts[c];j++){
   const u32 target=texture_headers+j*0x24;
   if(!casual_selected&&(j<9||(j==9&&counts[c]<=10)))assert(!memcmp(memory+target,originals+j*0x24,0x24));
   else {assert(R_LINK_INIT(u32,target)==R_LINK_INIT(u32,casual));assert(R_LINK_INIT(u32,target+0x1C)==(uint32_t)(R_LINK_INIT(u32,casual+0x1C)+casual-target));assert(R_LINK_INIT(u32,target+0xC)==(uint32_t)(R_LINK_INIT(u32,casual+0xC)+casual-target));assert(R_LINK_INIT(u32,target+0x20)==R_LINK_INIT(u32,casual+0x20));}
  }
  uint32_t expected_masks[4]={0};
  for(unsigned index=0;index<counts[c];index++){
   unsigned shift=index<64?index:index-64,bank=index<64?0:2;
   uint64_t seed=UINT64_C(0x8000000000000001);uint64_t shifted=shift<64?seed<<shift:0;
   expected_masks[bank]|=(uint32_t)(shifted>>32);expected_masks[bank+1]|=(uint32_t)shifted;
  }
  for(unsigned word=0;word<4;word++)assert(R_LINK_INIT(u32,texture+8+word*4)==expected_masks[word]);
  assert(R_LINK_INIT(u32,body_model+0xB8)==self&&R_LINK_INIT(u32,aura_model+0xB8)==self);
  assert(R_LINK_INIT(u16,hand_block+4)==0x10);for(unsigned i=0;i<12*10;i++)assert(R_LINK_INIT(u32,matrices+0x30+i*4)==0);
  for(unsigned i=0;i<4;i++){assert(R_LINK_INIT(u32,self+0x62C+i*4)==shapes+i*8);assert(R_LINK_INIT(u32,self+0x61C+i*4)==shapes+(i+4)*8);assert(R_LINK_INIT(u32,self+0x60C+i*4)==shapes+(i+8)*8);}
  assert(R_LINK_INIT(u32,self+0x348)==body_model+0xC8);
  unsigned filtered[128],length=0;for(unsigned i=0;i<n;i++)if(trace[i]!=ID_terminate&&trace[i]!=ID_joint_tree&&trace[i]!=ID_name)filtered[length++]=trace[i];
  unsigned expected[]={1,5,6,6,6,100,100,7,8,8,8,9,9,9,8,9,10,9,11,11,11,11,11,11,11,12,13,12,13,11,14,14,14,14,15,16,16,100,17,18,19,20,16};unsigned expected_size=sizeof expected/sizeof *expected;
  assert(length==expected_size+1+(!casual_selected?2:0)&&!memcmp(filtered,expected,sizeof expected));
  if(!casual_selected)assert(filtered[expected_size]==ID_array&&filtered[expected_size+1]==ID_copy_texture_list);
  assert(filtered[length-1]==100);checks++;
 }
 printf("full player init: %u outfit/spawn/texture cases passed\n",checks);return 0;
}

'''
        with tempfile.TemporaryDirectory() as directory:
            c,exe=Path(directory)/'test.c',Path(directory)/'test'
            c.write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
