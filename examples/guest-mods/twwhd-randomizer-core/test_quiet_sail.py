"""Source sailing paths, callback ordering and local stock jump restoration."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

class QuietSail(unittest.TestCase):
    def test_consumer(self):
        header=Path(__file__).with_name('quiet_sail.h')
        source=r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;typedef int32_t s32;typedef float f32;
#define WWHD_RANDOMIZER_QUIET_SWIFT_SAIL 1
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static _Alignas(16) u8 actor[0x1000],play[0x6000],body[0x200],grid[0x2C00];
static f32 trig[16384],vector[3],power;
#define WWHD_GAME_DATA(a,t) (*(t*)trig)
#define QS_PTR(a,o) (*(volatile u32*)((a)+(o)))
#define QS_FNMSUB(a,b,c) (-fmaf(a,b,-(c)))
#include "quiet_sail.h"
static u32 self;static unsigned seen[64],count,obstacle,frame,trigger,early,normal_calls,scale_calls,wind_calls;
static unsigned mutate_active,mutate_swift;static f32 scale,target,decremented;static s16 heading;
static void record(unsigned n){assert(count<64);seen[count++]=n;}
s32 qs_next(u32 a,s32 m){assert(a==self&&m==1);record(1);return early;}
void qs_angle(u32 p,s16 t,s16 r,s16 mx,s16 mn){assert(p==self+0x64E&&r==4&&mx==700&&mn==256);record(2);QS_S(p,0)=t;}
void qs_move_angle(u32 a,s16 v){assert(a==self&&v==QS_S(self,0x64E));record(3);}
void qs_sail_angle(u32 a){assert(a==self);record(4);}
u32 qs_play(void){record(5);return (u32)play;}
void qs_wind_set(s16 x,s16 y){assert(x==0);record(6);++wind_calls;heading=y;}
u32 qs_wind_vector(void){record(7);return (u32)vector;}
u32 qs_wind_power(void){record(8);return (u32)&power;}
s16 qs_atan(f32 x,f32 z){assert(x==vector[0]&&z==vector[2]);record(9);return heading;}
s32 qs_obstacle(u32 a){assert(a==self);record(10);return obstacle;}
s32 qs_frame(u32 p,f32 f){assert(p==(u32)body+0x98&&f==9);record(11);return frame;}
void qs_first_decrement(u32 a,f32 f){assert(a==self);record(12);decremented=f;}
void qs_decrement(u32 a,f32 f){assert(a==self);record(13);decremented=f;}
void qs_accelerate(u32 a,f32 f,f32 r,f32 mx,f32 mn){assert(a==self+0x370&&r==0.1f&&mx==2&&mn==0.1f);record(14);target=f;}
s32 qs_jump_trigger(u32 pad){assert(!pad);record(15);return trigger;}
void qs_control_angle(u32 a,s16 t){assert(a==self&&!t);record(16);}
void qs_part(u32 a,s32 p){assert(a==self&&p==1);record(17);}
void wwhd_ship_normal_sail_wind(u32 a){assert(a==self);record(18);++normal_calls;if(mutate_active)QS_B(self,0x63D)=1;if(mutate_swift)QS_B((u32)play,0x5D2C)=1;}
f32 wwhd_ship_sail_target(f32 stock){record(19);++scale_calls;return stock*scale*(QS_B((u32)play,0x5D2C)?2:1);}
static unsigned occurrences(unsigned n){unsigned total=0;for(unsigned i=0;i<count;i++)total+=seen[i]==n;return total;}
static void reset(void){
 memset(actor,0,sizeof actor);memset(play,0,sizeof play);memset(body,0,sizeof body);memset(grid,0,sizeof grid);memset(trig,0,sizeof trig);
 self=(u32)actor;count=obstacle=frame=trigger=early=normal_calls=scale_calls=wind_calls=mutate_active=mutate_swift=0;scale=1;target=decremented=-999;heading=0;
 QS_PTR(self,0x3B4)=(u32)body;QS_PTR(self,0x6FC)=(u32)grid;QS_H(self,0x67A)=11;QS_B(self,0x637)=1;
 QS_F((u32)body,0x9C)=9;QS_F((u32)body,0x98)=1;QS_F(self,0x6C4)=1000;QS_F(self,0x6E0)=31;QS_PTR((u32)play,0x5B34)=QS_PTR((u32)play,0x5B2C)=self;
 vector[0]=0;vector[2]=1;power=0.5f;trig[1]=1;
}
int main(void){
 reset();early=1;assert(randomizer_quiet_sail(self)==1&&count==1);
 // Normal and Swift transition state machines, obstacle and frame paths.
 for(unsigned swift=0;swift<2;swift++)for(unsigned active=0;active<3;active++)for(unsigned block=0;block<2;block++)for(unsigned at_frame=0;at_frame<2;at_frame++){
  reset();QS_B((u32)play,0x5D2C)=swift;QS_B(self,0x63D)=active;obstacle=block;frame=at_frame;
  assert(randomizer_quiet_sail(self)==1);
  assert(normal_calls==!swift&&wind_calls==swift&&scale_calls==1);
  assert(QS_B(self,0x63D)==(swift?(active==0?1:active):(active==1?0:active)));
  const unsigned transitioned=swift?active==0:active==1;
  assert(QS_H((u32)grid,0x2B8C)==(transitioned?15:0));
  assert(!!(QS_W(self,0x644)&0x80000000u)==transitioned);
  assert(occurrences(10)==1&&occurrences(15)==1);
 }
 // Wind direction boundaries include signed wrap and both piecewise curves.
 for(unsigned raw=0;raw<65536;raw++){
  reset();heading=(s16)raw;
  const int a=heading<0?-(int)heading:heading;
  f32 expected=a<0x4000?-fmaf((f32)a,1.0f/65536,-1.0f):a<=0x6000?-fmaf((f32)(a-0x4000),5.493164e-05f,-0.75f):-fmaf((f32)(a-0x6000),0.00032967035f,-0.3f);
  if(!(expected>=0))expected=0;
  expected=expected*55;
  assert(randomizer_quiet_sail(self)==1);
  if(expected>0)assert(qs_bits(target)==qs_bits(expected));else assert(occurrences(13)==1);
 }
 // A normal-wind callback can publish active/swift flags before subsequent loads.
 reset();mutate_active=mutate_swift=1;assert(randomizer_quiet_sail(self)==1);assert(normal_calls==1&&QS_B(self,0x63D)==0&&target==110&&QS_H((u32)grid,0x2B8C)==15);
 // Scaling accelerates towards the boosted target, but jump threshold/height are stock.
 for(unsigned strength=0;strength<101;strength++)for(unsigned factor=1;factor<5;factor++)for(unsigned block=0;block<2;block++){
  reset();power=(f32)strength/200;scale=(f32)factor;trigger=1;obstacle=block;
  const f32 stock=55.0f*(power+power);const f32 jump=block?30.0f:stock;
  assert(randomizer_quiet_sail(self)==1);
  if(block||stock>0)assert(target==(block?30.0f:stock*scale));else assert(target==-999&&occurrences(13)==1);
  assert(occurrences(15)==(jump>=20));
  assert(!!(QS_W(self,0x644)&0x40)==(jump>=20));
  if(jump>=20){const f32 vy=jump>40?40:jump;assert(QS_F(self,0x340)==vy&&QS_F(self,0x6C8)==(vy-20)*0.008f);}
 }
 // A replaced target equal to the scaled token restores by bit equality, as ED24 did.
 reset();power=0.125f;scale=30.0f/(55.0f*0.25f);obstacle=1;trigger=1;
 assert(randomizer_quiet_sail(self)==1&&!occurrences(15));
 // Flying skips both wind paths and scaling, retaining control spring and footer.
 reset();QS_W(self,0x644)=1;QS_B(self,0x637)=2;QS_F((u32)body,0x98)=0;
 assert(randomizer_quiet_sail(self)==1&&!scale_calls&&!normal_calls&&!wind_calls&&occurrences(16)==1&&occurrences(17)==1&&QS_H(self,0x656)==0);
 // Event mode suppresses the entire jump section without losing acceleration.
 reset();QS_B((u32)play,0x5292)=1;assert(randomizer_quiet_sail(self)==1&&occurrences(14)==1&&!occurrences(15));
 // Early frame callback and first-decrement paths still retain live actor reads.
 reset();QS_F(self,0x6C4)=-1;assert(randomizer_quiet_sail(self)==1&&occurrences(12)==1&&decremented==-1&&!occurrences(14));
 return 0;
}
'''
        # SDK declarations normally come from wwhd_guest.h, before this header.
        source=source.replace('#include "quiet_sail.h"','void wwhd_ship_normal_sail_wind(u32);f32 wwhd_ship_sail_target(f32);\n#include "quiet_sail.h"')
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(source)
            subprocess.run([os.environ.get('CC','cc'),'-std=c11','-O2','-Wno-int-to-pointer-cast','-Wno-pointer-to-int-cast','-I',str(header.parent),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__':unittest.main()
