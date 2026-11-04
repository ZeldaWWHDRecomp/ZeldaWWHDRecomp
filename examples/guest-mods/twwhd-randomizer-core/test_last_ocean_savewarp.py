from pathlib import Path
import subprocess
import tempfile
import unittest

class LastOceanSavewarp(unittest.TestCase):
    def test_full_consumer_paths_callbacks_and_flag(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef int8_t s8;typedef int32_t s32;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(uintptr_t) u8 save[2][0x12C0],play[0x6000],info[32],map[64],player[0x7000],ship[0x400],gfx[0x240],ocean[0x50],vt[64],table[84],scene[12];
static char sea[]="sea",pship[]="PShip",empty[]="";
static u32 save_pointer;static unsigned init_entrances_shuffled,original,sets,string_calls,v_calls,map_calls,meshes,scenes,landings,times,assertions,events;
static unsigned mode,landing,swap_save,expected_save,sector,expected_room,expected_point;static u32 expected_position;static char expected_name[8];
static void* data(u32 a){switch(a){case 1:return &save_pointer;case 0x101D5E18:return table;case 0x1004B8C0:return vt;case 0x1004BA60:return sea;case 0x1004BA64:return pship;case 0x1004BA6C:return empty;case 0x101F8344:return &(*(u32*)(vt+32));default:return empty;}}
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
static u32 wwhd_dComIfG_getGameInfo(void){return (u32)play;}
static s32 wwhd_dSv_event_c_isEventBit(u32 self,u32 flag){assert(self==save_pointer+0x644&&flag==0x0101);events++;return 1;}
static u8 wwhd_dSv_event_c_getEventReg(u32 self,u32 flag){assert(self==save_pointer+0x644);return flag==0xC3FF?200:149;}
static u32 vcall(u32 object,u32 slot){assert(object==(u32)play+0x5150);v_calls++;if(slot==0x15C)return (u32)info;assert(slot==0x74);return (u32)map;}
#define RANDOMIZER_SAVEWARP_VCALL(object,slot) vcall(object,slot)
static void string_call(u32 string){(void)string;string_calls++;}
#define RANDOMIZER_SAVEWARP_STRING_CALL(target,string) ((void)(target),string_call(string))
#include "last_ocean_savewarp.h"
void randomizer_native_savewarp_stage(void){original++;}
void randomizer_savewarp_assure(u32 string){string_call(string);}
void randomizer_savewarp_set(u32 place,u32 name,s32 room,u32 point){assert(place==(u32)save[expected_save]+0x50);assert(!memcmp((void*)name,expected_name,8));assert(room==(s32)expected_room&&point==expected_point);sets++;ocean[0x3E]=17;}
void randomizer_savewarp_time(f32 t){assert(t==120);times++;}
u32 randomizer_savewarp_landing(u32 room){assert(room==19);landings++;return landing;}
u32 randomizer_savewarp_mesh(u32 position){if(mode==6){assert(*(uint32_t*)position==1&&*(uint32_t*)(position+4)==2&&*(uint32_t*)(position+8)==3);}else assert(position==expected_position);meshes++;return (u32)scene;}
u32 randomizer_savewarp_scene(s32 number){assert(number==0);scenes++;return (u32)scene;}
u32 randomizer_savewarp_ocean_z(u32 m){assert(m==(u32)map&&map_calls++==0);return (u32)(s32)-2;}
u32 randomizer_savewarp_ocean_x(u32 m){assert(m==(u32)map&&map_calls++==1);ocean[0x3E]=(u8)sector;if(swap_save)save_pointer=(u32)save[1];return (u32)(s32)-3;}
void randomizer_savewarp_assert(u32 file,s32 line,u32 condition){(void)file;(void)line;(void)condition;assertions++;}
static void reset(void){memset(save,0,sizeof(save));memset(play,0,sizeof(play));memset(info,0,sizeof(info));memset(table,0,sizeof(table));memset(player,0,sizeof(player));memset(scene,0,sizeof(scene));save_pointer=(u32)save[0];*(u32*)(vt+32)=(u32)gfx;*(u32*)(gfx+0x218)=(u32)ocean;table[0]=1;*(u16*)(table+2)=0x0101;memcpy(scene,"Room",5);scene[8]=77;scene[9]=201;*(u32*)(play+0x5B34)=(u32)player;*(u32*)(play+0x5B3C)=(u32)ship;memcpy(play+0x5134,"Other",6);player[0x326]=19;player[0x69E7]=33;memset(expected_name,0,8);memcpy(expected_name,"sea",4);sets=original=string_calls=v_calls=map_calls=meshes=scenes=landings=times=assertions=events=0;expected_room=11;expected_point=0;expected_save=swap_save=0;landing=1;ocean[0x3E]=0;}
int main(void){
 init_entrances_shuffled=0;reset();randomizer_savewarp_stage();assert(original==1&&!sets&&!events);
 init_entrances_shuffled=1;
 for(mode=0;mode<9;mode++)for(sector=0;sector<256;sector++)for(swap_save=0;swap_save<2;swap_save++){
  unsigned requested_swap=swap_save;reset();swap_save=requested_swap;
  if(mode==0){memcpy(table+4,"Custom",7);table[12]=200;table[13]=255;memcpy(expected_name,"Custom",7);expected_room=(unsigned int)(s32)-56;expected_point=255;}
  if(mode==1){memcpy(play+0x5134,"PShip",6);expected_room=(unsigned int)(s32)-56;expected_point=149;}
  if(mode>=2&&mode<=4){*(u32*)(info+12)=7u<<16;if(mode==2){memcpy(expected_name,"Other",6);expected_room=19;expected_point=33;}else{landing=0;memcpy(expected_name,"Room",5);expected_room=(unsigned int)(s32)-55;expected_point=77;expected_position=(u32)ship+0x314;if(mode==4){*(u32*)(play+0x5B3C)=0;expected_position=(u32)player+0x314;}}}
  if(mode==5){*(u32*)(info+12)=3u<<16;memcpy(expected_name,"Room",5);expected_room=(unsigned int)(s32)-55;expected_point=77;}
  if(mode==6){info[9]=20;*(uint32_t*)(play+0x5D18)=1;*(uint32_t*)(play+0x5D1C)=2;*(uint32_t*)(play+0x5D20)=3;memcpy(expected_name,"Room",5);expected_room=(unsigned int)(s32)-55;expected_point=77;}
  if(mode==7){info[9]=22;expected_room=sector+1;expected_save=swap_save;}
  randomizer_savewarp_stage();assert(sets==1&&!original&&!assertions&&events==1);
  if(mode==0){assert(string_calls==3&&!v_calls);continue;}
  assert(string_calls==6&&v_calls==(mode==7?3u:2u));
  assert(times==(mode==1));assert(landings==(mode>=2&&mode<=4));assert(meshes==(mode==3||mode==4||mode==6));assert(scenes==(mode==5));assert(map_calls==(mode==7?2u:0u));
 }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            header=Path(__file__).with_name('last_ocean_savewarp.h').read_text()
            for offset in ['5D18','5D1C','5D20']:
                header=header.replace('(volatile u32*)(game+0x'+offset+')','(volatile uint32_t*)(game+0x'+offset+')')
            header=header.replace('u32 position[3]','uint32_t position[3]')
            (root/'last_ocean_savewarp.h').write_text(header)
            (root/'test.c').write_text(source)
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'test.c'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)
    def test_string_capture_reload_and_bound(self):
        header=Path(__file__).with_name('last_ocean_savewarp.h').read_text()
        helper=header[header.index('static int randomizer_savewarp_equal'):header.index('static void randomizer_savewarp_copy')]
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef uint8_t u8;
static _Alignas(uintptr_t) u8 vt[64];static char old[]="old",good[]="new",other[]="new",bad[]="bad",long_a[0x40002],long_b[0x40002];
static unsigned calls,mode;static u32 literal;
static void call_string(u32 target,u32 object){(void)target;calls++;if(calls<3){literal=object;return;}if(mode){*(u32*)literal=mode==1?(u32)bad:mode==2?(u32)good:(u32)bad;*(u32*)object=mode==1?(u32)old:(u32)other;}}
static void randomizer_savewarp_assure(u32 object){call_string(0,object);}
#define RANDOMIZER_SAVEWARP_STRING_CALL(target,object) call_string(target,object)
#define WWHD_GAME_DATA(a,t) (*(t*)vt)
'''+helper+r'''
int main(void){for(unsigned direct=0;direct<2;direct++)for(mode=1;mode<4;mode++){calls=0;assert(randomizer_savewarp_equal((u32)old,(u32)other,direct)==(mode!=3)&&calls==3);}mode=0;memset(long_a,'a',0x40001);memset(long_b,'a',0x40001);calls=0;assert(!randomizer_savewarp_equal((u32)long_a,(u32)long_b,0)&&calls==3);}
'''
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'test.c').write_text(source)
            subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(root/'test.c'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
