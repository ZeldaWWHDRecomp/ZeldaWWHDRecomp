"""Reverse-key gates, complete consumer side effects and transient Cabana state."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


class ReverseEntrances(unittest.TestCase):
    def test_native_consumer_contracts(self):
        header = Path(__file__).with_name('reverse_entrances.h')
        imports = re.findall(r'WWHD_GAME_FUNC\([^,]+,([^,]+),(\w+),\((.*?)\)\);', header.read_text())
        enum = 'enum {' + ','.join('C_' + name for _, name, _ in imports) + ',C_game,C_grab,C_event,C_original10,C_original12,C_original_cabana,C_count};'
        bodies = {
            'entrance_door_type': 'return type;', 'entrance_door_arg': 'return arg;',
            'entrance_door10_stop_back': 'return stop_answer;', 'entrance_door10_stop_front': 'return stop_answer;',
            'entrance_door12_stop_back': 'return stop_answer;', 'entrance_door12_stop_front': 'return stop_answer;',
            'entrance_door10_size': 'return 225.0f;', 'entrance_door12_shape': 'return shape;',
            'entrance_has_dungeon_item': 'assert(memory==save+0x798&&item==2);return boss_key;',
            'entrance_check_area': 'assert(y==12100.0f&&z==62500.0f&&(x==12100.0f||x==225.0f));if(mutate_area){R_ENTRANCE(u8,self+0x3E2)=6;R_ENTRANCE(u16,self+0xFA)=0x200;}return area;',
            'entrance_search_actor': 'assert(callback==0xABCD&&*actor_id==grab);return found;',
            'entrance_event_bit': 'assert(event==save+0x644&&selector==0x2D80);return event_flag;',
            'entrance_knob_set_event': 'event_state=R_ENTRANCE(u8,self+0x5A4);R_ENTRANCE(u8,self+0x5A4)=99;',
            'entrance_init_open_demo': 'assert(value==0);',
            'entrance_knob_demo': 'assert(R_ENTRANCE(u8,self+0x5A4)==7);return 1;',
        }
        mocks = []
        for ret, name, args in imports:
            argnames = [a.strip().split()[-1].lstrip('*') for a in args.split(',') if a.strip() != 'void']
            unused = ''.join('(void)' + name + ';' for name in argnames)
            body = bodies.get(name, 'return 0;')
            mocks.append(f'{ret} {name}({args}){{{unused}record(C_{name});{body}}}')
        source = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef int32_t s32;typedef int16_t s16;typedef float f32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_GAME_ORIGINAL(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 memory[0x80000],expected_save[0x1300],expected_actor[0x600];
static const u32 self=0x10000,save=0x20000,play=0x30000;
static u32 save_pointer=0x20000,init_entrances_shuffled;
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)&save_pointer)
#define R_ENTRANCE(t,a) (*(volatile t*)(memory+(a)-0x10000))
#define R_ENTRANCE_SEARCH_CALLBACK 0xABCD
static s32 mock_grab(u32 player);
#define R_ENTRANCE_GRAB_ID(player) mock_grab(player)
ENUM
static unsigned counts[C_count],trace[128],trace_size;static u8 type,arg,boss_key,shape,found,area,stop_answer,event_flag,event_state,mutate_area;static s32 grab;
static void record(unsigned id){assert(trace_size<128);trace[trace_size++]=id;++counts[id];}
u32 wwhd_dComIfG_getGameInfo(void){record(C_game);return play;}
#include "reverse_entrances.h"
MOCKS
static s32 mock_grab(u32 player){assert(player==0x40000);record(C_grab);return grab;}
void entrance_native_door10_event(u32 actor){assert(actor==self);record(C_original10);}
void entrance_native_door12_event(u32 actor){assert(actor==self);record(C_original12);}
s32 entrance_native_cabana(u32 actor){assert(actor==self);record(C_original_cabana);return 77;}
static void reset(void){
 memset(memory,0x9D,0x600);memset(memory+save-0x10000,0xA7,0x1300);memset(counts,0,sizeof counts);trace_size=0;mutate_area=0;
 for(u32 i=0;i<12;i++){R_ENTRANCE(s16,self+0x3BE + i*2)=(s16)(0x1000+i);R_ENTRANCE(u8,self+0x3D6+i)=(u8)(0x40+i);}
 R_ENTRANCE(u16,self+0xFA)=0xA012;R_ENTRANCE(u32,play+0x5B2C)=0x40000;
}
static void remember(void){memcpy(expected_actor,memory,0x600);memcpy(expected_save,memory+save-0x10000,0x1300);}
static void isolated(unsigned door){
 assert(!memcmp(expected_save,memory+save-0x10000,0x1300));
 for(unsigned i=0;i<0x600;i++)if(i!=0x3E2&&i!=(door==10?0x4CE:0x4AA)&&!(i>=0xFA&&i<=0xFE)&&!(door==0&&i==0x5A4))assert(expected_actor[i]==memory[i]);
}
int main(void){
 unsigned checks=0;init_entrances_shuffled=1;
 for(unsigned door=10;door<=12;door+=2)for(type=0;type<8;type++)for(unsigned side=0;side<2;side++)for(unsigned lock=0;lock<2;lock++)for(boss_key=0;boss_key<2;boss_key++)for(unsigned keys=0;keys<3;keys++)for(unsigned stop=0;stop<3;stop++)for(unsigned bars=0;bars<2;bars++)for(area=0;area<2;area++)for(shape=0;shape<7;shape++)for(unsigned carry=0;carry<3;carry++){
  reset();arg=16;grab=carry?42:-1;found=carry==2;stop_answer=stop==1?1:0;
  R_ENTRANCE(u8,self+0x3BC)=(u8)side;R_ENTRANCE(u8,self+(door==10?0x424:0x400))=(u8)lock;
  R_ENTRANCE(u8,self+(door==10?0x4CC:0x4A8))=(u8)bars;R_ENTRANCE(u8,self+(door==10?0x4CE:0x4AA))=stop==2?255:(u8)stop;
  R_ENTRANCE(u8,save+0x7B8)=(u8)keys;remember();
  if(door==10)entrance_door10_event(self);else entrance_door12_event(self);
  const int boss=type==(door==10?1:3);const int prefix_return=door==12&&side&&boss;
  const int key_pass=!lock||(boss?boss_key:side||keys);const int allowed=!prefix_return&&!bars&&key_pass&&area;
  assert(counts[C_entrance_check_area]==(unsigned)(!prefix_return&&!bars&&key_pass));
  assert((R_ENTRANCE(u16,self+0xFA)==(u16)(0xA012|4))==allowed);
  if(allowed){unsigned action=side?3:2;if(door==12&&boss)action=6;else if(stop==1)action+=2;if(door==12&&(shape==1||shape==2||shape==4||shape==5)&&carry==2)action=11;
   assert(R_ENTRANCE(u8,self+0x3E2)==action);assert(R_ENTRANCE(s16,self+0xFC)==(s16)(0x1000+action));assert(R_ENTRANCE(u8,self+0xFE)==0x40+action);
   assert(trace[trace_size-1]==C_entrance_check_area);
  }
  assert(counts[C_entrance_has_dungeon_item]==(unsigned)(!prefix_return&&!bars&&lock&&boss));
  if(door==12&&!prefix_return&&!bars){unsigned n=shape==1?1:shape==2?2:shape==4?3:4;assert(counts[C_entrance_door12_shape]==n);assert(counts[C_game]==(unsigned)(shape==1||shape==2||shape==4||shape==5));}
  isolated(door);checks++;
 }
 // Exact ordered native helper traces, including the grabbed actor path.
 reset();type=0;arg=16;shape=5;grab=42;found=area=1;stop_answer=0;
 R_ENTRANCE(u8,self+0x3BC)=0;R_ENTRANCE(u8,self+0x400)=1;R_ENTRANCE(u8,self+0x4A8)=0;R_ENTRANCE(u8,self+0x4AA)=255;R_ENTRANCE(u8,save+0x7B8)=1;
 entrance_door12_event(self);
 {const unsigned wanted[]={C_entrance_door12_stop_back,C_entrance_door_type,C_entrance_door12_shape,C_entrance_door12_shape,C_entrance_door12_shape,C_entrance_door12_shape,C_game,C_grab,C_entrance_search_actor,C_entrance_door_type,C_entrance_check_area};assert(trace_size==sizeof wanted/sizeof *wanted&&!memcmp(trace,wanted,sizeof wanted));}
 reset();type=1;arg=0;boss_key=area=1;stop_answer=0;
 R_ENTRANCE(u8,self+0x3BC)=0;R_ENTRANCE(u8,self+0x424)=1;R_ENTRANCE(u8,self+0x4CC)=0;R_ENTRANCE(u8,self+0x4CE)=255;
 entrance_door10_event(self);
 {const unsigned wanted[]={C_entrance_door10_stop_back,C_entrance_door_type,C_entrance_door_arg,C_entrance_door_type,C_entrance_has_dungeon_item,C_entrance_door10_size,C_entrance_check_area};assert(trace_size==sizeof wanted/sizeof *wanted&&!memcmp(trace,wanted,sizeof wanted));}
 // Boss door10 reverse early return remains; argument16 is the native exception.
 reset();type=1;arg=0;R_ENTRANCE(u8,self+0x3BC)=1;entrance_door10_event(self);assert(!counts[C_entrance_check_area]&&!counts[C_entrance_door10_stop_front]);
 // The event tail re-reads actor fields after the native area helper.
 reset();type=0;R_ENTRANCE(u8,self+0x3BC)=1;R_ENTRANCE(u8,self+0x424)=0;R_ENTRANCE(u8,self+0x4CC)=0;R_ENTRANCE(u8,self+0x4CE)=0;mutate_area=area=1;entrance_door10_event(self);assert(R_ENTRANCE(s16,self+0xFC)==0x1006&&R_ENTRANCE(u16,self+0xFA)==0x204);
 for(unsigned option=0;option<2;option++)for(unsigned action=0;action<256;action++)for(unsigned side=0;side<2;side++)for(unsigned command=0;command<4;command++)for(event_flag=0;event_flag<2;event_flag++){
  reset();init_entrances_shuffled=option;R_ENTRANCE(u8,self+0x5A4)=(u8)action;R_ENTRANCE(u8,self+0x3BC)=(u8)side;R_ENTRANCE(u16,self+0xF8)=(u16)command;remember();
  s32 result=entrance_cabana(self);
  if(!option){assert(result==77&&counts[C_original_cabana]==1&&trace_size==1);}
  else if(command==3){assert(result==1&&trace_size==2&&trace[0]==C_entrance_init_open_demo&&trace[1]==C_entrance_knob_demo&&R_ENTRANCE(u8,self+0x5A4)==7);}
  else if(event_flag){assert(trace_size==1&&trace[0]==C_entrance_event_bit&&R_ENTRANCE(u8,self+0x5A4)==1);}
  else{assert(trace_size==2&&trace[0]==C_entrance_event_bit&&trace[1]==C_entrance_knob_set_event);assert(event_state==(side?1:action));assert(R_ENTRANCE(u8,self+0x5A4)==(side?8:99));}
  isolated(0);checks++;
 }
 init_entrances_shuffled=0;reset();remember();entrance_door10_event(self);assert(trace_size==1&&counts[C_original10]==1);isolated(10);reset();remember();entrance_door12_event(self);assert(trace_size==1&&counts[C_original12]==1);isolated(12);
 assert(checks>100000);return 0;
}
'''.replace('ENUM', enum).replace('MOCKS', '\n'.join(mocks))
        with tempfile.TemporaryDirectory() as directory:
            c, exe = Path(directory) / 'test.c', Path(directory) / 'test'
            c.write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-function', '-I', str(header.parent), str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
