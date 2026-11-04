from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class BeedleBlueJelly(unittest.TestCase):
    def test_dialogue_consumer(self):
        code = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;
static _Alignas(8) u8 actor[0xA00],play[0x5300],save[0x800];static u32 saveptr=(u32)save;static u16 table[7]={0xA000,0xA001,0xA002,0xA003,0xA004,0xA005,0xA006};
#define WWHD_ADDR_save_info_pointer 1
static void* data(u32 a){if(a==1)return &saveptr;assert(a==0x100185D4);return table;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#define RBB(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)(a))
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#include "beedle_blue_jelly.h"
static unsigned spoils,reg,points,sold,owned,sells,events[65536],ons[65536],assertions,mutate,queries;
u32 rbb_game(void){++queries;return (u32)play;}
s32 rbb_spoils(u32 item){assert(item==play[0x52B1]);if(mutate)actor[0x99E]=99;return spoils;}
s32 rbb_sold_out(u32 p){assert(p==(u32)actor+0x940);return sold;}
s32 rbb_owned(u32 item){assert(item==0x31);return owned;}
s32 rbb_sells_bombs(u32 a){assert(a==(u32)actor);return sells;}
u8 rbb_register_native(u32 p,u32 key){assert(p==saveptr+0x644);if(key==0x86FF)return points;if(key==0x7F0F||key==0xBB07)return reg;assert(0);return 0;}
s32 rbb_event_native(u32 p,u32 flag){assert(p==saveptr+0x644);return events[flag];}
void rbb_on_native(u32 p,u32 flag){assert(p==saveptr+0x644);++ons[flag];}
void rbb_assert(u32 file,s32 line,u32 cond){assert(file==0x100185E4&&line==0x5D5&&cond==0x100185D0);++assertions;}
static void reset(void){memset(actor,0,sizeof actor);memset(play,0,sizeof play);memset(events,0,sizeof events);memset(ons,0,sizeof ons);spoils=reg=points=sold=owned=sells=assertions=mutate=queries=0;}
int main(void){unsigned cases=0;
const s8 types[]={-128,-1,0,1,127};
for(unsigned t=0;t<5;t++)for(unsigned mode=0;mode<256;mode++)for(unsigned item=0;item<256;item++)for(spoils=0;spoils<2;spoils++) {
 memset(actor,0,sizeof actor);actor[0x99E]=(u8)types[t];play[0x52B0]=mode;play[0x52B1]=item;reg=12;points=sold=owned=sells=mutate=0;
 unsigned expected;
 if(mode>=1&&mode<=4){
  if(types[t]==0){if(spoils){if(item==0x1F)expected=0xF94;else if(item==0x4B)expected=0xF75;else if((item>=0x45&&item<=0x47)||(item>=0x49&&item<=0x4A))expected=table[item-0x45];else expected=0xFD4;}else expected=item==0x9D?0xF6F:item==0x9E?0xF73:0xF75;}
  else expected=(item==0x9D||item==0x9E)?0x2F56:spoils?0x2F79:0x2F57;
 }else expected=types[t]==0?0xF3D:0;
 if(!(mode>=1&&mode<=4)&&types[t]!=0){reg=0;expected=0x2F45;}
 assert(randomizer_beedle_get_message((u32)actor)==expected);
 if(mode>=1&&mode<=4&&types[t]==0&&spoils)assert(RBB(u32,(u32)actor+0x9AC)==item);
 ++cases;
}
reset();RBB(u32,(u32)actor+0x8B8)=0xDEADBEEF;assert(randomizer_beedle_get_message((u32)actor)==0xDEADBEEF&&!queries&&!RBB(u32,(u32)actor+0x8B8));
reset();play[0x52B0]=1;play[0x52B1]=0x4B;spoils=mutate=1;assert(randomizer_beedle_get_message((u32)actor)==0xF75&&actor[0x99E]==99);
for(unsigned type=0;type<2;type++)for(points=0;points<256;points++)for(unsigned seen=0;seen<2;seen++){
 memset(actor,0,sizeof actor);actor[0x99E]=type;actor[0x9A2]=seen;play[0x52B0]=0;spoils=sold=owned=sells=0;reg=0;
 unsigned expected=type?(seen?0x2F46:0x2F45):points>=60?(seen?0xF5D:0xF5C):points?(seen?0xF5B:0xF5A):seen?0xF41:0xF3D;
 assert(randomizer_beedle_get_message((u32)actor)==expected);
}
reset();sold=1;assert(randomizer_beedle_get_message((u32)actor)==0xF3D);actor[0x99E]=1;assert(randomizer_beedle_get_message((u32)actor)==0x2F62);
reset();owned=sells=1;assert(randomizer_beedle_get_message((u32)actor)==0xF55&&ons[0x1F20]&&actor[0x9A3]==1);
reset();actor[0x99E]=1;owned=sells=1;assert(randomizer_beedle_get_message((u32)actor)==0x2F64&&ons[0x1F20]&&actor[0x9A3]==1);
for(unsigned r=0;r<256;r++)for(unsigned seen=0;seen<2;seen++){
 reset();reg=r;actor[0x99E]=1;actor[0x9A2]=seen;
 const unsigned first[]={0x2F45,0x2F58,0x2F59,0x2F5A,0x2F5B,0x2F5C,0x2F5D,0x2F5E};
 unsigned expected=r<8?(seen?(r<6?0x2F46:r==6?0x2F69:0x2F6A):first[r]):0;
 assert(randomizer_beedle_get_message((u32)actor)==expected&&assertions==(r>=8));
}
assert(cases==655360);return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'test.c'
            executable = Path(directory) / 'test'
            source.write_text(code)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent), str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
