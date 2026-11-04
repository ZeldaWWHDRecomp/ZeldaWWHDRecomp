from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class ItemGetMusic(unittest.TestCase):
    def test_item_selection_and_callback_order(self):
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint16_t u16;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_ADDR_save_info_pointer 1
static _Alignas(8) u8 first[0x5C30],second[0x5C30],save[0x30];
static u32 saveptr=(u32)save,play;static unsigned system_calls,bgm_calls,getters,mutate;static u32 selected;
#define WWHD_GAME_DATA(a,t) (*(assert((a)==1),(t*)&saveptr))
#include "item_get_music.h"
void rig_system_sound(u32 id){assert(!getters&&!bgm_calls);++system_calls;selected=id;if(mutate)play=(u32)second;}
void rig_sub_bgm(u32 id){assert(!getters&&!system_calls);++bgm_calls;selected=id;if(mutate)play=(u32)second;}
u32 rig_game(void){assert(system_calls+bgm_calls==1);++getters;return play;}
static void check(unsigned item,unsigned maximum,s32 short_flag){
 memset(first,0xA5,sizeof first);memset(second,0xA5,sizeof second);play=(u32)first;*(u16*)(save+0x20)=maximum;system_calls=bgm_calls=getters=0;
 randomizer_item_get_music(0x1234,item,short_flag);
 unsigned system=item==1||item==2||item==3||item==4||item==5||item==0xB3;
 unsigned music=0x80000002;
 if(item==8||(item==7&&(maximum%4)==3))music=0x80000024;
 else if(item==6||item==7||item==15||item==21||item==31)music=0x80000025;
 else if(item==105||item==106||item==107)music=0x8000004F;
 else if(item==109||item==110||item==111||item==112||item==113||item==114)music=0x80000027;
 assert(system_calls==system&&bgm_calls==!system&&getters==1&&selected==(system?0x8F5:music));
 assert(*(u8*)(play+0x5C20)==(system?2:1));assert((mutate?first:second)[0x5C20]==0xA5);
 for(unsigned i=0;i<sizeof first;i++)if(i!=0x5C20)assert(first[i]==0xA5&&second[i]==0xA5);
}
int main(void){
 const s32 flags[]={0,1,-1,0x7FFFFFFF};
 for(unsigned item=0;item<256;item++)for(unsigned mod=0;mod<4;mod++)for(unsigned f=0;f<4;f++)for(mutate=0;mutate<2;mutate++)check(item,12+mod,flags[f]);
 for(unsigned max=0;max<65536;max++)check(7,max,0);
 check(0xFFFFFFFF,12,1);check(0x80000000,12,1);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'test.c'
            executable = Path(directory) / 'test'
            source.write_text(code)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent), str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
