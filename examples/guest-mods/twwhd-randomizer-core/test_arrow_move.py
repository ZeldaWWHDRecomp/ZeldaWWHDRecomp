"""Complete arrow movement, exact joint-stop callback invalidation and tail."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class ArrowMove(unittest.TestCase):
    def test_consumer(self):
        source=r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
typedef uintptr_t u32;typedef int32_t s32;typedef int16_t s16;typedef uint16_t u16;typedef int8_t s8;typedef uint8_t u8;typedef float f32;typedef double f64;
#define WWHD_GAME_FUNC(a,r,n,p) r n p
#define WWHD_REPLACE(a,r,n,p) r n p
static unsigned char arena[0x40000],data[0x500000];static const u32 actor=0x30000000,game=0x30010000,target=0x30020000,model=0x30024000,triangle=0x30028000;
static void* mem(u32 a){if(a>=actor&&a<actor+sizeof arena)return arena+a-actor;if(a>=0x10000000&&a<0x10500000)return data+a-0x10000000;const u32 low=(uintptr_t)data&0xFFFFFFFFu;if(a>=low&&a<low+sizeof data)return data+a-low;if(a>0xFFFFFFFFu)return(void*)a;assert(0);return 0;}
#define W(t,a) (*(__typeof__(_Generic((t)0,u32:(uint32_t)0,default:(t)0))*)mem(a))
#define R_ARROW(t,a) W(t,a)
#define WWHD_GAME_DATA(a,t) W(t,a)
#include "arrow_move.h"
static void ram_vector(u32 to, f32 x, f32 y, f32 z) {
  R_ARROW(f32, to) = x;
  R_ARROW(f32, to + 4) = y;
  R_ARROW(f32, to + 8) = z;
}

static unsigned attacked,linehit,water,group,attribute,hastriangle,mutate,searches,deleted,stopped,reflects,followends,sounds,particles,children,creates,blurs,models,capsules,collisions,masses,translations,squares,sqrtcalls,matcopies;
static f32 rebound_y;static s32 jointindex;static f64 forced_length;static unsigned length_override;static u32 particle_ids[8],sound_ids[8];static unsigned expected_clear;
static void copy(u32 out,u32 in){unsigned int words[3];for(unsigned i=0;i<3;i++)words[i]=W(u32,in+4*i);for(unsigned i=0;i<3;i++)W(u32,out+4*i)=words[i];}
void ram_add(u32 a,u32 b,u32 out){f32 v[3];for(unsigned i=0;i<3;i++)v[i]=W(f32,a+4*i)+W(f32,b+4*i);copy(out,(u32)v);}
void ram_add_value(u32 a,u32 out,u32 b){ram_add(a,b,out);}
void ram_subtract(u32 a,u32 out,u32 b){f32 v[3];for(unsigned i=0;i<3;i++)v[i]=W(f32,a+4*i)-W(f32,b+4*i);copy(out,(u32)v);}
void ram_subtract_into(u32 a,u32 b,u32 out){ram_subtract(a,out,b);}
void ram_multiply(u32 a,u32 out,f32 f){f32 v[3];for(unsigned i=0;i<3;i++)v[i]=W(f32,a+4*i)*f;copy(out,(u32)v);}
void ram_scale(u32 a,u32 out,f32 f){assert(f==-0.1f);ram_multiply(a,out,f);}
f64 ram_square(u32 v){squares++;const f64 result=(f64)W(f32,v)*W(f32,v)+(f64)W(f32,v+4)*W(f32,v+4)+(f64)W(f32,v+8)*W(f32,v+8);if(mutate==5)W(f32,actor+0x340)=20;return result;}
f64 ram_sqrt(f64 value){sqrtcalls++;return length_override?forced_length:sqrt(value);}
s32 ram_atan(f64 y,f64 x){(void)y;(void)x;return 0x1234;}
void ram_line_set(u32 c,u32 start,u32 end,u32 a){assert(c==actor+0x3BC&&start==actor+0x300&&a==actor&&W(f32,end)==W(f32,a+0x314)+W(f32,a+0x33C)*.25f);}
void ram_matrix_copy(u32 from,u32 to){assert((from==model+0xC8&&to==actor+0x7CC)||(from==ram_stack()&&to==actor+0x7CC));unsigned int words[12];for(unsigned i=0;i<12;i++)words[i]=W(u32,from+4*i);for(unsigned i=0;i<12;i++)W(u32,to+4*i)=words[i];matcopies++;}
s32 ram_attack_hit(u32 c){assert(c==actor+0x4B8);return attacked;}
u32 ram_hit_actor(u32 at){assert(at==actor+0x508);searches++;return target;}
void ram_reflect(u32 a){assert(a==actor&&W(u8,a+0x780)==1);reflects++;ram_proc(actor,0x02055734);}
s32 ram_joint_hit(u32 joint,u32 hit,u32 angle,u32 pos,u32 rot){assert(joint==77&&angle==actor+0x328);(void)hit;ram_vector(pos,1,2,3);W(s16,rot)=11;W(s16,rot+2)=12;W(s16,rot+4)=13;return jointindex;}
void ram_follow_end(u32 f){assert(f==actor+0x784&&W(u32,actor+0x510)!=0);followends++;if(mutate==1)W(u32,actor+0x510)=0xDEADBEEF;if(mutate==3)W(u8,actor+0x71D)=2;}
void ram_stop_matrix(u32 a){assert(a==actor&&W(u32,a+0x510)==0&&W(s16,a+0x7A8)==0&&W(s16,a+0x7AA)==-1&&W(u32,a+0x7AC)==0x020553C0&&W(u32,a+0xB0)==2);stopped++;if(mutate==2)ram_proc(actor,0x02055734);}
u32 ram_game(void){return game;}
s32 ram_line_cross(u32 bg,u32 c){assert(bg==game+0x12A0&&c==actor+0x3BC);return linehit;}
s32 ram_water(u32 a){assert(a==actor);if(water)ram_proc(actor,0x02055734);return water;}
void ram_color(u32 p,s32 c){assert(p==actor+0x314&&c>=0&&c<=2);}
u32 ram_triangle(u32 bg,u32 b,u32 p){assert(bg==game+0x12A0&&b==2&&p==3);return hastriangle?triangle:0;}
void ram_translate(u32 m,f32 x,f32 y,f32 z){assert(m==ram_stack());(void)x;(void)y;(void)z;translations++;for(unsigned i=0;i<12;i++)W(u32,m+4*i)=0xF0000000+i;}
void ram_rotate(u32 m,s32 x,s32 y,s32 z){assert(m==ram_stack()&&!z);(void)x;(void)y;models++;}
u32 ram_particle(u32 c,s32 g,u32 id,u32 p,u32 a,u32 s,s32 alpha,u32 cb,s32 setup,u32 color,u32 env,u32 s2){assert(c==0x3002F000&&g==1&&p==actor+0x7C0&&a&&s==0&&alpha==255&&!cb&&setup==-1&&!color&&!env&&!s2);particle_ids[particles++]=id;return 0;}
s32 ram_reverb(s32 room){assert(room==-3);if(expected_clear)assert(W(u32,actor+0x510)==0);return 4;}
void ram_sound(u32 id,u32 p,u32 param,s32 reverb){assert(p==actor+0x314&&reverb==4);if(expected_clear)assert(W(u32,actor+0x510)==0);(void)param;if(id==0x287A)rebound_y=W(f32,actor+0x340);sound_ids[sounds++]=id;}
void ram_delete(u32 a){assert(a==actor);deleted++;}
s32 ram_group(u32 bg,u32 p,u32 mask){assert(bg==game+0x12A0&&p==actor+0x3BC&&mask==0x200);return group;}
u32 ram_create(s32 n,u32 params,u32 p,s32 room,u32 a,u32 s,s32 subtype,u32 cb){assert(n==0x2A&&!params&&p==actor+0x7C0&&room==-3&&!a&&!s&&subtype==-1&&!cb);creates++;return 55;}
u32 ram_create_child(s32 n,u32 parent,u32 params,u32 p,s32 room,u32 a,u32 s,s32 subtype,u32 cb){assert(n==0x1D9&&parent==0x123&&params==2&&p==actor+0x7C0&&room==-3&&a==actor+0x7FE&&!s&&subtype==-1&&!cb);children++;return 56;}
u32 ram_attribute(u32 bg,u32 p){assert(bg==game+0x12A0&&p==actor+0x3BC);return attribute;}
u32 ram_material(u32 bg,u32 p){assert(bg==game+0x12A0&&p==actor+0x3BC);return 7;}
void ram_vector_angle(u32 p,u32 a){assert(p==(hastriangle?triangle:0));W(s16,a)=1;W(s16,a+2)=2;W(s16,a+4)=3;}
void ram_blur(u32 a){assert(a==actor);blurs++;}
void ram_capsule(u32 p,u32 start,u32 end,f32 r){assert(p==actor+0x5D0&&start==actor+0x314&&r==5);copy(p,start);copy(p+12,end);capsules++;}
void ram_collision_set(u32 manager,u32 collision){assert(manager==game+0x26A4&&collision==actor+0x4B8);collisions++;}
void ram_mass_set(u32 manager,u32 collision,u32 priority){assert(manager==game+0x4EF8&&collision==actor+0x4B8&&priority==1);masses++;}
static void reset(void){memset(arena,0,sizeof arena);memset(data,0,sizeof data);attacked=linehit=water=group=attribute=mutate=searches=deleted=stopped=reflects=followends=sounds=particles=children=creates=blurs=models=capsules=collisions=masses=translations=squares=sqrtcalls=matcopies=expected_clear=length_override=0;hastriangle=1;jointindex=0;W(u32,actor+4)=0x123;W(s8,actor+0x326)=-3;W(u32,actor+0x3B0)=model;W(u32,actor+0x510)=0x02052388;W(u32,game+0x5AB0)=0x3002F000;W(u32,target+0x36C)=77;W(s16,target+8)=0xB;W(u16,actor+0x3BC)=3;W(u16,actor+0x3BE)=2;ram_vector(actor+0x33C,4,8,12);ram_vector(actor+0x314,10,20,30);ram_vector(actor+0x300,1,2,3);ram_vector(actor+0x3EC,10,20,30);ram_vector(triangle,0,1,0);for(unsigned i=0;i<12;i++)W(u32,model+0xC8+i*4)=i;W(u8,0x10007478)=0;W(u8,0x10007479)=1;W(u8,0x1000747A)=2;}
int main(void){unsigned cases=0;
 for(unsigned type=0;type<4;type++)for(int index=-4;index<=10;index++)for(unsigned blocked=0;blocked<2;blocked++)for(unsigned buffered=0;buffered<2;buffered++){
  reset();attacked=1;jointindex=index;W(u8,actor+0x71D)=type;W(u32,actor+0x50C)=blocked;W(u8,actor+0x818)=buffered;W(u32,actor+0x804)=target;expected_clear=type!=3&&(!blocked||buffered)&&index>=0;
  assert(randomizer_arrow_move(actor)==1);const unsigned del=type!=3&&(!blocked||buffered)&&index==-3;assert(deleted==del&&stopped==expected_clear);assert(W(u32,actor+0x510)==(expected_clear?0:0x02052388));assert(capsules==!del&&collisions==!del&&masses==!del);cases++;
 }
 for(unsigned type=0;type<4;type++)for(unsigned attr=0;attr<25;attr++)for(unsigned tri=0;tri<2;tri++)for(unsigned grp=0;grp<2;grp++){
  reset();linehit=1;group=grp;attribute=attr;hastriangle=tri;W(u8,actor+0x71D)=type;randomizer_arrow_move(actor);assert(W(u32,actor+0x510)==0x02052388&&!stopped&&capsules==1);assert(particles==(type==1?2:type==2?!grp:type==3?1:(attr==3||attr==9||attr==15||attr==20||attr==21)));assert(creates==(type==2&&grp)&&children==(type==2&&!grp));cases++;
 }
 reset();attacked=1;W(u8,actor+0x71D)=3;W(u32,actor+0x50C)=1;W(s16,target+8)=0xA8;randomizer_arrow_move(actor);assert(reflects==1&&searches==2&&ram_is_water(actor)&&!capsules&&!particles&&W(u32,actor+0x510));cases++;
 for(s16 name=0xF3;name<=0xF5;name++){reset();attacked=1;W(u8,actor+0x71D)=3;W(u32,actor+0x50C)=1;W(s16,target+8)=name;randomizer_arrow_move(actor);assert(deleted==1&&particles==1&&particle_ids[0]==0x2A1&&W(u32,actor+0x510));cases++;}
 for(unsigned mut=1;mut<=2;mut++){reset();attacked=1;mutate=mut;W(u32,actor+0x788)=1;expected_clear=1;randomizer_arrow_move(actor);assert(stopped==1&&followends==1&&W(u32,actor+0x510)==0&&capsules==(mut!=2));cases++;}
 for(unsigned bg=0;bg<2;bg++){reset();water=1;linehit=bg;randomizer_arrow_move(actor);assert(ram_is_water(actor)&&!capsules&&!particles&&!sounds&&W(u32,actor+0x510));cases++;}
 const f64 distances[]={19999.999999,20000,20000.000001,24999.999999,25000,25000.000001,NAN};
 for(unsigned i=0;i<7;i++){reset();length_override=1;forced_length=distances[i];randomizer_arrow_move(actor);assert(W(u8,actor+0x7FC)==(i==2||i==3||i==4));assert(W(f32,actor+0x340)==(i==5?6:8));cases++;}
 reset();length_override=1;forced_length=26000;W(f32,actor+0x7C4)=100;randomizer_arrow_move(actor);assert(deleted==1&&!capsules&&!blurs);cases++;
 reset();attacked=1;W(u32,actor+0x50C)=1;mutate=5;randomizer_arrow_move(actor);assert(W(u32,actor+0x7AC)==0x02055450&&rebound_y>20&&W(u32,actor+0x510));cases++;

 reset();attacked=1;mutate=3;W(u8,actor+0x71D)=1;W(u8,actor+0x7B0)=1;W(u32,actor+0x788)=1;expected_clear=1;randomizer_arrow_move(actor);assert(stopped==1&&sounds==1&&sound_ids[0]==0x69DB&&W(u8,actor+0x7B0)==1);cases++;
 /* Native inline model copy reads all12 words before overlapping writes. */
 reset();W(u32,actor+0x3B0)=ram_stack()-0xC8+4;ram_model(actor);for(unsigned i=0;i<12;i++)assert(W(u32,ram_stack()+4+4*i)==0xF0000000+i);cases++;
 /* Existing callback stays unchanged even when a joint query rejects the hit. */
 for(int index=-10;index<0;index++)if(index!=-3){reset();attacked=1;jointindex=index;randomizer_arrow_move(actor);assert(!stopped&&!deleted&&W(s32,actor+0x730)==index&&W(u32,actor+0x510)==0x02052388);cases++;}
 printf("%u complete arrow movement cases passed\n",cases);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);(path/'test.c').write_text(source)
            subprocess.run(['clang','-O2','-ffp-contract=off','-I',str(Path(__file__).parent),str(path/'test.c'),'-lm','-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
