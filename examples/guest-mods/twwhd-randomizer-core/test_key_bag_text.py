"""Full native tag dispatch plus all five live/saved dungeon key commands."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class KeyBagText(unittest.TestCase):
    def test_consumer(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef uint16_t u16;typedef int32_t s32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned char arena[0x50000],data[0x200000];static const u32 tag=0x30020000,save=0x30030000,other=0x30040000;
static void* mem(u32 a){if(a>=0x30000000&&a<0x30000000+sizeof arena)return arena+a-0x30000000;if(a>=0x10000000&&a<0x10200000)return data+a-0x10000000;if(a>0xFFFFFFFFu)return(void*)a;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_KEY_TEXT(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
#include "key_bag_text.h"
static unsigned native,number_calls,unit_calls,mutate,nested;static u32 expected_info,expected_count,expected_big,position,number_result,unit_result;
static u32 ordinary(u32 group,u32 self,u32 message,u32 index,u32 header,u32 a7,u32 a8){assert(self==11&&message==(group>=4?tag:22)&&index==position&&header==tag&&a7==77&&a8==88);native=group+1;return 0x80000000u+group;}
u32 rkt_group0(u32 a,u32 b,u32 c,u32 d,u32 e,u32 f){return ordinary(0,a,b,c,d,e,f);}
u32 rkt_group1(u32 a,u32 b,u32 c,u32 d,u32 e,u32 f){return ordinary(1,a,b,c,d,e,f);}
u32 rkt_group2(u32 a,u32 b,u32 c,u32 d,u32 e,u32 f){return ordinary(2,a,b,c,d,e,f);}
u32 rkt_sound(u32 a,u32 b,u32 c,u32 d,u32 e,u32 f){return ordinary(4,a,b,c,d,e,f);}
u32 rkt_play_value(u32 a,u32 b,u32 c,u32 d,u32 e,u32 f){return ordinary(5,a,b,c,d,e,f);}
u32 rkt_play_byte(u32 a,u32 b,u32 c,u32 d,u32 e,u32 f){return ordinary(6,a,b,c,d,e,f);}
u32 rkt_number(u32 self,u32 message,u32 index,u32 count,u32 digits,u32 no_pad){assert(self==11&&message==22&&index==position&&count==expected_count&&!digits&&no_pad==1);number_calls++;if(mutate){W(u32,0x101F84DC)=other;W(u8,expected_info+0x21)=expected_big?4:0;W(u8,other+0x7BC)=3;W(u8,other+0x798+0x21)=expected_big?0:4;}if(nested){const u16 previous=W(u16,tag+2);W(u16,tag+2)=0;assert(randomizer_key_bag_text(11,22,position,tag,77,88)==(s32)0x80000000u);W(u16,tag+2)=previous;native=0;}return number_result;}
u32 rkt_unit(u32 self,u32 message,u32 index,u32 label){assert(self==11&&message==22&&index==(unsigned int)(position+number_result));const u32* words=(const u32*)label;assert(words[1]==(u32)&WWHD_GAME_DATA(0x1010394C,u8));assert(strcmp((const char*)words[0],expected_big?"Unit_Key_01":"Unit_Key_00")==0);unit_calls++;return unit_result;}
static void reset(void){native=number_calls=unit_calls=mutate=nested=0;position=13;number_result=2;unit_result=7;W(u32,0x101F84DC)=save;W(u16,tag+2)=7;W(u16,tag+4)=0;}
int main(void){unsigned cases=0;
 for(unsigned group=0;group<65536;group++){reset();W(u16,tag+2)=group;s32 result=randomizer_key_bag_text(11,22,position,tag,77,88);unsigned known=group<=2||(group>=4&&group<=6);assert(native==(known?group+1:0)&&!number_calls&&!unit_calls&&result==(known?(s32)(0x80000000u+group):-1));cases++;}
 for(unsigned cmd=0;cmd<65536;cmd++){reset();W(u16,tag+4)=cmd;const unsigned valid=cmd>=0x4B&&cmd<=0x4F;expected_count=9;expected_big=1;expected_info=save+0x3A0+(cmd-0x48)*0x24;if(valid){W(u8,save+0x7BC)=0xFF;W(u8,expected_info+0x20)=9;W(u8,expected_info+0x21)=4;}const s32 result=randomizer_key_bag_text(11,22,position,tag,77,88);assert(!native&&number_calls==valid&&unit_calls==valid&&result==(valid?9:-1));cases++;}
 for(unsigned dungeon=3;dungeon<=7;dungeon++)for(unsigned current=0;current<10;current++)for(unsigned count=0;count<256;count++)for(unsigned flags=0;flags<8;flags++){
  reset();W(u16,tag+4)=dungeon+0x48;W(u8,save+0x7BC)=current;const u32 stored=save+0x3A0+dungeon*0x24,live=save+0x798;expected_info=current==dungeon?live:stored;expected_count=count;expected_big=(flags&4)!=0;W(u8,stored+0x20)=123;W(u8,live+0x20)=234;W(u8,stored+0x21)=~flags;W(u8,live+0x21)=~flags;W(u8,expected_info+0x20)=count;W(u8,expected_info+0x21)=flags;assert(randomizer_key_bag_text(11,22,position,tag,77,88)==9&&number_calls==1&&unit_calls==1);cases++;
 }
 const unsigned lengths[]={0,1,2,0x7FFFFFFFu,0x80000000u,0xFFFFFFFFu};
 for(unsigned i=0;i<6;i++)for(unsigned j=0;j<6;j++)for(unsigned k=0;k<6;k++)for(unsigned big=0;big<2;big++)for(unsigned live=0;live<2;live++){
  reset();position=lengths[i];number_result=lengths[j];unit_result=lengths[k];expected_count=255;expected_big=big;mutate=1;nested=1;W(u16,tag+4)=0x4E;W(u8,save+0x7BC)=live?6:0xFF;expected_info=live?save+0x798:save+0x3A0+6*0x24;W(u8,expected_info+0x20)=255;W(u8,expected_info+0x21)=big?0:4;assert(randomizer_key_bag_text(11,22,position,tag,77,88)==(s32)(unsigned int)(number_result+unit_result)&&number_calls==1&&unit_calls==1);cases++;
 }
 printf("%u complete key-bag message cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);(path/'test.c').write_text(source)
            subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
