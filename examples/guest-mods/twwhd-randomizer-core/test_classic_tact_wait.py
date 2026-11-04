"""Staged classic tact consumer: timers, falling input and beat/render lifetimes."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

class ClassicTact(unittest.TestCase):
    def test_binary_policy_contract(self):
        header=Path(__file__).with_name('classic_tact_wait.h')
        for value in (None,0,1,2):
            prefix='' if value is None else f'#define WWHD_RANDOMIZER_CLASSIC_MODE {value}\n'
            result=subprocess.run([os.environ.get('CC','cc'),'-E','-x','c','-'],input=prefix+f'#include "{header}"\n',text=True,capture_output=True)
            self.assertEqual(result.returncode==0,value in (0,1))
            if value==0:self.assertNotIn('randomizer_classic_tact_wait',result.stdout)

    def test_full_consumer(self):
        header=Path(__file__).with_name('classic_tact_wait.h')
        imports=re.findall(r'WWHD_GAME_FUNC\([^,]+,([^,]+),(\w+),\((.*?)\)\);',header.read_text())
        overrides={
            'ctw_play':'return play;',
            'ctw_cancel_trigger':'if(mutate_cancel)CTW_S(self,0x6918)=0;return cancel_trigger;',
            'ctw_direction':'return hand?right:left;',
            'ctw_cursor':'return cursor;',
            'ctw_right_animation':'assert(actor==self);return 100+direction;',
            'ctw_left_animation':'assert(actor==self);return 200+direction;',
            'ctw_beat_frames':'assert(beat_calls<16);return beats[beat_calls++];',
            'ctw_beat_word':'return beat_word;',
            'ctw_judge':'return judge;',
            'ctw_is_tact':'return ownership;',
            'ctw_camera':'return camera;',
            'ctw_update_functor':'assert(object==self+0x4918);return;',
            'ctw_arm_sound':'if(mutate_arm)CTW_S(self,0x691C)=0;return;',
        }
        mocks=[]
        for ret,name,args in imports:
            # Use actual declared argument spelling, including actor-vs-self.
            argnames=[a.strip().split()[-1].lstrip('*') for a in args.split(',') if a.strip()!='void']
            body=overrides.get(name,'return;' if ret=='void' else 'return 1;')
            body=body.replace('assert(actor==self);','assert(self==link);')
            mocks.append(f'{ret} {name}({args}){{'+''.join('(void)'+a+';' for a in argnames)+f'record(C_{name});'+body+'}')
        enum='enum{'+','.join('C_'+name for _,name,_ in imports)+',C_count};'
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
typedef uint32_t u32;typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;typedef int32_t s32;typedef float f32;typedef double f64;
#define WWHD_RANDOMIZER_CLASSIC_MODE 1
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static u8* guest_memory;
#define CTW_B(a,o) (*(volatile u8*)(guest_memory+(u32)(a)+(o)-0x10000000))
#define CTW_H(a,o) (*(volatile u16*)(guest_memory+(u32)(a)+(o)-0x10000000))
#define CTW_S(a,o) (*(volatile s16*)(guest_memory+(u32)(a)+(o)-0x10000000))
#define CTW_W(a,o) (*(volatile unsigned int*)(guest_memory+(u32)(a)+(o)-0x10000000))
#define CTW_I(a,o) (*(volatile s32*)(guest_memory+(u32)(a)+(o)-0x10000000))
#define CTW_F(a,o) (*(volatile f32*)(guest_memory+(u32)(a)+(o)-0x10000000))
#define CTWPTR(a,o) CTW_W(a,o)
#define WWHD_GAME_DATA(a,t) (*(volatile t*)(guest_memory+(a)-0x10000000))
#define WWHD_ADDR_save_info_pointer 0x101F84DC
#define CTW_COSINE_TABLE 0x104A44FC
#define CTW_FRAME_CALL(fn,arg,frame,p2,p3) frame_callback(fn,arg,frame,p2,p3)
static f32 frame_callback(u32,u32,f32,f32,f32);
ENUM
static unsigned counts[C_count],trace[1024],trace_count,beat_calls,virtual_calls;
static s32 cancel_trigger,right,left,cursor,beat_word,judge,ownership;
static unsigned mutate_cancel,mutate_arm,mutate_frame;static f64 beats[16];
static const u32 self=0x11000000,link=0x11000000,play=0x11010000,hd_system=0x11020000,tact=0x11021000,pad=0x11022000,save=0x11030000,camera=0x11040000,anim=0x11041000,functor=0x11042000,under=0x11043000,upper=0x11044000,ratios=0x11045000;
static void record(unsigned n){assert(n<C_count&&trace_count<1024);++counts[n];trace[trace_count++]=n;}
#include "classic_tact_wait.h"
MOCKS
static f32 frame_callback(u32 fn,u32 arg,f32 value,f32 a,f32 b){assert(fn==0x2468&&arg==0x1357&&a==2&&b==3);assert(CTW_F(anim,0)==value&&CTW_F(self,0x48FC)==value);if(mutate_frame)CTWPTR(self,0x4918)=0x11046000;++virtual_calls;return value+a+b;}
static void reset(void){
 memset(guest_memory+0x1000000,0,0x50000);memset(counts,0,sizeof counts);trace_count=beat_calls=virtual_calls=0;cancel_trigger=right=left=cursor=judge=ownership=mutate_cancel=mutate_arm=mutate_frame=0;beat_word=3;
 for(unsigned i=0;i<16;i++)beats[i]=100;
 WWHD_GAME_DATA(0x101F8344,u32)=hd_system;WWHD_GAME_DATA(0x101F5088,u32)=pad;WWHD_GAME_DATA(0x101F84DC,u32)=save;WWHD_GAME_DATA(0x101FF560,u32)=0;
 CTWPTR(hd_system,0x1DC)=tact;CTW_B(tact,0x16B)=0;CTW_I(self,0x69C0)=-5;CTW_S(self,0x6918)=-1;CTW_S(self,0x6916)=-1;CTW_F(self,0x6A04)=10;CTW_F(self,0x69F8)=5;
 CTWPTR(self,0x4908)=anim;CTWPTR(self,0x4918)=functor;CTWPTR(functor,0x10)=0x2468;CTWPTR(functor,0x14)=0x1357;CTW_F(functor,4)=2;CTW_F(functor,8)=3;CTWPTR(self,0x57FC)=under;CTWPTR(self,0x582C)=upper;CTW_S(self,0x58A2)=60;CTW_S(self,0x58D2)=80;
}
int main(void){
 guest_memory=calloc(1,0x02000000);assert(guest_memory);
 // Target: all negative countdowns and both ground states preserve wait absent real cancel.
 for(unsigned raw=0;raw<65536;raw++)for(unsigned grounded=0;grounded<2;grounded++){
  const s16 p=(s16)raw;if(p>=-1)continue;
  reset();CTW_S(self,0x6918)=p;CTW_W(self,0x834)=grounded?0x20:0;
  assert(randomizer_classic_tact_wait(self)==1&&counts[C_ctw_cancel_trigger]==1);
  assert(!counts[C_ctw_cut]&&!counts[C_ctw_reset]&&!counts[C_ctw_reset_upper]);
  assert(CTW_S(self,0x6918)==p+1&&virtual_calls==1);
 }
 // Real cancel still resets, including exact camera/cut callback ordering.
 for(s32 mode=-5;mode<9;mode++)for(unsigned trigger=0;trigger<2;trigger++){
  reset();CTW_I(self,0x69C0)=mode;cancel_trigger=trigger;CTW_B(play,0x5BB6)=7;
  assert(randomizer_classic_tact_wait(self)==1);
  if(mode==-4){assert(counts[C_ctw_cut]==1&&!counts[C_ctw_cancel_trigger]);continue;}
  if(trigger){assert(counts[C_ctw_reset]==1&&counts[C_ctw_reset_upper]==2);assert(counts[C_ctw_camera]==(mode==-1));assert(counts[C_ctw_cut]==(mode!=-1));}
  else assert(!counts[C_ctw_reset]);
 }
 // Positive countdown completion chooses cut/metronome or play-init exactly.
 for(s32 mode=-8;mode<10;mode++)for(s16 timer=1;timer<4;timer++){
  reset();CTW_I(self,0x69C0)=mode;CTW_S(self,0x6918)=timer;CTW_I(self,0x69C4)=7;
  assert(randomizer_classic_tact_wait(self)==1&&!counts[C_ctw_cancel_trigger]);
  if(mode==-4){assert(counts[C_ctw_cut]==1);continue;}
  assert(CTW_S(self,0x6918)==timer-1);
  assert(counts[C_ctw_cut]==(timer==1&&(mode==-5||mode==6||mode==7)));
  assert(counts[C_ctw_play_init]==(timer==1&&(mode==-1||mode==-3||(mode>=0&&mode!=6&&mode!=7))));
 }
 // A callback cannot substitute a live countdown for the captured cancel operand.
 reset();CTW_S(self,0x6918)=-3;mutate_cancel=1;
 assert(randomizer_classic_tact_wait(self)==1&&!counts[C_ctw_cut]&&!counts[C_ctw_reset_upper]&&CTW_S(self,0x6918)==0);
 // First left input compares two unrounded doubles, even though the field stores float.
 reset();CTW_B(tact,0x16B)=1;CTW_I(self,0x69C0)=-2;left=1;beats[0]=1.00000003;beats[1]=1.00000002;beats[2]=100;
 assert(randomizer_classic_tact_wait(self)==1&&counts[C_ctw_metronome]==1&&beat_calls==5);
 assert(CTW_F(self,0x69F8)==(f32)(1.0-100.0)&&CTW_W(self,0x6A7C)==2&&CTW_S(self,0x6916)==2);
 // Right arm beat changes retain ratio tables and old-frame/reset ordering.
 reset();CTW_B(tact,0x16B)=1;CTW_I(self,0x69C0)=-2;right=2;CTW_I(self,0x5830)=3;CTWPTR(self,0x5834)=ratios;CTW_F(ratios,0)=CTW_F(ratios,4)=CTW_F(ratios,8)=4;
 assert(randomizer_classic_tact_wait(self)==1&&counts[C_ctw_set_beat]==1&&counts[C_ctw_old_frame]==1&&CTW_S(self,0x691A)==2&&CTW_S(self,0x6916)==-1);
 assert(CTW_F(ratios,0)==0&&CTW_F(ratios,4)==0&&CTW_F(ratios,8)==0);
 // The ratio loop reloads its live bound after each store, including aliases.
 reset();CTW_B(tact,0x16B)=1;CTW_I(self,0x69C0)=-2;right=2;CTW_I(self,0x5830)=3;CTWPTR(self,0x5834)=self+0x5830;
 assert(randomizer_classic_tact_wait(self)==1&&CTW_I(self,0x5830)==0);
 // Arm callbacks publish live direction before deciding whether a beat starts.
 reset();CTW_B(tact,0x16B)=1;CTW_I(self,0x69C0)=-2;left=1;mutate_arm=1;
 assert(randomizer_classic_tact_wait(self)==1&&counts[C_ctw_arm_sound]==1&&counts[C_ctw_beat_word]==0&&CTW_S(self,0x691C)==0);
 // Functor callback may replace the actor's pointer; returned value stays on
 // the captured original functor, then its updater sees the live owner field.
 reset();mutate_frame=1;
 assert(randomizer_classic_tact_wait(self)==1&&CTWPTR(self,0x4918)==0x11046000&&CTW_F(functor,0)==5.0f&&counts[C_ctw_update_functor]==1);
 // Beat judge, song ownership and miss counters preserve the surrounding state.
 for(s32 mode=-5;mode<9;mode++)for(s32 result=-1;result<9;result++)for(s32 own=0;own<2;own++){
  if(mode==-4)continue;reset();CTW_I(self,0x69C0)=mode;CTW_S(self,0x6916)=0;judge=result;ownership=own;
  assert(randomizer_classic_tact_wait(self)==1&&counts[C_ctw_judge]==1);
  const unsigned match=mode>=0?mode==result:(mode==-1||mode==-3)&&result>=0&&result!=6&&result!=7&&own;
  assert(CTW_I(self,0x69C4)==(match?result:-1));assert(CTW_S(self,0x6918)==(match?30:-1));
  assert(!!(CTW_W(self,0x3C0)&0x1000000));
 }
 // Phase tail preserves unsigned modulo-zero behavior and fdivs rounding.
 for(unsigned period=0;period<33;period++)for(unsigned frame=0;frame<41;frame++){
  reset();beats[0]=100;beats[1]=beats[2]=(double)period/2;WWHD_GAME_DATA(0x101FF560,u32)=frame;
  assert(randomizer_classic_tact_wait(self)==1);
  const float phase=(float)(period?frame%period:frame);const float ratio=phase/(float)period;
  if(period){assert(CTW_F(self,0x589C)==ratio*60&&CTW_F(self,0x58CC)==ratio*80);assert(CTW_W(under,0)==CTW_W(self,0x589C)&&CTW_W(upper,0)==CTW_W(self,0x58CC));}
  else assert(!isfinite(CTW_F(self,0x589C)));
 }
 return 0;
}
'''
        source=source.replace('ENUM',enum).replace('MOCKS','\n'.join(mocks))
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-fno-strict-aliasing','-Wno-int-to-pointer-cast','-Wno-pointer-to-int-cast','-I',str(header.parent),str(root/'test.c'),'-o',str(root/'test')],check=True)
            subprocess.run([str(root/'test')],check=True)

if __name__=='__main__':unittest.main()
