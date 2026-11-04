"""Full scene-selection contracts, Grandma independence and callback reload/order."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


class GrandmaMusic(unittest.TestCase):
    def test_complete_scene_consumer(self):
        header = Path(__file__).with_name('grandma_music.h')
        source = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef int32_t s32;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 memory[0x80000],object_before[0x300],save_before[0x4000];
static const u32 self=0x10000u,save=0x30000u,save2=0x32000u;
static u32 save_pointer=0x30000;
#define WWHD_ADDR_save_info_pointer 1
#define WWHD_GAME_DATA(a,t) (*(t*)&save_pointer)
#define R_GRANDMA(t,a) (*(volatile t*)(memory+(u32)(a)-0x10000u))
#define GRANDMA_SCENE_TABLE 0x20000u
#define GRANDMA_ISLAND_TABLE 0x21000u
#include "grandma_music.h"
static const u32 selectors[]={0x3510,1,0x101,0xE20,0x2E01,0x280,0x801,0x2D04,0x2A20,0x2110,0xA02};
static s32 events[11];static unsigned trace[32],trace_size,stage_calls,expand_calls,scene_calls,checks;
static u32 final_bgm,final_w1,final_w2,expanded_id,stage_offset;static s32 final_keep;
static u8 stage5,stage3,seen274,callback_mutation,allocation_change;static s32 outset_chest;
static void record(unsigned value){assert(trace_size<32);trace[trace_size++]=value;}
s32 grandma_check_event(u32 actor,u32 selector){
 assert(actor==self);record(selector);assert(R_GRANDMA(u8,self+0xCD)==0&&R_GRANDMA(u8,self+0xDD)==0);
 for(unsigned i=0;i<11;i++)if(selectors[i]==selector)return events[i];assert(0);return 0;
}
u32 grandma_expand_bgm(u32 actor,u32 bgm){
 assert(actor==self);record(0xE000);expanded_id=bgm;expand_calls++;
 if(callback_mutation){R_GRANDMA(u8,GRANDMA_SCENE_TABLE+2*4+2)=91;R_GRANDMA(u8,GRANDMA_SCENE_TABLE+2*4+3)=92;R_GRANDMA(u8,self+0x274)=0xA5;}
 return 0xA0000000u|bgm;
}
s32 grandma_stage_item(u32 pointer,u32 flag){
 assert(pointer==save_pointer+stage_offset);assert(flag==5||flag==3);record(0xD000+flag);stage_calls++;
 if(allocation_change&&flag==5){save_pointer=save2;R_GRANDMA(u8,self+0x31)=0;}
 return flag==5?stage5:stage3;
}
s32 grandma_stage_chest(u32 pointer,u32 chest){assert(pointer==save_pointer+0x3A0+13*0x24&&chest==5);record(0xD105);return outset_chest;}
void grandma_change_scene(u32 actor,u32 bgm,u32 wave1,u32 wave2,s32 keep){
 assert(actor==self);record(0xC000);scene_calls++;final_bgm=bgm;final_w1=wave1;final_w2=wave2;final_keep=keep;seen274=R_GRANDMA(u8,self+0x274);
 assert(R_GRANDMA(s32,self+0x290)==999&&R_GRANDMA(u8,self+0x29C)==0&&R_GRANDMA(u8,self+0x29D)==0x71);
 if(callback_mutation){R_GRANDMA(u8,self+0x29C)=0xEE;R_GRANDMA(u8,self+0x2A8)=63;R_GRANDMA(u8,self+0xCD)=17;}
}
static void reset(void){
 memset(memory,0x73,0x300);memset(events,0,sizeof events);trace_size=stage_calls=expand_calls=scene_calls=0;
 callback_mutation=allocation_change=stage5=stage3=0;outset_chest=0;save_pointer=save;
 R_GRANDMA(u8,self+0x29C)=0;R_GRANDMA(u8,self+0x29D)=0x71;R_GRANDMA(s32,self+0x290)=999;
 R_GRANDMA(u8,self+0x31)=0;R_GRANDMA(u8,self+0x274)=76;
 memcpy(object_before,memory,0x300);memcpy(save_before,memory+save-0x10000,sizeof save_before);
}
static int written(unsigned i){return i==0xCD||i==0xDD||i==0x2A8||i==0x2A9||i==0x29C||i==0x29D||(i>=0x290&&i<0x294)||i==0x274;}
static void isolated(void){
 assert(!memcmp(save_before,memory+save-0x10000,sizeof save_before));
 for(unsigned i=0;i<0x300;i++)if(!written(i)&&!(allocation_change&&i==0x31))assert(memory[i]==object_before[i]);checks++;
}
static void finish(s32 sc,s32 layer,s32 keep,u32 bgm,u32 wave1,u32 wave2){
 assert(scene_calls==1&&final_bgm==bgm&&final_w1==wave1&&final_w2==wave2&&final_keep==keep);
 assert(R_GRANDMA(s32,self+0x290)==sc&&R_GRANDMA(u8,self+0x29D)==0&&R_GRANDMA(u8,self+0x29C)==1&&R_GRANDMA(u8,self+0x2A9)==(u8)layer);
 isolated();
}
int main(void){
 memset(memory+save-0x10000,0x6D,sizeof save_before);
 for(s32 i=-1;i<256;i++){R_GRANDMA(u16,GRANDMA_SCENE_TABLE+(u32)i*4u)=(u16)(0x100+i);R_GRANDMA(u8,GRANDMA_SCENE_TABLE+(u32)i*4u+2)=(u8)(i^0x5A);R_GRANDMA(u8,GRANDMA_SCENE_TABLE+(u32)i*4u+3)=(u8)(i^0xC3);R_GRANDMA(u16,GRANDMA_ISLAND_TABLE+(u32)i*4u)=(u16)(0x300+i);}
 const s32 healed_values[]={0,1,2,-1};const s32 keeps[]={0,1,2,-1};
 // Every shield byte and normal/special layer: only the real healing query matters.
 for(unsigned shield=0;shield<256;shield++)for(s32 layer=-1;layer<=16;layer++)for(unsigned h=0;h<4;h++)for(unsigned k=0;k<4;k++){
  reset();R_GRANDMA(u8,self+0x53)=(u8)shield;object_before[0x53]=(u8)shield;events[8]=healed_values[h];
  grandma_set_scene(self,0xB,44,keeps[k],layer);
  assert(trace[0]==0x2A20&&trace_size==(h?3u:2u));assert(expand_calls==(h?1u:0u));
  assert(R_GRANDMA(u8,self+0x53)==shield&&R_GRANDMA(u8,self+0x2A8)==0);
  finish(0xB,layer,keeps[k],h?0xA000010Bu:0x80000044u,(0xB^0x5A),(0xB^0xC3));
 }
 // Full unrelated non-sea scene domain, dungeon gates and layer suppression.
 for(s32 sc=-1;sc<0x79;sc++)if(sc!=0xB&&sc!=0x12)for(s32 layer=-1;layer<=16;layer++)for(unsigned enabled=0;enabled<2;enabled++)for(unsigned bits=0;bits<4;bits++)for(unsigned e=0;e<2;e++){
  reset();for(unsigned i=0;i<11;i++)events[i]=(s32)e;R_GRANDMA(u8,self+0x31)=(u8)enabled;object_before[0x31]=(u8)enabled;stage5=bits&1;stage3=bits&2;
  unsigned alternate=0,dungeon=(sc==2||sc==7||sc==0x25),temple=(sc==0x18||sc==0x2D);stage_offset=sc==2?0x40C:sc==7?0x430:sc==0x25?0x454:sc==0x18?0x49C:0x478;
  if(dungeon&&enabled&&stage3)alternate=sc==2?1:sc==7?3:10;
  u32 expected=0xA0000000u|(alternate?0x100u+alternate:(u16)(0x100+sc));int muted=0;
  const int lower_layer=layer>=8&&layer<=11;
  if(sc==0x13&&!e)muted=1;
  if(sc==0x35||sc==0x27||sc==0x29||sc==0x16||sc==0x2C)muted=lower_layer;
  if(temple&&enabled&&(lower_layer||stage3))muted=1;
  if(sc==0x59&&lower_layer)muted=1;
  if(sc==0x10&&!e)expected=0x80000016u;
  if((sc==0x36||(sc==0x59&&!lower_layer))&&!e)expected=0x80000034u;
  if(muted)expected=0;
  grandma_set_scene(self,sc,0,0,layer);
  finish(sc,layer,0,expected,(u8)(sc^0x5A),(u8)(sc^0xC3));
  assert(R_GRANDMA(u8,self+0xDD)==(u8)((dungeon||temple)&&enabled&&stage5));
  assert(R_GRANDMA(u8,self+0x274)==(sc==2&&enabled&&stage3?1:76));
  if(dungeon)assert(stage_calls==(enabled?2u:0u));
  if(temple)assert(stage_calls==(enabled?(lower_layer?1u:2u):0u));
 }
 // Pirate music's short-circuit query order uses two independent flags.
 for(unsigned a=0;a<3;a++)for(unsigned b=0;b<3;b++){
  reset();events[9]=a;events[10]=b;grandma_set_scene(self,0x20,0,0,0);
  finish(0x20,0,0,a==0&&b==1?0x8000003Du:0xA0000120u,0x20^0x5A,0x20^0xC3);
  assert(trace[0]==0x2110);assert((trace[1]==0xA02)==(a==0));
 }
 // Sea table, windfall/roost/flight layers, room narrowing and keep behavior.
 for(s32 room=-1;room<=256;room++)for(s32 layer=-1;layer<=16;layer++)for(unsigned k=0;k<4;k++){
  reset();const u32 island=room>0?((u32)room&255u):0;u32 expected=0xA0000300u|island;
  const int flight=island==14&&(layer==2||layer==3)&&keeps[k]!=1;
  if(keeps[k]==1||island==11||(island==13&&(layer==8||layer==10||layer==11))||flight||(island==44&&layer==10))expected=0;

  grandma_set_scene(self,0x12,room,keeps[k],layer);finish(0x12,layer,keeps[k],expected,0,0);
  assert(R_GRANDMA(u8,self+0x2A8)==island&&R_GRANDMA(u8,self+0xCD)==flight);
  if(keeps[k]==1)assert(trace_size==1&&trace[0]==0xC000);
 }
 // Windfall permits its table track for every nonzero native flag answer.
 for(s32 event=1;event<=2;event++){reset();events[4]=event;grandma_set_scene(self,0x12,11,0,0);finish(0x12,0,0,0xA000030Bu,0,0);assert(trace[0]==0x2E01);}
 // Persistent Outset chest replaces kidnapping, while intro never runs.
 for(s32 intro=-1;intro<=2;intro++)for(s32 first=-1;first<=2;first++)for(s32 second=-1;second<=2;second++)for(s32 chest=-1;chest<=2;chest++)for(s32 kidnapped=-1;kidnapped<=2;kidnapped++){
  reset();events[0]=intro;events[1]=first;events[2]=second;events[3]=kidnapped;outset_chest=chest;
  const int special=first==1&&second==0;
  const int a=second==1;
  const u32 expected=special?0x8000000Eu:a&&chest==0?0:chest==1?0x80000055u:0xA000032Cu;
  grandma_set_scene(self,0x12,44,0,0);finish(0x12,0,0,expected,0,0);
  assert(trace[0]==1&&trace[1]==0x101);
  assert((trace[2]==0xD105)==!special);
  for(unsigned n=0;n<trace_size;n++)assert(trace[n]!=0x3510&&trace[n]!=0xE20);
 }
 // Blocked/out-of-range entries neither clear flags nor call any helper.
 for(unsigned i=0;i<2;i++){reset();if(i)R_GRANDMA(u8,self+0x29C)=255;memcpy(object_before,memory,0x300);grandma_set_scene(i?self:0,i?0xB:INT_MAX,0,0,0);assert(!trace_size&&!memcmp(object_before,memory,0x300));}
 // Callback changes must be observed at the original reload/store seams.
 reset();R_GRANDMA(u8,self+0x31)=1;object_before[0x31]=1;stage_offset=0x40C;stage5=stage3=1;callback_mutation=allocation_change=1;
 grandma_set_scene(self,2,0,0,0);
 assert(stage_calls==2&&expanded_id==0x101&&seen274==1&&final_w1==91&&final_w2==92);
 assert(R_GRANDMA(u8,self+0x2A8)==63&&R_GRANDMA(u8,self+0xCD)==17&&save_pointer==save2);
 finish(2,0,0,0xA0000101u,91,92);
 {const unsigned expected[]={0xD005,0xD003,0xE000,0xC000};assert(trace_size==4&&!memcmp(trace,expected,sizeof expected));}
 printf("grandma scene consumer: %u cases passed\n",checks);assert(checks>100000);return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            c, exe = Path(directory) / 'test.c', Path(directory) / 'test'
            c.write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(header.parent), str(c), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
