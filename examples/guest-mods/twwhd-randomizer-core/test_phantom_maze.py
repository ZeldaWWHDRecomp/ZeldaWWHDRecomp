"""Complete Phantom movement, maze chest ownership separation and action ABI."""
from pathlib import Path
import re
import os
import subprocess
import tempfile
import unittest

class PhantomMaze(unittest.TestCase):
    def test_consumer(self):
        root=Path(__file__).parent
        declarations=re.findall(r'WWHD_GAME_FUNC\([^\n]+',(root/'phantom_maze.h').read_text())
        prefix=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef int8_t s8;typedef uint8_t u8;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
'''
        weak=[]
        for declaration in declarations:
            match=re.fullmatch(r'WWHD_GAME_FUNC\([^,]+,([^,]+),([^,]+),\((.*)\)\);',declaration)
            self.assertIsNotNone(match,declaration)
            result,name,args=match.groups();weak.append(f'__attribute__((weak)) {result} {name}({args}){{assert(0&&"unexpected {name}");'+('return 0;' if result!='void' else '')+'}')
        code=prefix+r'''
static unsigned char arena[0x40000],data[0x500000];
static const u32 actor=0x30000000,game=0x30010000,player=0x30020000,morf=0x30021000,model=0x30022000,weapon=0x30023000;
static void* mem(u32 a){if(a>=actor&&a<actor+sizeof arena)return arena+a-actor;if(a>=0x10000000&&a<0x10500000)return data+a-0x10000000;if(a>0xFFFFFFFFu)return(void*)a;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_PGM(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
#include "phantom_maze.h"
static unsigned owned,chest,queries,chests,damagecalls,cameras,mode_mutation,lines,linefinishes,cross_hit,appears,disappears,animations,deleted,native_action,positions,decels,calcs,voices,sounds,random_calls,query_mutation;
static f64 sqrt_value,random_value;static unsigned precise;static f32 square_input;
u32 pgm_game(void){return game;}
u32 pgm_camera(void){return 77;}
void pgm_camera_type(u32 c,u32 n,u32 a){assert(c==77&&!a&&(n==0x1000ED94||n==0x1000EDA0||n==0x1000EDAC));cameras++;if(mode_mutation)W(s16,actor+0x5BA)=0;}
s32 pgm_item_owned(u32 i){assert(i==0x36);queries++;return owned;}
s32 pgm_chest_open(u32 s,u32 c){assert(s==8&&!c);chests++;if(query_mutation){W(u8,actor+0x3D8)=2;W(f32,player+0x314)=101.0f;}return chest;}
f64 pgm_sqrt(f64 s){square_input=s;return precise?sqrt_value:sqrt(s);}
f64 pgm_square_magnitude(u32 v){f32* p=mem(v);return (f64)p[0]*p[0]+(f64)p[1]*p[1]+(f64)p[2]*p[2];}
f64 pgm_random(f64 r){random_calls++;return precise?random_value:r*.75;}
f64 pgm_random_signed(f64 r){return -r*.25;}
void pgm_y_rotation(u32 m,s32 a){assert(m==0x1048D0CC);(void)a;}
void pgm_x_rotation(u32 m,s32 a){assert(m==0x1048D0CC);(void)a;}
void pgm_transform(u32 v,u32 o){for(unsigned i=0;i<3;i++)W(u32,o+i*4)=W(u32,v+i*4);}
void pgm_vector_add(u32 a,u32 result,u32 b){for(unsigned i=0;i<3;i++)W(f32,result+i*4)=W(f32,a+i*4)+W(f32,b+i*4);}
void pgm_vector_subtract(u32 a,u32 result,u32 b){for(unsigned i=0;i<3;i++)W(f32,result+i*4)=W(f32,a+i*4)-W(f32,b+i*4);}
void pgm_vector_add_inplace(u32 a,u32 b,u32 result){f32 x=W(f32,a)+W(f32,b),y=W(f32,a+4)+W(f32,b+4),z=W(f32,a+8)+W(f32,b+8);W(f32,result)=x;W(f32,result+4)=y;W(f32,result+8)=z;}
void pgm_decelerate(u32 p,f32 r,f32 max){assert(r==1&&max>=0);W(f32,p)=0;decels++;}
void pgm_calculate(u32 p,f32 t,f32 r,f32 max){assert(r==1&&max>=0);W(f32,p)=t;calcs++;}
void pgm_angle_calculate(u32 p,s32 t,s32 r,s32 max){assert(r>0);(void)max;W(s16,p)=t;}
s32 pgm_atan(f32 x,f32 z){(void)x;(void)z;return 0x100;}
s32 pgm_search_angle(u32 a,u32 p){assert(a==actor&&p==player);return 0x1234;}
void pgm_animation(u32 a,s32 i,f32 blend,u32 loop,f32 speed,s32 snd){assert(a==actor&&i>0&&blend>=0&&loop<=2&&speed==1&&snd==-1);animations++;}
void pgm_appear(u32 a){assert(a==actor);appears++;}
void pgm_disappear(u32 a,u32 w){assert(a==actor&&w<=2);disappears++;}
void pgm_position_move(u32 a,u32 direct){assert(a==actor&&direct<=1);positions++;}
void pgm_flight_sound(u32 a){assert(a==actor);}
#define NATIVE(name,id) void name(u32 a){assert(a==actor);native_action=id;}
NATIVE(pgm_shot2,11) NATIVE(pgm_spin2,10) NATIVE(pgm_down,8) NATIVE(pgm_start,20) NATIVE(pgm_end,21) NATIVE(pgm_last_end,22)
void pgm_damage(u32 a){assert(a==actor);damagecalls++;W(f32,actor+0xAF0)=1;W(f32,actor+0xAF4)=2;W(f32,actor+0xAF8)=3;W(s8,actor+0x8C0)=99;}
void pgm_delete(u32 a){assert(a==actor);deleted++;}
s32 pgm_reverb(s32 room){assert(room==-3);return 17;}
void pgm_sound(u32 id,u32 p,u32 volume,s32 reverb){assert(p==actor+0x37C&&reverb==17);(void)id;(void)volume;sounds++;}
void pgm_monster_sound(u32 id,u32 p,u32 process,u32 arg,s32 reverb){assert(p==actor+0x37C&&process==1234&&!arg&&reverb==17);(void)id;voices++;}
void pgm_ball_sound(u32 id,u32 p,u32 volume,s32 reverb){assert(id==0x6237&&p==actor+0x614&&volume==100&&reverb==17);}
void pgm_line_construct(u32 p){memset(mem(p),0,0x6C);lines++;}
void pgm_line_destroy(u32 p,u32 f){unsigned int* a=mem(p);assert(!f&&a[0x58/4]==0x1000ED64&&a[0x64/4]==0x1000EC34&&a[0x20/4]==0x1000EC24&&a[0x10/4]==0x1000ED34);linefinishes++;}
void pgm_line_set(u32 l,u32 start,u32 end,u32 a){unsigned int* line=mem(l);assert(a==actor&&line[0x10/4]==0x1000ED34&&line[0x20/4]==0x1000ED44&&line[0x58/4]==0x1000ED64&&line[0x64/4]==0x1000ED54&&line[0x68/4]==1);assert(line[0]==(unsigned int)(l+0x58)&&line[1]==(unsigned int)(l+0x64));for(unsigned i=0;i<3;i++){W(f32,l+0x24+i*4)=W(f32,start+i*4);W(f32,l+0x30+i*4)=W(f32,end+i*4);}}
s32 pgm_line_cross(u32 bg,u32 line){assert(bg==game+0x12A0);if(cross_hit){W(f32,line+0x30)=1;W(f32,line+0x34)=2;W(f32,line+0x38)=3;}return cross_hit;}
void pgm_matrix_copy(u32 s,u32 d){assert(s==0x30024000+24*0x30&&d==0x1048D0CC);}
u32 pgm_actor_create(s32 name,u32 params,u32 pos,s32 room,u32 angle,u32 scale,s32 subtype,u32 fn){assert(name==0x1CF&&params==5&&pos==actor+0x314&&room==-3&&!angle&&!scale&&subtype==-1&&!fn);return 555;}
u32 pgm_actor_judge(u32 f,u32 key){assert(f==0x025E1234&&*(unsigned int*)key==555);return weapon;}
void pgm_carry(u32 a,u32 m){assert(a==weapon&&!m);}
void pgm_carry_cancel(u32 a){assert(a==weapon);}
static void reset(void){memset(arena,0,sizeof arena);memset(data,0,sizeof data);owned=chest=queries=chests=damagecalls=cameras=mode_mutation=lines=linefinishes=cross_hit=appears=disappears=animations=deleted=native_action=positions=decels=calcs=voices=sounds=random_calls=precise=query_mutation=0;W(u32,actor+4)=1234;W(s8,actor+0x326)=-3;W(u32,actor+0x3DC)=morf;W(u32,morf+0x90)=model;W(f32,morf+0x98)=1;W(u32,model+0x2C)=0x30025000;W(u32,0x30025010)=0x30024000;W(u32,game+0x5B2C)=player;W(u32,0x1018C7B0)=0x1048D0CC;W(f32,player+0x318)=1000;W(u8,actor+0x3D8)=1;W(u8,actor+0x3D9)=10;W(s16,actor+0x5BC)=-1;W(s16,0x10463D54)=10;W(s16,0x10463D56)=20;W(f32,actor+0x318)=1000;W(u32,actor+0x8E4)=555;W(u32,weapon+0x3B4)=model;W(u32,actor+0x3E4)=model;}
int main(void){unsigned cases=0;
for(unsigned type=0;type<256;type++)for(unsigned o=0;o<2;o++)for(unsigned c=0;c<2;c++)for(unsigned d=0;d<4;d++){
reset();W(u8,actor+0x3D8)=type;owned=o;chest=c;W(f32,player+0x314)=d==0?0:d==1?99:d==2?100:101;const unsigned hidden=(type==2&&!owned)||(type==1&&chest);assert(randomizer_phantom_move(actor)==0);assert(queries==(type==2)&&chests==(type==1));assert(W(f32,actor+0x318)==(hidden?-19998:1002));assert(W(f32,actor+0x314)==1&&W(f32,actor+0x31C)==3);assert(damagecalls==1&&W(s8,actor+0x8C0)==0);assert(W(s16,actor+0x5BC)==(!hidden&&d<2?1:-1));cases++;}
const s16 actions[]={-32768,-1,0,1,2,3,4,5,6,7,8,9,10,11,20,21,22,23,32767};const s16 modes[]={-11,-10,-1,0,1,2,3,4,5,99,32767};
for(unsigned a=0;a<sizeof actions/sizeof*actions;a++)for(unsigned m=0;m<sizeof modes/sizeof*modes;m++)for(unsigned stopped=0;stopped<2;stopped++)for(unsigned blocked=0;blocked<2;blocked++){
reset();W(s16,actor+0x5BA)=actions[a];W(s16,actor+0x5BC)=modes[m];W(u8,morf+0xA7)=stopped;cross_hit=blocked;W(s16,actor+0x5D8)=0;W(s16,actor+0x5DA)=1;W(s16,actor+0x5DC)=1;const s32 ret=randomizer_phantom_move(actor);const s16 action=actions[a];assert(ret==(action==3||action==5||action==6||action==7||action==8||action==9||action==11));assert(damagecalls==1&&lines==linefinishes);assert(queries==0);if(action==8||action==10||action==11||action==20||action==21||action==22)assert(native_action==(unsigned)action);cases++;}
for(unsigned action=0;action<23;action++)for(unsigned low=0;low<2;low++){reset();W(u8,actor+0x3D8)=0;W(s16,actor+0x5BA)=action;W(s16,actor+0x5BC)=99;W(f32,player+0x318)=low?709.999f:710.0f;const s32 r=randomizer_phantom_move(actor);if(low&&action==0)assert(r==0&&!damagecalls);else assert(damagecalls==1);if(low&&action>0&&action<20&&action!=2)assert(W(s16,actor+0x5BA)==0||W(s16,actor+0x5BA)==2);cases++;}
reset();precise=1;sqrt_value=99.99999999999999;randomizer_phantom_move(actor);assert(W(s16,actor+0x5BC)==1);cases++;
reset();precise=1;sqrt_value=NAN;randomizer_phantom_move(actor);assert(W(s16,actor+0x5BC)==-1);cases++;
reset();precise=1;random_value=0.49999999999999994;W(u8,actor+0x3D8)=0;W(s16,actor+0x5BA)=1;W(s16,actor+0x5BC)=0;randomizer_phantom_move(actor);assert(W(s16,actor+0x5BA)==5&&W(s16,actor+0x5BC)==-10);cases++;
reset();query_mutation=1;randomizer_phantom_move(actor);assert(chests==1&&!queries&&W(s16,actor+0x5BC)==-1&&W(u8,actor+0x3D8)==2);owned=0;randomizer_phantom_move(actor);assert(queries==1&&W(f32,actor+0x318)==-19998.0f);cases++;
reset();mode_mutation=1;chest=1;W(s16,actor+0x5BA)=23;randomizer_phantom_move(actor);assert(cameras==1&&chests==1&&W(f32,actor+0x318)==-19998.0f);cases++;
reset();precise=1;random_value=.400000003;W(s16,actor+0x5BA)=1;W(s16,actor+0x5BC)=0;randomizer_phantom_move(actor);assert(W(s16,actor+0x5BA)==10&&lines==1&&linefinishes==1);cases++;
reset();precise=1;sqrt_value=NAN;W(s16,actor+0x5BC)=1;W(s16,actor+0x5DA)=1;W(s16,actor+0x5DC)=1;pgm_fly(actor,0);assert(decels==1&&calcs==1&&lines==linefinishes);cases++;
reset();precise=1;sqrt_value=NAN;W(f32,player+0x314)=0x1.50b23ap-1f;W(f32,player+0x31C)=0x1.ada456p-1f;randomizer_phantom_move(actor);assert(square_input==0x1.22f922p0f&&square_input!=0x1.22f920p0f);cases++;
reset();owned=0x80000000u;W(u8,actor+0x3D8)=2;randomizer_phantom_move(actor);assert(queries==1&&!chests&&W(f32,actor+0x318)==1002);cases++;
reset();chest=0x80000000u;randomizer_phantom_move(actor);assert(chests==1&&!queries&&W(f32,actor+0x318)==-19998);cases++;
printf("Phantom maze: %u cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'test.c').write_text(code);(p/'weak.c').write_text(prefix+'\n'.join(weak))
            subprocess.run([os.environ.get('CC','clang'),'-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-I',str(root),str(p/'test.c'),str(p/'weak.c'),'-lm','-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)

if __name__=='__main__':unittest.main()
