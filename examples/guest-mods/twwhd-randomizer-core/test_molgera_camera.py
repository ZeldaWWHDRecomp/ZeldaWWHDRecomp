"""Complete camera state machine and callback-visible safe-position seam."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

class MolgeraCamera(unittest.TestCase):
    def test_optimized_typed_local_copy(self):
        source=(Path(__file__).parent/'molgera_camera.h').read_text()
        start=source.index('static void rmc_copy_vector(')
        end=source.index('}',start)+1
        helper=source[start:end]
        code=r'''#include <stdint.h>
#include <assert.h>
#include <string.h>
typedef uintptr_t u32;typedef unsigned char u8;
#define T_u32 unsigned int
#define T_u8 u8
#define R_MC(t,a) (*(volatile T_##t*)(a))
'''+helper+r'''
__attribute__((noinline)) static float tracked_y(u32 source){float local[3];rmc_copy_vector((u32)local,source);local[1]+=100.0f;return local[1];}
int main(void){float input[3]={-12.5f,203.25f,47};assert(tracked_y((u32)input)==303.25f);
 unsigned int bits[3]={0x80000000u,0x7FC12345u,0x3F800000u},output[3];float local[3];
 rmc_copy_vector((u32)local,(u32)bits);memcpy(output,local,sizeof output);assert(!memcmp(bits,output,sizeof bits));
 rmc_copy_vector((u32)local,(u32)local);memcpy(output,local,sizeof output);assert(!memcmp(bits,output,sizeof bits));return 0;}
'''
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'copy.c';exe=Path(temporary)/'copy';path.write_text(code)
            subprocess.run([os.environ.get('CC','cc'),'-O2','-fstrict-aliasing','-Wall','-Wextra','-Werror',str(path),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_consumer(self):
        root=Path(__file__).parent
        prefix='''#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n#include <math.h>\n#include <stdio.h>\n#include <stdarg.h>\ntypedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef int8_t s8;typedef uint8_t u8;typedef float f32;typedef double f64;\n#define WWHD_GAME_FUNC(a,r,n,p) r n p\n#define WWHD_REPLACE(a,r,n,p) r n p\n'''
        weak=[]
        for d in re.findall(r'WWHD_GAME_FUNC\([^;]+;', (root/'molgera_camera.h').read_text()):
            m=re.fullmatch(r'WWHD_GAME_FUNC\([^,]+,([^,]+),([^,]+),\s*\((.*)\)\);',d,re.S)
            self.assertIsNotNone(m,d)
            r,n,a=(s.strip() for s in m.groups())
            weak.append(f'__attribute__((weak)) {r} {n}({a}){{assert(0&&"unexpected {n}");'+('return 0;' if r!='void' else '')+'}')
        code=prefix+r'''
static unsigned char arena[0x40000],data[0x500000];
static const u32 actor=0x30000000,game=0x30010000,player=0x30020000,camera=0x30024000,item=0x30028000;
static void* mem(u32 a){if(a>=actor&&a<actor+sizeof arena)return arena+a-actor;if(a>=0x10000000&&a<0x10500000)return data+a-0x10000000;if(a>0xFFFFFFFFu)return(void*)a;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_MC(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
static void player_position(u32,u32,s32);
#define RMC_PLAYER_POSITION(p,v,a) player_position(p,v,a)
static u32 randomizer_boss_factory_active,randomizer_boss_factory_item;
static u8 init_reward_boss[6]={0,0,0,0,0,0x44};
#include "molgera_camera.h"
static unsigned contexts,calcs,positions,sets,resets,starts,stops,trims,orders,sqrts,grounds,destructs,randoms,reports,sounds,items,warps,dungeons;
static int entry_mode,mutate_sqrt,mutate_calc,mutate_report,mutate_random;
static f64 height,sqrt_result,random_result;
static f32 last_position[3],last_eye[3],last_center[3];static s32 last_bank;
u32 rmc_game(void){contexts++;if(entry_mode>=0&&contexts==3)W(s16,actor+0x3E96)=entry_mode;return game;}
void rmc_order(u32 a,s32 f,s32 h,s32 p){assert(a==actor&&f==2&&h==65535&&!p);assert(randomizer_boss_factory_active==1&&randomizer_boss_factory_item==0x44);orders++;W(u16,actor+0xFA)=0x40;}
void rmc_stop(u32 c){assert(c==camera+0x248);stops++;}
void rmc_start(u32 c){assert(c==camera+0x248);starts++;}
void rmc_trim(u32 c,s32 size){assert(c==camera+0x248&&(size==0||size==2));trims++;}
void rmc_calculate(u32 p,f32 target,f32 ratio,f32 max){assert(ratio>0&&max>=0);calcs++;W(f32,p)=target;if(mutate_calc&&p==actor+0x3EAC){W(f32,actor+0x1B88)=700;W(f32,actor+0x31C)=88;W(s8,actor+0x3E9C)=1;W(f32,actor+0x3EBC)=2;}}
void rmc_decelerate(u32 p,f32 ratio,f32 max){assert(p==actor+0x3EC4&&ratio==1&&max>=0);W(f32,p)=0;}
void rmc_angle_calculate(u32 p,s32 t,s32 r,s32 max){assert(p==actor+0x3E94&&t==1&&r==1&&max==1);W(s16,p)=1;}
void rmc_y_rotation(u32 m,s32 a){assert(m==0x1048D0CC);(void)a;}
void rmc_transform(u32 v,u32 result){rmc_copy_vector(result,v);}
void rmc_vector_add(u32 a,u32 b,u32 result){f32 v[3];for(unsigned i=0;i<3;i++)v[i]=W(f32,a+4*i)+W(f32,b+4*i);rmc_copy_vector(result,(u32)v);}
void rmc_stream_play(void){}
void rmc_quake_start(u32 v,s32 strength,s32 flags,u32 d){assert(v==game+0x599C&&strength==3&&flags==-33&&W(f32,d+4)==1);}
void rmc_quake_stop(u32 v,s32 n){assert(v==game+0x599C&&n==-1);}
s32 rmc_reverb(s32 room){assert(room==-3);return 7;}
void rmc_sound(u32 id,u32 p,u32 n,s32 r){assert((id==0x69E1||id==0x105E||id==0x1077||id==0x1076)&&!n&&r==7);(void)p;sounds++;}
void rmc_report(s32 x,s32 y,u32 text,...){assert(x==30);va_list args;va_start(args,text);if(y==360){assert(text==0x1000BE7C&&va_arg(args,f64)==65);}else{assert(y>=390&&text>=0x1000BE88);(void)va_arg(args,int);reports++;if(mutate_report&&y==430){W(s16,actor+0x3E98)=70;W(s16,actor+0x3E9A)=500;}}va_end(args);}
void rmc_bgm(u32 id){assert(id==0x8000004C||id==0x80000029);}
void rmc_dungeon_item(u32 memory,s32 bit){assert(memory==0x30030798&&(bit==3||bit==5));dungeons++;}
void rmc_ground_construct(u32 p){memset(mem(p),0xCC,0x54);}
void rmc_ground_destroy(u32 p,s32 flags){assert(!flags&&W(u32,p+0x20)==0x1000BDEC&&W(u32,p+0x4C)==0x1000BDCC);destructs++;}
f64 rmc_ground_cross(u32 bg,u32 p){assert(bg==game+0x12A0&&W(u32,p+0x50)==1&&W(u32,p+0x10)==0x1000BDDC&&W(u32,p+0x20)==0x1000BDEC);assert(W(f32,p+0x24)==W(f32,rmc_target())&&W(f32,p+0x28)==W(f32,rmc_target()+4)+1000&&W(f32,p+0x2C)==W(f32,rmc_target()+8));grounds++;return height;}
u32 rmc_item_create(u32 p,s32 n,s32 room,u32 angle,u32 scale,s32 param){assert(p==actor+0x1B68&&!n&&room==-3&&!angle&&param==1&&W(f32,scale)==1&&W(f32,scale+8)==1);assert(randomizer_boss_factory_active==1&&randomizer_boss_factory_item==0x44);items++;return 333;}
u32 rmc_actor_judge(u32 callback,u32 key){assert(callback==0x025E1234&&W(u32,key)==333);return item;}
void rmc_warp_create(u32 p,u32 angle,s32 room,s32 param){assert(W(f32,p)==0&&W(f32,p+4)==300&&W(f32,p+8)==0&&!angle&&room==-3&&!param);warps++;}
void rmc_reset(u32 c,u32 center,u32 eye){assert(c==camera+0x248);rmc_copy_vector((u32)last_center,center);rmc_copy_vector((u32)last_eye,eye);resets++;}
void rmc_set(u32 c,u32 center,u32 eye,s32 bank,f32 fov){assert(c==camera+0x248&&fov==W(f32,actor+0x3EC0));rmc_copy_vector((u32)last_center,center);rmc_copy_vector((u32)last_eye,eye);last_bank=bank;sets++;}
f64 rmc_random(f64 range){assert(range==20);randoms++;if(mutate_random){W(f32,actor+0x3EA0)=9000;W(f32,actor+0x3EC0)=75;W(f32,0x1047B610+10*0x90+8*4)=20;}return random_result;}
f64 rmc_sqrt(f64 square){assert(square>=0||isnan(square));sqrts++;assert(W(f32,rmc_target()+4)==W(f32,player+0x318));if(mutate_sqrt){W(f32,rmc_target()+4)=99;W(s16,actor+0x3E96)=0;}return sqrt_result>=0?sqrt_result:sqrt(square);}
static void player_position(u32 p,u32 v,s32 a){assert(p==player);(void)a;rmc_copy_vector((u32)last_position,v);positions++;}
static void reset(void){memset(arena,0,sizeof arena);memset(data,0,sizeof data);contexts=calcs=positions=sets=resets=starts=stops=trims=orders=sqrts=grounds=destructs=randoms=reports=sounds=items=warps=dungeons=0;randomizer_boss_factory_active=3;randomizer_boss_factory_item=0x99;entry_mode=-1;mutate_sqrt=mutate_calc=mutate_report=mutate_random=0;height=42;sqrt_result=-1;random_result=10;W(u32,game+0x5B2C)=player;W(u32,game+0x5AF8)=camera;W(u32,0x101F84DC)=0x30030000;W(u32,0x1018C7B0)=0x1048D0CC;W(u16,actor+0xF8)=2;W(s8,actor+0x326)=-3;W(f32,player+0x318)=10;W(f32,actor+0x314)=100;W(f32,actor+0x31C)=-777;W(f32,actor+0x3EC0)=55;W(u32,actor+0x1B64)=0xFFFFFFFF;for(unsigned i=0;i<8192;i++){W(f32,0x104A44F8+8*i)=.5f;W(f32,0x104A44FC+8*i)=.25f;}}
int main(void){unsigned cases=0;
 const s16 modes[]={0,1,2,3,50,51,52,53,60,100,101,102,104,105,106,150,-1,777};
 for(unsigned m=0;m<sizeof modes/sizeof*modes;m++)for(s16 count=0;count<700;count+=7){reset();W(s16,actor+0x3E96)=modes[m];W(s16,actor+0x3E98)=count;W(s16,actor+0x3E9A)=count;randomizer_molgera_camera(actor);assert(contexts>=3&&randomizer_boss_factory_active==3&&randomizer_boss_factory_item==0x99);cases++;}
 for(unsigned i=0;i<3;i++){reset();W(s16,actor+0x3E96)=i==0?1:i==1?50:100;W(u16,actor+0xF8)=0;randomizer_molgera_camera(actor);assert(randomizer_boss_factory_active==3&&randomizer_boss_factory_item==0x99);assert(orders==1&&W(u16,actor+0xFA)==0x42&&!sets&&!grounds&&!stops);cases++;}
 reset();W(s16,actor+0x3E96)=3;W(f32,player+0x314)=-12.5f;W(f32,player+0x318)=203.25f;W(f32,player+0x31C)=47;randomizer_molgera_camera(actor);assert(calcs==3&&sets==1&&isfinite(last_center[1])&&last_center[0]==-12.5f&&last_center[1]==303.25f&&last_center[2]==47);cases++;
 reset();entry_mode=100;randomizer_molgera_camera(actor);assert(sqrts==1&&grounds==1&&destructs==1&&last_position[0]==0&&last_position[1]==42&&last_position[2]==777&&W(s16,actor+0x3E96)==101);cases++;
 reset();W(s16,actor+0x3E96)=100;entry_mode=150;randomizer_molgera_camera(actor);assert(!sqrts&&!grounds&&resets==1&&starts==1&&W(s16,actor+0x3E96)==0);cases++;
 for(unsigned outside=0;outside<2;outside++){reset();W(s16,actor+0x3E96)=100;W(f32,actor+0x314)=123;W(f32,actor+0x31C)=456;sqrt_result=outside?450.0000001:450;mutate_sqrt=1;randomizer_molgera_camera(actor);assert(grounds==1&&destructs==1&&!sets&&last_position[0]==(outside?23:0)&&last_position[2]==(outside?1233:777)&&W(s16,actor+0x3E96)==0);cases++;}
 reset();W(s16,actor+0x3E96)=101;height=-1e9;randomizer_molgera_camera(actor);assert(last_position[1]==3000);cases++;
 reset();W(s16,actor+0x3E96)=60;mutate_calc=1;randomizer_molgera_camera(actor);assert(W(f32,actor+0x3EB0)==900&&W(f32,actor+0x3EB4)==88);cases++;
 reset();W(s16,actor+0x3E96)=102;W(f32,actor+0x584)=1;W(f32,actor+0x584+12+4)=2;W(f32,actor+0x584+12+8)=3;mutate_calc=1;randomizer_molgera_camera(actor);assert(W(f32,actor+0x3EAC)==1&&W(f32,actor+0x3EB0)==2&&W(f32,actor+0x3EB4)==3);cases++;
 reset();W(s16,actor+0x3E96)=777;mutate_report=1;randomizer_molgera_camera(actor);assert(reports==3&&W(s16,actor+0x3E98)==71&&W(s16,actor+0x3E9A)==501);cases++;
 reset();W(s16,actor+0x3E96)=777;W(f32,actor+0x3EC4)=2;W(f32,actor+0x3EA0)=12;mutate_random=1;randomizer_molgera_camera(actor);assert(randoms==1&&last_eye[0]==13&&last_bank==20&&W(f32,actor+0x3EC0)==75);cases++;
 reset();W(s16,actor+0x3E96)=104;W(s16,actor+0x3E98)=20;randomizer_molgera_camera(actor);assert(items==1&&W(u32,actor+0x1B64)==333);cases++;
 reset();W(s16,actor+0x3E96)=105;W(u32,actor+0x1B64)=333;randomizer_molgera_camera(actor);assert(W(f32,item+0x318)==310&&W(f32,item+0x340)==0);cases++;
 reset();W(s16,actor+0x3E96)=106;W(s16,actor+0x3E98)=520;randomizer_molgera_camera(actor);assert(warps==1&&dungeons==1&&W(u8,actor+0x1A48)==200);cases++;
 reset();W(s16,actor+0x3E96)=106;W(s16,actor+0x3E98)=680;randomizer_molgera_camera(actor);assert(resets==1&&starts==1&&!sets&&W(s16,actor+0x3E94)==1);cases++;

 /* Transition boundaries and one-shot callback sites, beyond the state sweep. */
 for(unsigned n=0;n<3;n++){reset();W(s16,actor+0x3E96)=n==0?51:n==1?52:53;W(s16,actor+0x3E9A)=n==0?170:n==1?640:760;randomizer_molgera_camera(actor);assert(W(s16,actor+0x3E96)==(n==0?52:n==1?53:60)&&W(s16,actor+0x3E98)==1);cases++;}
 reset();W(s16,actor+0x3E96)=51;W(s16,actor+0x3E98)=50;randomizer_molgera_camera(actor);assert(W(u32,player+0x430)==28);cases++;
 reset();W(s16,actor+0x3E96)=52;W(s16,actor+0x3E98)=20;randomizer_molgera_camera(actor);assert(W(s8,actor+0x1A69)==1&&sounds==3);cases++;
 reset();W(s16,actor+0x3E96)=53;W(s16,actor+0x3E98)=83;randomizer_molgera_camera(actor);assert(W(u32,player+0x430)==49);cases++;
 reset();W(s16,actor+0x3E96)=60;W(s16,actor+0x3E98)=551;W(s16,actor+0x3E9A)=1460;randomizer_molgera_camera(actor);assert(dungeons==1&&W(s8,actor+0x3EC8)==1&&W(s16,actor+0x3E96)==150);cases++;
 reset();W(s16,actor+0x3E96)=102;W(s16,actor+0x3E98)=50;randomizer_molgera_camera(actor);assert(W(u32,player+0x430)==26);cases++;
 reset();W(s16,actor+0x3E96)=104;W(s16,actor+0x3E98)=44;randomizer_molgera_camera(actor);assert(W(s16,actor+0x3E96)==105&&W(s16,actor+0x3E98)==1);cases++;
 for(unsigned forbidden=0;forbidden<2;forbidden++){reset();W(s16,actor+0x3E96)=105;W(s16,actor+0x3E98)=120;W(s16,0x1047B688+3*2)=forbidden;randomizer_molgera_camera(actor);assert(W(s16,actor+0x3E96)==(forbidden?105:106));assert(W(u8,actor+0x1A48)==(forbidden?0:202));cases++;}
 for(unsigned n=0;n<2;n++){reset();W(s16,actor+0x3E96)=106;W(s16,actor+0x3E98)=n?110:50;randomizer_molgera_camera(actor);assert(W(s8,actor+(n?0x1A69:0x1A68))==1);cases++;}
 const s16 audio_modes[]={49,50,99,100,101,777};const s16 audio_counts[]={39,40,41,339,340,341,698,699,700,729,730,1199,1200};
 for(unsigned m=0;m<6;m++)for(unsigned n=0;n<13;n++){reset();W(s16,actor+0x3E96)=audio_modes[m];W(s16,actor+0x3E9A)=audio_counts[n];randomizer_molgera_camera(actor);const s16 mode=W(s16,actor+0x3E96),count=W(s16,actor+0x3E9A);unsigned expected=mode>=50&&mode<100?((count>40&&count<730)+(count>340&&count<700)):(mode>=100&&count>=700&&count<=1200);assert(sounds==expected);cases++;}
 printf("%u complete camera cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp)
            (tmp/'test.c').write_text(code)
            (tmp/'weak.c').write_text(prefix+'\n'.join(weak))
            subprocess.run([os.environ.get("CC", "cc"),'-O2','-ffp-contract=off','-I',str(root),str(tmp/'test.c'),str(tmp/'weak.c'),'-lm','-o',str(tmp/'test')],check=True)
            subprocess.run([str(tmp/'test')],check=True)

if __name__=='__main__':
    unittest.main()
