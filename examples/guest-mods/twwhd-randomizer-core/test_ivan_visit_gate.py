from pathlib import Path
import re,subprocess,tempfile,unittest
class IvanVisitGate(unittest.TestCase):
 def test_manual_talk_retained_and_auto_query_order(self):
  header=Path(__file__).with_name('npc_replacement_cutscene_gates.h').read_text().replace('volatile u32*','volatile uint32_t*')
  for off in ['actor+0x3B4','randomizer_gate_info()+0x5B34']:header=header.replace('*(volatile uint32_t*)('+off+')','*(volatile u32*)('+off+')')
  manual={
   'randomizer_gate_info':'return(u32)play;',
   'randomizer_gate_event_query':'assert(event==(u32)save+(flag==0x580?0x1178:0x644));query_calls++;if(branch==0){assert(flag==0x580);return temporary;}assert(branch==2&&flag==0x1F80);if(clear_at_query)actor[0x834]=0x80;return first_flag;',
   'randomizer_gate_event_off':'assert(event==(u32)save+0x1178&&flag==0x580);off_calls++;*(uint32_t*)(bytes+0x2E0)=0x87654000;',
   'randomizer_gate_ivan_talk':'assert(actor==(u32)bytes);talk_calls++;assert(bytes[0x836]==8);bytes[0x836]=55;bytes[0x835]=99;return 12;',
   'randomizer_gate_ivan_animation':'assert(actor==(u32)bytes);animation_calls++;assert(bytes[0x835]==(branch==0?12:10));*(f32*)(bytes+0x370)=3;bytes[0x818]=4;',
   'randomizer_gate_ivan_demo_check':'assert(actor==(u32)bytes);demo_check_calls++;if(branch==1){bytes[0x835]=9;return 1;}return 0;',
   'randomizer_gate_ivan_demo':'assert(actor==(u32)bytes&&bytes[0x835]==10&&bytes[0x836]==9&&bytes[0x818]==1&&*(f32*)(bytes+0x370)==0);demo_calls++;return 0;',
   'randomizer_gate_ivan_next_visit':'assert(actor==(u32)bytes);next_calls++;bytes[0x818]=next_current;return next_mode;',
   'randomizer_gate_ivan_visit_init':'assert(actor==(u32)bytes&&mode==next_mode);init_calls++;bytes[0x818]=99;',
   'randomizer_gate_ivan_visit':'assert(actor==(u32)bytes);visit_calls++;bytes[0x818]=visit_mode;bytes[0x829]=animation;*(f32*)(bytes+0x370)=speed;',
   'randomizer_gate_chase':'assert(speed==(u32)bytes+0x370&&target==expected_target);assert(step==expected_step);chase_calls++;bytes[0x834]=0xA0;bytes[0x818]=angle_mode;return 0;',
   'randomizer_gate_actor_angle':'assert(actor==(u32)links[0]&&target==(u32)bytes);angle_calls++;*(u32*)(play+0x5B34)=(u32)links[1];*(s16*)(links[0]+0x32A)=100;*(s16*)(links[1]+0x32A)=200;return angle;',
   'randomizer_gate_ivan_event':'assert(actor==(u32)bytes&&query_calls==1&&(bytes[0x834]&1));event_calls++;',
  }
  source=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef int32_t s32;typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int16_t s16;typedef float f32;
static _Alignas(16) u8 bytes[0x900],save[0x1300],play[0x6000],links[2][0x400];
static u8* actor=bytes;static unsigned branch,temporary,first_flag,clear_at_query,next_current,next_mode,visit_mode,angle_mode,animation;
static unsigned query_calls,off_calls,talk_calls,animation_calls,demo_check_calls,demo_calls,next_calls,init_calls,visit_calls,chase_calls,angle_calls,event_calls;static f32 speed,expected_target,expected_step;static s16 angle;
#define WWHD_GAME_DATA(a,t) ((u32)save)
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
#include "npc_replacement_cutscene_gates.h"
'''
  for m in re.finditer(r'WWHD_GAME_FUNC\([^,]+,([^,]+),([^,]+),\(([^\n]*)\)\);',header):
   ret,name,args=m.groups();body=manual.get(name,'assert(0);'+(''if ret=='void'else'return 0;'));source+=f'{ret} {name}({args}){{{body}}}\n'
  source+=r'''
