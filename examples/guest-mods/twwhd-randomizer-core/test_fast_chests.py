"""Fast chest source flow, retained callback order, GHS PMF and fixed data policy."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from fast_chest_data import MODEL_INFO, MODEL_INFO_SIZE, FAST_OPEN_FIELDS, validate_fast_chest_data


class FastChests(unittest.TestCase):
    def test_seed_table_exact_policy(self):
        original=bytes((n*43+17)&255 for n in range(MODEL_INFO_SIZE))
        expected=bytearray(original)
        for offset in FAST_OPEN_FIELDS:
            expected[offset:offset+2]=b'\0\x09'
        def reader(value):
            def read(address,size):
                self.assertEqual((address,size),(MODEL_INFO,MODEL_INFO_SIZE))
                return value
            return read
        validate_fast_chest_data(reader(original),reader(expected))
        for index in range(MODEL_INFO_SIZE):
            for bit in range(8):
                changed=bytearray(expected);changed[index]^=1<<bit
                with self.assertRaisesRegex(ValueError,'unsupported treasure-chest'):
                    validate_fast_chest_data(reader(original),reader(changed))
        for size in (0,1,MODEL_INFO_SIZE-1,MODEL_INFO_SIZE+1):
            for first,second in ((bytes(size),expected),(original,bytes(size))):
                with self.assertRaisesRegex(ValueError,'short treasure-chest'):
                    validate_fast_chest_data(reader(first),reader(second))

    def test_complete_consumer(self):
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t u32;typedef int32_t s32;typedef uint16_t u16;typedef int16_t s16;typedef uint8_t u8;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#define WWHD_HOOK(a,n,p) void n p
static _Alignas(16) u8 memory[0x30000],before[0x800],old_play[3][0x7000];
static const u32 self=0x1000,info1=0x8000,info2=0x10000,info3=0x18000;
static u32 current_info,create_result,shape_answer;static s32 box_answer;
static unsigned trace[16],n,checks;static u32 seen_item;
static u16 table[24];
static u16 *data(u32 a){assert(a>=0x101D2320&&a<0x101D2350&&!(a&1));return table+(a-0x101D2320)/2;}
#define WWHD_GAME_DATA(a,t) (*(t*)data(a))
#define R_FAST_CHEST(t,a) (*(volatile t*)(memory+(a)))
#define FAST_CHEST_DATA(a) (a)
static void record(unsigned x){assert(n<16);trace[n++]=x;}
static u32 wwhd_dComIfG_getGameInfo(void){record(1);return current_info;}
#include "fast_chests.h"
u32 fast_chest_demo_item(u32 pos,u32 item,s32 room,s32 flag,u32 angle,u32 scale){
 record(2);assert(pos==self+0x314&&room==-1&&flag==-1&&!angle&&!scale);seen_item=item;
 assert(R_FAST_CHEST(u16,info1+0x52B8)==(u16)(0xA5A1|4));current_info=info2;return create_result;
}
s32 fast_chest_shape(u32 actor){
 record(3);assert(actor==self);
 if(R_FAST_CHEST(u16,self+0xF8)==3){assert(R_FAST_CHEST(u32,self+0x6D8)==0xA5A5A5A5u);current_info=info3;}
 else {assert(R_FAST_CHEST(u16,self+0xFA)==0x104);R_FAST_CHEST(u16,self+0xFA)|=0x200;}
 return (s32)shape_answer;
}
s32 fast_chest_staff(u32 manager,u32 name,u32 actor,u32 id){
 record(4);assert(manager==info3+0x52C4&&name==0x10040014&&!actor&&!id);
 assert(R_FAST_CHEST(s16,self+0x6D4)==0&&R_FAST_CHEST(s16,self+0x6D6)==-1&&R_FAST_CHEST(u32,self+0x6D8)==0x024B7708);
 return -123;
}
s32 fast_chest_demo(u32 actor){record(5);assert(actor==self&&R_FAST_CHEST(s32,self+0x6E0)==-123);return 7;}
s32 fast_chest_box_check(u32 actor){record(6);assert(actor==self);if(box_answer)R_FAST_CHEST(u16,self+0xFA)=0x100;return box_answer;}
void fast_chest_event_name(u32 event,u32 name){record(7);assert(event==self+0xF8&&name==0x10040020&&R_FAST_CHEST(u16,self+0xFA)==0x304);}
static void reset(u16 command){
 memset(memory,0xA5,sizeof memory);n=0;current_info=info1;create_result=0;shape_answer=0;box_answer=0;
 R_FAST_CHEST(u16,self+0xF8)=command;R_FAST_CHEST(u16,info1+0x52B8)=0xA5A1;
 memcpy(before,memory+self,sizeof before);memcpy(old_play[0],memory+info1,0x7000);memcpy(old_play[1],memory+info2,0x7000);memcpy(old_play[2],memory+info3,0x7000);
}
static void unchanged(unsigned opening,unsigned active){
 for(unsigned i=0;i<sizeof before;i++){
  const int permitted=opening?((i>=0x6D4&&i<0x6DC)||(i>=0x6E0&&i<0x6E4)):(active&&i>=0xFA&&i<0xFC);
  assert(permitted||memory[self+i]==before[i]);
 }
 if(opening){*(u16*)(old_play[0]+0x52B8)|=4;if(create_result!=0xFFFFFFFFu)*(u32*)(old_play[1]+0x52A0)=create_result;}
 assert(!memcmp(old_play[0],memory+info1,0x7000)&&!memcmp(old_play[1],memory+info2,0x7000)&&!memcmp(old_play[2],memory+info3,0x7000));checks++;
}
int main(void){
 // Every native event command, all nonboolean shape answers ignored, false box gate.
 for(unsigned command=0;command<65536;command++){
  reset((u16)command);assert(randomizer_fast_chest_wait(self)==1);
  if(command==3){unsigned expected[]={1,2,1,3,1,4,5};assert(n==7&&!memcmp(trace,expected,sizeof expected));}
  else assert(n==1&&trace[0]==6);
  unchanged(command==3,0);
 }
 const u32 results[]={0,1,0x7FFFFFFF,0x80000000,0xFFFFFFFE,0xFFFFFFFF};
 for(unsigned item=0;item<256;item++)for(unsigned result=0;result<6;result++)for(unsigned shape=0;shape<4;shape++){
  reset(3);R_FAST_CHEST(s16,self+0x2FC)=(s16)((item<<8)|0xD3);memcpy(before,memory+self,sizeof before);create_result=results[result];shape_answer=shape;
  assert(randomizer_fast_chest_wait(self)==1&&seen_item==item);
  unsigned expected[]={1,2,1,3,1,4,5};if(create_result==0xFFFFFFFFu){unsigned failed[]={1,2,3,1,4,5};assert(n==6&&!memcmp(trace,failed,sizeof failed));}else assert(n==7&&!memcmp(trace,expected,sizeof expected));
  unchanged(1,0);
 }
 for(s32 box=-1;box<=2;box++)for(unsigned shape=0;shape<256;shape++){
  reset(1);box_answer=box;shape_answer=shape;assert(randomizer_fast_chest_wait(self)==1);
  if(box){unsigned expected[]={6,3,7};assert(n==3&&!memcmp(trace,expected,sizeof expected));}else assert(n==1&&trace[0]==6);
  unchanged(0,box!=0);
 }
 // Data initialization changes only the exact three halfwords, and is idempotent.
 for(unsigned i=0;i<24;i++)table[i]=(u16)(0xA500+i);u16 expected[24];memcpy(expected,table,sizeof table);expected[7]=expected[13]=expected[19]=9;
 randomizer_initialize_fast_chest_animations();assert(!memcmp(table,expected,sizeof table));randomizer_initialize_fast_chest_animations();assert(!memcmp(table,expected,sizeof table));
 printf("fast chest: %u complete callback/state cases passed\n",checks);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            c,exe=Path(directory)/'test.c',Path(directory)/'test'
            c.write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(Path(__file__).parent),str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