static void reset(unsigned path){memset(bytes,0,sizeof(bytes));memset(play,0,sizeof(play));memset(links,0,sizeof(links));branch=path;query_calls=off_calls=talk_calls=animation_calls=demo_check_calls=demo_calls=next_calls=init_calls=visit_calls=chase_calls=angle_calls=event_calls=clear_at_query=0;*(u32*)(play+0x5B34)=(u32)links[0];bytes[0x835]=8;}
int main(void){
 for(temporary=0;temporary<2;temporary++){reset(0);*(u16*)(bytes+0x7FC)=1;*(uint32_t*)(bytes+0x2E0)=0xABCD4000;assert(randomizer_ivan_visit((u32)bytes)==1&&talk_calls==1&&animation_calls==1&&demo_check_calls==0&&query_calls==1&&off_calls==temporary);assert(bytes[0x835]==12&&bytes[0x836]==55&&bytes[0x818]==1&&*(f32*)(bytes+0x370)==0&&*(uint32_t*)(bytes+0x2E0)==(temporary?0x87650000:0xABCD4000));}
 reset(1);assert(randomizer_ivan_visit((u32)bytes)==1&&demo_check_calls==1&&animation_calls==1&&demo_calls==1&&query_calls==0);
 const f32 speeds[7]={0,1,2.5f,9,12,13,NAN};
 for(animation=0;animation<8;animation++)for(visit_mode=0;visit_mode<10;visit_mode++)for(unsigned i=0;i<7;i++)for(unsigned same=0;same<2;same++){
  reset(2);next_mode=4;next_current=same?4:9;angle_mode=0;speed=speeds[i];expected_target=animation==4?2.5f:animation==5?(visit_mode==5?9.0f:12.0f):0;expected_step=expected_target-speed>=0?1.1f:2.8f;assert(randomizer_ivan_visit((u32)bytes)==1&&next_calls==1&&init_calls==!same&&visit_calls==1&&chase_calls==1&&query_calls==1&&event_calls==1&&angle_calls==0&&bytes[0x834]==0xA1);
 }
 animation=0;visit_mode=2;speed=0;expected_target=0;expected_step=1.1f;next_mode=next_current=2;angle_mode=4;
 for(unsigned raw=0;raw<65536;raw++)for(first_flag=0;first_flag<2;first_flag++){
  reset(2);angle=(s16)raw;assert(randomizer_ivan_visit((u32)bytes)==1&&angle_calls==1&&query_calls==1);
  uint16_t delta=(uint16_t)(raw-100u);unsigned keep=delta<=0x1800||delta>=0xE800||delta==0x8000;assert(event_calls==keep&&bytes[0x834]==(keep?0xA1:0xA0));
 }
 reset(2);angle=100;*(u16*)(bytes+0x81C)=1;assert(randomizer_ivan_visit((u32)bytes)==1&&query_calls==1&&event_calls==0&&bytes[0x834]==0xA0);
 reset(2);angle=100;clear_at_query=1;assert(randomizer_ivan_visit((u32)bytes)==1&&query_calls==1&&event_calls==0&&bytes[0x834]==0x80);
}
'''
  # Avoid confusing the byte-array alias with the scalar actor argument in stubs.
  source=source.replace('actor[0x834]=0x80','bytes[0x834]=0x80').replace('static u8* actor=bytes;','')
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);(root/'npc_replacement_cutscene_gates.h').write_text(header);(root/'test.c').write_text(source)
   subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',str(root/'test.c'),'-o',str(root/'test')],check=True);subprocess.run([str(root/'test')],check=True)
if __name__=='__main__':unittest.main()
